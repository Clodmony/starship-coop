#!/usr/bin/env python3
"""Shape search for the nine boost gauge needle textures in a Star Fox 64 ROM (#357).

    python tools/coop/needle_search.py <rom.z64> [--raw] [--top 15] [--dump <folder>]

Asks one question: is `port/assets/textures/hud/sBoostGaugeArrow0..8` (or the needle they show) a copy of a
graphic in the ROM, turned, mirrored, scaled or recoloured? It prints counts, scores and asset-table names only,
and holds no game data. `--dump` writes pictures for the person at the keyboard; keep that folder out of git.

What it does:
  1. decodes the nine frames and prints the shape in numbers;
  2. decompresses every file of the ROM's file table (MIO0), reads the asset tables in `assets/yaml/us/rev0`,
     walks the display lists, and says which bytes no table entry explains;
  3. stage A: every listed texture at its true format and size -> masks (alpha, not the most common colour,
     bright, dark; whole and per connected blob) -> each mask centred and scaled to one size -> normalised
     cross-correlation with the needle (clean needle, blade alone, each frame) at 360 angles x 2 mirrors x 3 scales;
  4. stage B (`--raw`): every 8-byte offset of the bytes no table explains and of the files without a table
     (audio left out), as 4, 8 and 16 bit pixels at 130 sizes, filtered by how blob-like the window is, ranked
     by a cheap descriptor that turning, mirroring and scaling leave alone, the best 3000 scored as in stage A;
  5. the control, in both stages: three real textures, turned by an odd angle, scaled and recoloured, are
     searched for the same way and must come back among the first `--top` by both measures (correlation, and
     the stricter overlap of the two masks), which are the ones a person then looks at. A "not found" without
     that is no result. Stage B reads palette textures without their palette (index 0 as the background), so
     it cannot see one whose transparent index is another: the second control shows that.

Needs numpy; Pillow only for `--dump`. US 1.0 only (the file table offset is fixed).
"""
import argparse
import collections
import glob
import hashlib
import math
import os
import re
import struct
import sys

import numpy as np

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
US10_SHA1 = "d8b1088520f7c5f81433292a9258c1184afa1457"
FILE_TABLE = 0xD9A90  # tools/comptool.py, "US 1.0"
AUDIO_FILES = (3, 4, 5)  # audio_seq, audio_bank, audio_table
BPP = {"RGBA16": 16, "RGBA32": 32, "IA16": 16, "IA8": 8, "IA4": 4, "I8": 8, "I4": 4, "CI8": 8, "CI4": 4}
N = 32  # side of the canonical patch
R0 = 6.0  # RMS radius of a mask inside the patch
HUB = (16.0, 15.5)  # centre of the pixels all nine frames share (printed by describe_needle)
CONTROLS = (("aRadarMarkArwingTex", 37.0, 1.6), ("D_1012290", 143.0, 0.6), ("D_ME_6026478", 251.0, 0.7))


# ---------------------------------------------------------------- ROM

def mio0(d):
    n, co, uo = struct.unpack_from(">III", d, 4)
    out = bytearray()
    lp, cp, up, mask, bits = 16, co, uo, 0, 0
    while len(out) < n:
        if mask == 0:
            bits = d[lp]
            lp += 1
            mask = 0x80
        if bits & mask:
            out.append(d[up])
            up += 1
        else:
            v = (d[cp] << 8) | d[cp + 1]
            cp += 2
            off = (v & 0xFFF) + 1
            for _ in range((v >> 12) + 3):
                out.append(out[-off])
        mask >>= 1
    return bytes(out[:n])


def rom_files(rom):
    """{virtual start: (index, decompressed bytes, was compressed)}"""
    out = {}
    i = 0
    while True:
        v, ps, pe, _ = struct.unpack_from(">IIII", rom, FILE_TABLE + 16 * i)
        if i > 0 and ps == 0 and pe == 0:
            break
        raw = rom[ps:pe]
        comp = raw[:4] == b"MIO0"
        out[v] = (i, mio0(raw) if comp else raw, comp)
        i += 1
    return out


def yaml_entries(repo):
    res = []
    for y in sorted(glob.glob(os.path.join(repo, "assets", "yaml", "us", "rev0", "*.y*ml"))):
        txt = open(y, encoding="utf-8").read()
        m = re.search(r"segments:\s*\n((?:\s*-\s*\[.*\]\s*\n)+)", txt)
        if not m:
            continue
        segs = {int(a, 0): int(b, 16) for a, b in re.findall(r"\[\s*(\w+)\s*,\s*(0x[0-9A-Fa-f]+)\s*\]", m.group(1))}
        # flow style `{ type: ..., offset: ... }` and block style (a name, then indented `key: value` lines)
        blocks = [mm.group(1) for mm in re.finditer(r"\{([^{}]*)\}", txt)]
        blocks += [mm.group(0).replace("\n", ",") for mm in re.finditer(r"^\w+:\s*\n(?:[ \t]+\w+:[^\n{]*\n)+", txt, re.M)]
        for b in blocks:
            def g(k, conv=str):
                x = re.search(r"\b" + k + r":\s*([\w:]+)", b)
                return conv(x.group(1)) if x else None

            def ih(s):
                return int(s, 0)
            off = g("offset", ih)
            if off is None or g("type") is None:
                continue
            res.append(dict(file=os.path.basename(y).split(".")[0], seg=off >> 24, off=off & 0xFFFFFF, type=g("type"),
                            format=g("format"), w=g("width", ih), h=g("height", ih), tlut=g("tlut", ih),
                            colors=g("colors", ih), count=g("count", ih), symbol=g("symbol") or "?",
                            rom=segs.get(off >> 24)))
    return res


def rgba16(buf, n):
    px = np.frombuffer(buf[:2 * n], dtype=">u2").astype(np.int32)
    return np.stack([(px >> 11) & 31, (px >> 6) & 31, (px >> 1) & 31, (px & 1) * 31], -1) / 31.0


