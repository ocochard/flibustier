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
- **Pace:** each pass (tick) of the loop waits for the game clock to
  advance by 5, or by 1 in the fast mode: 12.2 or 60.9 passes a second.
  The T key toggles the fast mode (VA `0x1b6ef`); the voyage's end (VA
  `0x284b6`) turns it off. The engine does so on reaching a town.
- **Ticks:** a tick counter; 64 ticks make a day. When a voyage ends,
  the game adds `ticks / 64 + 1` days (VA `0x28789`).

### Each tick

1. **Steering:** a turn input of -1, 0 or +1 changes the heading by
   16 x input, except on ticks where bit 2 of the counter is set.
2. **Target speed:**
   `(speed_table[class][((heading + 8 - wind) >> 4) & 15] + bonus) x 3`.
   - `class` is the largest ship type in the fleet (`& 7`).
   - `bonus = (5 - d) >> 1`, where `d = clamp(difficulty - navigator,
     0, 4)` and `navigator` is 1 with the navigation ability. The game
     numbers the abilities from 1 in the order of CSTN group 83:
     fencing 1, navigation 3 and gunnery 4 are each tested where they
     belong.
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

## Encounters at sea

### Sighting (sailing loop, VA `0x231b8`)

1. **When:** every 32 ticks (when `(ticks + 4) & 31 == 0`), the game
   finds the nearest town and its distance `d` in map cells (`|dx| +
   |dy|`, at least 1).
2. **Lingering counter:** a counter `h` (VA `0x4c09e`) resets to 0 when
   the nearest town changes.
3. **Chance of a sail:** with `r` random:

| `d` | A sail is sighted when |
|---|---|
| below 8 | `(r & 7) + (h >> 2) < 2`, after `h += 8 - d` |
| 8 to 15 | `r & 15 < 2` (1 in 8) |
| 16 to 31 | `r & 31 < 4` (1 in 8) |
| 32 or more (capped at 63) | `r & 63 < 4` (1 in 16) |

4. **Lingering raises the counter:** the game reaches it through
   `h += 8 - d`, so near towns sightings thin out the longer the fleet
   stays.
5. **Ignoring raises it too:** an encounter that ends without a fight
   adds 16 to `h` (VA `0x2849c`).

### Encounter (VA `0xd1fc`)

1. **"Sail ho!"**
   - The lookout reports a sail (CSTR 118). Below difficulty 2, it adds
     whose waters these are (CSTR 119, with the nation word of the
     nearest town).
   - Choices: investigate or continue the voyage. Continuing ends the
     encounter.
   - Layout: no box; the text and choices are written at (100, 300) up
     to x 540. The later questions are at (200, 256), below the ship's
     portrait or flag, on a screen cleared to colour 0 (black in those
     pictures, VA `0x247b`).
2. **Which ship:**
   - The ship comes from the per-era block (offset `0x100`, VA
     `0x47086`): 4 nations x 5 records of 7 bytes.
   - Normally it is record `k` of the nearest town's nation, with
     `k = min(rand(crew / 50 + h / 6 - town.f9) / 3, 4)`, or `k = 0`
     when that range is below 1.
   - With chance 4/15, or when the nearest town is more than 12 cells
     away, `r = rand(20)` gives record `r / 4` of nation `r % 4`
     instead.
   - Missions, pirate bases and treasure fleets have special cases, not
     specified yet.
3. **Ship record:**

| Byte | Content |
|---|---|
| 0 | ship type (`& 7`): 0 pinnace … 7 galleon; type 5 shows as 8 (fast galleon) while VA `0x45e8c` is below 4 |
| 1 | nation, 4 = pirate |
| 2 | crew (judging by the values) |
| 3 | guns (judging by the values) |
| 4 | role: 3 to 9; pirates are 7, the big warships 8 or 9 |
| 5, 6 | not decoded |

4. **"She looks like %s, captain"** (CSTR 120, with the article and
   type name from CSTN group 11). Choices: investigate or sail away.
5. **Hostility:**
   - A ship whose role is above 5, of a nation with which the player's
     standing is negative, chases the player.
   - Pirates (nation 4) start at standing -1, so they always do.
   - A chasing pirate gets CSTR 121 ("It's the %s %s! He's chasing us"),
     any other ship CSTR 122 ("She's flying %s colors").
   - Choices: close for battle, hail for news, or sail away.
6. **Outcomes:**
   - **Sailing away:** when `rand(3) <` difficulty, the other ship
     overtakes the player (CSTR 126) and there is a battle anyway.
   - **Hailing:** a nation with standing below -3 does not answer
     (CSTR 315) and attacks; otherwise the player gets news (VA
     `0x1e78d`).
   - **Closing for battle:** starts a sea battle (phase 6).

### Engine choices for encounters (not traced)

- **Picking the ship:** it is picked when the sail is sighted, not after
  the first answer. The draws are the same; only their order differs.
- **Hailing** (traced, VA `0x1e78d` mode 2): the "Ship's News" page
  (`pic/shipnews`, CSTN 18.2) lists the 6 latest public news items
  (types 10-19) dated up to today, or "(no entries)" (CSTN 18.3). The
  game also records the date of the hail.
- **Hostile pirates:** they use the "He's chasing us" dialog (CSTR 311).
  The original's named pirates (CSTR 121) need the pirate name lists.
- **Pictures:**
  - Ship portraits (`S_*.PCX`) hold 5 round views; the engine shows the
    first.
  - Flags (`F-*.PCX`, and `FJOLLY*` for pirates) hold 8 frames of
    200x200 on a pure green key; the engine waves them in turn. The
    original composites them with `BASEFLAG.PCX` (VA `0x108cf`), not
    reproduced yet.

## Sea battles (VA `0xe245`; loop at VA `0x7db8`)

Helper names below are those `tools/ghidra/RegParams.java` gives:
`div16(a, b)` is `a / b`, or 0 when `a < b` or `b == 0`; `sine_mul`
and `cosine_mul` are the sailing sine products.

**Level.** Where this section says "difficulty", the game uses a level
(VA `0x1b087`): `clamp(difficulty + extra - gunner, 0, 4)`, where
`gunner` is 1 with the gunnery ability. `extra` is 0 for ordinary ships;
named captains set it to 1 or `(id & 7) / 3` (VA `0xd1fc`). The sword
fight computes its level the same way with fencing.

### Setup (VA `0x9832`)

- **Flagship:** the ship that fights is the first of the fleet. With
  several ships, the player first picks which one leads (VA `0xf246`).
  Its type byte holds the class (`& 7`) and the hull damage it already
  has (`& 0xF8`).
