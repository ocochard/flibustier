#!/usr/bin/env python3
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbe
#
"""A bot living a whole flibustier career through flibustier-sim.

It sails, fights, trades, courts, duels villains, rescues the lost
relatives and digs up treasure, writing a dated journal of what happens
and of anything that looks wrong.  See README.md.
"""
import argparse, collections, heapq, json, math, os, random, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
DATA = os.environ.get("FLIBUSTIER_DATA")
BUILD = os.path.join(REPO, "build")
OUT = "."
FORT_MARGIN = 8		# hull to spare, by fort_margin, before fighting a fort
FALLBACK_YEARS = int(os.environ.get("BOT_FALLBACK", 9))	# after this, a governor's daughter will do instead of his niece
NATIONS = ["spain", "england", "france", "holland"]


# Tuning knobs, from 144-seed comparisons: with the reef guard the fleet
# limit makes no difference (2, 3 and 4 ships average 13.9, 14.0, 14.2).
REEF_GUARD = int(os.environ.get("BOT_REEF_GUARD", 1))
REEF_MARGIN = int(os.environ.get("BOT_REEF_MARGIN", 10))	# pixels kept clear of shallows
REEF_HORIZON = int(os.environ.get("BOT_REEF_HORIZON", 6))	# chunks of 4 ticks looked ahead
DIVIDE_MONTHS = int(os.environ.get("BOT_DIVIDE_MONTHS", 6))
CREW_MAX = int(os.environ.get("BOT_CREW_MAX", 160))
RECRUIT_AHEAD = int(os.environ.get("BOT_RECRUIT_AHEAD", 0))	# months the mood is sized for
CREW_HUNT = int(os.environ.get("BOT_CREW_HUNT", 0))	# men enough to hunt ships; 0: always CREW_MAX
DIVIDE_ANY = int(os.environ.get("BOT_DIVIDE_ANY", 1))	# divide in any town entered openly (144 seeds: 14.0 -> 15.5, mutinies halved)
HUNT_WAR = int(os.environ.get("BOT_HUNT_WAR", 0))	# hunt villains in towns at war, sneaking in
MISSIONS = [int(c) for c in os.environ.get("BOT_MISSIONS", "")]	# governors' mission types to take: 1 letter, 2 papers
ADVANCE = int(os.environ.get("BOT_ADVANCE", 0))	# raise the difficulty at the bank: 1 always, 2 with a treasure map in hand
WIND = int(os.environ.get("BOT_WIND", 0))	# price routes by the wind
RANK_NATIONS = [int(c) for c in os.environ.get("BOT_RANKS", "")]	# nations to win rank with besides its own: 2 France, 3 Holland
HOME_MONTHS = int(os.environ.get("BOT_HOME_MONTHS", 8))	# months without a division before heading for a bank
HOME_ANY = int(os.environ.get("BOT_HOME_ANY", 0))	# any friendly town will do, not only the bot's own nation's
MAX_SHIPS = int(os.environ.get("BOT_MAXSHIPS", 3))
GUNNERY = int(os.environ.get("BOT_GUNNERY", 2))	# 0 ram only, 1 aimed fire, wind and lead, 2 also turn to fire on a fleeing enemy, 3 also spare a prize the sinking shot
FLEE_DAMAGE = int(os.environ.get("BOT_FLEE_DAMAGE", 32))	# hull damage that sends the bot running from a losing fight
SHIPS = os.environ.get("BOT_SHIPS", "rating")	# rating, big, small or proof
FOOD_DIV = int(os.environ.get("BOT_FOOD_DIV", 4))
CONQUER = int(os.environ.get("BOT_CONQUER", 1))
CONQUER_FORTS = int(os.environ.get("BOT_CQ_FORTS", 1))
CONQUER_MEN = int(os.environ.get("BOT_CQ_MEN", 15))


