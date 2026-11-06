// Test helper for tools/coop/version_test.py, loaded with LD_PRELOAD: writes one shown frame as a PPM, so a test can
// read what a player would see without a window and without adding anything to the game.
//   SF64_COOP_SHOT_FRAME=<n>     the swap to capture, counted from 1
//   SF64_COOP_SHOT_FILE=<file>   where to write it
// Linux, OpenGL only. The game calls SDL_GL_SwapWindow once per shown frame (libultraship, gfx_sdl2.cpp).

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

#define GL_BACK 0x0405
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_RGB 0x1907
#define GL_UNSIGNED_BYTE 0x1401
#define GL_READ_FRAMEBUFFER 0x8CA8

void* SDL_GL_GetProcAddress(const char* name);
void SDL_GL_GetDrawableSize(void* window, int* w, int* h);

static void WriteFrame(void* window, const char* path) {
    void (*bindFramebuffer)(unsigned, unsigned) = SDL_GL_GetProcAddress("glBindFramebuffer");
    void (*readBuffer)(unsigned) = SDL_GL_GetProcAddress("glReadBuffer");
    void (*pixelStore)(unsigned, int) = SDL_GL_GetProcAddress("glPixelStorei");
    void (*readPixels)(int, int, int, int, unsigned, unsigned, void*) = SDL_GL_GetProcAddress("glReadPixels");
    int w = 0;
    int h = 0;
    unsigned char* pixels;
    FILE* out;

    SDL_GL_GetDrawableSize(window, &w, &h);
    pixels = malloc((size_t) w * h * 3);
    out = fopen(path, "wb");
    if (pixels != NULL && out != NULL) {
        bindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        readBuffer(GL_BACK);
        pixelStore(GL_PACK_ALIGNMENT, 1);
        readPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels);
        fprintf(out, "P6\n%d %d\n255\n", w, h);
        for (int y = h - 1; y >= 0; y--) { // OpenGL rows start at the bottom.
            fwrite(pixels + (size_t) y * w * 3, 3, w, out);
        }
    }
    if (out != NULL) {
        fclose(out);
    }
    free(pixels);
}

void SDL_GL_SwapWindow(void* window) {
    static void (*realSwap)(void*);
    static long swaps;
    const char* frame = getenv("SF64_COOP_SHOT_FRAME");
    const char* path = getenv("SF64_COOP_SHOT_FILE");

    if (realSwap == NULL) {
        realSwap = dlsym(RTLD_NEXT, "SDL_GL_SwapWindow");
    }
    if (frame != NULL && path != NULL && ++swaps == atol(frame)) {
        WriteFrame(window, path);
    }
    realSwap(window);
}