- **Player crew aboard:** `min(crew + 8 - 8 x ships, (class + 2) x 32)`.
  The rest stays with the fleet.
- **Player guns:** `min(guns, crew aboard / 4, (class + 2) x 4)`.
- **Gunnery `g`:** `clamp(block[0x18] / 2 + 4, 0, 16)`, from the
  player block (field not identified). Reloading takes
  `|144 - 8 x g|` ticks.
- **Enemy:** from the encounter record: guns = byte 3, crew = byte 2 x
  4, class = byte 0, role = byte 4.
- **Turn rate, both ships:** `t = 3 - (class + 2) / 4`. Small ships turn
  and gain speed in wind differently from big ones.
- **Positions:** the ships start mirrored about (240, 240): each axis
  offset by `rand(16) + 32` pixels, sign random.
- **Headings:** the player faces the enemy's side (east or west, 45°
  toward it when not level); the enemy heading is random within ±56° of
  the player's.
- **Battle state:** the wind keeps its direction from sailing, with
  strength `w = rand(3)` rolled during the encounter (VA `0xd1fc`).
  Both ships start with battle sails.
- **Which ship (VA `0xf246`):** with more than one ship, the player
  picks the ship to command on the fleet panel (VA `0xffae`), under
  "Which ship will you command?" (CSTR 286). The code fills CSTR 123
  ("We have %s men and %s cannon ready for battle. Winds are %s…")
  first, but copies CSTR 286 over it. The chosen ship swaps places
  with the flagship, then fights with `crew - 8 x (ships - 1)` men. The
  engine shows a list of the ships instead of the panel.
- **Enemy reload:** `|144 - 8 x role|` ticks.

### Each tick

- **Steering:** the player's heading changes by 16 x turn input every
  8 ticks.
- **Sails:** Up raises and Down lowers them. Changing sails takes 160
  ticks, while the panel says "Raising sail" or "Lowering sail" (CSTN
  30.4, 30.5, VA `0xef06` event 10) and reloading waits. Then it shows
  "Full sails" or "Battle sails" (30.3, 30.2), and full sails multiply
  the speed by 1.5.
- **Speed:**
  - Start from `(speed_table[class][sector] x 4 - damage / 4)`, with
    `sector = ((heading + 8 - wind) >> 4) & 15`.
  - Full sails multiply it by 1.5.
  - The speed is then `div16(that x 8, 8 - (3 - t) x w)`, clamped to
    40. In stronger wind, big ships go faster.
  - For the enemy, a table value equal to the difficulty counts one
    less (as traced; purpose unknown).
- **Movement:** a position in 1/256 pixels gains
  `sine_mul(heading, speed) x 4` on x and loses
  `cosine_mul(heading, speed) x 4` on y.
- **Firing:**
  - The player fires on demand when reloaded and with guns.
  - The enemy fires when the player is within 6 of abeam (90° off its
    bow) and it is reloaded.
  - A shot flies abeam, toward the target's side, at
    `sine_mul(h, 0x80) x 4` per tick, for `d / 2` ticks with
    `d = max(|dx|, |dy|) + min(|dx|, |dy|) / 4`, at least 16.
- **Hits:** when the shot lands, it hits if it is inside the target's
  box.
  - Half sizes by type: 20 x 10 (0-2), 30 x 20 (3-5), 40 x 30 (6-8).
  - The player's shots see the enemy's box widened by 3, 2, 1, 1 or 1
    quarter sizes for difficulty 0 to 4.
- **Damage of a hit by guns `G` on class `c`:**
  - **Hull:** `max(1, div16(G x 4, c + 2))`. It is halved when the
    target has battle sails. Shots from the player gain half again at
    difficulty 0.
  - **Crew lost:** `((rand & 63) + 4 x div16(crew x G, c + 3)) >> 6`. The
    player keeps at least 1 and at most 999.
  - **Guns lost:** `((rand & 63) + div16(guns x 4 x G, c + 3)) >> 6`.
- **Damage levels:** `damage / 16` (0-4), shown with CSTN group 14.
  Level 4 sinks the ship: the player's (CSTR 167) or the enemy's
  (CSTR 168, she blows up).
- **Wind:** every 512 ticks it may shift by one sixteenth either way,
  and its strength by one step.

### Enemy decisions (every 64 ticks)

1. Compare the two ships, with `eff = crew x role >> 3`:

| Test | Adds |
|---|---|
| enemy damage < player damage / 2 + 1 | 9 |
| enemy damage < player damage x 2 | 9 |
| enemy guns > player guns / 2 + 1 | 3 |
| enemy guns > player guns x 2 | 3 |
| enemy eff > player crew / 2 + 1 | 1 |
| enemy eff > player crew x 2 | 1 |

2. Look the total (0-26) up in the era block at offset `0x80`:
   - 0: close in, full sails;
   - 1: take a broadside position, battle sails;
   - 2: flee, full sails.

   A total of 0 (hopeless) with the ships alongside: the enemy strikes
   its colours (CSTR 170).
3. Target heading:
   - for 0 and 2, the bearing to the player plus `decision x 0x40`,
     clamped to within 0x50 of downwind;
   - for 1, the bearing plus 0x40, turned half round when that is more
     than 0x40 from the current heading or 0x5F from the wind.

   The enemy then turns toward the target by `t` every 8 ticks.

### The coast

The battle field is the map around the fleet at twice its scale: each
map cell is drawn as a 32 x 32 battle tile (`battle.imb`). The engine
places the field's top left 7 cells up and left of the fleet's cell.
The terrain under a ship is the class of the battle tile's colour there
(table at VA `0x4113c`, the one sailing uses).
- **The player's ship (VA `0x81f0`), after it moves:**
  - Shallows: once a cooldown has run out, a roll `rand(16) >= class`
    (table at VA `0x327ce`, 0..7) costs 16 damage and starts a 20-tick
    cooldown. The cooldown only counts down on the shallows. A pinnace
    always takes the damage; a galleon on 9 rolls in 16.
  - Land: the ship is beached (outcome 13). If the enemy is alongside
    (the boarding test, VA `0x9c84`), it is a boarding instead (outcome
    5).
- **The enemy (VA `0x8732`):** the same shallows rule, with its own
  cooldown. Land does not stop it.
- **Beached:** the enemy's portrait (VA `0xe9e6`) and CSTR 173 at (100,
  300) up to x 560. Nothing is lost.
