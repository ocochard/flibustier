# Simulation protocol

`stmalo-sim` runs the game rules without graphics, for bots, AI agents
and tests. It reads one JSON object per line on stdin and writes one per
line on stdout. Blank lines and lines starting with `#` are ignored, so a
file of commands works as a replay: the same file always gives the same
output. The implementation is in `src/sim/proto.c`; the rules are in
[game-rules](game-rules.md).

```sh
stmalo-sim --data ~/games/pirates_gold < tests/replays/ten_years.jsonl
```

Every response has `"ok": true|false`. A failure also carries `"error"`
with a message. Numbers are integers only.

## Commands

| Command | Fields | Response |
|---|---|---|
| `new_game` | `era` (1560, 1600, 1620, 1640, 1660, 1680); `nation` (`spain`, `england`, `france`, `holland`; default `england`); `seed` (integer; default 1) | `scenario` (start name), `towns` (count) |
| `advance` | `days` (1..36500) | `day`, `news` (items logged by this advance) |
| `state` | none | `state`: the full game state (below) |
| `load` | `state`: an object as returned by `state` | none |
| `sail` | `turn` (-1, 0, +1; default 0), `ticks` (1..23360; default 64) | `stop` (`ticks`, `land` or `town`), `town` when it is `town`, `x`, `y`, `heading`, `speed`, `wind`, `day` |
| `encounter` | `action`: `investigate`, `ignore`, `battle` or `hail` | `result` (`continue`, `escaped`, `news`, `battle`, `overtaken`, `unanswered`), `encounter` |
| `battle` | `ticks` (1..10000; default 1), `turn` (-1, 0, +1), `fire` (0, 1: on the first tick only), `sails` (-1 battle, 0 keep, +1 full) | `events` (bits, below), `battle` |
| `finish` | `keep` (0, 1): a captured ship joins the fleet | `outcome`, `fleet_ships`, `crew`, `gold` |
| `town` | `action`: `enter` (the town `sail` stopped at) or `leave` | `player_town`, `visit_flags` |
| `shipwright` | `action`: `prices`, `repair` or `sell`; `ships` (fleet indices) | `result`, or `repair` and `price` lists; `gold`, `shipwright_gold` |
| `merchant` | `action`: `open`, `buy`, `sell` (`good`: food, goods, sugar; `count`) or `close` | `traded`, `gold`, `merchant_gold`, `hold_space`, `stock`, `price` |
| `tavern` | `action`: `open` (counts the recruits) or `recruit` (`accept`: 0 or 1) | `recruits`, `crew`, `tavern_spots` |
| `board` | `weapon` (0 rapier, 1 longsword, 2 cutlass; default 2), after a `boarding` outcome | `sword` |
| `sword` | `ticks` (1..10000; default 1), `input` (0..11, optional: the move to take next) | `events` (bits, below), `sword` |
| `set` | `town` (name or index), `field`, `value` | none |
| `set` | `relation` ([a, b], nation indices), `value` (-1, 0, 1) | none |
| `set` | `field`: `player_town` (-1 or town index), `value` | none |
| `set` | `field`: `diplomacy_range`, `value` | none |
| `set` | `field`: `encounters`, `value` (0 or 1) | none |
| `quit` | none | none, then the program exits |

- `new_game` fails for the Dutch in 1560, which has no career start.
- Commands other than `new_game`, `load` and `quit` need a game first.
- `set` is the debug and test hook. A setting holds until the rules
  change it again.

## State

```json
{
  "version": 9,
  "rng": ["<state hex>", "<inc hex>"],
  "player": {"name": "Olivier", "title": "English Buccaneer",
             "nation": 1, "difficulty": 1, "ability": 2,
             "expedition": -1},
  "fleet": {"ships": [1], "damage": [0], "gunnery": 0,
            "food": 13, "goods": 0, "sugar": 0, "capacity": 4,
            "crew": 40, "guns": 8, "gold": 2000,
            "x": 5477, "y": 4319, "fx": 0, "fy": 0, "heading": 0,
            "speed": 0, "ticks": 0, "leaving": 24},
  "wind": 192, "encounters": 1, "heat": 0, "heat_town": -1,
  "encounter": {"stage": "none", "type": 4, "nation": 1, "crew": 8,
                "guns": 8, "role": 5, "cargo": 60, "gold": 25,
                "hostile": 0, "town": 20},
  "battle": {"active": 0, "tick": 0, "wind": 192, "wind_strength": 1,
             "decision": 1, "outcome": "none", "crew_ashore": 0,
             "guns_ashore": 0,
             "you": {"x": 69632, "y": 53760, "heading": 160,
                     "target": 0, "speed": 0, "type": 1, "guns": 10,
                     "crew": 40, "damage": 0, "full_sails": 0,
                     "sail_timer": 0, "reload": 0, "turn_rate": 3,
                     "role": 0, "shot": [0, 0, 0, 0]},
             "foe": {"...": "same fields"}},
  "sword": {"active": 0, "kind": 0, "level": 0, "title": 4,
            "nation": 1, "counter": 1, "result": "none", "beaten": 0,
            "input": 0, "script": 3, "step": 0, "you_hits": 0,
            "foe_hits": 0, "men_start": 40,
            "you": {"x": 340, "move": -1, "frame": 0, "men": 40,
                    "bar": 22, "weapon": 2, "guard": 0,
                    "wounds": [0, 0, 0]},
            "foe": {"...": "same fields"}},
  "practice": 0,
  "era": 1660, "year": 1661, "day_of_year": 125, "month": 4, "day": 485,
  "diplomacy_range": 100,
  "player_town": -1, "visit_town": -1, "visit_flags": 0,
  "wright_gold": -1, "merchant_gold": -1,
  "reputation": 0, "tavern_spots": 1023, "recruits": 0,
  "market": {"open": 0, "stock": [0, 0, 0], "price": [0, 0, 0],
             "gold_at_open": 0},
  "relations": [[0,-1,0,0], [-1,0,0,-1], [0,0,0,0], [0,-1,0,0]],
  "standing": [0, 0, 0, 0],
  "towns": [{"name": "Havana", "flags": 24, "x": 245, "y": 179,
             "nation": 0, "forts": 4, "soldiers": 45, "citizens": 65,
             "gold": 60, "wealth": 195, "standing": 0, "governor": 0,
             "id": 6}],
  "news_total": 95,
  "news": [{"day": 30, "event": "gold_mine", "a": 12, "b": 0,
            "town": "Tortuga"}]
}
```