def decode(data, off, fmt, w, h, tlut=None):
    """h x w x 4 floats (r, g, b, a in 0..1), or None"""
    n = w * h
    if off + (n * BPP[fmt] + 7) // 8 > len(data):
        return None
    if fmt == "RGBA16":
        return rgba16(data[off:off + 2 * n], n).reshape(h, w, 4)
    if fmt == "RGBA32":
        return np.frombuffer(data[off:off + 4 * n], dtype=np.uint8).reshape(h, w, 4) / 255.0
    if fmt == "IA16":
        a = np.frombuffer(data[off:off + 2 * n], dtype=np.uint8).reshape(h, w, 2) / 255.0
        return np.stack([a[..., 0]] * 3 + [a[..., 1]], -1)
    if BPP[fmt] == 8:
        a = np.frombuffer(data[off:off + n], dtype=np.uint8)
    else:
        a = np.frombuffer(data[off:off + (n + 1) // 2], dtype=np.uint8)
        a = np.stack([a >> 4, a & 15], -1).reshape(-1)[:n]
    if fmt == "IA8":
        a = a.reshape(h, w)
        return np.stack([(a >> 4) / 15.0] * 3 + [(a & 15) / 15.0], -1)
    if fmt == "IA4":
        a = a.reshape(h, w)
        return np.stack([(a >> 1) / 7.0] * 3 + [(a & 1) * 1.0], -1)
    if fmt in ("I8", "I4"):
        a = a.reshape(h, w) / (255.0 if fmt == "I8" else 15.0)
        return np.stack([a] * 4, -1)
    if tlut is None:  # CI without a palette: the index as grey
        g = a.reshape(h, w) / (255.0 if fmt == "CI8" else 15.0)
        return np.stack([g] * 3 + [np.ones((h, w))], -1)
    pal = np.zeros((256, 4))
    k = min(256, (len(data) - tlut) // 2)
    pal[:k] = rgba16(data[tlut:tlut + 2 * k], k)
    return pal[a].reshape(h, w, 4)


def coverage(files, ents):
    """per asset file: which bytes a table entry or a display list explains.
    1 listed texture/palette/vertices, 2 display list, 3 vertices and 4 textures and 6 palettes a display list
    names, 5 an entry of unknown size (counted up to the next entry), 0 nothing."""
    fmtname = {0: "RGBA", 2: "CI", 3: "IA", 4: "I"}
    byfile = collections.defaultdict(list)
    for e in ents:
        if e["rom"] in files:
            byfile[e["rom"]].append(e)
    res = {}
    for rom, es in byfile.items():
        d = files[rom][1]
        n = len(d)
        segn = es[0]["seg"]
        cov = np.zeros(n, np.uint8)

        def mark(a, b, v):
            sl = cov[max(0, a):min(n, b)]
            sl[sl == 0] = v
        es = sorted(es, key=lambda e: e["off"])
        offs = [e["off"] for e in es] + [n]
        listed, stack = set(), []
        for e in es:
            if e["type"] == "TEXTURE" and e["format"] == "TLUT":
                mark(e["off"], e["off"] + 2 * e["colors"], 1)
            elif e["type"] == "TEXTURE":
                mark(e["off"], e["off"] + (e["w"] * e["h"] * BPP[e["format"]] + 7) // 8, 1)
                listed.add(e["off"])
            elif e["type"] == "VTX":
                mark(e["off"], e["off"] + 16 * e["count"], 1)
            elif e["type"] == "GFX":
                stack.append(e["off"])
        seen, timg = set(), set()
        while stack:  # F3DEX: G_VTX 04, G_DL 06, G_ENDDL B8, G_SETTIMG FD, G_LOADTLUT F0, G_SETTILESIZE F2
            o = stack.pop()
            if o in seen or o < 0 or o + 8 > n:
                continue
            seen.add(o)
            cur = None
            while o + 8 <= n:
                w0, w1 = struct.unpack_from(">II", d, o)
                op = w0 >> 24
                cov[o:o + 8] = 2
                o += 8
                same, a = (w1 >> 24) == segn, w1 & 0xFFFFFF
                if op == 0x04 and same:
                    mark(a, a + 16 * ((w0 & 0xFFFF) >> 10), 3)
                elif op == 0x06 and same:
                    stack.append(a)
                    if (w0 >> 16) & 0xFF == 1:
                        break
                elif op == 0xFD:
                    cur = (a, (w0 >> 21) & 7, (w0 >> 19) & 3) if same else None
                elif op == 0xF0 and cur:
                    mark(cur[0], cur[0] + 2 * (((w1 >> 14) & 0x3FF) + 1), 6)
                    cur = None
                elif op == 0xF2 and cur:
                    w, h = (((w1 >> 12) & 0xFFF) >> 2) + 1, ((w1 & 0xFFF) >> 2) + 1
                    bits = [4, 8, 16, 32][cur[2]]
                    if cur[0] not in listed and cur[1] in fmtname:
                        timg.add((cur[0], fmtname[cur[1]] + str(bits), w, h))
                        mark(cur[0], cur[0] + (w * h * bits + 7) // 8, 4)
                    cur = None
                elif op == 0xB8:
                    break
        for k, e in enumerate(es):
            if e["type"] not in ("TEXTURE", "VTX", "GFX"):
                mark(e["off"], offs[k + 1], 5)
        res[rom] = dict(file=es[0]["file"], cov=cov, timg=sorted(timg))
    return res


# ---------------------------------------------------------------- masks and patches

def bilinear(img, X, Y):
    """img: H x W; X, Y: any shape; 0 outside"""
    H, W = img.shape
    x0 = np.floor(X).astype(np.int32)
    y0 = np.floor(Y).astype(np.int32)
    fx = (X - x0).astype(np.float32)
    fy = (Y - y0).astype(np.float32)
    out = np.zeros(X.shape, np.float32)
    for dy, wy in ((0, 1 - fy), (1, fy)):
        for dx, wx in ((0, 1 - fx), (1, fx)):
            xi, yi = x0 + dx, y0 + dy
            ok = (xi >= 0) & (xi < W) & (yi >= 0) & (yi < H)
            out += np.where(ok, img[np.clip(yi, 0, H - 1), np.clip(xi, 0, W - 1)], 0) * wx * wy
    return out


def moments(m):
    """centroid x, y and RMS radius of masks (..., H, W)"""
    H, W = m.shape[-2:]
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float32)
    s = m.sum((-1, -2)) + 1e-9
    cx = (m * xs).sum((-1, -2)) / s
    cy = (m * ys).sum((-1, -2)) / s
    r2 = (m * ((xs - cx[..., None, None]) ** 2 + (ys - cy[..., None, None]) ** 2)).sum((-1, -2)) / s
    return cx, cy, np.sqrt(r2 + 1e-9)


def unit(p):
    p = p - p.mean(-1, keepdims=True)
    return p / (np.linalg.norm(p, axis=-1, keepdims=True) + 1e-9)


GRID = (np.mgrid[0:N, 0:N].astype(np.float32) - (N - 1) / 2).reshape(2, -1)  # row 0: y, row 1: x


RINGS = np.arange(0.25, 2.51, 0.25, dtype=np.float32)  # in RMS radii
SPOKES = 64
_th = np.arange(SPOKES, dtype=np.float32) * (2 * np.pi / SPOKES)
POLAR = np.stack([(RINGS[:, None] * np.sin(_th)[None]).reshape(-1), (RINGS[:, None] * np.cos(_th)[None]).reshape(-1)])


def ring_batch(masks):
    """masks K x H x W -> K x 90 descriptors that do not change when a mask is moved, turned, mirrored or scaled:
    the mask read along 10 rings about its centroid (radii in RMS radii), and of each ring the size of its first
    9 Fourier terms. Cheap; the first pass of the raw scan."""
    v = sample_batch(masks, POLAR * R0).reshape(len(masks), len(RINGS), SPOKES)
    f = np.abs(np.fft.rfft(v, axis=2)[:, :, :9]) * np.sqrt(RINGS)[None, :, None]
    f = f.reshape(len(masks), -1).astype(np.float32)
    return f / (np.linalg.norm(f, axis=1, keepdims=True) + 1e-9)


def canon_batch(masks):
    """masks K x H x W (0/1) -> K x N*N patches: centred on the centroid, RMS radius R0, zero mean, unit norm"""
    return unit(sample_batch(masks, GRID))


def sample_batch(masks, grid):
    """masks K x H x W read at grid (2 x P; y, x in patch pixels, R0 of them one RMS radius) about each centroid"""
    K, H, W = masks.shape
    m = masks.astype(np.float32)
    cx, cy, r = moments(m)
    s = (r / R0)[:, None]
    X = cx[:, None] + grid[1][None] * s
    Y = cy[:, None] + grid[0][None] * s
    x0 = np.floor(X).astype(np.int32)
    y0 = np.floor(Y).astype(np.int32)
    fx, fy = X - x0, Y - y0
    flat = m.reshape(K, -1)
    out = np.zeros(X.shape, np.float32)
    for dy, wy in ((0, 1 - fy), (1, fy)):
        for dx, wx in ((0, 1 - fx), (1, fx)):
            xi, yi = x0 + dx, y0 + dy
            ok = (xi >= 0) & (xi < W) & (yi >= 0) & (yi < H)
            v = np.take_along_axis(flat, np.clip(yi, 0, H - 1) * W + np.clip(xi, 0, W - 1), 1)
            out += np.where(ok, v, 0) * wx * wy
    return out


def templates(mask, step=1.0, scales=(0.85, 1.0, 1.18)):
    """every turn (step degrees), both mirrors and the scales of one mask, as canonical patches"""
    m = mask.astype(np.float32)
    cx, cy, r = moments(m)
    out = []
    for sc in scales:
        for mir in (1.0, -1.0):
            ang = np.radians(np.arange(0, 360, step, dtype=np.float32))[:, None]
            gx, gy = GRID[1][None] * mir, GRID[0][None]
            k = float(r) / R0 * sc
            X = cx + k * (np.cos(ang) * gx - np.sin(ang) * gy)
            Y = cy + k * (np.sin(ang) * gx + np.cos(ang) * gy)
            out.append(unit(bilinear(m, X, Y)))
    return np.concatenate(out)


M = 48  # side of the patch for the overlap check
R0V = 9.0
GRIDV = (np.mgrid[0:M, 0:M].astype(np.float32) - (M - 1) / 2).reshape(2, -1)
_overlap_cache = {}


def overlap(qmask, cmask):
    """the stricter second measure: best intersection over union of two binary masks, both centred and scaled to
    one size, over 360 turns x 2 mirrors x 7 scales of the first and 9 one-pixel shifts of the second"""
    key = qmask.tobytes() + bytes(qmask.shape)
    if key not in _overlap_cache:
        m = qmask.astype(np.float32)
        cx, cy, r = moments(m)
        out = []
        ang = np.radians(np.arange(0, 360, 1.0, dtype=np.float32))[:, None]
        for sc in (0.8, 0.87, 0.93, 1.0, 1.07, 1.15, 1.25):
            for mir in (1.0, -1.0):
                gx, gy = GRIDV[1][None] * mir, GRIDV[0][None]
                k = float(r) / R0V * sc
                out.append(bilinear(m, cx + k * (np.cos(ang) * gx - np.sin(ang) * gy),
                                    cy + k * (np.sin(ang) * gx + np.cos(ang) * gy)) > 0.5)
        T = np.concatenate(out).astype(np.float32)
        _overlap_cache[key] = (T, T.sum(1))
    T, tsum = _overlap_cache[key]
    m = cmask.astype(np.float32)
    cx, cy, r = moments(m)
    k = float(r) / R0V
    C = np.stack([bilinear(m, cx + (GRIDV[1] + dx) * k, cy + (GRIDV[0] + dy) * k) > 0.5
                  for dx in (-1, 0, 1) for dy in (-1, 0, 1)]).astype(np.float32)
    inter = C @ T.T
    return float((inter / (C.sum(1)[:, None] + tsum[None] - inter + 1e-9)).max())


def blobs(mask, max_blobs=6, min_area=12):
    """the largest 8-connected blobs of a small binary mask"""
    H, W = mask.shape
    if H * W > 20000:
        return []
    lab = np.zeros((H, W), np.int32)
    todo = mask.copy()
    res = []
    for y, x in zip(*np.nonzero(mask)):
        if not todo[y, x]:
            continue
        stack, pix = [(y, x)], []
        todo[y, x] = False
        while stack:
            cy, cx = stack.pop()
            pix.append((cy, cx))
            for ny in (cy - 1, cy, cy + 1):
                for nx in (cx - 1, cx, cx + 1):
                    if 0 <= ny < H and 0 <= nx < W and todo[ny, nx]:
                        todo[ny, nx] = False
                        stack.append((ny, nx))
        if len(pix) >= min_area:
            res.append(pix)
    res.sort(key=len, reverse=True)
    out = []
    if len(res) < 2:
        return out  # one blob is the whole mask
    for pix in res[:max_blobs]:
        b = np.zeros((H, W), bool)
        ys, xs = zip(*pix)
        b[list(ys), list(xs)] = True
        out.append(b)
    return out


def image_masks(im):
    """binary masks of one decoded texture: alpha, not the most common colour, bright, dark; whole and per blob"""
    h, w = im.shape[:2]
    out = []
    a = im[..., 3]
    q = np.round(im * 31).astype(np.int64)
    key = ((q[..., 0] * 32 + q[..., 1]) * 32 + q[..., 2]) * 32 + q[..., 3]
    vals, cnt = np.unique(key, return_counts=True)
    lum = im[..., :3].mean(-1) * (a > 0.5)
    cands = []
    if a.min() < 0.5 < a.max():
        cands.append(("alpha", a > 0.5))
    if len(vals) > 1:
        cands.append(("notmodal", key != vals[cnt.argmax()]))
    if lum.max() - lum.min() > 0.2:
        t = (lum.max() + lum.min()) / 2
        cands.append(("bright", lum > t))
        cands.append(("dark", (lum <= t) & (a > 0.5)))
    seen = set()
    for name, m in cands:
        for kind, mm in [(name, m)] + [(name + "/blob", b) for b in blobs(m)]:
            n = int(mm.sum())
            if n < 12 or n > 0.9 * h * w:
                continue
            k = mm.tobytes()
            if k in seen:
                continue
            seen.add(k)
            out.append((kind, mm))
    return out


# ---------------------------------------------------------------- the needle

def load_needle(repo):
    frames = []
    for i in range(9):
        d = open(os.path.join(repo, "port", "assets", "textures", "hud", f"sBoostGaugeArrow{i}"), "rb").read()
        t, w, h, sz = struct.unpack_from("<IIII", d, 0x40)
        if (t, w, h, sz) != (2, 32, 32, 0x800) or len(d) != 0x50 + sz:
            sys.exit(f"sBoostGaugeArrow{i}: not an 80 byte header and 32x32 RGBA16 ({t}, {w}, {h}, {sz}, {len(d)})")
        frames.append(np.frombuffer(d[0x50:], dtype=">u2").reshape(32, 32).astype(np.int32))
    return np.stack(frames)


def describe_needle(px):
    A = (px & 1).astype(bool)
    common = A.all(0)
    ys, xs = np.nonzero(common)
    print("== 1. the needle ==")
    print(f"nine files, each an 80 byte header (type 2 = RGBA16, 32x32, 2048 bytes of pixels), 1 bit of alpha, no palette")
    print(f"opaque in all nine frames: {common.sum()} pixels, box x {xs.min()}..{xs.max()} y {ys.min()}..{ys.max()}, "
          f"centre ({xs.mean():.1f}, {ys.mean():.1f}); the same pixel value in all nine: "
          f"{(common & (px == px[0]).all(0)).sum()}")
    angs, lens = [], []
    for i in range(9):
        ys, xs = np.nonzero(A[i])
        d = np.hypot(xs - HUB[0], ys - HUB[1])
        k = d.argmax()
        ang = math.degrees(math.atan2(xs[k] - HUB[0], HUB[1] - ys[k]))  # 0 = up, + = clockwise
        blade = A[i] & ~common
        angs.append(ang)
        lens.append(d[k])
        print(f"frame {i}: opaque {A[i].sum():3d}, colours {len(set(px[i][A[i]].tolist())):2d}, "
              f"transparent values {len(set(px[i][~A[i]].tolist()))}, blade outside the shared pixels {blade.sum():2d}, "
              f"tip {d[k]:.1f} px from the hub centre at {ang:+.1f} deg")
    st = np.diff(angs)
    print(f"tip angle steps between frames: min {st.min():.1f}, max {st.max():.1f}, mean {st.mean():.1f} deg; "
          f"tip distance {min(lens):.1f}..{max(lens):.1f} px")
    mir = [(A[i] == A[8 - i][:, ::-1]).mean() for i in range(4)]
    print("frame i against frame 8-i mirrored left-right (share of equal alpha): " + ", ".join(f"{m:.3f}" for m in mir))
    # clean needle: every frame turned upright about the hub centre, averaged
    ys, xs = np.mgrid[0:32, 0:32].astype(np.float32)
    acc = np.zeros((32, 32), np.float32)
    for i in range(9):
        t = math.radians(angs[i])
        dx, dy = xs - HUB[0], ys - HUB[1]
        acc += bilinear(A[i].astype(np.float32), HUB[0] + math.cos(t) * dx - math.sin(t) * dy,
                        HUB[1] + math.sin(t) * dx + math.cos(t) * dy)
    clean = acc / 9 > 0.5
    blade = clean & ~common
    wid = [int(clean[y].sum()) for y in range(32) if clean[y].any()]
    print(f"clean needle (nine frames turned upright, averaged, cut at one half): {clean.sum()} pixels, "
          f"{len(wid)} rows, widths from the tip down: {wid}; blade without the shared pixels: {blade.sum()} pixels")
    agree = []
    for i in range(9):
        t = -math.radians(angs[i])
        dx, dy = xs - HUB[0], ys - HUB[1]
        r = bilinear(clean.astype(np.float32), HUB[0] + math.cos(t) * dx - math.sin(t) * dy,
                     HUB[1] + math.sin(t) * dx + math.cos(t) * dy) > 0.5
        agree.append((r & A[i]).sum() / (r | A[i]).sum())
    print("clean needle turned back onto each frame, overlap (intersection over union): "
          + ", ".join(f"{a:.2f}" for a in agree))
    return A, clean, blade


# ---------------------------------------------------------------- search

class Query:
    def __init__(self, name, parts, step):
        """parts: [(label, mask)]"""
        self.name = name
        self.labels = [p[0] for p in parts]
        self.masks = [p[1] for p in parts]
        self.T = [templates(p[1], step) for p in parts] if step else []
        self.rings = np.concatenate([ring_batch(p[1][None]) for p in parts])

    def ring_score(self, desc):
        return (desc @ self.rings.T).max(1)

    def score(self, patches):
        """best score per patch and which part gave it"""
        best = np.full(len(patches), -1, np.float32)
        which = np.zeros(len(patches), np.int32)
        for k, T in enumerate(self.T):
            for a in range(0, len(patches), 4096):
                s = (patches[a:a + 4096] @ T.T).max(1)
                up = s > best[a:a + 4096]
                best[a:a + 4096][up] = s[up]
                which[a:a + 4096][up] = k
        return best, which


def make_control(im, angle, scale, size=64):
    """a real texture turned, scaled and recoloured, cut to 1 bit of alpha like the needle frames"""
    h, w = im.shape[:2]
    ys, xs = np.mgrid[0:size, 0:size].astype(np.float32)
    t = math.radians(angle)
    dx, dy = (xs - size / 2) / scale, (ys - size / 2) / scale
    X = w / 2 + math.cos(t) * dx - math.sin(t) * dy
    Y = h / 2 + math.sin(t) * dx + math.cos(t) * dy
    a = bilinear(im[..., 3].astype(np.float32), X, Y)
    rgb = np.stack([bilinear(im[..., c].astype(np.float32), X, Y) for c in (2, 0, 1)], -1)  # channels swapped
    rgb = 1.0 - rgb  # and inverted: no colour of the source survives
    return np.concatenate([rgb, (a > 0.5)[..., None].astype(np.float32)], -1)


VERIFY = 100  # how many of the best by correlation get the overlap check


def stage_a(files, ents, queries, top):
    tex = [e for e in ents if e["type"] == "TEXTURE" and e["format"] != "TLUT" and e["rom"] in files]
    meta, groups = [], collections.defaultdict(list)
    for ti, e in enumerate(tex):
        im = decode(files[e["rom"]][1], e["off"], e["format"], e["w"], e["h"],
                    e["tlut"] & 0xFFFFFF if e["tlut"] else None)
        if im is None:
            continue
        for kind, m in image_masks(im):
            groups[m.shape].append((len(meta), m))
            meta.append((ti, kind, m))
    patches = np.zeros((len(meta), N * N), np.float32)
    for shape, lst in groups.items():
        for a in range(0, len(lst), 2000):
            patches[[i for i, _ in lst[a:a + 2000]]] = canon_batch(np.stack([m for _, m in lst[a:a + 2000]]))
    print(f"listed textures searched: {len(tex)} (of {sum(1 for e in ents if e['type'] == 'TEXTURE' and e['format'] != 'TLUT')} "
          f"in the tables), masks: {len(meta)}")
    tis = np.array([m[0] for m in meta])
    content = [hashlib.sha1(files[e["rom"]][1][e["off"]:e["off"] + (e["w"] * e["h"] * BPP[e["format"]] + 7) // 8]).digest()
               + bytes([e["w"] % 256, e["h"] % 256]) for e in tex]
    out = {}
    for q in queries:
        s, which = q.score(patches)
        per = np.full(len(tex), -1.0)
        arg = np.zeros(len(tex), np.int64)
        for i in np.argsort(s):  # ascending: the last write per texture is its best mask
            per[tis[i]] = s[i]
            arg[tis[i]] = i
        order = np.argsort(-per)
        valid = per[per >= 0]
        pc = np.percentile(valid, [50, 90, 99, 99.9])
        print(f"-- {q.name}: best score per texture over {len(valid)} textures: median {pc[0]:.3f}, 90% {pc[1]:.3f}, "
              f"99% {pc[2]:.3f}, 99.9% {pc[3]:.3f}, max {valid.max():.3f}")
        iou = {int(ti): overlap(q.masks[which[arg[ti]]], meta[arg[ti]][2]) for ti in order[:VERIFY]}
        byiou = sorted(iou, key=lambda ti: -iou[ti])
        vals = np.array([iou[ti] for ti in byiou])
        print(f"   overlap check on the best {len(vals)} by correlation: median {np.median(vals):.2f}, "
              f"90% {np.percentile(vals, 90):.2f}, max {vals.max():.2f}; sorted by overlap:")
        rows = []
        for r, ti in enumerate(byiou[:top]):
            e = tex[ti]
            rows.append((r + 1, per[ti], e, meta[arg[ti]][1], q.labels[which[arg[ti]]]))
            print(f"   {r + 1:2d}. overlap {iou[ti]:.2f}, correlation {per[ti]:.3f}  {e['file']}/{e['symbol']} {e['format']} "
                  f"{e['w']}x{e['h']}  mask {meta[arg[ti]][1]}, against {q.labels[which[arg[ti]]]}")
        best_mask = [meta[arg[ti]][2] if per[ti] >= 0 else None for ti in range(len(tex))]
        out[q.name] = dict(per=per, order=order, tex=tex, rows=rows, content=content, iou=iou, best_mask=best_mask)
    return out


WIDTHS = (8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 56, 64)
HEIGHTS = (8, 12, 16, 20, 24, 28, 32, 40, 48, 64)


def streams(d):
    """binary pixel streams of a byte buffer: (name, bits per pixel, stream)"""
    b = np.frombuffer(d, np.uint8)
    b16 = np.frombuffer(d[:len(d) & ~1], ">u2")
    nib = np.stack([b >> 4, b & 15], -1).reshape(-1)
    return [("rgba16-alpha", 16, (b16 & 1).astype(np.uint8)), ("16bit-nonzero", 16, (b16 != 0).astype(np.uint8)),
            ("ia16-alpha", 16, ((b16 & 0xFF) > 127).astype(np.uint8)),
            ("ia8-alpha", 8, ((b & 15) > 7).astype(np.uint8)), ("8bit-high", 8, (b > 127).astype(np.uint8)),
            ("8bit-nonzero", 8, (b != 0).astype(np.uint8)),
            ("4bit-nonzero", 4, (nib != 0).astype(np.uint8)), ("4bit-high", 4, (nib > 7).astype(np.uint8)),
            ("ia4-alpha", 4, (nib & 1).astype(np.uint8))]


STREAM_BITS = {"rgba16-alpha": 16, "16bit-nonzero": 16, "ia16-alpha": 16, "ia8-alpha": 8, "8bit-high": 8,
               "8bit-nonzero": 8, "4bit-nonzero": 4, "4bit-high": 4, "ia4-alpha": 4}
KEEP = 3000  # windows kept per query and file by the ring descriptor
RESCORE = 3000  # of all files, the best by the ring descriptor that get the full score at 1 degree
_scan = None  # (scope, fast queries), read by the forked workers


def scan_file(fi):
    """one file of the scope: every window, the blob filter, the fast score. Returns counts, histograms, the best."""
    scope, queries_fast = _scan
    fname, d, startok = scope[fi]
    tested = passed = 0
    heaps = {q.name: [] for q in queries_fast}  # (score, file, start byte, stream, w, h, inverted)
    hist = {q.name: np.zeros(101, np.int64) for q in queries_fast}
    starts8 = np.nonzero(startok[::8])[0] * 8
    for sname, bits, st in streams(d) if len(starts8) else []:
        c = np.concatenate([[0], np.cumsum(st, dtype=np.int64)])
        th = np.concatenate([[0], np.cumsum(st[:-1] ^ st[1:], dtype=np.int64)])
        s0 = starts8 * 8 // bits  # start as a pixel index
        for w in WIDTHS:
            tv = np.concatenate([[0], np.cumsum(st[:-w] ^ st[w:], dtype=np.int64)])
            for h in HEIGHTS:
                n = w * h
                s = s0[s0 + n <= len(st)]
                tested += len(s)
                cnt = c[s + n] - c[s]
                inv = cnt > n // 2
                small = np.where(inv, n - cnt, cnt)
                good = (small >= 12) & (small <= 0.6 * n)  # some shape, not half noise
                good &= (th[s + n - 1] - th[s]) <= 4 * h  # at most 4 edges per row
                good &= (tv[s + n - w] - tv[s]) <= 4 * w  # and per column
                s, inv = s[good], inv[good]
                passed += len(s)
                step = max(256, 2000000 // n)
                for a in range(0, len(s), step):
                    ss, ii = s[a:a + step], inv[a:a + step]
                    m = st[ss[:, None] + np.arange(n)[None]].reshape(-1, h, w)
                    m = np.where(ii[:, None, None], 1 - m, m)
                    p = ring_batch(m)
                    for q in queries_fast:
                        sc = q.ring_score(p)
                        hist[q.name] += np.bincount(np.clip((sc * 100).astype(int), 0, 100), minlength=101)
                        hp = heaps[q.name]
                        cut = hp[-1][0] if len(hp) >= KEEP else -1
                        for k in np.nonzero(sc > cut)[0]:
                            hp.append((float(sc[k]), fname, int(ss[k]) * bits // 8, sname, w, h, bool(ii[k])))
                        if len(hp) > 2 * KEEP:
                            hp.sort(reverse=True)
                            del hp[KEEP:]
    for hp in heaps.values():
        hp.sort(reverse=True)
        del hp[KEEP:]
    return fname, tested, passed, hist, heaps


def stage_b(scope, queries_fast, queries_full, top, controls, jobs):
    """scope: [(file name, bytes, boolean array: may a window start here)]
    controls: {query name: (file name, first byte, end byte, width)}"""
    global _scan
    import multiprocessing
    import time
    _scan = (scope, queries_fast)
    tested = passed = 0
    heaps = {q.name: [] for q in queries_fast}
    hist = {q.name: np.zeros(101, np.int64) for q in queries_fast}
    t0 = time.time()
    order = sorted(range(len(scope)), key=lambda i: -int(scope[i][2].sum()))
    with multiprocessing.get_context("fork").Pool(jobs) as pool:
        for k, (fname, t, p, hi, hp) in enumerate(pool.imap_unordered(scan_file, order)):
            tested += t
            passed += p
            for name in heaps:
                hist[name] += hi[name]
                heaps[name] += hp[name]
            print(f"   {k + 1}/{len(scope)} {fname}: {t} windows, {p} scored, {time.time() - t0:.0f} s", flush=True)
    print(f"windows tested: {tested}, blob-like enough to score: {passed}")
    data = {f: d for f, d, _ in scope}
    out = {}
    for qf, qq in zip(queries_fast, queries_full):
        seen, hp = set(), []
        for x in sorted(heaps[qf.name], reverse=True):  # one window per 512 bytes of a file and stream width
            key = (x[1], x[2] >> 9, x[4])
            if key not in seen:
                seen.add(key)
                hp.append(x)
        hp = hp[:RESCORE]
        res = []
        for sc, fname, start, sname, w, h, inv in hp:  # re-score the best at 1 degree
            bits = STREAM_BITS[sname]
            st = [x for nme, b, x in streams(data[fname][start:start + (w * h * bits + 7) // 8]) if nme == sname][0]
            m = st[:w * h].reshape(1, h, w)
            m = 1 - m if inv else m
            s2, which = qq.score(canon_batch(m))
            res.append([float(s2[0]), fname, start, sname, w, h, qq.labels[which[0]], m[0].astype(bool), int(which[0])])
        res.sort(key=lambda r: -r[0])
        res = res[:VERIFY]
        for r in res:
            r.append(overlap(qq.masks[r[8]], r[7]))
        res.sort(key=lambda r: -r[9])
        vals = np.array([r[9] for r in res])
        hh = hist[qf.name]
        cum = np.cumsum(hh) / max(1, hh.sum())
        pcs = [int(np.searchsorted(cum, p)) / 100 for p in (0.5, 0.9, 0.99, 0.999)]
        print(f"-- {qf.name}: ring descriptor of all scored windows: median {pcs[0]:.2f}, 90% {pcs[1]:.2f}, "
              f"99% {pcs[2]:.2f}, 99.9% {pcs[3]:.2f}; {len(hp)} re-scored at 1 degree, the best {len(res)} of them have correlation "
              f"{min(r[0] for r in res):.3f}..{max(r[0] for r in res):.3f} and overlap median {np.median(vals):.2f}, "
              f"max {vals.max():.2f}")
        if qf.name in controls:
            cf, c0, c1, cw = controls[qf.name]
            hit = [k for k, r in enumerate(res)
                   if r[1] == cf and r[2] < c1 and r[2] + r[4] * r[5] * STREAM_BITS[r[3]] // 8 > c0]
            other = [r for r in res if not (r[1] == cf and r[2] < c1 + 4096 and r[2] + 4096 > c0)]
            if hit:
                r = res[hit[0]]
                print(f"   CONTROL: first window on the source's bytes is rank {hit[0] + 1} by overlap, overlap "
                      f"{r[9]:.2f}, correlation {r[0]:.3f} (stream {r[3]}, {r[4]}x{r[5]}, true width {cw}); best window "
                      f"away from it: overlap {other[0][9] if other else float('nan'):.2f}")
            else:
                print(f"   CONTROL: no window on the source's bytes in the best {len(res)}: FAILED")
            out[qf.name] = (hit[0] + 1 if hit else None, res)
        else:
            for k, r in enumerate(res[:top]):
                print(f"   {k + 1:2d}. overlap {r[9]:.2f}, correlation {r[0]:.3f}  {r[1]} +0x{r[2]:X}  stream {r[3]}, "
                      f"{r[4]}x{r[5]}, against {r[6]}")
            out[qf.name] = (None, res)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("rom", help="US 1.0 ROM, big-endian (.z64)")
    ap.add_argument("--raw", action="store_true", help="also scan the bytes no asset table explains (minutes)")
    ap.add_argument("--top", type=int, default=15)
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2), help="processes for --raw")
    ap.add_argument("--raw-files", help="only these files of the raw scope, comma separated (for a quick look)")
    ap.add_argument("--dump", help="folder for pictures of the best candidates (game data: keep it out of git)")
    args = ap.parse_args()

    rom = open(args.rom, "rb").read()
    sha = hashlib.sha1(rom).hexdigest()
    if sha != US10_SHA1:
        sys.exit(f"not the US 1.0 ROM in big-endian order (SHA-1 {sha})")
    px = load_needle(REPO)
    A, clean, blade = describe_needle(px)

    print("\n== 2. the ROM ==")
    files = rom_files(rom)
    ents = yaml_entries(REPO)
    print(f"files in the ROM's table: {len(files)}, MIO0-compressed: {sum(1 for f in files.values() if f[2])}, "
          f"decompressed bytes: {sum(len(f[1]) for f in files.values())}")
    cov = coverage(files, ents)
    tot = collections.Counter()
    for c in cov.values():
        tot.update(dict(zip(*np.unique(c["cov"], return_counts=True))))
    names = {1: "listed textures, palettes, vertices", 2: "display lists", 3: "vertices named by a display list",
             4: "textures named by a display list but not listed", 6: "palettes named by a display list",
             5: "entries of unknown size, up to the next entry", 0: "explained by nothing"}
    print(f"asset tables: {len(ents)} entries in {len(cov)} files; bytes by what explains them:")
    for k in (1, 2, 3, 4, 6, 5, 0):
        print(f"   {tot.get(k, 0):9d}  {names[k]}")
    zero_free = sum(int(((c["cov"] == 0) & (np.frombuffer(files[r][1], np.uint8) != 0)).sum()) for r, c in cov.items())
    print(f"   of the bytes explained by nothing, not zero: {zero_free}")
    common = [c for c in cov.values() if c["file"] == "ast_common"][0]
    crom0 = [r for r, c in cov.items() if c["file"] == "ast_common"][0]
    cnz = int(((common["cov"] == 0) & (np.frombuffer(files[crom0][1], np.uint8) != 0)).sum())
    print(f"   ast_common (the HUD's file): explained by nothing {int((common['cov'] == 0).sum())} (not zero: {cnz}), unknown size "
          f"{int((common['cov'] == 5).sum())}, textures only a display list names {len(common['timg'])}")
    nolist = [(i, len(d)) for v, (i, d, c) in files.items() if v not in cov]
    print(f"files without an asset table: {len(nolist)} ({sum(n for _, n in nolist)} bytes; indices "
          f"{[i for i, _ in nolist]}; {AUDIO_FILES} are audio)")

    # the needle's colours against the palettes and textures of ast_common
    cols = set(px[(px & 1) == 1].tolist())
    crom = [r for r, c in cov.items() if c["file"] == "ast_common"][0]
    cd = files[crom][1]
    inpal, intex = set(), set()
    for e in ents:
        if e["rom"] != crom or e["type"] != "TEXTURE":
            continue
        if e["format"] == "TLUT":
            inpal |= cols & set(np.frombuffer(cd[e["off"]:e["off"] + 2 * e["colors"]], ">u2").tolist())
        elif e["format"] == "RGBA16":
            intex |= cols & set(np.frombuffer(cd[e["off"]:e["off"] + 2 * e["w"] * e["h"]], ">u2").tolist())
    print(f"the needle's {len(cols)} opaque colour values: {len(inpal)} are in a palette of ast_common, "
          f"{len(intex)} in an RGBA16 texture of ast_common")

    whole = [("clean needle", clean)] + [(f"frame {i}", A[i]) for i in range(9)]

    bysym = {e["symbol"]: e for e in ents if e["type"] == "TEXTURE" and e["format"] != "TLUT" and e["rom"] in files}
    ctl = []
    for sym, ang, sc in CONTROLS:
        e = bysym[sym]
        im = decode(files[e["rom"]][1], e["off"], e["format"], e["w"], e["h"], e["tlut"] & 0xFFFFFF if e["tlut"] else None)
        c = make_control(im, ang, sc)
        ctl.append((e, c[..., 3] > 0.5, f"control {sym} ({e['format']} {e['w']}x{e['h']}, turned {ang:.0f} deg, "
                    f"scaled x{sc}, recoloured)"))

    print("\n== 3. stage A: listed textures at their true format and size, 1 degree steps ==")
    qa = [Query("needle", whole, 1.0), Query("blade alone", [("blade alone", blade)], 1.0)] + \
         [Query(name, [("control", m)], 1.0) for _, m, name in ctl]
    ra = stage_a(files, ents, qa, args.top)
    ok_a = True
    for e, m, name in ctl:
        r = ra[name]
        si = r["tex"].index(e)
        bm = r["best_mask"]

        def twin(ti):  # the same bytes, or the same shape, in another texture: counted as the source
            return r["content"][ti] == r["content"][si] or (
                bm[ti] is not None and bm[ti].shape == bm[si].shape and bool((bm[ti] == bm[si]).all()))
        twins = [ti for ti in range(len(r["tex"])) if twin(ti)]
        others = np.array([r["per"][ti] for ti in range(len(r["tex"])) if ti not in twins])
        rank = 1 + int((others > r["per"][si]).sum())
        oi = [v for ti, v in r["iou"].items() if ti not in twins]
        orank = 1 + sum(1 for v in oi if v > r["iou"].get(si, 0))
        passed = rank <= args.top and orank <= args.top  # among those a person then looks at
        print(f"CONTROL A {e['symbol']}: source is rank {rank} by correlation with {r['per'][si]:.3f} (best other "
              f"{others.max():.3f}), rank {orank} by overlap with {r['iou'].get(si, 0):.2f} (best other {max(oi):.2f}); "
              f"{len(twins) - 1} textures with the same bytes or the same mask count as the source: "
              f"{'passed' if passed else 'FAILED'}")
        ok_a &= passed

    # what a true copy would score: the needle itself, turned, scaled, mirrored and drawn anew
    print("planted copies (frame 4, frame 1 and the blade alone; five turns and scales each), scored like a texture:")
    for label, m, q in (("whole needle", A[4], qa[0]), ("whole needle", A[1], qa[0]), ("blade alone", blade, qa[1])):
        got = []
        for ang, sc, mir, size in ((77, 1.5, False, 64), (200, 0.6, True, 32), (123, 2.0, False, 64),
                                   (311, 1.0, True, 32), (19, 0.8, False, 32)):
            im = np.zeros(m.shape + (4,), np.float32)
            im[..., 3] = m[:, ::-1] if mir else m
            c = make_control(im, ang, sc, size)[..., 3] > 0.5
            sc1, w1 = q.score(canon_batch(c[None]))
            got.append((float(sc1[0]), overlap(q.masks[w1[0]], c)))
        print(f"   {label}: correlation {min(g[0] for g in got):.3f}..{max(g[0] for g in got):.3f}, "
              f"overlap {min(g[1] for g in got):.2f}..{max(g[1] for g in got):.2f}")

    ok_b = None
    if args.raw:
        print("\n== 4. stage B: bytes no table explains and files without a table, every 8th byte ==")
        scope, controls = [], {}
        for rom_start, c in cov.items():
            d = files[rom_start][1]
            ok = (c["cov"] == 0) | (c["cov"] == 5)
            for e, m, name in ctl:  # the controls' bytes go into the scope as if no table listed them
                if e["rom"] == rom_start:
                    size = e["w"] * e["h"] * BPP[e["format"]] // 8
                    ok[max(0, e["off"] - 2048):e["off"] + size + 2048] = True
                    controls[name] = (c["file"], e["off"], e["off"] + size, e["w"])
            scope.append((c["file"], d, ok))
        for v, (i, d, comp) in files.items():
            if v not in cov and i not in AUDIO_FILES:
                scope.append((f"file {i}", d, np.ones(len(d), bool)))
        print(f"scope: {sum(int(ok.sum()) for _, _, ok in scope)} bytes in {len(scope)} files "
              f"(of them {3 * 4096}+ around the three controls), {len(WIDTHS) * len(HEIGHTS)} sizes, 9 pixel streams")
        qf = [Query("needle", [("clean needle", clean), ("frame 0", A[0]), ("frame 2", A[2])], 0),
              Query("blade alone", [("blade alone", blade)], 0)] + [Query(name, [("control", m)], 0) for _, m, name in ctl]
        if args.raw_files:
            scope = [x for x in scope if x[0] in args.raw_files.split(",")]
        qfull = {q.name: q for q in qa}
        rb = stage_b(scope, qf, [qfull[q.name] for q in qf], args.top, controls, args.jobs)
        ok_b = all(rb[name][0] is not None and rb[name][0] <= args.top for _, _, name in ctl)
        print(f"CONTROL B: {'passed' if ok_b else 'FAILED'} "
              f"(ranks {[rb[name][0] for _, _, name in ctl]})")

    if args.dump:
        from PIL import Image
        os.makedirs(args.dump, exist_ok=True)
        for r, sc, e, kind, label in ra["needle"]["rows"] + [(100 + x[0],) + x[1:] for x in ra["blade alone"]["rows"]]:
            im = decode(files[e["rom"]][1], e["off"], e["format"], e["w"], e["h"], e["tlut"] & 0xFFFFFF if e["tlut"] else None)
            a = im[..., 3:4]
            rgb = im[..., :3] * a + (1 - a) * np.array([1.0, 0.0, 1.0])
            Image.fromarray((rgb * 255).astype(np.uint8)).resize((e["w"] * 6, e["h"] * 6), Image.NEAREST).save(
                os.path.join(args.dump, f"a{r:02d}_{e['symbol']}.png"))
        if args.raw:
            for k, r in enumerate(rb["needle"][1][:args.top] + rb["blade alone"][1][:args.top]):
                sc, fname, start, sname, w, h, label = r[:7]
                d = [x for f, x, _ in scope if f == fname][0]
                bits = STREAM_BITS[sname]
                st = [x for nme, b, x in streams(d[start:start + w * h * bits // 8]) if nme == sname][0]
                Image.fromarray(st[:w * h].reshape(h, w) * 255).resize((w * 6, h * 6), Image.NEAREST).save(
                    os.path.join(args.dump, f"b{k + 1:02d}_{w}x{h}_{sname}.png"))

    print("\n== controls ==")
    print(f"stage A: {'passed' if ok_a else 'FAILED'}; stage B: "
          f"{'not run' if ok_b is None else 'passed' if ok_b else 'FAILED'}")
    return 0 if ok_a and ok_b is not False else 1


if __name__ == "__main__":
    sys.exit(main())