def fort_margin(st, town, ship, damage):
    """The hull a ship would have left on reaching the fort of town
    "town" under battle sails, by the fort battle rules: half speed, the
    best way the wind allows (a ship makes about 0.023 pixels a tick per
    point of sector speed, measured), the fort firing every 144 ticks
    for forts x 12 guns, each hit doing guns x 4 / (class + 2), halved by
    battle sails.  The fort aims from its corner and misses small ships
    more: it hits about a quarter, half and nine tenths of its shots on
    classes 0-2, 3-5 and 6-8 (measured).  Below 0, she sinks first; the
    fight also slows her as she takes damage."""
    t = st["towns"][town]
    cls = ship & 7
    dx = t["fort_x"] * 32 + 16 - (t["ship_x"] * 32 + 56)
    dy = t["fort_y"] * 32 + 16 - (t["ship_y"] * 32 + 46)
    want = Bot.bearing(dx, dy)
    sectors = st["ship_classes"][cls]["sectors"]
    way = max(sectors[((h + 8 - st["wind"]) >> 4) & 15] *
              math.cos((h - want) * math.pi / 128) for h in range(0, 256, 8))
    if way <= 0:
        return -64
    ticks = max(0, math.hypot(dx, dy) - 24) / (way * 0.023)
    hit = max(1, (t["forts"] & 7) * 12 * 4 // (cls + 2) // 2)
    return 0.75 * (64 - damage) - ticks / 144 * hit * (0.25, 0.55, 0.9)[cls // 3]


class Fail(Exception):
    pass


class Sim:
    def __init__(self, log):
        self.p = subprocess.Popen([os.path.join(BUILD, "flibustier-sim"),
                                   "--data", DATA], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, text=True, bufsize=1)
        self.log = log
        self.calls = collections.Counter()

    def __call__(self, cmd, check=True, **kw):
        kw["cmd"] = cmd
        self.calls[cmd] += 1
        self.p.stdin.write(json.dumps(kw) + "\n")
        line = self.p.stdout.readline()
        if not line:
            raise Fail("sim died on %r" % kw)
        r = json.loads(line)
        if check and not r.get("ok"):
            raise Fail("%r -> %s" % (kw, r.get("error")))
        return r

    def state(self):
        return self("state")["state"]


class Search:
    """Dijkstra over a flat grid padded by one cell, run only as far as the
    cells asked for.  Costs: shallows and shores are dear (in base), cells
    that wrecked us dearer still."""

    def __init__(self, base, w2, reefs, goal, far, muls=None):
        self.base, self.w2, self.reefs = base, w2, reefs
        self.muls = muls or (1.0,) * 8	# time per step, by direction, as steps
        self.dist = [far] * len(base)
        self.dist[goal] = 0
        self.q = [(0, goal)]
        self.steps = ((1, 0, 10), (-1, 0, 10), (w2, 0, 10), (-w2, 0, 10),
                      (w2 + 1, 1, 14), (w2 - 1, 2, 14), (-w2 + 1, 3, 14), (-w2 - 1, 4, 14))

    def reach(self, target):
        """Settle every cell at most as costly as target; returns the
        distances (final for those cells, which is all a descent needs)."""
        dist, q, base, w2, reefs = self.dist, self.q, self.base, self.w2, self.reefs
        muls = self.muls
        pop, push = heapq.heappop, heapq.heappush
        while q and (not q[0][0] > dist[target] or dist[target] >= (1 << 40)):
            d0, cur = pop(q)
            if d0 > dist[cur]:
                continue
            for k, (o, diag, cost) in enumerate(self.steps):
                n = cur + o
                m = base[n]
                if not m:
                    continue
                if diag:
                    # no cutting a corner: both orthogonal cells must be water
                    ox = 1 if diag in (1, 3) else -1
                    oy = w2 if diag in (1, 2) else -w2
                    if not (base[cur + ox] and base[cur + oy]):
                        continue
                d = d0 + cost * m * (40 if n in reefs else 1) * muls[k]
                if d < dist[n]:
                    dist[n] = d
                    push(q, (d, n))
        return dist


class Bot:
    def __init__(self, era=1660, nation="england", seed=1, verbose=False):
        self.events = []
        self.anomalies = []
        self.verbose = verbose
        self.sim = Sim(self.note)
        r = self.sim("new_game", era=era, nation=nation, seed=seed)
        self.scenario = r["scenario"]
        self.era = era
        self.grid = subprocess.run(
            [os.path.join(BUILD, "flibustier-seamap"), "--data", DATA,
             str(era)], stdout=subprocess.PIPE, text=True,
            check=True).stdout.splitlines()
        self.H, self.W = len(self.grid), len(self.grid[0])
        self.components()
        if REEF_GUARD or WIND:
            self.load_pixels(era)
        # per-cell cost factor on a grid padded by one cell; 0 is not water
        self.base = [0] * ((self.W + 2) * (self.H + 2))
        for y in range(self.H):
            for x in range(self.W):
                if self.grid[y][x] in ".,":
                    self.base[(y + 1) * (self.W + 2) + x + 1] = (
                        (40 if self.grid[y][x] == "," else 1) * (4 if (x, y) in self.shore else 1))
        self.journal = open(os.path.join(OUT, "journal-%d-%s-seed%d.txt" % (
            era, nation, seed)), "w", buffering=1)
        self.unreachable = set()
        self.main_sea = None
        self.fields = {}
        self.no_repair_until = 0
        self.food_town = None
        self.reefs = set()	# cells where a ship was lost on shallows
        self.wrecks = 0	# ships lost on shallows so far
        self.forts_lost = set()	# towns whose fort beat us off
        self.bad_landings = set()	# dig landings whose voyage wrecked us
        self.dig_fails = collections.Counter()	# failed trips, per map
        self.visited = {}
        self.mission_tries = 0
        self.dead_ends = set()
        self.bumps = collections.Counter()
        self.st = self.sim.state()
        self.stats = collections.Counter()

    # ------------------------------------------------------------ logging
    def note(self, msg):
        s = self.st
        stamp = "%d-%02d" % (s["year"], s["month"] + 1) if s else "?"
        self.events.append("%s %s" % (stamp, msg))
        self.journal.write(self.events[-1] + "\n")
        if self.verbose:
            print(self.events[-1], flush=True)

    def odd(self, msg):
        self.anomalies.append(msg)
        self.note("ANOMALY " + msg)

    def refresh(self):
        self.st = self.sim.state()
        return self.st

    # --------------------------------------------------------- navigation
    def water(self, x, y):
        return 0 <= x < self.W and 0 <= y < self.H and self.grid[y][x] in ".,"

    def components(self):
        comp = {}
        n = 0
        for y in range(self.H):
            for x in range(self.W):
                if self.grid[y][x] == "." and (x, y) not in comp:
                    n += 1
                    stack = [(x, y)]
                    comp[(x, y)] = n
                    while stack:
                        cx, cy = stack.pop()
                        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                            q = (cx + dx, cy + dy)
                            if self.water(*q) and q not in comp:
                                comp[q] = n
                                stack.append(q)
        self.comp = comp
        # the same over open water alone: a lagoon behind a reef is apart
        deep = {}
        n = 0
        for y in range(self.H):
            for x in range(self.W):
                if self.grid[y][x] == "." and (x, y) not in deep:
                    n += 1
                    stack = [(x, y)]
                    deep[(x, y)] = n
                    while stack:
                        cx, cy = stack.pop()
                        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                            q = (cx + dx, cy + dy)
                            if 0 <= q[0] < self.W and 0 <= q[1] < self.H and \
                                    self.grid[q[1]][q[0]] == "." and q not in deep:
                                deep[q] = n
                                stack.append(q)
        self.deep = deep
        # open cells touching a ',' cell or land: ships drift onto shallows there
        self.shore = {(x, y) for y in range(self.H) for x in range(self.W)
                      if self.grid[y][x] == "." and any(
                          self.grid[y + dy][x + dx] != "."
                          for dy in (-1, 0, 1) for dx in (-1, 0, 1)
                          if 0 <= y + dy < self.H and 0 <= x + dx < self.W)}

    def path(self, start, goal):
        """A* over water cells, 8-connected; shallows only as a last step."""
        if not self.water(*goal):
            # nearest water cell to the goal
            best = None
            for r in range(1, 6):
                for dy in range(-r, r + 1):
                    for dx in range(-r, r + 1):
                        if self.water(goal[0] + dx, goal[1] + dy):
                            d = abs(dx) + abs(dy)
                            if best is None or d < best[0]:
                                best = (d, (goal[0] + dx, goal[1] + dy))
                if best:
                    break
            if best is None:
                return None
            goal = best[1]
        if start not in self.comp:
            # on land, or in a ',' cell: head for the nearest open sea
            near = [(abs(dx) + abs(dy), (start[0] + dx, start[1] + dy))
                    for dy in range(-4, 5) for dx in range(-4, 5)
                    if (start[0] + dx, start[1] + dy) in self.comp]
            if near:
                start = min(near)[1]
        if start not in self.comp:
            return None
        if self.comp.get(start) != self.comp.get(goal):
            c = self.comp.get(start)
            near = [(abs(dx) + abs(dy), (goal[0] + dx, goal[1] + dy))
                    for dy in range(-8, 9) for dx in range(-8, 9)
                    if self.comp.get((goal[0] + dx, goal[1] + dy)) == c]
            if not near:
                return None
            goal = min(near)[1]
        W2 = self.W + 2
        sp = (start[1] + 1) * W2 + start[0] + 1
        gp = (goal[1] + 1) * W2 + goal[0] + 1
        if self.wind_key() != getattr(self, 'fields_key', None):
            self.fields.clear()
            self.fields_key = self.wind_key()
        search = self.fields.get(goal)
        if search is None:
            search = self.fields[goal] = self.search(goal)
        field = search.reach(sp)
        if field[sp] >= self.FAR:
            return None
        out = [start]
        cur = sp
        while cur != gp:
            best = None
            for o in (1, -1, W2, -W2, W2 + 1, W2 - 1, -W2 + 1, -W2 - 1):
                n = cur + o
                if field[n] < field[cur] and (best is None or field[n] < field[best]):
                    best = n
            cur = best
            out.append((cur % W2 - 1, cur // W2 - 1))
        return out

    FAR = 1 << 40

    def wind_key(self):
        """What the wind costs depends on the fleet's biggest ship class and
        the wind's sector; routes are searched afresh when it changes."""
        if not WIND:
            return None
        f = self.st["fleet"]
        return (max(t & 7 for t in f["ships"]), self.st["wind"] >> 4)

    def speed_at(self, heading):
        """The fleet's speed on a heading, as the game works it out."""
        cls, _ = self.wind_key()
        sector = ((heading + 8 - self.st["wind"]) >> 4) & 15
        d = max(self.st["player"]["difficulty"] - (self.st["player"]["ability"] == 2), 0)
        return min(max((self.speed_table[cls][sector] + ((5 - min(d, 4)) >> 1)) * 3, 4), 12)

    def wind_muls(self):
        """Time per step in the order of Search.steps: the step is sailed
        from the cell found to the one searched from, so against the offset."""
        if not WIND:
            return None
        out = []
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, 1), (1, -1), (-1, -1)):
            out.append(12.0 / self.speed_at(self.bearing(-dx, -dy)))
        return tuple(out)

    def search(self, goal):
        """A resumable Dijkstra from goal: cost of sailing from each cell to
        it (see Search.reach)."""
        reefs = {(y + 1) * (self.W + 2) + x + 1 for x, y in self.reefs}
        gp = (goal[1] + 1) * (self.W + 2) + goal[0] + 1
        return Search(self.base, self.W + 2, reefs, gp, self.FAR, self.wind_muls())

    def los(self, a, b):
        n = max(abs(b[0] - a[0]), abs(b[1] - a[1]), 1)
        for i in range(1, n + 1):
            x = round(a[0] + (b[0] - a[0]) * i / n)
            y = round(a[1] + (b[1] - a[1]) * i / n)
            if self.grid[y][x] != "." if 0 <= y < self.H and 0 <= x < self.W else True:
                return False	# ',' cells hide shallows: never cut across them
        return True

    def load_pixels(self, era):
        """The exact terrain a ship sails on, as the player sees it: the 256
        map tiles' pixels, and the tile each cell draws."""
        lines = subprocess.run(
            [os.path.join(BUILD, "flibustier-seamap"), "--data", DATA,
             "--pixels", str(era)], stdout=subprocess.PIPE, text=True,
            check=True).stdout.splitlines()
        self.tile_px = lines[:256]
        self.cell_tile = [[int(row[i:i + 2], 16) for i in range(0, len(row), 2)]
                          for row in lines[256:736]]
        self.speed_table = [[int(v) for v in row.split()] for row in lines[736:744]]
        # cells within two cells of anything but open sea: where to look
        self.reef_zone = set()
        for y in range(self.H):
            for x in range(self.W):
                if self.grid[y][x] != ".":
                    for dy in range(-2, 3):
                        for dx in range(-2, 3):
                            self.reef_zone.add((x + dx, y + dy))

    def sea_at(self, px, py):
        """Is the pixel open water (not shallows, not land)?"""
        if not (0 <= px < self.W * 16 and 0 <= py < self.H * 16):
            return False
        t = self.cell_tile[py >> 4][px >> 4]
        return self.tile_px[t][(py & 15) * 16 + (px & 15)] == "."

    def room(self, x, y):
        """Pixels of open water kept clear of shallows and land around a
        point, to the nearest of 16, 12, 8, 4, 2 and 0; -1 when the pixel
        itself is not open water."""
        for m in (16, 12, 8, 4, 2, 0):
            ok = True
            for dx, dy in ((0, 0), (m, 0), (-m, 0), (0, m), (0, -m),
                           (m, m), (m, -m), (-m, m), (-m, -m)):
                if not self.sea_at(x + dx, y + dy):
                    ok = False
                    break
            if ok:
                return m
        return -1

    def wrecked(self, r):
        """Did this sail step lose a ship on a reef?"""
        return r["stop"] == "crew" and self.sim.state()["crew_event"] == 6

    def wrecks_ahead(self, turn):
        """Would one step of 4 ticks on this turn lose a ship on a reef?"""
        before = self.sim.state()
        wreck = self.wrecked(self.sim("sail", turn=turn, ticks=4))
        self.sim("load", state=before)
        return wreck

    def rollout(self, turn, before, wrecks_only=False):
        """Sail REEF_HORIZON chunks of 4 ticks on one turn and report the
        least room from shallows met on the way (-1: ran onto it or land);
        with wrecks_only, -1 only when a reef takes a ship or the ship
        runs aground, the shallows being crossed otherwise (a reef waits
        100 passes between ships). The game state is put back afterwards."""
        least = 99
        for _ in range(REEF_HORIZON):
            r = self.sim("sail", turn=turn, ticks=4)
            # a wreck counts whatever the ship stands on afterwards: a lone
            # ship's loss puts the marooned player back on the shore
            if r["stop"] == "land" or self.wrecked(r):
                room = -1
            elif wrecks_only:
                room = 0
            else:
                room = self.room(r["x"], r["y"])
            least = min(least, room)
            if room < 0 or r["stop"] not in ("ticks", "town"):
                break
        self.sim("load", state=before)
        return least

    def guarded_sail(self, turn, ticks, f, margin=REEF_MARGIN):
        """One sail step that keeps clear of shallows: the turn the bot
        wants is run ahead (a rollout), and when it comes within
        "margin" pixels of shallows the other turns are too; the one
        keeping the most room is sailed for one chunk. A margin of 0
        (a narrow passage) refuses only the turns that wreck. Returns the sail
        reply, or None when the wanted turn is safe enough (the caller
        sails it as usual)."""
        before = self.sim.state()
        narrow = margin == 0
        want = self.rollout(turn, before, narrow)
        if want >= margin:
            return None
        best = (want, turn)
        for t in (0, -1, 1):
            if t == turn:
                continue
            room = self.rollout(t, before, narrow)
            if room > best[0]:
                best = (room, t)
        self.stats["reef_avoided"] += best[1] != turn
        self.stats["reef_unsafe"] += best[0] < 0
        return self.sim("sail", turn=best[1], ticks=4)

    def back_off(self, tries):
        """After a grounding (the ship turned about): sail clear, trying
        other headings when the way back is closed too."""
        rnd = random.Random(tries)
        for attempt in range(8):
            r = self.sim("sail", turn=0, ticks=24 + 8 * min(tries, 6))
            if r["stop"] == "ticks":
                return
            if r["stop"] != "land":
                return  # an encounter or the crew: the caller's job
            self.sim("shore", ashore=0)
            turn = rnd.choice((-1, 1))
            self.sim("sail", turn=turn, ticks=4 * rnd.randrange(1, 5))

    @staticmethod
    def bearing(dx, dy):
        return int(round(math.atan2(dx, -dy) * 128 / math.pi)) & 0xFF

    def sail_to(self, tx, ty, town=None, max_days=120):
        """Sail to cell (tx, ty). Returns the sail stop dict that ended it."""
        f = self.refresh()["fleet"]
        start_day = self.st["day"]
        if (tx, ty) in self.dead_ends:
            return {"stop": "noroute"}
        route = self.path((f["x"] // 16, f["y"] // 16), (tx, ty))
        if route is None and self.rescue_pond():
            f = self.refresh()["fleet"]
            route = self.path((f["x"] // 16, f["y"] // 16), (tx, ty))
        if route is None:
            self.odd("no sea route to %d,%d" % (tx, ty))
            self.dead_ends.add((tx, ty))
            return {"stop": "noroute"}
        idx = 0
        bumps = 0
        last_pos, stuck = None, 0
        while True:
            x, y = f["x"], f["y"]
            cx, cy = x // 16, y // 16
            if town is None and (abs(cx - tx) <= 1 and abs(cy - ty) <= 1 or
                                 # the route ends short when the target lies
                                 # beyond the open sea (a lagoon)
                                 max(abs(cx - route[-1][0]), abs(cy - route[-1][1])) <= 1):
                return {"stop": "arrived"}
            # drift can carry the ship past points it never came near: resync
            # on the nearest upcoming point (later one on ties)
            bd = None
            for k in range(idx, min(len(route), idx + 40)):
                d = max(abs(route[k][0] - cx), abs(route[k][1] - cy))
                if bd is None or d <= bd:
                    idx, bd = k, d
            # advance along the route while we are close to its points
            while idx < len(route) - 1 and max(abs(route[idx][0] - cx), abs(route[idx][1] - cy)) <= 2:
                idx += 1
            # look ahead for the farthest visible route point
            j = idx
            for k in range(idx, min(len(route), idx + 14)):
                if self.los((cx, cy), route[k]):
                    if WIND and self.speed_at(self.bearing(route[k][0] - cx, route[k][1] - cy)) < \
                            self.speed_at(self.bearing(route[idx][0] - cx, route[idx][1] - cy)):
                        continue	# a shortcut that heads into the wind
                    j = k
            wx, wy = route[j]
            want = self.bearing(wx * 16 + 8 - x, wy * 16 + 8 - y)
            want = (want + 8) & 0xF0
            diff = ((want - f["heading"] + 128) & 0xFF) - 128
            turn = 0 if abs(diff) < 8 else (1 if diff > 0 else -1)
            ticks = 4 if turn else 16
            r = None
            self.guard_mode = "outside"
            if REEF_GUARD and (cx, cy) in self.reef_zone:
                # where the route itself crosses shallows ahead (the only
                # way in, as into Lake Maracaibo) no turn keeps the margin:
                # then only running onto them is refused; a ship already on
                # the shallows keeps the margin, to get off them
                narrow = self.grid[cy][cx] != "," and any(
                    self.grid[py][px] == "," and 0 < max(abs(px - cx), abs(py - cy)) <= 3
                    for px, py in route[max(0, idx - 4):idx + 4])
                r = self.guarded_sail(turn, ticks, f, 0 if narrow else REEF_MARGIN)
                self.guard_mode = ("narrow_" if narrow else "") + ("steered" if r else "safe")
            if r is None:
                r = self.sim("sail", turn=turn, ticks=ticks)
            f.update(x=r["x"], y=r["y"], heading=r["heading"])
            stop = r["stop"]
            if stop != "ticks":
                self.st["day"] = r["day"]
                if stop == "land":
                    self.sim("shore", ashore=0)
                    self.back_off(bumps)
                    f = self.refresh()["fleet"]
                    route = self.path((f["x"] // 16, f["y"] // 16), (tx, ty))
                    idx = 0
                    self.stats["land_bumps"] += 1
                    self.bumps[(f["x"] // 16, f["y"] // 16, tx, ty)] += 1
                    bumps += 1
                    if town is None and bumps >= 8 and max(
                            abs(f["x"] // 16 - tx), abs(f["y"] // 16 - ty)) <= 10:
                        return {"stop": "arrived"}	# close enough: go on by land
                    if bumps > 30:
                        if self.comp.get((f["x"] // 16, f["y"] // 16)) is not None:
                            self.odd("kept running aground near %d,%d" % (f["x"] // 16, f["y"] // 16))
                        return {"stop": "stuck"}
                    if route is None:
                        self.odd("lost the route after a land stop")
                        return {"stop": "noroute"}
                    continue
                return r
            if (r["x"], r["y"]) == last_pos:
                stuck += 1
                if stuck > 50:
                    self.odd("ship stuck at %d,%d" % (cx, cy))
                    return {"stop": "stuck"}
            else:
                stuck = 0
            last_pos = (r["x"], r["y"])
            if r["day"] - start_day > max_days:
                self.odd("voyage to %d,%d took over %d days" % (tx, ty, max_days))
                return {"stop": "slow"}

    # ------------------------------------------------------------- swords
    def fight(self, policy=None):
        """Run the active sword fight to its end; returns its result."""
        rnd = random.Random(len(self.events))
        for n in range(20000):
            r = self.sim("sword", ticks=1, input=self.sword_input(rnd))
            sw = r["sword"]
            if sw["result"] != "none":
                self.stats["sword_" + sw["result"]] += 1
                return sw["result"]
        self.odd("sword fight never ended")
        return "none"

    def sword_input(self, rnd):
        # lunges in a random line, now and then a parry of the middle
        k = rnd.random()
        if k < 0.7:
            return 9 + rnd.randrange(3)
        if k < 0.85:
            return 6 + rnd.randrange(3)
        return rnd.randrange(3)

    # ------------------------------------------------------------- crew
    def crew_stop(self):
        ev = self.sim("crew")["event"]
        if ev == "mutiny":
            st = self.refresh()
            f = st["fleet"]
            self.note("mutiny! mood %d, months %d, crew %d, gold %d, %d days since a visit, %d ships" % (
                f["mood"], st["months"], f["crew"], f["gold"],
                st["day"] - max(self.visited.values(), default=st["day"]), len(f["ships"])))
            self.sim("crew", action="duel", weapon=2)
            res = self.fight()
            self.sim("crew", action="end")
            self.note("mutiny duel: %s" % res)
            self.stats["mutinies"] += 1
        elif ev != "none":
            self.stats["crew_" + ev] += 1
            if ev in ("hungry", "starving"):
                self.hungry = True
                f = self.refresh()["fleet"]
                self.note("%s: food %d, crew %d, gold %d" % (
                    ev, f["food"], f["crew"], f["gold"]))
            if ev == "wrecked":
                f = self.refresh()["fleet"]
                cx, cy = f["x"] // 16, f["y"] // 16
                self.note("wrecked on a reef at %d,%d (cell %s, %d ships, %s)" % (
                    cx, cy, self.grid[cy][cx], len(f["ships"]),
                    getattr(self, "guard_mode", "?")))
                self.stats["wreck_" + getattr(self, "guard_mode", "other")] += 1
                self.reefs.add((cx, cy))	# plan around it from now on
                self.wrecks += 1
                self.fields.clear()
            self.sim("crew", action="heard")
        self.check_fleet()

    def check_fleet(self):
        c = self.sim("crew", action="check")["check"]
        if c == "abandon":
            f = self.refresh()["fleet"]
            worst = max(range(len(f["ships"])), key=lambda i: f["damage"][i])
            self.sim("crew", action="abandon", ship=worst)
            self.note("abandoned a ship, too few men")
        elif c == "last":
            self.sim("crew", action="abandon_last")
            self.note("abandoned the last ship")

    # --------------------------------------------------------- sea battle
    def battle_score(self, i, fort):
        """What fleet ship i brings to the battle, from the battle rules:
        the men and guns she can carry, the hits she can take (a hit does
        guns x 4 / (class + 2), 64 sinks her) and her speed, which hull
        damage slows.  The weighting is a guess.  Before a fort, reaching
        the landing comes first (fort_margin), then her men, who are the
        assault party; her guns barely matter, as the bot runs for the
        landing."""
        f = self.st["fleet"]
        cls = f["ships"][i] & 7
        crew = max(1, min(f["crew"] + 8 - 8 * len(f["ships"]), (cls + 2) * 32))
        if fort:
            margin = fort_margin(self.st, self.st["fleet"]["leaving"],
                                 f["ships"][i], f["damage"][i])
            return (margin > 0, crew, margin)
        guns = max(1, min(f["guns"], crew // 4, (cls + 2) * 4))
        hull = (64 - f["damage"][i]) * (cls + 2)
        speed = max(1, self.st["ship_classes"][cls]["speed"] - f["damage"][i])
        return (True, crew * hull * speed * guns, 0)

    def choose_ship(self):
        """With several ships, command the best one for this battle."""
        s = self.refresh()
        f = s["fleet"]
        if len(f["ships"]) < 2:
            return
        fort = s["battle"]["fort"]
        best = max(range(len(f["ships"])), key=lambda i: self.battle_score(i, fort))
        if best != 0:
            self.sim("flagship", ship=best)
            self.note("commanding ship %d (%s) for the %s" % (
                best, f["ships"][best], "fort" if fort else "battle"))
        self.stats["flagship_changed"] += best != 0

    def beat(self, you, want, wind):
        """The heading that makes the most way toward bearing "want": a
        fort is often upwind, where a ship heading straight at it stops.
        Keep the current heading unless another one is clearly better,
        so as not to flip between tacks."""
        sectors = self.st["ship_classes"][you["type"] & 7]["sectors"]

        def way(h):
            v = sectors[((h + 8 - wind) >> 4) & 15]
            return v * math.cos((h - want) * math.pi / 128)
        best = max(range(0, 256, 8), key=way)
        return you["heading"] if way(you["heading"]) >= 0.8 * way(best) > 0 else best

    @staticmethod
    def turn_to(you, want):
        diff = ((want - you["heading"] + 128) & 0xFF) - 128
        return 0 if abs(diff) < 6 else (1 if diff > 0 else -1)

    def shot_lands(self, b, turn, sails):
        """Would a broadside fired now hit? Run it ahead in the sim (fire,
        then the shot's flight of d / 2 ticks) and put the battle back."""
        you, foe = b["you"], b["foe"]
        dx, dy = abs(foe["x"] - you["x"]) // 256, abs(foe["y"] - you["y"]) // 256
        flight = max(16, max(dx, dy) + min(dx, dy) // 4) // 2
        before = self.sim.state()
        r = self.sim("battle", ticks=flight + 2, turn=turn, fire=1, sails=sails)
        self.sim("load", state=before)
        return r["events"] & 4 != 0

    def tactics(self, b, fleeing, sails):
        """One step of a sea battle: the turn and whether to fire.
        - Closing: lead the enemy (aim where she will be when the bot
          gets there) and beat to windward when she lies upwind.
        - Fleeing: the best way off, away from her.
        - Guns: fire whenever a look-ahead says the shot hits; a hit cuts
          her crew and guns, slows her and turns her into the wind.
        - With GUNNERY 2, a fleeing enemy (her decision 2) is brought
          abeam when the guns are loaded, to stop her with a hit; with 3,
          also no shot that would sink her."""
        you, foe = b["you"], b["foe"]
        fx, fy = foe["x"] / 256, foe["y"] / 256
        yx, yy = you["x"] / 256, you["y"] / 256
        dist = math.hypot(fx - yx, fy - yy)
        bearing = self.bearing(fx - yx, fy - yy)
        if fleeing:
            want = self.beat(you, (bearing + 128) & 0xFF, b["wind"])
            return self.turn_to(you, want), 0
        # lead: her velocity in pixels a tick, over the time to reach her
        a = foe["heading"] * math.pi / 128
        vx, vy = math.sin(a) * foe["speed"] * 4 / 256, -math.cos(a) * foe["speed"] * 4 / 256
        mine = max(0.05, you["speed"] * 4 / 256)
        t = min(dist / mine, 400)
        want = self.beat(you, self.bearing(fx + vx * t - yx, fy + vy * t - yy), b["wind"])
        ready = you["reload"] == 0 and you["guns"] > 0 and you["shot"][0] == 0 and you["sail_timer"] == 0
        if ready and GUNNERY >= 2 and b["decision"] == 2 and 40 < dist < 220:
            # bring her abeam, on the side nearer the present heading
            sides = [(bearing + 64) & 0xFF, (bearing - 64) & 0xFF]
            want = min(sides, key=lambda h: abs(((h - you["heading"] + 128) & 0xFF) - 128))
        turn = self.turn_to(you, want)
        off = abs(abs(((bearing - you["heading"] + 128) & 0xFF) - 128) - 64)
        fire = 0
        # a prize is worth more than a wreck: spare her the hit that would
        # sink her (hull damage of a hit, halved under battle sails)
        hit = max(1, you["guns"] * 4 // ((foe["type"] & 7) + 2) // (1 if foe["full_sails"] else 2))
        spare = GUNNERY >= 3 and foe["damage"] + hit >= 64
        if ready and not spare and off < 24 and dist < 320 and self.shot_lands(b, turn, sails):
            fire = 1
            self.stats["broadsides"] += 1
        return turn, fire

    def battle(self):
        """Steer at the foe and board her (or land by the fort); returns
        the finish reply."""
        self.battle_day = self.st["day"]
        self.choose_ship()
        b = self.sim("battle", ticks=1)["battle"]
        n = 0
        fleeing = False
        while b["outcome"] == "none" and n < 20000:
            you, foe = b["you"], b["foe"]
            # run from a fight being lost (escape: over 240 pixels apart);
            # damage counts to 64 (16 a level, level 4 sinks her)
            if not b.get("fort") and (foe["guns"] >= 2 * you["guns"] or
                                      (you["damage"] >= FLEE_DAMAGE and foe["damage"] < you["damage"])):
                fleeing = True
            sails = -1 if b.get("fort") else 1
            if GUNNERY and not b.get("fort"):
                turn, fire = self.tactics(b, fleeing, sails)
            else:
                want = self.bearing(foe["x"] - you["x"], foe["y"] - you["y"])
                if fleeing:
                    want = (want + 128) & 0xFF
                if b.get("fort"):
                    want = self.beat(you, want, b["wind"])
                diff = ((want - you["heading"] + 128) & 0xFF) - 128
                turn = 0 if abs(diff) < 6 else (1 if diff > 0 else -1)
                dist = math.hypot(foe["x"] - you["x"], foe["y"] - you["y"]) / 256
                fire = 1 if not fleeing and (b.get("fort") or dist > 60) and you["reload"] == 0 and abs(diff) > 40 else 0
            # before a fort, battle sails halve its hits for a third
            # less speed: less damage on the way in
            b = self.sim("battle", ticks=4, turn=turn, fire=fire, sails=sails)["battle"]
            n += 1
        out = b["outcome"]
        self.stats["battle_" + out] += 1
        if out == "boarding":
            self.sim("board", weapon=2)
            res = self.fight()
            self.note("boarding fight: %s" % res)
        # a proof fleet leaves the ships a reef can take (bigger than a
        # sloop) to their owners, except when she is all there is
        keep = 1
        if SHIPS == "proof" and (b["foe"]["type"] & 7) > 1 and (self.st["fleet"]["ships"][0] & 7) <= 1:
            keep = 0
        r = self.sim("finish", keep=keep)
        if self.st["fleet"]["ships"] and r.get("fleet_ships", 0) > 8:
            pass
        return r

    def after_battle(self, fin):
        out = fin["outcome"]
        if out in ("surrender",):
            self.stats["captures"] += 1
            if fin.get("volunteers"):
                self.sim("crew", action="volunteers", accept=1)
            loot = fin.get("loot") or [0, 0, 0, 0]
            if any(loot):
                self.sim("merchant", action="loot")
                for good, n in zip(("food", "goods", "sugar"), loot):
                    if n:
                        self.sim("merchant", action="buy", good=good, count=n, check=False)
                if loot[3]:
                    self.sim("merchant", action="buy", good="cannon", count=loot[3], check=False)
                self.sim("merchant", action="close")
            if self.refresh()["prize_pending"]:
                f = self.st["fleet"]
                worst = max(range(len(f["ships"])), key=lambda i: f["damage"][i])
                self.sim("prize", slot=worst)
            if self.st["hostage"]:
                # ask where the silver train is
                self.sim("captain", action="hostage", answer=1)
                self.note("a captured captain talks about the silver train")
            vn = fin.get("villain_news")
            if vn:
                self.note("villain news at town %d" % vn["town"])
        elif out in ("defeated", "player_sunk"):
            self.note("lost the battle: %s" % out)
        self.check_fleet()
        if out in ("defeated", "player_sunk"):
            if self.refresh()["day"] - self.battle_day > 20:
                self.prison()

    def prison(self):
        cause = ["in a town", "after losing at sea", "marooned", "at a fort"][self.st["jail"].get("cause", 0) & 3]
        self.note("imprisoned (%s), health %d" % (cause, self.st["jail"].get("health", 0)))
        self.stats["prison"] += 1

    def encounter(self):
        """Handle a sighting: attack Spaniards and pirates, else sail on."""
        e = self.sim("encounter", action="investigate")
        enc = e["encounter"]
        fin = None
        for _ in range(6):
            if e["result"] in ("battle", "overtaken", "unanswered"):
                fin = self.battle()
                break
            if enc["stage"] == "none":
                break
            s = self.refresh()
            nation = enc["nation"]
            enemy = nation == 4 or (nation < 4 and s["relations"][s["player"]["nation"]][nation] < 0)
            f = s["fleet"]
            # never pick a fight with a much bigger or better armed ship
            outclassed = (enc["type"] > max(t & 7 for t in f["ships"]) + 1 or
                          enc["guns"] * 2 > f["guns"] * 3)
            strong = f["crew"] > max(25, enc["crew"] * 3 // 2) and not outclassed
            act = "battle" if enemy and strong and enc["stage"] == "colours" else (
                "investigate" if enc["stage"] != "colours" else "ignore")
            if enemy and strong and enc["stage"] == "identified":
                act = "battle"
            e = self.sim("encounter", action=act)
            enc = e["encounter"]
        if fin:
            self.note("fought a %s ship (type %d): %s" % (
                (NATIONS + ["pirate"])[enc["nation"]], enc["type"], fin["outcome"]))
            self.after_battle(fin)

    # ------------------------------------------------------------- towns
    def pick_nation(self):
        """The nation a conquered town goes to: the one whose rank is lowest
        of the bot's own and those it builds a rank with (never Spain)."""
        st = self.refresh()
        nations = [self.mynation()] + RANK_NATIONS
        return min(nations, key=lambda n: (st["rank"][n], -st["standing"][n]))

    def promotable(self):
        """The nations whose governor would promote the bot now."""
        st = self.st
        return [n for n in [self.mynation()] + RANK_NATIONS
                if st["rank"][n] < 9 and st["standing"][n] >= (st["rank"][n] + 1) ** 2 + 2]

    def mynation(self):
        return self.st["player"]["nation"]

    def at_war(self, nation):
        return nation < 4 and self.st["relations"][self.mynation()][nation] < 0

    def town_index(self, name):
        return next(i for i, t in enumerate(self.st["towns"]) if t["name"] == name)

    def visit(self, ti):
        """Enter town ti (reached) and do the rounds."""
        t = self.st["towns"][ti]
        day0 = self.st["day"]
        r = self.sim("town", action="enter" if not self.at_war(t["nation"]) else "sneak")
        arr = r.get("arrival")
        if arr in ("fired", "sunk", "caught"):
            self.note("arrival at %s: %s" % (t["name"], arr))
            self.stats["arrival_" + arr] += 1
        if arr == "caught":
            self.sim("town", action="guard", weapon=2)
            self.fight()
            self.sim("town", action="duel_end")
        s = self.refresh()
        if s["player_town"] < 0:
            if s["day"] - day0 > 20:
                self.prison()
            return
        self.stats["visits"] += 1
        self.visited[ti] = s["day"]
        self.hungry = False
        self.food_town = None
        if self.governor(ti):
            self.back()
        ms = self.refresh()["mission"]
        if ms["type"] in (1, 2) and ms["dest"] == ti:
            d = self.sim("mission", action="deliver", check=False)
            if d.get("ok"):
                self.note("delivered the mission to %s" % t["name"])
                self.stats["missions_delivered"] += 1
        if (DIVIDE_ANY and not self.at_war(t["nation"]) or t["nation"] == self.mynation()) and (s["months"] >= DIVIDE_MONTHS or s["fleet"]["mood"] < 2):
            d = self.sim("bank", action="divide")
            self.note("divided the plunder at %s: share %d, mine %d, fortune %d" % (
                t["name"], d["share"], d["yours"], d["fortune"]))
            self.stats["divides"] += 1
            adv = 0
            if ADVANCE == 1:
                adv = 1
            elif ADVANCE == 2:
                m = self.refresh()["maps"]
                adv = int(bool(m[1]["pieces"] & 0xF or m[2]["pieces"] & 0xF))
            self.sim("bank", action="continue", advance=adv)
            self.back()
        self.tavern()
        self.back()
        self.merchant()
        self.back()
        self.shipwright()
        self.back()
        s = self.refresh()
        if s["ransom_slot"] < 0 and any(s["prisoners"]):
            self.sim("captain", action="offer")
            if self.refresh()["ransom_slot"] >= 0:
                g = self.sim("captain", action="take")
                self.note("ransom taken, gold %d" % g["gold"])
                self.stats["ransoms"] += 1
        r = self.sim("town", action="leave")
        if r.get("deserted"):
            self.stats["deserted"] += r["deserted"]

    def back(self):
        """Out of a building, back to the town's streets: three days."""
        self.sim("town", action="back", check=False)

    def governor(self, ti):
        """Visit the governor; False when he was not seen."""
        s = self.st
        t = s["towns"][ti]
        if t["citizens"] < 6 or self.at_war(t["nation"]) or t["nation"] == 4:
            return False
        g = self.sim("governor", action="visit", check=False)
        if not g.get("ok"):
            return False
        if g["promoted"]:
            self.note("promoted by the %s governor of %s (+%d acres)%s, standing %s" % (
                NATIONS[t["nation"]], t["name"], g["acres"],
                ", a new villain appears" if g["villain"] >= 0 else "",
                self.refresh()["standing"]))
            self.stats["promotions"] += 1
        if g["report"]:
            self.note("mission report: %d" % g["report"])
        if g["villain_here"] >= 0:
            self.villain_duel("governor's tip at " + t["name"])
        if g["mission_offer"] in MISSIONS and self.refresh()["mission"]["type"] == 0:
            m = self.sim("mission", action="accept", check=False)
            if m.get("ok"):
                self.note("took a mission (type %d) from %s to %s" % (
                    m["type"], t["name"], self.st["towns"][m["dest"]]["name"]))
                self.stats["missions_taken"] += 1
                self.mission_tries = 0
        if g["offer"] in ("marque", "pardon") and 0 < g["price"] <= s["fleet"]["gold"] // 3:
            p = self.sim("governor", action="pay")
            if p["paid"]:
                self.note("bought a %s from %s for %d" % (g["offer"], t["name"], g["price"]))
        if g["offer"] == "daughter" and not self.refresh()["married"]:
            self.daughter(t)
        return True

    def daughter(self, t):
        m = self.sim("daughter", action="meet")
        if m["result"] == 0:
            return
        if m["result"] == 2:
            return  # already the wife
        if not m["niece"] and self.refresh()["year"] < self.start_year + FALLBACK_YEARS:
            return  # holding out for a governor's niece
        self.stats["daughter_meetings"] += 1
        self.sim("daughter", action="talk")
        p = self.sim("daughter", action="propose")
        if p["result"] == 144:
            self.note("proposed to the governor of %s's %s; her suitor challenges" % (
                t["name"], "niece" if p["niece"] else "daughter"))
            self.sim("daughter", action="duel", weapon=0)
            res = self.fight()
            e = self.sim("daughter", action="end")
            self.note("duel for her hand: %s, married=%d" % (res, e["married"]))
            if e["married"]:
                self.married_town = t["name"]
        else:
            self.stats["proposal_refused"] += 1

    def villain_duel(self, why):
        d = self.sim("villain", action="duel", weapon=0, check=False)
        if not d.get("ok"):
            return
        res = self.fight()
        e = self.sim("villain", action="end")
        self.note("duel with villain %d (%s): %s" % (d["villain"], why, "won" if e["won"] else res))
        self.stats["villain_duels"] += 1
        if e["won"]:
            self.stats["villains_beaten"] += 1

    def crew_goal(self):
        """The men to sail with: enough to board ships, or what the cheapest
        city worth attacking asks, whichever is more (at most CREW_MAX)."""
        if not CREW_HUNT:
            return CREW_MAX
        s = self.st
        goal = CREW_HUNT
        need = []
        for tt in (s["train_town"], s["fleet_town"]):
            if 0 <= tt < len(s["towns"]) and s["towns"][tt]["sea_x"] and s["towns"][tt]["nation"] == 0:
                need.append(2 * s["towns"][tt]["soldiers"] + 30)
        if CONQUER:
            for i, t in enumerate(s["towns"]):
                if t["nation"] == 0 and t["sea_x"] and i not in self.unreachable and t["forts"] <= CONQUER_FORTS:
                    need.append(max(2 * t["soldiers"] + 30, t["citizens"] * CONQUER_MEN))
        need = [n for n in need if n <= CREW_MAX]
        if need:
            goal = max(goal, min(need))
        return goal

    def tavern(self):
        r = self.sim("tavern", action="open")
        s = self.refresh()
        f = s["fleet"]
        room = (len(f["ships"]) * 2 + f["capacity"]) * 16 - f["crew"]
        # the crew the gold keeps content (mood >= 2): sqrt(gold/10/crew) x 2
        # - months + 4 - difficulty >= 2
        need = s["months"] + RECRUIT_AHEAD + s["player"]["difficulty"] - 2
        cap = room if need <= 0 else int((f["gold"] / 10) / ((need / 2) ** 2)) - f["crew"]
        if r["recruits"] > 0 and room > 0 and min(room, cap) >= r["recruits"] // 2 and f["crew"] < self.crew_goal():
            r = self.sim("tavern", action="recruit", accept=1)
            self.stats["recruited"] += 1
        # the map seller, while the pirate's map is not whole
        if r["tavern_spots"] & 1 and (s["maps"][0]["pieces"] & 0xF) != 0xF and f["gold"] > 500:
            if self.sim("tavern", action="buy_map", check=False).get("bought"):
                self.note("bought a piece of a pirate treasure map")
                self.stats["maps_bought"] += 1
        # a villain hiding here: the mystery man
        for k, v in enumerate(s["villains"]):
            if v["active"] and v["town"] == s["player_town"]:
                self.villain_duel("tavern")

    def merchant(self):
        m = self.sim("merchant", action="open", check=False)
        if not m.get("ok"):
            return
        f = self.refresh()["fleet"]
        for good in ("goods", "sugar"):
            if f[good]:
                self.sim("merchant", action="sell", good=good, count=f[good], check=False)
        f = self.refresh()["fleet"]
        want = f["crew"] // 2 + 20 - f["food"]
        while want > 0:
            r = self.sim("merchant", action="buy", good="food", count=want, check=False)
            if r.get("ok") and r.get("traded", 1):
                break
            want //= 2
        g = self.refresh()["fleet"]
        self.note("food %d -> %d for %d crew (wanted %d, gold %d, room %s, stock %s)" % (
            f["food"], g["food"], g["crew"], f["crew"] // 2 + 20 - f["food"],
            g["gold"], m.get("hold_space"), m.get("stock")))
        self.sim("merchant", action="close", traded=0, check=False)

    def rating(self, ship):
        """What a ship is worth to a pirate: her fighting power (guns and
        hull) times her speed, from the sim's per-class figures:
        (guns + 4 x hull) x speed, per class (era 1660):

            class  name                    rating
              5    Frigate / fast galleon    3024
              7    Galleon                   2808
              6    War galleon               2752
              4    Merchantman               2160
              2    Barque                    2080
              1    Sloop                     1944
              3    Cargo fluyt               1840
              0    Pinnace                   1328

        The weighting is a guess, not tuned against results."""
        c = self.st["ship_classes"][ship & 7]
        return (c["guns"] + 4 * c["hull"]) * c["speed"]

    def keep_value(self, ship):
        """How much the bot wants to keep a ship: the lowest is sold first."""
        if SHIPS == "big":
            return ship & 7
        if SHIPS in ("small", "proof"):
            return -(ship & 7)
        return self.rating(ship)

    def shipwright(self):
        p = self.sim("shipwright", action="prices", check=False)
        if not p.get("ok"):
            return
        f = self.refresh()["fleet"]
        if SHIPS == "flag" and len(f["ships"]) > 1:
            # the best ship takes the lead: the ships ahead of her go, then
            # the extras beyond the limit, the weakest first
            rate = [self.rating(t) for t in f["ships"]]
            best = max(range(len(rate)), key=lambda i: (rate[i], -i))
            rest = sorted(range(best + 1, len(rate)), key=lambda i: rate[i])
            idx = list(range(best)) + rest[: max(len(rest) - (MAX_SHIPS - 1), 0)]
            if idx:
                self.sim("shipwright", action="sell", ships=idx, check=False)
                self.stats["ships_sold"] += len(idx)
                if best:
                    self.stats["flagship_changes"] += 1
            f = self.refresh()["fleet"]
        if len(f["ships"]) > MAX_SHIPS:
            # sell the worst extras
            idx = sorted(range(1, len(f["ships"])), key=lambda i: self.keep_value(f["ships"][i]))[: len(f["ships"]) - MAX_SHIPS]
            r = self.sim("shipwright", action="sell", ships=idx, check=False)
            self.stats["ships_sold"] += len(idx)
        f = self.refresh()["fleet"]
        dmg = [i for i, d in enumerate(f["damage"]) if d > 0]
        if dmg and f["gold"] > 1000:
            self.sim("shipwright", action="repair", ships=dmg, check=False)
            if sum(self.refresh()["fleet"]["damage"]):
                self.no_repair_until = self.st["day"] + 60

    # ------------------------------------------------------------ voyages
    def go(self, tx, ty, town=None, max_days=150):
        """Sail to (tx, ty), dealing with what happens on the way.
        Returns 'arrived', 'town' (entered ti in self.last_town) or a failure."""
        t0 = self.refresh()["day"]
        f = self.st["fleet"]
        dest = self.st["towns"][town]["name"] if isinstance(town, int) else "%d,%d" % (tx, ty)
        self.note("sailing from %d,%d to %s (food %d, crew %d, gold %d)" % (
            f["x"] // 16, f["y"] // 16, dest, f["food"], f["crew"], f["gold"]))
        wrecks0 = self.wrecks
        while self.refresh()["day"] - t0 < max_days:
            if town is None and self.wrecks - wrecks0 >= 3:
                self.odd("voyage to %s wrecked three times" % dest)
                return "slow"
            r = self.sail_to(tx, ty, town=town, max_days=max_days)
            stop = r["stop"]
            if stop == "stuck" and self.ship_on_land():
                continue
            if stop == "encounter":
                self.encounter()
                continue
            if stop == "crew":
                self.crew_stop()
                if self.hungry and self.food_town is None and self.refresh()["fleet"]["gold"] >= 100:
                    return "hungry"	# the main loop resupplies
                continue
            if stop == "town":
                t = r["town"]
                self.last_town = t if isinstance(t, int) else self.town_index(t)
                return "town"
            return stop
        return "slow"

    def ship_on_land(self):
        """Test-mode rescue: a ship set down on land (the original's
        placement after prison steps inland on south coasts) is moved to
        the nearest water.  Returns True when it did so."""
        s = self.refresh()
        f = s["fleet"]
        cx, cy = f["x"] // 16, f["y"] // 16
        if self.comp.get((cx, cy)) is not None or f["ashore"]:
            return False
        near = [(abs(dx) + abs(dy), (cx + dx, cy + dy)) for dy in range(-3, 4)
                for dx in range(-3, 4) if (cx + dx, cy + dy) in self.comp]
        if not near:
            return False
        nx, ny = min(near)[1]
        self.note("ship set on land at %d,%d (heading %d): rescued to %d,%d" % (
            cx, cy, f["heading"], nx, ny))
        self.stats["rescued_from_land"] += 1
        f["x"], f["y"], f["fx"], f["fy"] = nx * 16 + 8, ny * 16 + 8, 4, 4
        self.sim("load", state=s)
        return True

    def rescue_pond(self):
        """Test-mode rescue: the marooned ship can be set down in a pond with
        no way out (the original's nearest-shore rule does not check).  Moves
        it to the nearest cell of the main sea.  Returns True when it did."""
        s = self.refresh()
        f = s["fleet"]
        cx, cy = f["x"] // 16, f["y"] // 16
        if self.main_sea is None:
            self.main_sea = collections.Counter(self.comp.values()).most_common(1)[0][0]
        if self.comp.get((cx, cy)) == self.main_sea:
            return False
        best = min(((x - cx) ** 2 + (y - cy) ** 2, (x, y)) for (x, y), c in self.comp.items()
                   if c == self.main_sea)[1]
        self.odd("ship trapped in a pond at %d,%d: rescued to %d,%d" % (cx, cy, best[0], best[1]))
        f["x"], f["y"], f["fx"], f["fy"] = best[0] * 16 + 8, best[1] * 16 + 8, 4, 4
        self.sim("load", state=s)
        self.unreachable.clear()
        self.dead_ends.clear()
        return True

    def go_town(self, ti, attack=False):
        if ti is None:
            self.unreachable.clear()
            self.rescue_pond()
            return "noroute"
        t = self.refresh()["towns"][ti]
        if ti in self.unreachable:
            return "noroute"
        if not attack and self.visited.get(ti, -99) > self.st["day"] - 7:
            return "recent"
        if self.st["fleet"]["leaving"] == ti and self.st["player_town"] < 0 \
                and self.st["day"] != getattr(self, "reentered", None):
            # still by the town just left: back in without sailing
            self.reentered = self.st["day"]
            self.last_town = ti
            if attack:
                return self.assault(ti)
            self.visit(ti)
            return "visited"
        res = self.go(t["sea_x"], t["sea_y"], town=ti)
        if res == "noroute":
            self.unreachable.add(ti)
        if res != "town":
            return res
        if self.last_town != ti:
            # reached another town on the way: visit it if friendly, go on
            other = self.last_town
            if not self.at_war(self.st["towns"][other]["nation"]) and \
                    self.visited.get(other, -99) <= self.st["day"] - 7:
                self.visit(other)
            self.detours = getattr(self, "detours", 0) + 1
            if self.detours > 3:
                self.detours = 0
                return "detoured"
            r = self.go_town(ti, attack)
            self.detours = 0
            return r
        if attack:
            return self.assault(ti)
        self.visit(ti)
        return "visited"

    def nearest_town(self, pred):
        s = self.refresh()
        f = s["fleet"]
        best = None
        for i, t in enumerate(s["towns"]):
            if not t["sea_x"] or i in self.unreachable or not pred(t):
                continue
            d = abs(t["sea_x"] - f["x"] // 16) + abs(t["sea_y"] - f["y"] // 16)
            if best is None or d < best[0]:
                best = (d, i)
        return best[1] if best else None

    # ------------------------------------------------------------ assault
    def assault(self, ti):
        s = self.refresh()
        t = s["towns"][ti]
        self.note("attacking %s (%d soldiers, %d forts, our crew %d)%s" % (
            t["name"], t["soldiers"], t["forts"], s["fleet"]["crew"],
            " with the silver train there" if s["train_town"] == ti else ""))
        train = s["train_town"] == ti
        day0 = s["day"]
        fleet_there = s["fleet_town"] == ti
        aboard = 0
        fort = t["forts"] and not s["fleet"]["ashore"]
        if fort:
            f = s["fleet"]
            margin = max(fort_margin(s, ti, f["ships"][i], f["damage"][i])
                         for i in range(len(f["ships"])))
            if margin < FORT_MARGIN or ti in self.forts_lost:
                # the fort would sink us before the landing: not today
                self.note("the fort at %s is too strong %s (margin %d)" % (
                    t["name"], "for us" if ti in self.forts_lost else "in this wind", margin))
                self.stats["fort_avoided"] += 1
                return "fortified"
        self.sim("town", action="attack")
        if fort:
            # from the sea the fort fights first; only a landing goes on
            self.sim("town", action="fort")
            fin = self.battle()
            self.note("fort battle at %s: %s" % (t["name"], fin["outcome"]))
            if fin["outcome"] != "landed":
                # the estimate missed (the coast, most likely): not again
                self.forts_lost.add(ti)
                self.after_battle(fin)
                return "repulsed"
            aboard = fin["aboard"]
        self.sim("assault", action="start", weapon=2, aboard=aboard)
        res = self.fight()
        e = self.sim("assault", action="end")
        self.note("assault on %s: %s (result %d), crew %d" % (t["name"], res, e["result"], e["crew"]))
        self.stats["assault_%d" % e["result"]] += 1
        if e["result"] == 1:
            k = self.sim("assault", action="sack")
            self.note("sacked %s: %d gold, goods %s, cannon %d%s" % (
                t["name"], k["gold"], k["goods"], k["cannon"],
                ", conquest possible" if k["conquest"] else ""))
            after = self.refresh()
            if train and after["train_town"] != ti:
                self.note("CAPTURED THE SILVER TRAIN at %s" % t["name"])
                self.stats["silver_train"] += 1
            if fleet_there and after["fleet_town"] != ti:
                self.note("CAPTURED THE TREASURE FLEET at %s" % t["name"])
                self.stats["treasure_fleet"] += 1
            if k["conquest"]:
                gov = self.pick_nation()
                self.sim("assault", action="governor", nation=gov)
                self.note("%s is now %s" % (t["name"], NATIONS[gov]))
                self.stats["conquests"] += 1
            if self.refresh()["player_town"] >= 0:
                self.sim("town", action="leave")
        elif self.refresh()["day"] - day0 > 20:
            self.prison()
        self.check_fleet()
        return "assaulted"

    # ------------------------------------------------------------- search
    def reboarded(self, mapi):
        """The party stepped onto water near its landing point, as the
        original lets it on a winding coast: try landing again, and only
        flag a map that keeps failing."""
        self.stats["dig_reboarded"] += 1
        self.bumps["dig%d" % mapi] += 1
        if self.bumps["dig%d" % mapi] == 6:
            self.odd("the party kept walking back aboard on its way to map %d" % mapi)
        else:
            self.note("the party walked back aboard on its way to map %d" % mapi)

    def land_path(self, a, b):
        """Cells to walk from a to b over land and shallows, 8-connected without
        cutting corners; the ends may be anything.  None when shut in."""
        ok = lambda c: c in (a, b) or (0 <= c[0] < self.W and 0 <= c[1] < self.H
                                       and self.grid[c[1]][c[0]] in "#,")
        prev = {a: None}
        q = collections.deque([a])
        while q:
            cur = q.popleft()
            if cur == b:
                out = []
                while cur is not None:
                    out.append(cur)
                    cur = prev[cur]
                return out[::-1]
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1), (-1, 1), (-1, -1)):
                n = (cur[0] + dx, cur[1] + dy)
                if n in prev or not ok(n):
                    continue
                if dx and dy and not (ok((cur[0] + dx, cur[1])) and ok((cur[0], cur[1] + dy))):
                    continue
                prev[n] = cur
                q.append(n)
        return None

    def retrace(self, crumbs):
        """Walk back along the positions the party marched through.
        Returns True once back aboard."""
        for px, py in reversed(crumbs):
            for _ in range(40):
                g = self.refresh()["fleet"]
                if not g["ashore"]:
                    return True
                if abs(g["x"] - px) <= 3 and abs(g["y"] - py) <= 3:
                    break
                r = self.sim("march", dx=(px > g["x"]) - (px < g["x"]), dy=(py > g["y"]) - (py < g["y"]), ticks=2)
                if r["stop"] == "boarded":
                    return True
                if r["stop"] == "crew":
                    self.crew_stop()
        return not self.refresh()["fleet"]["ashore"]

    def walk_round(self, f):
        """The party, cut off from the landing point, walks to it by the
        land path.  Returns True once back aboard."""
        path = self.land_path((f["x"] // 16, f["y"] // 16), (f["land_x"] // 16, f["land_y"] // 16))
        if not path:
            return False
        for cx, cy in path[1:]:
            for _ in range(60):
                g = self.refresh()["fleet"]
                if not g["ashore"]:
                    return True
                tx, ty = cx * 16 + 8, cy * 16 + 8
                if abs(g["x"] - tx) < 6 and abs(g["y"] - ty) < 6:
                    break
                r = self.sim("march", dx=(tx > g["x"]) - (tx < g["x"]), dy=(ty > g["y"]) - (ty < g["y"]), ticks=4)
                if r["stop"] == "boarded":
                    return True
                if r["stop"] == "crew":
                    self.crew_stop()
        return not self.refresh()["fleet"]["ashore"]

    def dig(self, mapi):
        s = self.refresh()
        m = s["maps"][mapi]
        tx, ty = m["x"], m["y"]
        self.note("going to dig map %d near %d,%d (pieces %d)" % (mapi, tx, ty, m["pieces"]))
        # nearest open water to the spot on the fleet's own sea: a cell in
        # the shallows, or in a lagoon behind a reef, wrecks the fleet on
        # its way in; a landing that wrecked it anyway is not tried again
        f = s["fleet"]
        sea = self.deep.get((f["x"] // 16, f["y"] // 16))
        best = None
        for open_sea in (True, False):
            for r in range(1, 40):
                for dy in range(-r, r + 1):
                    for dx in range(-r, r + 1):
                        c = (tx + dx, ty + dy)
                        if max(abs(dx), abs(dy)) != r or not self.water(*c) or c in self.bad_landings:
                            continue
                        if open_sea and (c not in self.deep or sea is not None and self.deep[c] != sea):
                            continue
                        best = c
                        break
                    if best:
                        break
                if best:
                    break
            if best:
                break
        if best is None:
            return False
        wrecks = self.wrecks
        res = self.go(best[0], best[1])
        if res != "arrived":
            if self.wrecks > wrecks:
                self.bad_landings.add(best)
            return False
        # steer into the land toward the spot, or its nearest land when the
        # spot itself lies in the shallows
        lx, ly = tx, ty
        for r in range(0, 40):
            hits = [(tx + dx, ty + dy) for dy in range(-r, r + 1) for dx in range(-r, r + 1)
                    if max(abs(dx), abs(dy)) == r and 0 <= tx + dx < self.W
                    and 0 <= ty + dy < self.H and self.grid[ty + dy][tx + dx] == "#"]
            if hits:
                lx, ly = min(hits, key=lambda c: (c[0] - tx) ** 2 + (c[1] - ty) ** 2)
                break
        for _ in range(400):
            f = self.refresh()["fleet"]
            want = (self.bearing(lx * 16 + 8 - f["x"], ly * 16 + 8 - f["y"]) + 8) & 0xF0
            diff = ((want - f["heading"] + 128) & 0xFF) - 128
            turn = 0 if abs(diff) < 8 else (1 if diff > 0 else -1)
            # the run in to the beach crosses the shallows: no step that
            # loses a ship (a lone one strands the captain)
            if self.wrecks_ahead(turn):
                turn = next((t for t in (0, -1, 1) if t != turn and not self.wrecks_ahead(t)), None)
                if turn is None:
                    self.note("no way ashore near map %d without striking a reef" % mapi)
                    self.stats["dig_reefed"] += 1
                    return False
            r = self.sim("sail", turn=turn, ticks=4)
            if r["stop"] == "land":
                self.sim("shore", ashore=1)
                break
            if r["stop"] == "encounter":
                self.encounter()
            elif r["stop"] == "crew":
                self.crew_stop()
        else:
            self.odd("could not land near map %d spot" % mapi)
            return False
        # march to the spot, leaving breadcrumbs to walk back by
        crumbs = []
        for _ in range(3000):
            f = self.refresh()["fleet"]
            cx, cy = f["x"] // 16, f["y"] // 16
            if f["ashore"] and (not crumbs or abs(crumbs[-1][0] - f["x"]) + abs(crumbs[-1][1] - f["y"]) >= 6):
                crumbs.append((f["x"], f["y"]))
            if abs(cx - tx) < 2 and abs(cy - ty) < 2:
                break
            if not f["ashore"]:
                self.reboarded(mapi)
                return False
            dx = (tx > cx) - (tx < cx)
            dy = (ty > cy) - (ty < cy)
            r = self.sim("march", dx=dx, dy=dy, ticks=8)
            if r["stop"] == "boarded":
                self.reboarded(mapi)
                return False
            if r["stop"] == "town":
                self.visit(self.town_index(r["town"]) if isinstance(r["town"], str) else r["town"])
                self.odd("marched into a town while digging")
                return False
            if r["stop"] == "crew":
                self.crew_stop()
        found = self.sim("search")["found"]
        self.note("dug: %s" % json.dumps(found))
        for item in found:
            self.stats["dug_" + str(item["map"])] += 1
        # back aboard
        wander = random.Random(tx * 1000 + ty)
        still, last, stalls = 0, None, 0
        for _ in range(4000):
            f = self.refresh()["fleet"]
            if not f["ashore"]:
                break
            still = still + 1 if (f["x"], f["y"]) == last else 0
            last = (f["x"], f["y"])
            if still >= 3:
                # walled in on the way: wander off in a random direction
                stalls += 1
                if stalls <= 3:
                    ddx, ddy = wander.choice(((1, 0), (-1, 0), (0, 1), (0, -1),
                                              (1, 1), (1, -1), (-1, 1), (-1, -1)))
                    for _ in range(wander.randrange(2, 12)):
                        r = self.sim("march", dx=ddx, dy=ddy, ticks=8)
                        if r["stop"] != "ticks":
                            break
                # still shut in: walk back the way the party came
                elif stalls <= 6 and self.retrace(crumbs):
                    break
                # or round an inlet over the land
                elif stalls <= 9 and self.walk_round(f):
                    break
                else:
                    # last resort, feel along the shore: a biased random walk
                    for _ in range(120):
                        if wander.random() < 0.5:
                            ddx = (f["land_x"] > f["x"]) - (f["land_x"] < f["x"])
                            ddy = (f["land_y"] > f["y"]) - (f["land_y"] < f["y"])
                        else:
                            ddx, ddy = wander.choice(((1, 0), (-1, 0), (0, 1), (0, -1),
                                                      (1, 1), (1, -1), (-1, 1), (-1, -1)))
                        r = self.sim("march", dx=ddx, dy=ddy, ticks=4)
                        if r["stop"] != "ticks":
                            break
                        f = dict(f, x=r["x"], y=r["y"])
                if r["stop"] == "boarded":
                    break
                still = 0
                continue
            lx, ly = f["land_x"], f["land_y"]
            dx = (lx > f["x"] + 1) - (lx < f["x"] - 1)
            dy = (ly > f["y"] + 1) - (ly < f["y"] - 1)
            if dx == 0 and dy == 0:
                # on the spot: step toward the water beside it
                dx, dy = ((f["ship_x"] > lx) - (f["ship_x"] < lx),
                          (f["ship_y"] > ly) - (f["ship_y"] < ly))
            near = abs(lx - f["x"]) + abs(ly - f["y"]) < 24
            if abs(lx - f["x"]) < 8 and abs(ly - f["y"]) < 8:
                # by the landing point: the water is one of the eight ways
                for ddx, ddy in ((dx, dy), (1, 0), (-1, 0), (0, 1), (0, -1),
                                 (1, 1), (1, -1), (-1, 1), (-1, -1)):
                    r = self.sim("march", dx=ddx, dy=ddy, ticks=2)
                    if r["stop"] == "boarded":
                        break
                    self.sim("march", dx=-ddx, dy=-ddy, ticks=2)
                if r["stop"] == "boarded":
                    break
            r = self.sim("march", dx=dx, dy=dy, ticks=1 if near else 8)
            if r["stop"] == "boarded":
                break
            if r["stop"] == "crew":
                self.crew_stop()
            if r["stop"] == "town":
                self.visit(self.town_index(r["town"]) if isinstance(r["town"], str) else r["town"])
        else:
            self.odd("could not get back aboard")
        return bool(found)

    # ------------------------------------------------------------- career
    def friendly(self, t):
        return t["nation"] < 4 and not self.at_war(t["nation"]) and t["citizens"] >= 6

    def career(self, years=12):
        self.hungry = False
        self.married_town = None
        self.last_town = None
        seen_jail = 0
        start = self.start_year = self.refresh()["year"]
        self.note("career starts: %s, %s, %d gold, crew %d" % (
            self.scenario, NATIONS[self.mynation()], self.st["fleet"]["gold"], self.st["fleet"]["crew"]))
        home = self.st["fleet"]["leaving"]
        loops = 0
        while self.refresh()["year"] < start + years and loops < 6000:
            loops += 1
            s = self.st
            j = s["jail"]
            if j.get("fresh") and j.get("end"):
                if True:
                    self.note("career ends in prison (end %d)" % j["end"])
                    break
            if not s["fleet"]["ships"]:
                self.note("no ship left")
                break
            f = s["fleet"]
            # 1. food and repairs, before any long trip
            hurt = sum(f["damage"]) > 0 and f["gold"] > 1500 and s["day"] >= self.no_repair_until
            if (self.hungry or f["food"] < f["crew"] // FOOD_DIV + 5 or hurt) and f["gold"] >= 100:
                ti = self.food_town
                if ti is None:
                    ti = self.nearest_town(lambda t: self.friendly(t) and self.visited.get(
                        self.st["towns"].index(t), -99) <= self.st["day"] - 7)
                    self.food_town = ti	# keep to it until reached
                if ti is not None:
                    res = self.go_town(ti)
                    if res in ("recent", "noroute", "slow"):
                        if res != "recent":
                            self.unreachable.add(ti)
                        self.food_town = None
                    if res != "recent":
                        continue
            # 2. the lost relative's map, then the lost treasure
            for mapi in (1, 2, 0):
                if s["maps"][mapi]["pieces"] & 0xF and self.dig_fails[mapi] < 3:
                    if not self.dig(mapi):
                        self.dig_fails[mapi] += 1
                    break
            else:
                mapi = None
            if mapi is not None:
                continue
            # 2b. a mission under way: to its destination, then back to the
            # governor who gave it, who only thanks and rewards it there
            ms = s["mission"]
            if MISSIONS and ms["type"] != 0 and self.mission_tries < 12:
                goal = ms["dest"] if ms["type"] > 0 else ms["giver"]
                if ms["type"] in (1, 2) or ms["type"] < 0:
                    self.mission_tries += 1
                    res = self.go_town(goal)
                    if res in ("noroute", "slow"):
                        self.mission_tries += 3
                    continue
            # 3. a known villain
            vill = [v for v in s["villains"] if v["active"] and
                    s["towns"][v["town"]]["sea_x"] and
                    (HUNT_WAR or not self.at_war(s["towns"][v["town"]]["nation"])) and
                    v["town"] not in self.unreachable and
                    self.visited.get(v["town"], -99) <= s["day"] - 7]
            if vill:
                v = vill[loops % len(vill)]
                self.note("hunting a villain in %s" % s["towns"][v["town"]]["name"])
                self.go_town(v["town"])
                continue
            # 4. the silver train and the treasure fleet, once strong
            convoy = None
            for tt in (s["train_town"], s["fleet_town"]):
                if 0 <= tt < len(s["towns"]) and tt not in self.unreachable and s["towns"][tt]["sea_x"] and f["crew"] >= 2 * s["towns"][tt]["soldiers"] + 30 \
                        and s["towns"][tt]["nation"] == 0 and self.at_war(0):
                    convoy = tt
                    break
            if convoy is not None:
                self.go_town(convoy, attack=True)
                continue
            # 4b. a small Spanish town the crew can hold: a conquest is
            # +3 standing with the nation, which is what promotions cost
            if CONQUER and self.at_war(0):
                prey = [i for i, t in enumerate(s["towns"])
                        if t["nation"] == 0 and t["sea_x"] and i not in self.unreachable and
                        t["forts"] <= CONQUER_FORTS and f["crew"] >= 2 * t["soldiers"] + 30 and
                        f["crew"] >= t["citizens"] * CONQUER_MEN]
                if prey:
                    prey.sort(key=lambda i: max(abs(s["towns"][i]["sea_x"] - f["x"] // 16),
                                                abs(s["towns"][i]["sea_y"] - f["y"] // 16)))
                    self.go_town(prey[0], attack=True)
                    continue
            # 5. back home for promotions every few loops, else hunt prizes
            # a nation's standing only climbs through the sacks once it is 2
            # or more: its governor's letter of marque gets it there
            due = [n for n in RANK_NATIONS if s["standing"][n] < 2 and not self.at_war(n)]
            due += [n for n in self.promotable() if n != self.mynation() and n not in due]
            if due and not self.at_war(due[0]):
                ti = self.nearest_town(lambda t: t["nation"] in due and t["citizens"] >= 6 and not self.at_war(t["nation"]))
                if ti is not None and self.go_town(ti) != "recent":
                    continue
            if loops % 4 == 0 or s["months"] >= HOME_MONTHS:
                if HOME_ANY:
                    # any town where the bank is open, the nearest first
                    ti = self.nearest_town(lambda t: t["nation"] < 4 and not self.at_war(t["nation"]))
                else:
                    ti = self.nearest_town(lambda t: t["nation"] == self.mynation() and t["citizens"] >= 6)
                self.go_town(ti)
                continue
            # cruise toward a random Spanish town to meet ships
            sp = [i for i, t in enumerate(s["towns"]) if t["nation"] == 0 and t["sea_x"]]
            # one of the nearest, so the voyage fits its time cap
            sp.sort(key=lambda i: max(abs(s["towns"][i]["sea_x"] - f["x"] // 16),
                                      abs(s["towns"][i]["sea_y"] - f["y"] // 16)))
            ti = random.Random(loops).choice(sp[:5])
            t = s["towns"][ti]
            gx, gy = t["sea_x"] + 6, t["sea_y"] + 6
            if not self.water(gx, gy):
                gx, gy = t["sea_x"], t["sea_y"]	# the offset fell inland
            res = self.go(gx, gy, max_days=40)
            if res == "town" and not self.at_war(s["towns"][self.last_town]["nation"]):
                self.visit(self.last_town)
        return self.report()

    def report(self):
        s = self.refresh()
        f = s["fleet"]
        lines = [
            "End: %s %d, %s, rank %s, gold %d, fortune %d, acres %d, crew %d, ships %s, married %d, relative %d, reputation %d" % (
                ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"][s["month"]],
                s["year"], s["player"]["title"], s["rank"], f["gold"], s["fortune"], s["acres"], f["crew"],
                f["ships"], s["married"], s["relative"], s["reputation"]),
            "Reckoning: " + json.dumps({k: v for k, v in self.sim("career_end").items() if k != "ok"}),
            "Stats: " + json.dumps(dict(self.stats), sort_keys=True),
            "Calls: " + json.dumps(dict(self.sim.calls), sort_keys=True),
        ]
        return lines


def main():
    global DATA, BUILD, OUT
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--data", default=DATA, help="the original game data")
    ap.add_argument("--build", default=BUILD, help="where flibustier-sim is")
    ap.add_argument("--out", default=OUT, help="where the journal goes")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--era", type=int, default=1660)
    ap.add_argument("--nation", default="england", choices=NATIONS)
    ap.add_argument("--years", type=int, default=12)
    ap.add_argument("-v", action="store_true", help="print the journal live")
    a = ap.parse_args()
    if a.data is None:
        ap.error("no game data: use --data or set FLIBUSTIER_DATA")
    DATA, BUILD, OUT = os.path.expanduser(a.data), a.build, a.out
    b = Bot(a.era, a.nation, a.seed, a.v)
    try:
        out = b.career(a.years)
    except Fail as e:
        b.odd("protocol failure: %s" % e)
        out = b.report()
    print("\n".join(out))
    print("Journal: %s" % b.journal.name)
    print("Anomalies: %d" % len(b.anomalies))
    for x in sorted(set(b.anomalies)):
        print("  %dx %s" % (b.anomalies.count(x), x))
    return 1 if any("protocol failure" in x for x in b.anomalies) else 0


if __name__ == "__main__":
    raise SystemExit(main())