- **Not modelled yet:** the field scrolls when the player nears its
  edge (VA `0x95ac`); the engine's field is fixed, and a ship off it is
  on open sea.

### End of battle

Each ending has its own picture (VA `0xe67b`-`0xe918`), with the text in
palette entry 2 and no box:
- **Either ship sunk:** `sinking.pcx`, text at (100, 300) up to x 560.
- **Escape:** `outrun.pcx`, the same place.
- **Sunset:** `night.pcx`, text at (40, 150) up to x 280 (CSTR 317
  against a fort).
- **Colours struck, beached (CSTR 173):** written over the battle at
  (100, 300).

| Outcome | When | What follows |
|---|---|---|
| boarding | quarter-size boxes touch | CSTR 171, then a sword fight |
| escape | the ships are more than 240 pixels apart on either axis | CSTR 198 when the enemy was fleeing (decision 2, VA `0x44a20`), otherwise CSTR 197; CSTR 196 against a fort (VA `0xe82a`). The encounter ends; possible loss of a slow ship (CSTR 172) |
| sunset | 4500 ticks | CSTR 169 |
| surrender | as above | CSTR 170, then capture (CSTR 164: keep her or plunder and sink her) |
| player sunk | damage level 4 | CSTR 167; the fleet loses the ship and its cargo share (see losing a ship); with no ship left, marooned (CSTR 161) |
| enemy sunk | damage level 4 | CSTR 168 |

Afterwards the flagship's crew and guns, and its damage (in the type
byte), return to the fleet.

### Engine choices until more is traced

- **Boarding:** the sword fight decides it (see below). A win counts as
  a surrender.
- **Lost boarding:** the enemy takes the flagship, as if it sank;
  prison (VA `0x16647`) is not modelled.
- **Prize text (VA `0xdb7b`):** the prize is `class x 20 + 20` tons, its
  guns are rounded up to even, and the hold space is
  `sum(2 x class + 2) x 10` over the fleet, minus cargo (none yet).
- **Prize:** a captured ship pays the encounter record's byte 6 x 10
  gold, standing in for cargo and treasure. Kept, she joins the fleet
  with her guns.
- **Marooned:** with no ship left, the game shows CSTR 161 and 162
  (VA `0x1f2a1`). The player then continues with a pinnace and at least
  8 men. The original keeps the last ship's entry and runs routines not
  traced yet: VA `0x1be71` looks for land around the fleet, VA `0x1f803`
  resets the crew, and a value used as the difficulty drops by one.
- **Not done yet:** choosing the flagship among several ships, and the
  loss of a slow ship on escape (CSTR 172).
- The engine keeps hull damage beside each ship rather than packed in
  the type byte.

## Sword fights (VA `0x16205`; frame loop at VA `0x2747c`)

One routine runs every sword fight. A caller fills a block of four
words and four bytes:

| Field | Meaning |
|---|---|
| word 0, 1 | player's men and strength factor |
| word 2, 3 | opponent's men and strength factor |
| byte 8 | opponent's title, CSTN group 74 (0 guard … 3 enemy captain, 4 captain, 5 first mate) |
| byte 9 | opponent's nation, CSTN group 71; -1 a named captain |
| byte 10 | level `extra` (see the sea battle level) |
| byte 11 | kind: 0 crew fight, 1 duel, -2 practice |

The routine returns 1 when the player wins, 0 when the player is driven
back, -1 when the player loses. For a crew fight, both men counts come
back reduced.

### Callers

- **Boarding (VA `0xf10a`, outcome code 5 of the sea battle):**
  - Before the fight, CSTR 171 is shown.
  - Player: the crew aboard, factor `7 - player damage / 16`.
  - Opponent: the enemy crew (record byte 2 x 4), factor
    `record byte 4 - enemy damage / 16`.
  - The opponent is title 4 ("Captain") of the record's nation, with
    `extra` 0 for an ordinary ship.
  - A win leads to the capture (VA `0xdb7b`, CSTR 164). Being driven
    back ends the battle with no capture. Both lower a counter at VA
    `0x45e76` by 2 (not identified).
  - A loss (VA `0x16647`): CSTR 175, "Your men flee in panic…
    imprisoned in %s", then CSTR 190. Not traced further.
  - After a win or a repulse, the fleet loses the men who fell.
- **Duels (VA `0x161ba`, `FUN_161ba(scene, title, nation, extra)`):**
  one man each, factors 9 and 9.
  - Scenes (table at VA `0x3361c`): 0 alley, 1 and 2 fort, 3 and 4 the
    ship's deck. The opponent's head is `HeHead1` on the deck,
    `HeHead2` in the alley and `HeHead3` at the fort (VA `0x15f84`).
  - The weapon is chosen on the scene's picture.
  - The opponent is labelled with his title alone; crew fights add the
    nation.
  - A loss or being driven back (VA `0x16384`): the captain's portrait
    in its frame (VA `0x159df`), "You have disgraced yourself in
    personal combat!" (CSTR 174, at (100, 350) up to x 560), and the
    reputation becomes -1.
- **Practice (VA `0xbdae`):** 200 men each, factors 32, against a
  first mate. No consequences. Afterwards a fencing practice value
  (VA `0x45e90`, 0..30) rises by 5.

### Setup

- **Level:** `clamp(difficulty + extra - fencer, 0, 4)`, where `fencer`
  is 1 with the fencing ability.
- **Weapons (CSTN group 70):** 0 rapier, 1 longsword, 2 cutlass.
  - The player chooses first on `img/wpnsel` (CSTR 85), with the
    cutlass preselected. Only a kind of -1, which no traced caller
    uses, skips the choice.
  - The opponent fights with the rapier in duels and practice. In crew
    fights it picks the longsword or cutlass, `rand(2) + 1`.
- **Strength bar:** `B = 31 x Pm x Pf / (Pm x Pf + Om x Of) + 8`, in
  integer arithmetic, for the player; `O = 47 - B` for the opponent.
  `B / 8` places the two flags on the screen (table at VA `0x338a0`).
- **Positions on a line:** the player at x 340, the opponent at x 220.
  In other fights they start at 320 and 240. The distance is
  `player x - opponent x`.
- **Moves:** the opponent starts on guard with one of 8 scripts picked
  at random. The player starts with no move, so the first input counts
  at once.

### Moves

Each fighter plays one move at a time, one frame per tick. The tables
below are indexed by `move x 6 + frame`, from VA `0x338ac` (frames per
move) and `0x38a1a` to `0x38b99`.

