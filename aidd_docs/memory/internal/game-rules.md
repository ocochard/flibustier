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
- **Hailing:** the player gets the latest news from the log; the
  original's rumours (VA `0x1e78d`) are not traced.
- **Hostile pirates:** they use the "He's chasing us" dialog (CSTR 311).
  The original's named pirates (CSTR 121) need the pirate name lists.
- **Battles:** they end the encounter until sea battles exist (phase 6).
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
- **Enemy reload:** `|144 - 8 x role|` ticks.

### Each tick

- **Steering:** the player's heading changes by 16 x turn input every
  8 ticks.
- **Sails:** changing sails takes 160 ticks. Full sails then multiply
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

### End of battle

| Outcome | When | What follows |
|---|---|---|
| boarding | quarter-size boxes touch | CSTR 171, then a sword fight |
| escape | the ships are more than 240 pixels apart on either axis | the encounter ends; possible loss of a slow ship (CSTR 172) |
| sunset | 4500 ticks | CSTR 169 |
| surrender | as above | CSTR 170, then capture (CSTR 164: keep her or plunder and sink her) |
| player sunk | damage level 4 | CSTR 167; the fleet loses the ship; with no ship left, marooned (CSTR 161) |
| enemy sunk | damage level 4 | CSTR 168 |

Afterwards the flagship's crew and guns, and its damage (in the type
byte), return to the fleet.

### Engine choices until more is traced

- **Boarding:** the larger crew wins, standing in for the sword fight
  (traced below, not implemented yet). A win counts as a surrender.
- **Prize:** a captured ship pays the encounter record's byte 6 x 10
  gold, standing in for cargo and treasure. Kept, she joins the fleet
  with her guns.
- **Marooned:** with no ship left, the player gets a pinnace and at
  least 8 men.
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
- **Duels (VA `0x161ba`):** one man each, factors 9 and 9. A loss or
  being driven back shows CSTR 174 (disgrace).
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
  1 man, the opponent is beaten: his next hit taken makes him fall. The
  same holds the other way for the player.
- **Speed:** a tick lasts 12, 11, 10 or 9 timer units by `level & 3`
  (VA `0x389c4`).
- **Debug flag:** a byte at VA `0x33ee9`, 0 in the shipped game, would
  make the player invulnerable. No code sets it.

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
