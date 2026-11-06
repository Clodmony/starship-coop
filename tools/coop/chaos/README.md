# Chaos relay

A UDP relay that makes a link bad on purpose: delay, jitter, loss, bursts of loss, reordering, duplicates, bit flips,
a bandwidth cap and outages, all seeded so a failure can be repeated. `tools/coop/net_test.py` runs the transport
through it.

Copied unchanged from [Co-op Anything](https://github.com/cranebeaker/coop-anything), `tools/chaos/` at commit
`a71be2ca` (MIT, see `LICENSES/coop-anything-MIT.txt`). Only the files needed to run the relay are here. Its usage,
profiles and tests are documented in the hub's
[README](https://github.com/cranebeaker/coop-anything/blob/main/tools/chaos/README.md). Fix bugs in the hub, then copy
the files again.
