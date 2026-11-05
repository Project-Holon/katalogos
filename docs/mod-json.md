# mod.json, format 1

A mod's release carries `mod.json` beside `mod.bw`. The release workflow
writes it from the mod's manifest (`eidos_mod.txt`) and its package; nobody
writes it by hand.

```json
{
  "format": 1,
  "id": "project-holon/faint-penalty",
  "name": "Faint Penalty",
  "version": "1.0.0",
  "author": "Project-Holon",
  "description": "Lose 1000 money when one of your Pokemon faints.",
  "repo": "Project-Holon/faint-penalty",
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
| `id` | the repository | `owner/name` in lower case; it names the mod everywhere |
| `name` | manifest `name` | the mod's name (the repository's name when the manifest has none) |
| `version` | the release tag | `x.y.z`; a manifest `version` must be the same |
| `author` | manifest `author` | the repository's owner when the manifest has none |
| `description` | manifest `description` | required: a sentence or two for players |
| `repo` | the repository | `owner/name` as GitHub spells it |
| `games` | manifest `game` | `black`, `white` or both; both when the manifest names no cartridge |
| `game_code` | manifest `game` | the cartridge code (`IRBO`: Black, English), when there is one |
| `tags` | manifest `tags` | lower case, from `tags = challenge, rules` |
| `api` | manifest `api` | the mod interface version a mod with code needs |
| `has_code` | manifest `code` | the mod runs its own code in the game |
| `rules` | manifest `rule` lines | how many rules the mod adds |
| `package.file` | | always `mod.bw` |
| `package.sha256` | the package | the SHA-256 of `mod.bw`, lower-case hex |
| `package.size` | the package | the size of `mod.bw` in bytes |
| `package.files` | the package | how many files it unpacks to |
| `package.unpacked` | the package | their total size in bytes |

## The manifest keys the index reads

eidos skips the keys it does not use, so these can sit in any manifest:

```
name        = Faint Penalty
description = Lose 1000 money when one of your Pokemon faints.
author      = Project Holon
version     = 1.0.0
game        = IRBO
tags        = challenge, money
```

`description` is the only one the packer requires.

## What the launcher checks

Before it installs a mod, the launcher reads `mod.json`, downloads `mod.bw`,
and refuses it when the size or the SHA-256 differ, when the package breaks
the package limits, or when `api` is not the interface its eidos speaks.
