# katalogos

The index of mods for [eidos](https://github.com/Project-Holon/eidos), the
native port of Pokémon Black and White. The eidos launcher reads it to list
mods and install them.

This repository holds no mods. `index.json` is a list of repositories; each
mod lives in its own repository, and that repository's releases carry the
mod's two files:

- `mod.bw`: the mod folder as one package (the format eidos and kenon use);
- `mod.json`: what the launcher shows and checks before installing
  ([docs/mod-json.md](docs/mod-json.md)).

The launcher downloads both from
`https://github.com/<repo>/releases/latest/download/`, so a new release of a
mod reaches players without a change here.

## The index

```json
{
  "format": 1,
  "mods": [
    { "repo": "Project-Holon/faint-penalty", "official": true },
    { "repo": "someone/some-mod" },
    { "repo": "someone/pinned-mod", "version": "1.2.0" }
  ]
}
```

| Field | Meaning |
|---|---|
| `repo` | the mod's GitHub repository, `owner/name` |
| `official` | a mod from Project Holon; the launcher marks it. Only we set it |
| `version` | optional: a fixed release (`x.y.z`) instead of the newest one |

## Listing a mod

1. Put the mod folder in a repository and add the release workflow
   ([docs/publishing.md](docs/publishing.md)). Pushing a tag such as
   `v1.0.0` then publishes `mod.bw` and `mod.json`.
2. Open a pull request here adding one line to `index.json`.

The check on the pull request downloads the mod's files the way the launcher
does and checks them: the `mod.json` format, the package's hash and size, and
the package's own limits. The same check runs once a day on every listed mod.

## What a listing means

A listed mod is checked for its format, not for its content. Its author can
publish a new version at any time. A mod with code (`has_code` in its
`mod.json`) runs inside the game with the game's own access; install it only
if you trust its author. A mod that breaks the rules of this index, or the
game, is taken off the list.

## The tool

`tool/` is the packer and the checker (`katalogos pack`, `check`, `inspect`).
The release workflow downloads its build from this repository's releases;
a tag `tool-v<version>` publishes a new build.

## License

MIT (the tool, the workflows, the documents). Each mod has its author's own
license.
