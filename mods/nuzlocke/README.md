# Nuzlocke

The Nuzlocke challenge for eidos, written in eidos's rule language
(`mod/rules`).

- Only the first wild Pokemon met in each area can be caught. An area is a
  place with one name on the map (a route, a city, a cave and its floors).
  If it runs or faints, the area is used up.
- The rule starts once you have a ball: an encounter met with none in the
  bag does not use the area.
- A Pokemon of yours that faints in battle is released after the battle, or
  moved to box 24 for good (option).
- Fainted Pokemon cannot be revived (option).
- Losing every Pokemon ends the run.
- Gifts are free and do not use the area's encounter.

## Options

| Option | Default | Meaning |
|---|---|---|
| Fainted Pokemon | release | `release`, or `box`: they go to box 24 and cannot be withdrawn |
| Dupes clause | on | a species you already caught does not count; the next encounter does |
| Shiny clause | on | a shiny can always be caught |
| Story Pokemon can be caught | on | story and static encounters do not use up the area |
| No revives | on | Revives, Max Revives and Pokemon Centres do not bring a fainted Pokemon back |

## Not included

- Nicknames: eidos cannot open the nickname screen for a mod yet.
- Level caps, set mode, item bans and the other Hardcore rules: they belong
  in a separate mod.
