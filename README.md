# katalogos

The index of mods for [eidos](https://github.com/Project-Holon/eidos), the
native port of Pokémon Black and White. The eidos launcher reads it to list
mods and install them.

`index.json` lists two kinds of mods:

- **Official mods** live in this repository, in `mods/<id>`. Being here is
  what makes a mod official.
- **Other mods** live in their authors' own repositories. The index only
  links them; each one's releases carry its files.

Every mod's release carries two files:

- `mod.bw`: the mod folder as one package (the format eidos and kenon use);
- `mod.json`: what the launcher shows and checks before installing
  ([docs/mod-json.md](docs/mod-json.md)).

## The index

```json
{
  "format": 1,
  "mods": [
    { "mod": "nuzlocke", "version": "1.0.0" },
    { "repo": "someone/some-mod" },
    { "repo": "someone/pinned-mod", "version": "1.2.0" }
  ]
}
```

| Field | Meaning |
|---|---|
| `mod` | an official mod: its folder `mods/<id>`. Its `version` is required |
| `repo` | another mod's GitHub repository, `owner/name` |
| `version` | for `mod`, the released version; for `repo`, an optional fixed release (`x.y.z`) instead of the newest one |
| `unofficial` | for `mod`: `true` for a mod kept here that is not official. A launcher that knows the mark lists it after the official mods, without the Official badge |

Where the launcher reads the files:

- an official mod: `https://github.com/Project-Holon/katalogos/releases/download/<id>-v<version>/`;
- another mod: `https://github.com/<repo>/releases/latest/download/`, or
  `.../releases/download/v<version>/` when pinned.

## The official mods

| Mod | What it does |
|---|---|
| [Nuzlocke](mods/nuzlocke) | catch only the first wild Pokemon of each area; a Pokemon that faints is gone |
| [Soul Link](mods/soul-link) | a Nuzlocke for two or more players in one multiplayer game, with linked pairs |
| [Exp. Share](mods/exp-share) | the whole party gains experience in every battle; an option switches it off |
| [Smart Trainers](mods/smart-trainers) | trainers battle like a thinking player: they learn your Pokemon in battle, switch and set up; needs eidos 0.2.3 |
| [RetroAchievements](mods/retroachievements) | earn RetroAchievements while you play: sign in from its own Settings tab; needs eidos 0.2.7 |

## Unofficial conversions

Drayano's ROM hacks, converted into eidos mods from their original patches.
They are not Drayano's own releases and he does not support them; his pages
are linked in each mod's README. Each needs eidos 0.2.7 and its own game.
They are listed from their own repositories. Their version is Drayano's
(3.1), and the last number counts the conversion's own releases.

| Mod | What it is |
|---|---|
| [Blaze Black (Full)](https://github.com/OukilIl/blaze-black-full) | Blaze Black 3.1, Full: new trainer teams, wild Pokemon, items, trades, and changed Pokemon and moves; Pokemon Black |
| [Blaze Black (Clean)](https://github.com/OukilIl/blaze-black-clean) | Blaze Black 3.1, Clean: the Full changes without the changed Pokemon, moves and attacks; Pokemon Black |
| [Volt White (Full)](https://github.com/OukilIl/volt-white-full) | Volt White 3.1, Full; Pokemon White |
| [Volt White (Clean)](https://github.com/OukilIl/volt-white-clean) | Volt White 3.1, Clean; Pokemon White |

A new version of an official mod: raise `version` in its `mod/eidos_mod.txt`
and in `index.json`, in the same pull request. Once it is on `main`, the
publish workflow builds the mod (`build.sh` when it has one), packs it and
releases it as `<id>-v<version>`.

## Listing your mod

1. Put the mod folder in a repository of your own and add the release
   workflow ([docs/publishing.md](docs/publishing.md)). Pushing a tag such
   as `v1.0.0` then publishes `mod.bw` and `mod.json`.
2. Open a pull request here adding one line to `index.json`:
   `{ "repo": "you/your-mod" }`.

The check on the pull request downloads the mod's files the way the launcher
does and checks them: the `mod.json` format, the package's hash and size, and
the package's own limits. The same check runs once a day on every listed mod.

## What a listing means

A listed mod is checked for its format, not for its content. Its author can
publish a new version at any time. A mod with code (`has_code` in its
`mod.json`) runs inside the game; install it only if you trust its author.
A mod that breaks the rules of this index, or the game, is taken off the
list.

## The tool

The workflows use the `katalogos` packer and checker from
[morphe](https://github.com/Project-Holon/morphe) (`crates/katalogos`),
built from a fixed tag. To try a mod locally:

```
cargo install --git https://github.com/Project-Holon/morphe --tag v0.13.3 katalogos --locked
katalogos pack path/to/mod --repo you/your-mod --version 1.0.0 --out dist
katalogos inspect dist/mod.json dist/mod.bw
```

## License

MIT (the official mods, the workflows, the documents). Each other mod has
its author's own license.