| Move | Frames | What it does |
|---|---|---|
| 0, 1, 2 | 6 | guard: 0 middle and high, 1 low and middle, 2 high and middle |
| 3, 4, 5 | 6 | the same guards while stepping back 14 on frame 0 |
| 6, 7, 8 | 6 | slash: middle, low, high; strikes on frame 3 (high: 4), steps in 4 on frame 5 |
| 9, 10, 11 | 4 | lunge: middle, low, high; strikes on frame 2, steps in 4 on frame 3 |
| 12 | 6 | advance 14 on frame 5 |
| 13 | 6 | back off 14 on frame 0 |
| 14 | 10 | falls; the fight ends after frame 8 |

- **Lines** are bits: 1 low, 2 middle, 4 high. A guard frame holds the
  bits it covers, and a strike frame holds the bit it attacks.
- **Steps** apply only while the distance is above 9.

### The player's input

- **Directions:** the input is a direction on a 3 x 3 pad, with the
  opponent to the left.
  - None, down and up pick guards 0, 1 and 2.
  - Away from the opponent (alone, with down, with up) picks 3, 4 and 5.
  - Toward the opponent picks an attack in the line given by up or
    down.
- **Lunge or slash:** with the keyboard, an attack is a lunge unless a
  shift key is held, which makes it a slash.
- **New moves:** a new move starts only when the current one ends.
  Guards and attacks (moves 0-2 and 6-11) take the input once; holding
  it does not repeat them.
- **At a distance:** beyond 59, attacks become advances (12) and the
  steps back become move 13.
- **When the opponent falls,** the player stands on guard 1.

### Each tick

1. **The player's frame.**
   - On a strike frame, if the opponent's guard does not cover the
     line, the strike reaches when
     `distance <= reach_level[level] + reach_weapon[weapon]`. The reach
     tables (VA `0x389b8`, `0x389bd`) shrink with the level and from
     rapier to cutlass.
   - It hits when `threshold[level] <= (rand & 255) + practice` (VA
     `0x389e4`).
   - A hit:
     - pushes the opponent back 4;
     - moves the strength bar toward the player `2 x weapon + 1`
       times, each time by 2 steps (1 for a lunge);
     - counts the hit by line.
   - If the opponent was already beaten (below), he falls (move 14).
   - A covered strike at a distance under 36 is a clash with no
     effect.
2. **The player steps.** Reaching x 442 drives the player back, which
   ends the fight with 0.
3. **The opponent's frame.** It works the same way, with these
   differences:
   - reach by weapon only (VA `0x389c1`);
   - it hits when `threshold[level] <= rand & 255` (VA `0x389e9`);
   - a hit pushes the player back 4;
   - it moves the bar `weapon + 1` times, 2 steps each (1 for a lunge);
   - a covered strike under 35 clashes.
4. **The opponent steps.** At x 60 or less he is driven off, and the
   player wins.
5. **The crew melee (crew fights, every 16 ticks):**
   - Each side rolls `rand(|((men + 8) >> 2) x bar|)`. The product is
     taken as a signed 16-bit value, and `rand(0)` is 0.
   - If the opponent's roll is lower, he loses `((Om + Pm) >> 6) + 1`
     men, kept within 1..9999, and the bar moves one step toward the
     player.
   - Otherwise the player loses men the same way and the bar moves one
     step toward the opponent.
6. **End of the fight:** a fighter whose fall (move 14) passes frame 8
   loses.

- **Bar steps:** a step toward the player raises `B` and lowers `O`,
  only while `O > 0`. When `O` falls below 1, or the opponent is down to
  1 man, the opponent is beaten. A hit that leaves him beaten fells him
  at once (move 14); one beaten by the melee falls at the next hit. The
  same holds the other way for the player.
- **Speed:** a tick lasts 12, 11, 10 or 9 units of the game clock
  (60.9 Hz, see the game clock) by `level & 3` (VA `0x389c4`).
- **Debug flag:** a byte at VA `0x33ee9`, 0 in the shipped game, would
  make the player invulnerable. No code sets it.

### Engine choices for sword fights

- **Lunges:** whether a strike moves the bar by 1 or 2 steps follows the
  striking move. The original follows the latest input, which differs
  only when a new key arrives in the middle of a move.
