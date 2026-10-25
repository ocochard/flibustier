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
| `set` | `town` (name or index), `field`, `value` | none |
| `set` | `relation` ([a, b], nation indices), `value` (-1, 0, 1) | none |
| `set` | `field`: `player_town` (-1 or town index), `value` | none |
| `set` | `field`: `diplomacy_range`, `value` | none |
| `quit` | none | none, then the program exits |

- `new_game` fails for the Dutch in 1560, which has no career start.
- Commands other than `new_game`, `load` and `quit` need a game first.
- `set` is the debug and test hook. A setting holds until the rules
  change it again.

## State

```json
{
  "version": 2,
  "rng": ["<state hex>", "<inc hex>"],
  "player": {"name": "Olivier", "title": "English Buccaneer",
             "nation": 1, "difficulty": 1, "ability": 2,
             "expedition": -1},
  "era": 1660, "year": 1661, "day_of_year": 125, "month": 4, "day": 485,
  "diplomacy_range": 100,
  "player_town": -1,
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
