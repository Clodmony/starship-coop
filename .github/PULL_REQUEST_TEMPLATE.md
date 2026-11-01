## What and why

Closes #

<!-- One issue per PR. What changed, and why this way. -->

## Upstream footprint

<!-- Upstream files touched (outside src/port/coop/, docs/coop/, tools/coop/), each change marked `// COOP:`.
     Write "none" if there are none. -->

## QA

- Level reached: none / community-reports / automated / verified-playthrough
- How it was tested (command, number of clients, what was checked):
- Identity (commit, ROM US 1.0/1.1, OS, compiler, backend):

## Checklist

- [ ] Co-op off behaves like upstream (nothing new runs without an active session)
- [ ] New network messages have ids reserved in `docs/coop/PROTOCOL.md`
- [ ] Findings about the game are in `docs/coop/NOTES.md`; architecture changes have a decision record
- [ ] `tools/coop/check.sh` passes; C/C++ formatted with `.clang-format`
- [ ] No ROMs, `.o2r`/`.otr`, extracted assets, tokens or personal info
- [ ] If an AI agent wrote this: it says so here, and the person running the agent reviewed the diff