- **Men:** the panels show the men only for kinds 0 and up (crew fights
  and duels; not the story's fight nor practice), as numbers. The original draws them
  as figures for 500, 50 and 1 men (VA `0x169b7`).
- **Duels and practice** use the same rules but have no caller yet.

### The opponent's choices (VA `0x280ef`, when a move ends)

- **Far (distance 20 or more):** a random pick from the 8-entry table
  at VA `0x38c5a`, mostly advances, some guards and backing off.
- **Close:** with `rand & 15` above the level's caution (VA `0x38c72`),
  the next step of the current 8-step script. Otherwise guard 1.
- **Scripts:** at VA `0x38c87`, 8 scripts of 8 steps per bank. Bank 0
  serves level 0, bank 1 level 1, bank 2 the higher levels.
  - A step below 0x80 is a move.
  - 0x90 parries the player's current line.
  - 0x91 parries while stepping back.
  - 0x92 slashes, and 0x93 lunges, in one of the two other lines at
    random. The lines come from the table at VA `0x38d47`.
- **New script:** when a script wraps around, a new one is picked at
  random:
  - at level 1, after more than 2 player hits;
  - at level 2, after any player hit;
  - at level 3, after any hit.
  - Levels 0 and 4 keep their script.

## Towns

### Arrival (VA `0x18fa9`)

- **Screen:** `arr_sea.pcx`, or `arr_land.pcx` for an inland town or a
  land arrival. CSTR 114 at (330, 40) up to x 620: "You have arrived at
  the lovely <CSTN 29.7 seaside town / 29.6 inland village> of <name>.
  <forts> The <nation> flag flies over the town.<extra>".
  - Forts: none, "One fort guards the harbor entrance." (29.0), or
    "<n> forts guard…" (29.1 with CSTN 79); 29.8 and 29.9 inland.
  - Extra: "The treasure fleet is in the harbor!" (29.2) or "The silver
    train is in town!" (29.3) in the towns at VA `0x45e71` and
    `0x45e70`. Not modelled yet.
- **Choices (CSTR 115, at (330, 260)):** sail into the harbour (29.4;
  "March into town", 29.5, on land), attack the town (VA `0x18b3f`, not
  traced yet), sneak in, or leave.
- **When the menu appears:** the visit flags keep bits 1 and 2; in
  another town than the last, they are cleared with the shipwright's
  and merchant's gold. The tavern's people are all back.
- **Sailing in (VA `0x19415`):**
  `x = (standing[town nation] + town word 9 - crew / 100) x
  rand(32767) x 2 / 65536`, in double precision.
  - Above -0.5, or when the town has no forts: in.
  - Otherwise "the fort opens fire!" (CSTR 116) and the fleet stays
    out. At -1 or below, and arriving by sea (VA `0x47aca` = 2, set on
    arrival when the player has a ship), "One of your ships is sunk!"
    (CSTR 117): a random ship is lost with its share of the cargo (see
    losing a ship). With the last ship, the prison path: CSTR 175 and
    190.
- **Sneaking in (VA `0x195f5`):** visit flags 2 and 8 are set. The
  player is in when `town word 9 x reputation > -rand(50)`. Otherwise,
  on `landpir.pcx`, a guard recognizes the player (CSTR 163, at (40,
  150) up to x 240), then a duel in the alley (scene 0), with the
  guard's title (0), the town's nation and one level more (extra 1).
  Whatever the duel's result, the player stays out.
- **Leaving:** back to sea.

### Losing a ship (VA `0x1f2a1`)

`FUN_1f2a1(ship, keep, from_fort)`:
- With other ships, the ship is removed and the fleet's capacity
  shrinks by its room. Unless `keep`, with `r = room / capacity before`
  in single precision:
  - the gold loses `trunc(gold x r)`;
  - food (with its eaten fraction, `food x 256 + fraction`), goods,
    sugar and guns each lose `trunc(x x r + 0.5)`.
  - The crew is untouched.
- The last ship: washed ashore (CSTR 161, 162) after a sea battle, or
  imprisoned (CSTR 175 with the town, then 190) when `from_fort`. The
  engine then gives the player a pinnace and at least 8 men, an engine
  choice until the rest is traced.
- Callers: a ship sold (keep 1), the flagship lost in battle (VA
  `0xe81d`, keep 0), the fort's salvo on arrival (keep 0, from_fort 1).

Every way in marks the town's information as current: flag 0x80 set,
0x20 cleared. A random town event (see the world update) clears 0x80
and, when it was set, sets 0x20 ("information obsolete").

### Visit flags (VA `0x45f0a`)

| Bit | Meaning |
|---|---|
| 1 | the governor has been seen this stay |
| 2 | kept on a return; set when sneaking in, not identified |
| 8 | sneaked in: the bank is closed |

On entering a different town the flags are cleared; on re-entering the
same town only bits 1 and 2 are kept.

### Town screen (VA `0x1869b`, picture VA `0x18547`)

- **Picture:** `town.pcx`.
  - `town.spr` frame 0 (the harbour, the ship and the shipwright's
    shed) is drawn at (0, 258) when the ship is in port.
  - Frame 1 (the mansion's gate shut) is drawn at (582, 216) when the
    governor is unavailable.
- **Scroll:** the town's name, PIRATE1, centred at (325, 38).
- **Buildings:** seven rectangles (table at VA `0x339f4`: x0, y0, x1,
  y1, enabled):
  - 0 governor: open while the town has 6 or more citizens and he has
    not been seen this stay;
  - 1 merchant;
  - 2 bank: closed after sneaking in;
  - 3 tavern;
  - 4 shipwright: closed when the ship is not in port;
  - 5 city information (the scroll);
  - 6 leave town.
- **Choice 7:** the captain's cabin, from the Space key.

### The captain's cabin (VA `0x9ffc`)

- **Picture:** `cabin.pcx`. `cabin.spr` frame 0 is drawn at (325, 229)
  while none of the three slots at VA `0x47186` (treasure maps, it
  seems) is filled.
- **Title:** "Captain's Cabin" (CSTN 92.7), PIRATE1, centred at (320,
  38), in ink 0. Pointing at a spot shows its label there (CSTN 86).
- **Spots (table at VA `0x327e0`, x0, y0, x1, y1, on):**

  | Spot | Label | Engine |
  |---|---|---|
  | 1 | Continue travels | back to the town or the sea |
  | 2 | Treasure maps | not yet |
  | 3 | Treasure cave | the cave, back to the cabin |
  | 4 | Logbook | the latest news on `shiplog.pcx` (layout not traced) |
  | 5 | Map of the Caribbean | not yet |
  | 6-8 | Search, Divide party, Join party | off unless a party is ashore |
  | 9 | Save game | the save slots |
  | 10 | Exit game | the retirement question |
  | 11 | Practice fencing | below |

- **Practice fencing (VA `0xbdae`):** a sword fight on the deck (scene
  3) against the first mate (title 5): 200 men of factor 32 a side, the
  weapon chosen first. Afterwards the practice value (VA `0x45e90`)
  rises by 5, to at most 30.
- **Labels:** pointing at a building shows its label (CSTN group 84) on
  the scroll; pointing elsewhere restores the name.
- **Keyboard:** the arrows step through the open buildings, wrapping
  around. Enter or Escape goes into the selected one.

### City information (VA `0x19c62`, text VA `0x196d0`)

- **Picture:** `arr_sea.pcx`, or `arr_land.pcx` for an inland town or a
  land arrival.
- **Text:** at (400, 150), up to x 620.
  - With current information (flag 0x80), CSTN 23.0 is filled with:
    - the name and nation;
    - the wealth word (CSTN 9, `wealth / 64`);
    - the forts (CSTN 23.2 with the number word of CSTN 79, plural
      unless 1);
    - soldiers x 10 and citizens x 100;
    - the gold, when there is any (CSTN 23.3, in thousands).
  - Otherwise CSTN 23.1 ("no info"), or 23.4 when the information is
    obsolete.

### Shipwright (VA `0x218c2`)

- **Refusals, before anything else:**
  - "We do not trade with pirates!" (CSTR 256) in a Spanish town when
    `rank with Spain x 25 + high byte of the diplomacy range` (VA
    `0x45e7f`, `0x4707f`) is below the town's wealth (word 8). The same
    test guards the merchant (VA `0x21424`).
  - "You have nothing of worth" (CSTR 278) when the arrival mode (VA
    `0x47aca`) is 3, probably a party on foot.
- **His gold (once per stay, VA `0x45f08`):**
  - `x = trunc((|6 - month| + 6) x wealth / 6)`, with the month 0-11;
  - `gold = ((citizens + 10) x x / 150) x 600`, at most 50,000.
  - It is kept for the stay, and reset in another town.
- **Menu (CSTR 277):** parting company, repairing a ship, selling a
  ship, selling some cannon.
- **Prices (VA `0xffae`):** with class `c` (type `& 0xF`) and damage
  level `d = min(damage / 16, 3)`:
  - a repair costs `50 x (c + 1) x d`;
  - a ship sells for `50 x (c + 1) x (10 - d)`.
- **Repairing:**
  - "I see nothing wrong" (CSTR 282) when no ship is damaged.
  - The chosen ships' total must be below the player's gold, or he
    cannot afford it (CSTR 283).
  - The gold goes to the shipwright, and the ships' damage is cleared.
- **Selling:**
  - Not the only ship (CSTR 279), and at most all but one.
  - Ships go in fleet order while the shipwright's gold covers the
    price; otherwise he lacks the funds (CSTR 319).
  - A sold ship keeps its cargo in the fleet (VA `0x1f2a1` with keep 1).
- **Cannon (VA `0x22c76`):** "not the only one" (CSTR 284) under 2
  guns. Otherwise the merchant's trading board, lower on the
  shipwright's picture, with "I pay 50 gold pieces per cannon" (CSTR
  313) above it:
  - One row: the shipwright's stock, from 0 each visit, and the
    player's guns, at 50 gold either way. The merchant's steps apply
    (VA `0x20ad4` sell, `0x20bcd` buy).
  - Selling needs the shipwright's gold to cover the price.
  - Buying back needs the player's gold, his stock, and room in the
    hold. As for the merchant, only a hold exactly full refuses (the
    test is `space != 0`), so an overfull hold still takes guns.
- **Engine choices:**
  - Ranks come from governors' promotions; standing rises only through
    captures and Letters of Marque so far.
  - The original picks several ships at once on a fleet panel; the
    engine takes one ship per choice and shows the list again.

### Bank (VA `0x180c8`)

Dividing the plunder ends an expedition. The date is on the small
sign, the texts at (40, 280) up to x 620.

1. **"Are you sure you want to divide the plunder?"** (CSTR 86 with 95).
   No leaves.
2. **The crew's share (CSTR 155):**
   - `share = trunc(gold / crew x 0.8 + 0.5)`.
   - The crew's mood (CSTN 72, angry to happy) is
     `min(trunc(sqrt(share) / 23), 3)`.
   - The reputation becomes `(trunc(sqrt(share)) + reputation + 1) / 2`.
3. **The captain's portions (CSTR 156):** `2 x difficulty + 2`
   portions, worth `(difficulty + 1) x gold / 20`, added to the
   captain's own fortune (VA `0x45efe`).
4. **The treasure cave (VA `0xbdfe`):** eight piles (`tcave0`-`7`),
   each drawn once the fortune passes its mark (table at VA `0x32928`:
   x, y and the mark; 500, 1000, then by thousands to 7000).
5. **"Do you... Plan another expedition? Retire from privateering?"**
   (CSTR 157). Below Grand Master (difficulty 3), "Advance to <level>
   level?" (CSTR 97) is a third choice that raises the difficulty.
   - Retire asks "Are you sure you want to end the game?" (CSTR 86
     with 96). Yes goes to the retirement.
   - Another expedition (VA `0x184a8`, `0x1ef87`): the refit (CSTR
     158) advances the calendar four times by 26 days (VA `0x1dd0e`,
     the world's monthly step for each month crossed). Then the
     flagship alone, repaired, with a tenth of the gold; when the gold
     is 0, a tenth of the fortune less a tenth. The fleet gets that
     ship's capacity, `type x 2 + 6` guns, 15 food, no goods, and a new
     crew of `reputation + 16` (at least 1).

### Cargo

- **Holdings:** the player block holds food (`+0x0c`), sugar (`+0x0e`),
  a food fraction (`+0x10`), the hold capacity in tens of tons
  (`+0x12`) and goods (`+0x2c`), besides the guns (`+0x0a`).
- **Start values:** a start scenario sets food, goods and sugar from
  bytes 0x1a-0x1c, the fraction from 0x1e and the capacity from 0x1f.
- **Capacity:** a captured ship adds `2 x class + 2`; a lost or sold
  ship removes its share.
- **Space in the hold:**
  `capacity x 10 - guns / 2 - food - goods - sugar` tons.

### Merchant (VA `0x21e56`)

- **Greeting:** written on `merchant.pcx` at (160, 40) up to x 480, in
  ink 0, without a box.

- **Refusal:** as at the shipwright (CSTR 256), except for the
  merchant's family (town word 12 `& 0xe0`, set by marrying his
  daughter).
- **Greeting by the player's gold:** up to 2000, 5000, 20000, then
  more: CSTR 257-260, or 291-294 for the family.
- **Prices and stock,** rolled at each visit, with
  `W = (|6 - month| + 6) x wealth / 6` kept in floating point:
  - **Merchant's gold:** `trunc(W x (citizens + 10) / 150) x 600`, at
    most 50,000, kept for the stay.
  - **For goods i = 0, 1, 2 (food, goods, sugar):**
    - stock `= b + rand(b)`, with `b = trunc(W x (citizens + 10) /
      150)`;
    - base `= trunc(W / 50 + 3 - i / 2)`;
    - halved plus 1 when town flag bit `3 - i` is set (a local
      product);
    - sugar's doubled when bit 4 is set;
    - price `= base x 10`.
    - **Quirk:** the code then reuses W's variable, so each later good
      uses the previous good's price as its W, for both stock and
      price. The engine does the same.
  - **Then:** food and goods x 1.5, sugar x 1.5 then x 2 (truncating).
    For the family, each price x 0.75, at least 1.
- **Trading:**
  - One price both ways, one unit at a time.
  - **Buying** needs merchant stock, the price in gold and space in the
    hold.
  - **Selling** needs a unit and the merchant's gold.
  - The gold changes hands; the merchant's stock rises and falls.
- **Closing:** if the player's gold moved by more than 20, the town's
  wealth becomes `wealth x 19 / 20 + 12`, at most 255.
- **The daughter:** then comes CSTR 295-301 (meeting the merchant's
  daughter, marriage for 5,000 gold); not done yet.
- **The board (VA `0x20229`):**
  - Layout records at VA `0x348c4`: frame, highlighted frame, x, y.
  - The merchant's column is on the left and the player's (the skull)
    on the right.
  - Digits are glyphs of `buysell.spr`: big 0-19, small 20-52.
  - Pieces are per language (`buysell`, `buyboard`, `buysell1`, with
    `f` and `g` variants).
  - Its "Take all" button and the slider (VA `0x20ff3`) are not traced.
    The engine places the slider by the hold's fill, a guess.

### Reputation (VA `0x45e76`)

A signed byte, 0 at the start:
- +1 for each capture (VA `0xdb7b`);
- -2 for a failed boarding (VA `0xe775`);
- set to -1 after a disgrace in a duel (CSTR 174).

It names the player in the tavern (CSTN 34[`clamp(rep / 4 + 1, 0,
5)`]: cowardly ... infamous) and brings recruits.

### Tavern (VA `0x11867`)

- **Picture:** `tavern.pcx`. The tavern's name (CSTN 96, by town) is on
  the scroll at (320, 38).
- **People:** ten spots (table at VA `0x330f0`: x0, y0, x1, y1,
  present):
  - 0 the map seller and 1 the information seller (`tavern.spr` frames
    0 and 1 at (0, 255) and (142, 259));
  - 2, 7, 8 and 9 seamen to recruit;
  - 3 the bartender;
  - 4 the barmaid;
  - 5 a stranger (frame 2 at (349, 199));
  - 6 the door.
- **Presence:** whoever is spoken to leaves; all are back on the next
  arrival in town (VA `0x18fa9`).
- **Recruits (VA `0x10e2a`), counted on entering:**
  - `n = (rep + 7 - difficulty) x (citizens + rand(citizens)) / 9`;
  - with fewer than `4m` men aboard, at least `m = (4 - difficulty) x
    6`;
  - at most `(ships x 2 + capacity) x 16 - crew`, and at least 1.
- **Inks:** the tavern sets ink entry 1 (palette index 2) and highlight
  entry 2 (index 6) for its pages.
- **Recruiting (VA `0x10f04`), on `crew.pcx`:**
  - Written on the picture at (60, 300) up to x 620, the choice up to x
    560, without a box.
  - CSTR 151 offers the men, naming the player by reputation, rank and
    name.
  - Signing them up adds them to the crew; all four seamen spots then
    leave.
  - With no recruits: CSTR 309.
- **The bartender (VA `0x1183e`):**
  - On `bartenda.pcx`, in ink entry 0, "The Bartender tells you that:"
    (CSTN 18.1), then up to 12 recent public news items, at (222, 40)
    up to x 610 (VA `0x1e78d`, mode 1).
  - With none, he listens to the player's last exploit (CSTR 125 with
    CSTN 33, by the latest personal log entry), at (320, 150) up to x
    560 (VA `0x10b10`).
  - The engine keeps no personal log yet, so it always says "your
    promising plans".
