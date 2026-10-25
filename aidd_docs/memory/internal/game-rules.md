# Game rules

Behaviour of the original game, described in prose, from analysis of
`PIRATESG.EXE` (GOG build; virtual addresses refer to its flat image, see
[asset-formats](asset-formats.md)). This is the specification the
engine's simulation (`src/sim/`) is written from; the code is not a
translation of the original routines.

Notation: `rand(n)` is a uniform integer in `0..n-1`. Town fields are
unsigned 16-bit values and wrap like them; the names are those of
`struct town` (`src/assets/towns.h`).

## Time

The calendar has twelve months of 30 days (VA `0x1dd0e`):

- **Day of the year:** VA `0x46052`, 0..359.
- **Month:** VA `0x46054`, the day of the year divided by 30.
- **Year:** VA `0x46056`.
- **Advancing:** when time advances by some days, one **world update**
  runs for each month boundary crossed. During that update, the month
  variable holds the number of the month just begun. That number is 1-12,
  and slightly more when a single advance runs past the year end. After
  the updates:
  - if the day of the year reached 360, 360 is subtracted once and the
    year advances;
  - the month variable is recomputed from the day of the year.
- **Engine choice:** the engine advances long periods in steps of at
  most 30 days. Each step then crosses at most one boundary, as in
  normal play.

## World update (monthly)

Runs, in this order (VA `0xceb8`):

1. Town growth.
2. Conquest.
3. Diplomacy.
4. Random town event.
5. Treasure fleet targeting (not specified yet).
6. Two treasure fleet movements (not specified yet).

Some housekeeping on player-related values precedes these steps; it is
not specified yet.

### Town growth (VA `0xc714`)

Each town, independently, grows with probability 1/4. When it does:

1. Wealth rises by `rand(255 - wealth) >> 5`.
2. Draw `r = rand(wealth)`, using the updated wealth.
3. Citizens rise by `((month * 24 + r) >> 8) & 0xFF`, where `month` is
   the month number of the update (1-12). This adds 0-2 citizens, more
   often late in the year and in wealthy towns.
4. Income `s` is the low 16 bits of `r * citizens`, shifted right by 9.
   Spanish towns get half (`s >> 1`).
5. Gold rises by `s`.
6. Garrison drift: let `d = (s + citizens) / 2 - soldiers`.
   - If `d >= 1` and soldiers are at most 254, soldiers rise by one.
   - Otherwise, if `d < 0` and there are soldiers, they fall by one.

### Conquest (VA `0xc81b`)

1. Pick an attacker `a = rand(towns)` and a target `b = rand(towns)`. The
   target is re-drawn while it is the town the player is in. That rule
   only applies when the player is in a town.
2. The target changes hands when all of these hold:
   - the two nations are at war;
   - the target's soldiers, doubled, are fewer than the attacker's;
   - the Manhattan distance between the towns is below 150 map cells;
   - the target is not the player's current town.
3. The target then takes the attacker's nation and gets a new governor
   (see below), and the news "<nation> captures <town>" is logged.

### Diplomacy (VA `0xc938`)

1. Draw `r = rand(range)`, where `range` is a game variable not traced
   yet. Nothing happens unless `r < 28`.
2. Nations `a = r & 3` and `b = (r >> 2) & 3`. Nothing happens when they
   are the same.
3. The step is +1 when bit 4 of `r` is set, -1 otherwise.
4. Nothing happens if the relation already equals the step, so a step
   never pushes past war (-1) or alliance (+1).
5. Otherwise the relation moves by the step, symmetrically for the
   pair. News is logged, except when the move returns to peace through
   a -1 step (from alliance to peace):
   - "at war" for a new relation of -1;
   - "make peace" for 0;
   - "allies" for +1.
6. On a move to alliance, nation `a` also "offers pirate amnesty", and
   the player's standing with `a` is raised to at least -3.

The initial relations come from the start scenario (below).

### Random town event (VA `0xca7f`)

