# Publishing a mod

## 1. The repository

```
my-mod/
  mod/
    eidos_mod.txt
    rules/...
    (the rest of the mod folder)
  .github/workflows/release.yml
  README.md
  LICENSE
```

The mod folder is the folder eidos loads (`docs/mods.md` in eidos), with its
manifest. It can sit elsewhere in the repository; the workflow takes its
path.

The manifest needs a `description`; see [mod-json.md](mod-json.md) for the
keys the index reads.

## 2. The workflow

`.github/workflows/release.yml`:

```yaml
name: release
on:
  push:
    tags: ["v*"]
permissions:
  contents: write
jobs:
  release:
    uses: Project-Holon/katalogos/.github/workflows/build-mod.yml@main
    # with:
    #   mod-dir: path/to/the/mod/folder   (default: mod)
    #   build: sh build.sh                (a step before packing; clang and
    #                                      lld are installed for it, to build
    #                                      a wasm module)
```

## 3. A release

```
git tag v1.0.0
git push origin v1.0.0
```

The workflow packs the folder into `mod.bw`, writes `mod.json`, checks both
and publishes the release `v1.0.0` with them. If the manifest has a
`version`, it must say `1.0.0` too.

## 4. The listing

Open a pull request on katalogos adding the repository to `index.json`:

```json
{ "repo": "you/my-mod" }
```

Its check reads the newest release's files as the launcher does. Later
releases need no change in katalogos.

## Trying it locally

With Rust installed, the tool the workflow uses (from
[morphe](https://github.com/Project-Holon/morphe)):

```
cargo install --git https://github.com/Project-Holon/morphe --tag v0.6.1 katalogos --locked
katalogos pack my-mod/mod --repo you/my-mod --version 1.0.0 --out dist
katalogos inspect dist/mod.json dist/mod.bw
```