- **Not done yet:** the map seller, the information seller, the barmaid
  and the stranger.

### Standing and ranks

- **Layout:** the player's standing per nation is a signed byte array at
  VA `0x45e7a` (index 4: pirates). The rank per nation (CSTN 78:
  Mr. ... Count) follows at VA `0x45e7f`.
- **Attacking a nation N by d (VA `0x1eaf5`):**
  - N's standing changes by d;
  - every other nation's by `d x relation(N, k)` (+1 alliance, -1 war);
  - but such changes cannot lift a standing above 1, nor drop one that
    is above 1 below 2.
- **Capturing a ship** of a nation (not pirates) is an attack with d =
  -1 (VA `0xdb7b`). Other callers (town attacks) are not traced yet.

### Governor (VA `0x13269`)

He receives the player once a stay, in towns of 6 citizens or more. In
order:

1. **The news (VA `0x11d64`):** "My dear <rank> <name>," with the
   nations his own is allied with and at war with (CSTN 25, names from
   CSTN 81). It ends with a charge to fight them when at war and the
   standing is 2 or more. With neither allies nor enemies: CSTR 149.
2. The family quest (VA `0x18913`), missions and captured pirates (VA
   `0x12e88`), and prisoners and ransoms (VA `0x12432`, `0x127da`): not
   done yet.
