# Career bot

`career.py` plays a whole career through `flibustier-sim`, as fast as the
machine allows, and writes a dated journal of what happens. It is a long
regression test for the rules: a career that crashes the simulator, gets
stuck or does something odd shows up in the journal as an `ANOMALY` line.

What it does, roughly in this order of priority:

- digs up the treasure maps it holds: the lost relative's first, then the
  lost treasure the relative gives, then the pirate's map;
- buys food and repairs at friendly towns, sells cargo, recruits only the
  men its gold keeps content, and divides the plunder before the crew turns
  mutinous;
- hunts the villains named after each promotion, which gives the next
  relative's map;
- attacks the town holding the silver train once strong enough;
- otherwise cruises off Spanish towns, boarding the ships it can beat,
  ransoming captains, courting governors' nieces (a daughter after nine years) and fighting their
  suitors.

It needs the build (`flibustier-sim` and `flibustier-seamap`, the sea map
it plans routes on) and the original game data:

```sh
cmake --build build
python3 tools/career-bot/career.py --data ~/games/pirates_gold --seed 3
```

Options: `--era` (1560 to 1680), `--nation`, `--seed`, `--years` (12 by
default), `--out DIR` for the journal (`journal-<era>-<nation>-seed<N>.txt`,
written as the career goes, so `tail -f` follows it), `--build DIR`, and
`-v` to print the journal live. At the end it prints the final state, the
reckoning retiring would give (`career_end`: score and Hall of Fame
rating), its counters (captures, duels, maps dug, ...), the protocol calls made and the
anomalies. It exits 1 when a protocol command failed.

Like a player, it sees the shallows (`flibustier-seamap --pixels` gives
the exact tiles) and sails a few ticks ahead in the sim to steer clear of
them; `BOT_REEF_GUARD=0` turns that off.

It enters a town by sailing onto one of the town's arrival cells
(`flibustier-seamap --arrivals` lists them), as the game requires.

After each division of the plunder the journal gets a `reckoning` line,
the score retiring there would give. `BOT_RETIRE=N` retires at the first
division N years into the career instead of playing all the years; on
500 careers, 10 scored best (mean 13.7 against 12.8 for 12 years).

The bot is a mediocre captain: it runs onto reefs, takes some fights it
should not, and its choices are not meant to be optimal. It is not part of
`ctest`, since a career takes a few minutes.
