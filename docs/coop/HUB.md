# Co-op Anything hub

This project follows [Co-op Anything](https://github.com/cranebeaker/coop-anything). The hub holds the shared playbook
and a directory of co-op projects; each game's code lives in its own repository, like this one. Give back to it when
a phase finishes (hub AGENTS.md, "Sending what you learned back").

## Not done yet

1. Open a "claim a game" issue on the hub for Star Fox 64 so others can find this project.
2. Once this repository is public, add a directory entry and a write-up (`games/star-fox-64/README.md` from the
   hub's `templates/game.md`) in a draft PR from a fork of the hub, branch `game/star-fox-64`.

## Draft directory entry

For `data/directory.yaml` in the hub. Fill in the URL and dates when publishing; keep the scope honest.

```yaml
- game: Star Fox 64
  year: 1997
  engine: N64 decompilation port (Starship, libultraship)
  project: Starship Co-op
  url: https://github.com/Clodmony/starship-coop
  other_urls:
  - https://github.com/HarbourMasters/Starship
  kind: fork
  status: prototype
  coop_scope:
    players_visible: unknown
    story: unknown
    enemies: unknown
    world_state: unknown
    items: unknown
  players: "2"
  networking: unknown
  open_source: true
  last_activity: YYYY-MM
  qa: none
  notes: Campaign co-op on the Starship PC port. Pre-alpha; sync model chosen after a determinism test.
  sources:
  - https://github.com/Clodmony/starship-coop
  checked: YYYY-MM-DD
  made_with_coop_anything: true
```