3. **Promotion (VA `0x12549`):**
   - With `r` the next rank, when `standing >= r^2 + 2` and `r <= 9`:
     the rank rises, and the standing drops to `r^2 + 2`.
   - CSTR 134 is shown with the proclamation (`gov.spr` frames at
     (140, 35) and (113, 262)).
   - When the surplus plus the town's word 9 is positive, that many
     times 50 acres are granted (CSTR 135, total at VA `0x45f02`).
4. **His offer, by standing s:**
   - With `s > -4`, the price is `(4 - s^3) x 200` gold.
   - From s = 2 the price is negative and his daughter is met instead
     (VA `0x128c1`, not done yet).
   - Otherwise (VA `0x12d6d`): a Letter of Marque for `s >= 0` (CSTR
     147), a pardon for `s < 0` (CSTR 148).
   - Paying raises the standing by 2, to at most 2, and resets the
     town's word 9. Without the gold: CSTR 94.

## Intro (VA `0x3325`)

1. `micro.pcx`: fade in, 2 s (20 x 100 ms), fade out.
2. On black, keeping that palette, each sprite faded in and out at its
   top-left position:
   - `and.spr` at (229, 227), 1 s;
   - `mpslabs.spr` at (195, 100), 2 s;
   - `present.spr` at (134, 231), 1 s.
3. `moon03.pcx`, faded in:
   - **Palette cycling:** 2000 loop passes (VA `0x36e4`). Each step
     rotates a range of palette entries up by one: 0xee-0xfe every 300
     passes, 100-163 every 50, 0xc2-0xce every 100.
   - **Title (VA `0x379f`):** `title.spr` and `title1.spr`, stacked at
     x 272, dissolve in by 2 x 2 cells of a 184 x 137 grid. The cells
     are visited in the order `x = (5x + 1) mod 32768` from a random
     start; values past the grid are skipped. One cell is revealed per
     pass, and the same cycling goes on.
   - **Then:** 0xd3-0xec rotates every 300 passes, 26 times, then pauses
     for 10000 passes.
   - **End:** after 45000 passes, at the end of a 26-step cycle, the
     picture fades out.
4. A key at any wait ends the intro.

Then the credits (VA `0x1fe91`):
- **Picture:** `credpic.pcx`, with the language's credits picture
  (`credtext`, `credtxtf`, `credtxtg`) as the source.
- **Blocks:** six rectangles of the credits picture (table at VA
  `0x34668`), shown one at a time. Each is centred at (508, 240), with x
  rounded down to a multiple of 8.
- **Fades:** the text is palette entry 0x20. Its colour steps through
  20 entries (table at VA `0x34698`) from the parchment colour to black,
  40 ms each. It then holds 1.6 s and steps back.