1. Draw `t = rand(64)`. There is an event only if `t` is a town index,
   so the chance shrinks with fewer towns.
2. Draw the event `e = rand(5)`. "New governor" is re-drawn while it
   would hit the player's current town.
3. The town's flag bit `0x80` moves to bit `0x20`: `0x20` is set if
   `0x80` was set and cleared otherwise, then `0x80` is cleared.
4. Effects:

| e | News | Effect |
|---|---|---|
| 0 | Indians attack | soldiers halved |
| 1 | New gold mine | wealth +64 (255 if it would exceed 254); gold +12 (255 if it would exceed 254) |
| 2 | Malaria strikes | soldiers and citizens halved |
| 3 | New governor | new governor |
| 4 | Pirates plunder | gold set to 0; wealth loses a quarter |

### New governor (VA `0xca51`)

- Field 9 (governor standing, presumably) is reset to 0.
- Unless bit `0x40` of field 11 is set, field 11 becomes `rand(16)`.

## Start scenarios

There are 30 records of 90 bytes at VA `0x35b90` (file `0x3a337`):
- 24 career starts, one per era and nation, in the order English,
  French, Dutch, Spanish within each era. The Dutch 1560 record is
  empty.
- Then the 6 famous expeditions (Hawkins, Drake, Heyn, L'Ollonais,
  Morgan, De Pointis).

Fields, as the scenario loader (VA `0x1386c`) uses them:

| Offset | Content |
|---|---|
| 0x00 | name, NUL-terminated (at most 20 bytes) |
| 0x34 | 4x4 table of signed bytes: initial relations, indexed `[a * 4 + b]` with the town nation order (Spain, England, France, Holland) |
| 0x14-0x33, 0x44-0x59 | other values copied into game state; meaning not traced |

## Sailing (main loop at VA `0x231b8`)

### Starting a voyage

A start scenario (VA `0x1386c`) sets the player block at VA `0x45f92`:

| Offset in block | Set from scenario | Content |
|---|---|---|
| +0 | `+0x44` | start position x (tiles); one of 4 (x, y) pairs, picked with `rand(4)` |
| +2 | `+0x46` | start position y (tiles) |
| +4 | `+0x14` | crew |
| +6 | `+0x20` | number of ships |
| +0x0a | `+0x16` | guns (judging by the values) |
| +0x08 | `+0x17` | nation |
| +0x1e | `+0x21` | 8 bytes of ship types: 0 pinnace … 7 galleon (CSTN group 8 order) |
| +0x28 | `+0x18` | gold, x 10 |

The town the player is in is the nearest entry of the era's coordinate
list (VA `0x1adfc`). That list holds (x, y, town) triplets, ending in
`0xFFFF`.

### Ship state

- **Position:** world pixels (16 per map cell), x in 65..10175 and y in
  65..7615, each with a 1/8-pixel remainder.
- **Bouncing off the world edges:** the heading is set to face back in,
  east (`0x40`) at the west edge, west at the east edge, south at the
  top and north at the bottom.
- **Heading:** one byte, 256 per turn: `0x00` north, `0x40` east,
  `0x80` south, `0xC0` west.
- **Ticks:** a tick counter; 64 ticks make a day. When a voyage ends,
  the game adds `ticks / 64 + 1` days (VA `0x28789`).

### Each tick

1. **Steering:** a turn input of -1, 0 or +1 changes the heading by
   16 x input, except on ticks where bit 2 of the counter is set.
2. **Target speed:**
   `(speed_table[class][((heading + 8 - wind) >> 4) & 15] + bonus) x 3`.
   - `class` is the largest ship type in the fleet (`& 7`).
   - `bonus = (5 - d) >> 1`, where `d = clamp(difficulty - navigator,
     0, 4)` and `navigator` is 1 with the special ability the game
     tests as 3. Which of our ability indices that is, is not
     confirmed.
   - The multiplier is 6 instead of 3 while a value at VA `0x4d292` is
     below 100; that value starts at 999 and is not traced.
   - When that value is below `d x 64`, the speed is halved and, every
     16 ticks, the heading turns 16 toward the wind. Not traced either.
