# RetroAchievements

Earn [RetroAchievements](https://retroachievements.org) while you play
Pokemon Black or White on eidos.

Needs eidos 0.2.7 or newer (mod interface 7).

## What it does

- **Sign in** from the mod's own RetroAchievements tab in Settings: type
  your user name and password on eidos's keyboard and choose Sign in. The
  mod sends the password once and clears it at once. It keeps only the
  sign-in token, in `login.txt` in the mod's folder of your profile. The
  password is never kept, and nothing goes in the game save. The next time
  you play, the mod signs in with the token.
- **Achievements:** when the game loads, the mod loads the game's
  achievement set and checks it each frame against the game's memory.
- **Unlocks** show as a pop-up in the interface style you chose. Unlocks
  made while offline are kept and sent when the network is back; the tab
  says when some are waiting.
- **Sign out** in the same tab deletes `login.txt`.

## Hardcore mode

Hardcore is read once, when the game loads; a change takes effect the next
time the game starts. eidos refuses it while another mod that changes the
game is on, or while a test switch that changes the game is set. Then the
mod plays in normal mode, and the tab says why hardcore is off.

## How it is made

- `src/ra_mod.c`: the mod. It runs rcheevos's `rc_client`. RetroAchievements'
  DS addresses 0 to 0x3FFFFF are main RAM at 0x02000000, and 0x1000000 to
  0x1003FFF are the data TCM at 0x02FE0000; the mod reads both through
  `eidos_read`.
- `src/ra_host.c`: the one file that names the game's calls (web requests,
  the game's RetroAchievements hash, the clock, the pop-up, the settings
  tab, the mod's files, the hardcore check).
- `src/vendor/rcheevos`: RetroAchievements' rcheevos 12.5.0, unchanged
  (MIT, `src/vendor/rcheevos/LICENSE`). Its public headers are in `include/`.
  The ROM hashing part (rhash) is left out except md5, which the runtime
  uses; the game gives the hash.
- `include/pthread.h`: a mod runs on one thread, so rcheevos's locks do
  nothing.
- `src/libc`, `include/`: the mod C library (morphe's modsdk/v7/libc; its
  README says where each file comes from and their licenses).
- `mod/settings.txt`: the RetroAchievements tab.

## Credits

- **Mythic Keeper** ([MythicForge](https://github.com/MythicForge)) first
  brought RetroAchievements to eidos, built into the game, in
  [eidos pull request 46](https://github.com/Project-Holon/eidos/pull/46).
  This mod follows that work as a mod outside the game, with the same
  features: rcheevos's rc_client on the game's memory, the sign-in, the
  unlock banner, and the game's hash (which eidos now gives every mod).
- [RetroAchievements](https://retroachievements.org) and its rcheevos
  library (MIT).

## Build

`sh build.sh` (clang with the wasm32 target and lld; set `CLANG` to use
another compiler).