- `sail` steers and sails, 64 ticks a day; it stops early on land or at
  a town. `fleet` holds the flagship's position in world pixels (16 per
  map cell) with 1/8-pixel remainders. `heading` and `wind` are
  directions in 256ths of a turn, 0 north, 64 east; `wind` is the
  direction it blows toward. `leaving` is the town just left, or -1.
- `sail` also stops with `encounter` when a sail is sighted. The
  `encounter` command then answers its dialogs one at a time: `stage`
  goes `sighted`, `identified`, `colours`, and is `none` once the
  encounter is over. Sailing waits until then.
  - Encounter `type` follows CSTN group 8: 0 pinnace … 8 fast galleon.
    `nation` 4 is pirates.
  - `hostile` ships chase the player and cannot be avoided once seen.
  - `encounters` set to 0 (with `set`) stops sightings, for tests and
    bots that only sail. `heat` is the counter that thins sightings near
    towns.
- A `battle`, `overtaken` or `unanswered` encounter result starts a sea
  battle; the encounter closes and `battle.active` is 1. Sailing and
  the encounter command wait until `finish` applies the outcome.
  - `battle` runs ticks with the same inputs and stops early once
    `outcome` is set: `boarding`, `escaped`, `sunset`, `surrender`,
    `player_sunk` or `enemy_sunk`.
  - `events` bits: 1 the player fired, 2 the enemy fired, 4 the player
    hit, 8 the enemy hit, 16 the player's guns reloaded, 32 the
    player's sails changed, 64 the wind changed.
  - Positions are in 1/256 pixels on a 480 x 480 field. `shot` is
    `[ticks left, x, y, heading]`; 0 ticks means no shot in flight.
  - A `boarding` outcome needs a sword fight: `board` starts it and
    `sword` fights it. `finish` then returns `surrender` for a win
    (the prize may be kept), `repelled` when the player was driven
    back, or `defeated` when the enemy takes the flagship.
  - Sword `input` moves: 0-2 guard middle, low, high; 3-5 the same
    while stepping back; 6-8 slash and 9-11 lunge, middle, low, high.
    Guards and attacks are taken once; steps back repeat until another
    input replaces them.
  - Sword `events` bits: 1 the player hit, 2 the opponent hit, 4 a
    clash, 8 the opponent's crew lost men, 16 the player's did.
  - The fight's `result` is `won`, `repelled` or `lost`. `bar` is each
    side's share of the strength bar (47 in all); `wounds` count hits
    taken high, middle and low.
  - `fleet.damage` is the hull damage per ship; `fleet.gunnery` is the
    unidentified player value that sets the reload time.
- `rng` holds the generator's two 64-bit words. Loading a state
  restores it, so a game continues exactly as it would have.
- Nations are indexed 0 Spain, 1 England, 2 France, 3 Holland.
  `relations[a][b]` is -1 for war, 0 for peace, +1 for alliance.
- `day` counts days since the start, and news items are dated with it.
- `standing` is the player's standing with each nation (only the
  amnesty rule changes it so far).
- `news` keeps the latest 256 items; `news_total` counts every item ever
  logged.
  - `event` is one of: `amnesty`, `indians`, `gold_mine`, `malaria`,
    `governor`, `plunder`, `war`, `peace`, `alliance`, `capture`.
  - `a` and `b` are the raw arguments: a town index, or nation indices
    for war, peace and alliance; for a capture, `a` is the nation and `b`
    the town.
  - Readable fields are added for convenience: `town`, `nation`, `text`.
- `diplomacy_range` replaces an original value not traced yet (see the
  open questions in [game-rules](game-rules.md)).

## Saved games

A saved game is one file per slot, `save0.json` to `save9.json`
(`src/sim/save.c`):

```json
{"format": "stmalo-save", "version": 1,
 "description": "Olivier, English Buccaneer, April 1660",
 "state": { ...the state above... }}
```

- **Location:** the directory comes from `--saves DIR`, else
  `$STMALO_SAVES`, else `$XDG_DATA_HOME/stmalo/saves`, else
  `~/.local/share/stmalo/saves`. It is created when missing.
- **Writing:** a save goes to a temporary file that is then renamed, so a
  crash never leaves a broken slot.
- **Readable:** saves are written one value per line so people can read
  and edit them; any valid JSON loads.
- **Hall of Fame:** kept in `halloffame.txt` in the same directory: a
  `# stmalo hall of fame 1` line, then one line per career, best first,
  with tab-separated fields: score, rating (0 = best), name, title. A
  hand-edited file is sorted again on load.
- **Original saves:** the original's `PIRATES-<n>.SAV` layout is in
  [asset-formats](asset-formats.md). The engine does not read those files
  yet.

## Tests

- `tests/test_sim.c` covers the RNG, JSON, the rules, the protocol and
  snapshots. It uses a synthetic era, so it needs no game data.
- Every `tests/replays/*.jsonl` file is run twice through `stmalo-sim`
  (`tests/replay.cmake`). The test fails if a command fails or the two
  outputs differ.