3. **Current speed:** moves 2 toward the target, then is clamped to
   4..12.
4. **Movement:**
   - `dx = sine[heading] x speed >> 8` and
     `dy = sine[heading + 0x40] x speed >> 8`, in 1/8 pixels.
   - `sine` is a 128-entry half-circle table (0 to 255 and back), and
     the second half of the circle is the first one negated.
   - x increases by `dx`, y decreases by `dy`.
5. **Wind:** every 128 ticks, with `w = wind >> 4` and `r = rand(8)`,
   `wind += 16 x ((r - w + 10) >> 1)`, mod 256. It drifts around
   `0xC0` to `0xE0`: blowing toward the west, as the trade winds do.
   The wind starts at `0xC0` on a new game.
6. **Terrain:** the colour of the map pixel under the ship gives a class
   through the 256-entry table at VA `0x4113c`:
   - 3: deep water (40 colours);
   - 1: shallows (16 colours);
   - 2: land (200 colours).

   Odd classes are water. Moving onto land ends the voyage step (VA
   `0x24808` handles it; landing parties are not specified yet).

### Engine choices (not traced)

Where the original's behaviour is not traced yet, the engine does this:
- **Leaving port:** a voyage starts on the water outside the start
  town's harbour, at town fields f26 and f27. The scenario's start cell
  itself is inside the town, on land.
- **Arriving:** the fleet reaches a town within 1 cell, of the town or of
  that outer approach point. The town it is leaving only counts again
  once the fleet is more than 2 cells away.
- **Land:** land always stops the ship. Landing parties come later.
- **Time:** days pass steadily, one every 64 ticks, rather than as
  `ticks / 64 + 1` at the end of a voyage, so stepping a voyage in small
  pieces gives the same calendar.

### Speed table

- **Location:** per era, a 0x200-byte block at VA `0x34d68` is copied
  to VA `0x46f86` (VA `0x1361c`). Its first 128 bytes are the speed
  table: 8 ship classes, each with 16 headings relative to the wind.
- **Rows by class:** row 0 reads 4, 5, 6, 7, 7, 6, 4, 3, 3, 3, 4, 6, 7,
  7, 6, 5. Running before the wind gives 4, a beam reach 7, and into
  the wind 3. Rows 4-7 have -1 into the wind.
- **Scale:** with the x3 multiplier and the 4..12 clamp, most entries
  give full speed. The table matters mostly near the wind.
- **Rest of the block:** the bytes after the table are not decoded.

## Retirement and the Hall of Fame (VA `0x45d0`)

When a career ends:

1. A score is computed from several career values (VA `0x5039`). The
   computation uses floating point and is not specified yet. Its inputs
   include the years since the start and five values taken from the
   start scenario, among others.
2. The score gives a rating `r`, where 0 is best:
   - if the score is at least 1: `r = max(19 - score / 5, 1) - 1`;
   - otherwise: `r = min(20 - score / 10, 22) - 1`.
3. `r / 4`, capped at 3, picks one of four retirement pictures.
4. Depending on the difficulty and the years elapsed, the game may offer
   "Remain in retirement / Return to pirating?" (CSTR 72).
5. It asks "Do you wish to have your exploits recorded in the Pirate
   Hall of Fame?" (CSTR 73). On "Yes", the career is recorded with its
   score and rating.
6. It asks whether to play another game (CSTR 80).

The engine records careers with a stand-in score (months at sea) until
the systems the real score weighs exist.

## Open questions

- The diplomacy `range` variable (VA `0x4707e`). It is probably set
  from the difficulty level.
- Treasure fleet targeting and movement (VA `0xcba7`, `0xcde9`).
- What the world update's housekeeping does.
- When the player counts as being in a town, for the exclusions above.