- **Keys:** a key during blocks 0-4 jumps to block 5; a key during
  block 5 ends the credits.

The welcome menu (VA `0x1443e`) is full screen on `textback.pcx`:
- the 10 lines of CSTN group 100 are centred at x 320, at heights 70,
  120, 145, 170, 195, 250, 300, 330, 360 and 390;
- the 6 heading lines use PIRATE4, and the 4 options PIRATE1. The game
  loads its fonts in the order PIRATE1, PIRATE2, CAPS, PIRATE3,
  PIRATE4;
- each option's mouse area is a full-width row 30 pixels high.

The title screen's passes wait 1/10 of a 10 ms tick, which is 0. So
their speed is the CPU's. The engine runs 167 passes per 1/60 s frame,
about 0.1 ms each, and starts the dissolve at cell 0.

## Screens in general

- **Inks:** text colours are palette entries of the current picture,
  from the table at VA `0x3421c`: 0, 2, 6 and 31.
  - The story uses entry 2, an orange in `frame.pcx`.
  - The engine reads that entry from the picture where it has been
    traced, and uses fixed colours elsewhere.
- **"Click to continue" (VA `0x1a3c4`):** after a message, the game
  waits for a key.
  - After 420 vertical retraces without one (about 7 s), it draws the
    plaque of `misc.crs` frame 7 (`miscf`, `miscg` by language) at
    (268, 449), then keeps waiting.
  - The engine draws the same plaque after 420 idle ticks wherever it
    waits for a key.

## New career: the time period (VA `0x15a3c`)

On `textback.pcx`:
- the title (CSTR 64) is centred at y 20 in PIRATE2;
- each period's name (CSTN 98) is centred in PIRATE1 at y 60, 130, 200,
  270, 340 and 410;
- its description (CSTR 261-266) sits under it in PIRATE3, across x
  40-620, at y 83 + 70 per period;
- each period is a full-width row 70 pixels high for the mouse (VA
  `0x333d8`).

## New career: the identity screen (VA `0x1482e`)

After the era (or a famous expedition), one picture, `idselect.pcx`,
fills in step by step. The captain's portrait (`bigman0.spr`) sits in
the oval at (237, 66).

1. **Nationality (VA `0x14aa4`, CSTR 70):**
   - Four small flags (`lilflgs.spr`, frames in the order English,
     French, Dutch, Spanish) at (0, 0), (51, 155), (481, 1) and
     (441, 151) (table at VA `0x33488`). The flags available per era are
     at VA `0x3357c`; no Dutch in 1560.
   - Pointing at a flag names it on the scroll (CSTN 82, centred at
     (320, 368)); a click chooses it, and the keys step through them.
   - The chosen nation's big flag (`big?flg.spr`) then stays at (0, 0),
     with its name at (160, 250).
2. **Special ability (VA `0x14e6e`, CSTR 67):**
   - Five icons (`lilskls.spr`) along the bottom (table at VA
     `0x334b0`). Slot i is ability i (CSTN 83 order); the icon frames
     shown are 0, 3, 1, 2, 4.
   - The chosen ability's picture (`bigfen.spr` ...) then stays at
     (440, 0), with its name at (520, 250).
   - A famous expedition starts here, with its own flag.
3. **Difficulty (VA `0x151c6`, CSTR 66):**
   - The four levels (CSTN 35) in a row at x 80, 190, 300, 410, y 345.
   - Pointing at one selects it: it is highlighted, described below
     (CSTN 101, centred at (320, 380)), and the portrait becomes
     `bigman0`-`3` for that level. A click or Enter confirms.
4. **Name (VA `0x155fa`, editor VA `0x157f4`, CSTR 65):**
   - The field starts as "Incognito" (CSTN 92.4); Enter or a click
     accepts it.
   - The first letter typed replaces it; Backspace only erases what was
     typed.
   - At most 11 characters.
5. **Confirmation (VA `0x15498`):** "Are ye satisfied with your choices,
   Captain?" (CSTR 273), with "Yes" (CSTR 274) at (208, 368) and "Nay"
   (275) at (390, 368) (click areas at VA `0x33470`). "Nay" starts the
   identity over from the nationality, keeping the era.

## New career: the story (VA `0x139e2`)

- **Setup (VA `0x1451e`):** before the story, the game lowers every
  nation's standing by 16, marks the towns whose information the player
  knows (by difficulty) and picks the missing family member.
- **Picture:** `frame.pcx` with the captain's portrait for the
  difficulty at (236, 58). The text is at (40, 340), up to x 620.
- **Scripts:** one byte script per career, at VA `0x35968`, separated by
  -1, in the order of the start scenarios (era x 4 + nation, English,
  French, Dutch, Spanish; then the expeditions).
- **Bytes:**
  - A CSTR number shows that text. Its `%s` are the treasure (CSTR 46,
    or 45 the silver train), a town, the year and the player's name.
  - -2 waits for a key and turns the page.
  - ')' waits for a key.
  - '*' prepares the fight, and 'a' fights it (VA `0x16205`).
  - 6 asks a question; 0 ends.
  - '2'-'7' show nothing.
- **The copy-protection question:**
  - ')' and '*' were the original's question.
  - **Pass:** the standings get their 16 back, and the fight is 200 men
    of factor 32 against 20 of 16.
  - **Fail:** difficulty 4, no guns, 8 gold, and a hard fight, followed
    by the script's other branch after its first 0.
  - In this build the comparison overwrites the expected answer with
    the given one (VA `0x14695`), so it always passes. The engine keeps
    the pass branch, and since standings start at 0, it applies neither
    the -16 nor the +16.
- **Engine choices:**
  - The career starts when the player confirms, before the story.
  - The story's fight uses the boarding fight's screen, with no weapon
    choice and nothing at stake.
  - Escape skips the story.

## New career: the sailing master (VA `0x13e3f`)

After the story, on `smaster.pcx`, with the text at (340, 100) up to x
620, two pages:

1. **CSTR 68:** "Here in <town>, you'll probably be wantin' to see the
   <nation> governor…".
2. **For each other nation:** its nearest town by `|dx| + |dy|` on the
   map, as CSTR 69, "The closest <nation> city is <town>, which lies
   <days> days to the <direction>."
   - `<days>` is "a few" (CSTR 48) under 31, otherwise "many" (CSTR 49).
   - The direction (CSTN 75) comes from the signs of dx and dy, after
     zeroing an axis that is less than half the other.

Each page's last character, a line break, is replaced by a closing
quote. The career then begins in the start town.

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
