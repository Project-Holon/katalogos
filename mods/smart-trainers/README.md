# Smart Trainers (work in progress)

Trainers battle like a thinking player instead of the game's point scripts.
Not in the index yet.

## What the trainer knows

- Its own team, exactly: stats, moves, abilities, items, HP.
- Of your Pokemon, only what a player at the screen could know: species,
  level, types, the species' base stats, the HP bar, stat stages, status,
  and what the battle shows (the moves used, the abilities and items that
  announce themselves). It never reads your Pokemon's real stats, IVs, EVs,
  nature, held item or moves it has not seen.
- The moves your Pokemon could have: the species' level-up moves up to its
  level (public knowledge), the strongest per type kept as candidates.

## What it learns during a battle

- Each hit it takes becomes an estimate of that Pokemon's attacking power
  (physical and special), and each hit it deals an estimate of its
  defences. The hits are remembered with the stat stages they happened at,
  so an exact repeat is predicted from memory and anything else from the
  estimate.
- How trained your team is as a whole: a Pokemon it has not met yet starts
  from what the earlier ones showed.
- Turn order bounds your Pokemon's speed.
- Move choices narrow the candidates: when your Pokemon uses a weaker move
  where a move of another type would have done clearly more, it probably
  does not have that type (a Serperior that uses Scratch when a grass move
  would hit harder likely has no grass move).
- Abilities: Contrary, Defiant and Competitive (no stat drops on them),
  Clear Body and the like, Levitate and the absorbing abilities, Wonder
  Guard, Unaware, Thick Fat; possible abilities count by the odds until one
  shows.

## How it chooses

Every option (each move, setting up, a status move, healing, switching to
each bench Pokemon) is scored with the same yardstick: how many turns each
side needs to knock the other out, who moves first, and the HP left. The
damage both ways comes from the estimates above, re-applied with each own
Pokemon's real stats and types, so it can ask what the player's Pokemon
would do to its bench. It switches only for a clear gain, at most twice in a
row, and picks the next Pokemon after a faint the same way.

## Files

- `src/st_core.c`, `src/st_core.h`: the decision core, plain C with no eidos
  calls (the benchmarks run the same core).
- `src/st_mod.c`: the eidos side: fills the core's view from the battle and
  replaces the game's choices (`battle.ai`, `battle.ai_replace`).
- `mod/`: the manifest and the options.

## Status

Benchmarks (2026-11-06), the share of battles the trainer side wins:

| Test | Opponent | Smart Trainers |
|---|---|---|
| Showdown sim, 1000 battles | random moves | 92.1% |
| Showdown sim, 1000 battles | strongest hit | 70.6% |
| Showdown sim, 1000 battles | copy of the game's regular AI | 91.7% |
| Showdown sim, 1000 battles | copy of the game's gym AI | 69.4% |
| poke-env bots, 150 battles | SimpleHeuristicsPlayer | 68.7% |

In game (eidos, fixed player that always uses its first move, 54 battles per
group per AI): regular and ranger trainers 50/54 against 45/54 for the game's
own AI; gym trio 11/27 against 13/27, where the type match decides nearly every
battle and both AIs mostly pick the same move.

Later runs (2026-11-06), share of battles won, in-game trainer teams:

| Opponent | Smart Trainers | game's gym AI | game's regular AI |
|---|---|---|---|
| strongest hit | 53.3% | 43.5% | 29.2% |
| human-like player | 51.9% | | |

With competitive teams, Smart Trainers wins 63.8% against the game's gym AI.
Against the human-like player in Gen 4 OU it wins 72.5%, where Metamon (a
neural network trained on Showdown replays) wins 71.1%.

Since version 0.2.0 the trainer also knows its own PP, the stat changes of
every move (from the game's move data), Choice items and the locks they
cause (its own and the player's), the common held items on both sides (the
player's once revealed), screens, hazards, Substitute, Leech Seed, Yawn,
Taunt and Tailwind, and it predicts the player's switches.

Needs eidos 0.2.3 or later (the move stat-change data). On an older eidos it
logs that and leaves every battle to the game's own AI.

Open items: doubles (only the front Pokemon is used), the trainer's items
(the game still picks them).
