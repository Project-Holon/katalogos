# Soul Link

A Nuzlocke for two or more players, each in their own game of one eidos
multiplayer session.

- Each player may catch only the first wild Pokemon of each area. The rule
  starts once the player has a ball; an encounter met with none does not
  use the area.
- The Pokemon the players catch (or are given) in the same area are linked.
- When a linked Pokemon faints or is released, every player loses theirs
  from that area. If a player's first encounter in an area gets away (it
  ran, or fainted without being caught), the area is lost for every player.
- With "Keep pairs together", when one player boxes a Pokemon, the others'
  Pokemon from the same area go to the boxes too, and come back to the
  party when it does.
- Fainted Pokemon cannot be revived (option).
- When a player loses every Pokemon, the run is over for every player.

Every player needs the mod, with the same options. Outside a multiplayer
game it does nothing.

## When a pair is lost during a battle

The other players' Pokemon of that pair do not wait for their battles to
end:

- on the bench, it faints at once;
- on the field, it cannot be switched out, and it faints at its next attack
  (after the move) or at the next hit it takes (the hit becomes lethal),
  whichever comes first;
- after the battle it is released, or moved to box 24, like any lost
  Pokemon.

Outside a battle, a lost Pokemon goes within half a second.

## Options

| Option | Default | Meaning |
|---|---|---|
| Lost Pokemon | release | `release`, or `box`: they go to box 24 and cannot be withdrawn |
| Keep pairs together | on | a pair is in the party, or in the boxes, on every side |
| Dupes clause | on | a species you already caught does not count; the next encounter does |
| Shiny clause | on | a shiny can always be caught |
| No revives | on | Revives, Max Revives and Pokemon Centres do not bring a fainted Pokemon back |

## How pairs are tracked

Each Pokemon is remembered, not guessed from its party slot: when a player
catches or receives a Pokemon, their game writes `mon.<personality>` = the
area to the session's shared table, which the host keeps with its game. A
Pokemon is linked only when the table holds it, so a Pokemon traded in, or
one caught before the mod was on, is never touched. The players also share
`lost.<area>`, `boxed.<area>` and `over` (eidos mod interface 5). Each game
acts on its own Pokemon only.

## Not included

- Nicknames: eidos cannot open the nickname screen for a mod yet.
- The type clause (no two pairs sharing a primary type): optional in most
  rule sets, left out for now.
- Level caps.

The mod is C built for WebAssembly (`src/soul_link.c`, `build.sh`); eidos
runs it in its wasm runtime. The publish workflow builds it.
