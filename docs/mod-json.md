# mod.json, format 1

A mod's release carries `mod.json` beside `mod.bw`. The release workflow
writes it from the mod's manifest (`eidos_mod.txt`) and its package; nobody
writes it by hand.

```json
{
  "format": 1,
  "id": "someone/some-mod",
  "name": "Some Mod",
  "version": "1.0.0",
  "author": "someone",
  "description": "What the mod does, in a sentence or two.",
  "repo": "someone/some-mod",
  "official": false,
  "games": ["black", "white"],
  "tags": ["challenge"],
  "has_code": false,
  "rules": 1,
  "package": {
    "file": "mod.bw",
    "sha256": "…",
    "size": 612,
    "files": 2,
    "unpacked": 290
  }
}
```

| Field | From | Meaning |
|---|---|---|
| `format` | | this document's version, 1 |
| `id` | where the mod lives | another mod: `owner/name` of its repository, in lower case; an official mod: `katalogos/<id>`. It names the mod everywhere |
| `name` | manifest `name` | the mod's name (the folder's name when the manifest has none) |
| `version` | the release | `x.y.z`; the manifest's `version` must be the same |
| `author` | manifest `author` | the repository's owner when the manifest has none |
| `description` | manifest `description` | required: a sentence or two for players |
| `repo` | where the mod lives | `owner/name` as GitHub spells it; `Project-Holon/katalogos` for an official mod |
| `path` | | an official mod's folder, `mods/<id>`; absent for another mod |
| `official` | | true for a mod of this repository |
| `multiplayer` | manifest `multiplayer` | the mod's multiplayer number, when it takes part in multiplayer |
| `games` | manifest `game` | `black`, `white` or both; both when the manifest names no cartridge |
| `game_code` | manifest `game` | the cartridge code (`IRBO`: Black, English), when there is one |
| `tags` | manifest `tags` | lower case, from `tags = challenge, rules` |
| `icon` | manifest `icon` | `{"item": 4}` (an item's bag icon), `{"species": 25}` (a Pokemon's sprite), both drawn from the player's own ROM, or `{"file": "icon.png"}`: the mod's own picture, published beside `mod.json` |
| `api` | manifest `api` | the mod interface version the mod needs |
| `has_code` | manifest `code` | the mod runs its own code in the game |
| `rules` | manifest `rule` lines | how many rules the mod adds |
| `package.file` | | always `mod.bw` |
| `package.sha256` | the package | the SHA-256 of `mod.bw`, lower-case hex |
| `package.size` | the package | the size of `mod.bw` in bytes |
| `package.files` | the package | how many files it unpacks to |
| `package.unpacked` | the package | their total size in bytes |

The check refuses a `mod.json` that says `official` without being this
repository's own (`repo`, `id` and `path` must agree).

## The manifest keys the index reads

eidos skips the keys it does not use, so these can sit in any manifest:

```
name        = Some Mod
description = What the mod does, in a sentence or two.
author      = someone
version     = 1.0.0
game        = IRBO
tags        = challenge, money
icon        = item 4
```

`description` is the only one the packer requires.

`icon` is one of `item <n>` (1 to 626), `species <n>` (1 to 649) or the
name of a PNG in the mod folder (square, at most 256 by 256 pixels and
64 KB). Game pictures are drawn from the player's own ROM, so no game art is
ever published.

## What the launcher checks

Before it installs a mod, the launcher reads `mod.json`, downloads `mod.bw`,
and refuses it when the size or the SHA-256 differ, when the package breaks
the package limits, or when `api` is newer than the interface its eidos
speaks.
