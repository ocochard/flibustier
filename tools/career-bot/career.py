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
NATIONS = ["spain", "england", "france", "holland"]


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
        self.journal = open(os.path.join(OUT, "journal-%d-%s-seed%d.txt" % (
            era, nation, seed)), "w", buffering=1)
        self.unreachable = set()
        self.fields = {}
        self.no_repair_until = 0
        self.visited = {}
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
        if not self.water(*start):
            near = [(abs(dx) + abs(dy), (start[0] + dx, start[1] + dy))
                    for dy in range(-4, 5) for dx in range(-4, 5)
                    if self.water(start[0] + dx, start[1] + dy)]
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
        field = self.fields.get(goal)
        if field is None:
            field = {goal: 0}
            q = [(0, goal)]
            while q:
                d0, cur = heapq.heappop(q)
                if d0 > field[cur]:
                    continue
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1),
                               (1, -1), (-1, 1), (-1, -1)):
                    n = (cur[0] + dx, cur[1] + dy)
                    if not self.water(*n):
                        continue
                    if dx and dy and not (self.water(cur[0] + dx, cur[1]) and
                                          self.water(cur[0], cur[1] + dy)):
                        continue
                    d = d0 + (14 if dx and dy else 10) * (6 if self.grid[n[1]][n[0]] == "," else 1)
                    if d < field.get(n, 1 << 30):
                        field[n] = d
                        heapq.heappush(q, (d, n))
            self.fields[goal] = field
        if start not in field:
            return None
        out = [start]
        cur = start
        while cur != goal:
            best = None
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1),
                           (1, -1), (-1, 1), (-1, -1)):
                n = (cur[0] + dx, cur[1] + dy)
                if field.get(n, 1 << 30) < field[cur] and (
                        best is None or field[n] < field[best]):
                    best = n
            cur = best
            out.append(cur)
        return out

    def los(self, a, b):
        n = max(abs(b[0] - a[0]), abs(b[1] - a[1]), 1)
        for i in range(1, n + 1):
            x = round(a[0] + (b[0] - a[0]) * i / n)
            y = round(a[1] + (b[1] - a[1]) * i / n)
            if not self.water(x, y):
                return False
        return True

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
            if abs(cx - tx) <= 1 and abs(cy - ty) <= 1 and town is None:
                return {"stop": "arrived"}
            # advance along the route while we are close to its points
            while idx < len(route) - 1 and max(abs(route[idx][0] - cx), abs(route[idx][1] - cy)) <= 2:
                idx += 1
            # look ahead for the farthest visible route point
            j = idx
            for k in range(idx, min(len(route), idx + 14)):
                if self.los((cx, cy), route[k]):
                    j = k
            wx, wy = route[j]
            want = self.bearing(wx * 16 + 8 - x, wy * 16 + 8 - y)
            want = (want + 8) & 0xF0
            diff = ((want - f["heading"] + 128) & 0xFF) - 128
            turn = 0 if abs(diff) < 8 else (1 if diff > 0 else -1)
            ticks = 4 if turn else 16
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
                    if bumps > 30:
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
            self.note("mutiny!")
            self.sim("crew", action="duel", weapon=2)
            res = self.fight()
            self.sim("crew", action="end")
            self.note("mutiny duel: %s" % res)
            self.stats["mutinies"] += 1
        elif ev != "none":
            self.stats["crew_" + ev] += 1
            if ev in ("hungry", "starving"):
                self.hungry = True
            if ev == "wrecked":
                self.note("wrecked on a reef")
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
    def battle(self):
        """Steer at the foe and board her; returns the finish reply."""
        self.battle_day = self.st["day"]
        b = self.sim("battle", ticks=1)["battle"]
        n = 0
        while b["outcome"] == "none" and n < 20000:
            you, foe = b["you"], b["foe"]
            want = self.bearing(foe["x"] - you["x"], foe["y"] - you["y"])
            diff = ((want - you["heading"] + 128) & 0xFF) - 128
            turn = 0 if abs(diff) < 6 else (1 if diff > 0 else -1)
            dist = math.hypot(foe["x"] - you["x"], foe["y"] - you["y"]) / 256
            fire = 1 if (b.get("fort") or dist > 60) and you["reload"] == 0 and abs(diff) > 40 else 0
            b = self.sim("battle", ticks=4, turn=turn, fire=fire, sails=1)["battle"]
            n += 1
        out = b["outcome"]
        self.stats["battle_" + out] += 1
        if out == "boarding":
            self.sim("board", weapon=2)
            res = self.fight()
            self.note("boarding fight: %s" % res)
        r = self.sim("finish", keep=1)
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
            strong = s["fleet"]["crew"] > max(25, enc["crew"] * 3 // 2)
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
        self.governor(ti)
        if t["nation"] == self.mynation() and (s["months"] >= 6 or s["fleet"]["mood"] < 2):
            d = self.sim("bank", action="divide")
            self.note("divided the plunder at %s: share %d, mine %d, fortune %d" % (
                t["name"], d["share"], d["yours"], d["fortune"]))
            self.stats["divides"] += 1
            self.sim("bank", action="continue", advance=0)
        self.tavern()
        self.merchant()
        self.shipwright()
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

    def governor(self, ti):
        s = self.st
        t = s["towns"][ti]
        if t["citizens"] < 6 or self.at_war(t["nation"]) or t["nation"] == 4:
            return
        g = self.sim("governor", action="visit", check=False)
        if not g.get("ok"):
            return
        if g["promoted"]:
            self.note("promoted by the %s governor of %s (+%d acres)%s" % (
                NATIONS[t["nation"]], t["name"], g["acres"],
                ", a new villain appears" if g["villain"] >= 0 else ""))
            self.stats["promotions"] += 1
        if g["report"]:
            self.note("mission report: %d" % g["report"])
        if g["villain_here"] >= 0:
            self.villain_duel("governor's tip at " + t["name"])
        if g["offer"] in ("marque", "pardon") and 0 < g["price"] <= s["fleet"]["gold"] // 3:
            p = self.sim("governor", action="pay")
            if p["paid"]:
                self.note("bought a %s from %s for %d" % (g["offer"], t["name"], g["price"]))
        if g["offer"] == "daughter" and not self.refresh()["married"]:
            self.daughter(t)

    def daughter(self, t):
        m = self.sim("daughter", action="meet")
        if m["result"] == 0:
            return
        if m["result"] == 2:
            return  # already the wife
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

    def tavern(self):
        r = self.sim("tavern", action="open")
        s = self.refresh()
        f = s["fleet"]
        room = (len(f["ships"]) * 2 + f["capacity"]) * 16 - f["crew"]
        # the crew the gold keeps content (mood >= 2): sqrt(gold/10/crew) x 2
        # - months + 4 - difficulty >= 2
        need = s["months"] + s["player"]["difficulty"] - 2
        cap = room if need <= 0 else int((f["gold"] / 10) / ((need / 2) ** 2)) - f["crew"]
        if r["recruits"] > 0 and room > 0 and min(room, cap) >= r["recruits"] // 2 and f["crew"] < 160:
            r = self.sim("tavern", action="recruit", accept=1)
            self.stats["recruited"] += 1
        # the map seller, while the pirate's map is not whole
        if r["tavern_spots"] & 1 and (s["maps"][0]["pieces"] & 0xF) != 0xF and f["gold"] > 500:
            if self.sim("tavern", action="buy_map", check=False).get("ok"):
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
        self.sim("merchant", action="close", traded=0, check=False)

    def shipwright(self):
        p = self.sim("shipwright", action="prices", check=False)
        if not p.get("ok"):
            return
        f = self.refresh()["fleet"]
        if len(f["ships"]) > 3:
            # sell the worst extras
            idx = sorted(range(1, len(f["ships"])), key=lambda i: f["ships"][i])[: len(f["ships"]) - 3]
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
        while self.refresh()["day"] - t0 < max_days:
            r = self.sail_to(tx, ty, town=town, max_days=max_days)
            stop = r["stop"]
            if stop == "stuck" and self.ship_on_land():
                continue
            if stop == "encounter":
                self.encounter()
                continue
            if stop == "crew":
                self.crew_stop()
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
        self.odd("ship set on land at %d,%d (heading %d): rescued to %d,%d" % (
            cx, cy, f["heading"], nx, ny))
        self.stats["rescued_from_land"] += 1
        f["x"], f["y"], f["fx"], f["fy"] = nx * 16 + 8, ny * 16 + 8, 4, 4
        self.sim("load", state=s)
        return True

    def go_town(self, ti, attack=False):
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
        self.sim("town", action="attack")
        self.sim("assault", action="start", weapon=2, aboard=0)
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
                self.sim("assault", action="governor", nation=self.mynation())
                self.note("%s is now %s" % (t["name"], NATIONS[self.mynation()]))
                self.stats["conquests"] += 1
            if self.refresh()["player_town"] >= 0:
                self.sim("town", action="leave")
        elif self.refresh()["day"] - day0 > 20:
            self.prison()
        self.check_fleet()
        return "assaulted"

    # ------------------------------------------------------------- search
    def dig(self, mapi):
        s = self.refresh()
        m = s["maps"][mapi]
        tx, ty = m["x"], m["y"]
        self.note("going to dig map %d near %d,%d (pieces %d)" % (mapi, tx, ty, m["pieces"]))
        # nearest water cell to the spot
        best = None
        for r in range(1, 40):
            for dy in range(-r, r + 1):
                for dx in range(-r, r + 1):
                    if max(abs(dx), abs(dy)) == r and self.water(tx + dx, ty + dy):
                        best = (tx + dx, ty + dy)
                        break
                if best:
                    break
            if best:
                break
        res = self.go(best[0], best[1])
        if res != "arrived":
            return False
        # steer into the land toward the spot
        for _ in range(400):
            f = self.refresh()["fleet"]
            want = (self.bearing(tx * 16 + 8 - f["x"], ty * 16 + 8 - f["y"]) + 8) & 0xF0
            diff = ((want - f["heading"] + 128) & 0xFF) - 128
            turn = 0 if abs(diff) < 8 else (1 if diff > 0 else -1)
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
        # march to the spot
        for _ in range(3000):
            f = self.refresh()["fleet"]
            cx, cy = f["x"] // 16, f["y"] // 16
            if abs(cx - tx) < 2 and abs(cy - ty) < 2:
                break
            if not f["ashore"]:
                self.odd("the party walked back aboard on its way to map %d" % mapi)
                return False
            dx = (tx > cx) - (tx < cx)
            dy = (ty > cy) - (ty < cy)
            r = self.sim("march", dx=dx, dy=dy, ticks=8)
            if r["stop"] == "boarded":
                self.odd("the party walked back aboard on its way to map %d" % mapi)
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
        for _ in range(4000):
            f = self.refresh()["fleet"]
            if not f["ashore"]:
                break
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
        start = self.refresh()["year"]
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
            # 1. the lost relative's map, then the lost treasure
            for mapi in (1, 2, 0):
                if s["maps"][mapi]["pieces"] & 0xF:
                    self.dig(mapi)
                    break
            else:
                mapi = None
            if mapi is not None:
                continue
            # 2. food and repairs
            hurt = sum(f["damage"]) > 0 and f["gold"] > 1500 and s["day"] >= self.no_repair_until
            if (self.hungry or f["food"] < f["crew"] // 4 + 5 or hurt) and f["gold"] >= 100:
                ti = self.nearest_town(lambda t: self.friendly(t) and self.visited.get(
                    self.st["towns"].index(t), -99) <= self.st["day"] - 7)
                if ti is not None and self.go_town(ti) != "recent":
                    continue
            # 3. a known villain
            vill = [v for v in s["villains"] if v["active"] and
                    s["towns"][v["town"]]["sea_x"] and
                    not self.at_war(s["towns"][v["town"]]["nation"]) and
                    v["town"] not in self.unreachable and
                    self.visited.get(v["town"], -99) <= s["day"] - 7]
            if vill:
                v = vill[loops % len(vill)]
                self.note("hunting a villain in %s" % s["towns"][v["town"]]["name"])
                self.go_town(v["town"])
                continue
            # 4. the silver train, once strong
            tt = s["train_town"]
            if 0 <= tt < len(s["towns"]) and tt not in self.unreachable and s["towns"][tt]["sea_x"] and f["crew"] >= 2 * s["towns"][tt]["soldiers"] + 30 \
                    and s["towns"][tt]["nation"] == 0 and self.at_war(0):
                self.go_town(tt, attack=True)
                continue
            # 5. back home for promotions every few loops, else hunt prizes
            if loops % 4 == 0 or s["months"] >= 8:
                ti = self.nearest_town(lambda t: t["nation"] == self.mynation() and t["citizens"] >= 6)
                self.go_town(ti)
                continue
            # cruise toward a random Spanish town to meet ships
            sp = [i for i, t in enumerate(s["towns"]) if t["nation"] == 0 and t["sea_x"]]
            ti = random.Random(loops).choice(sp)
            t = s["towns"][ti]
            res = self.go(t["sea_x"] + 6, t["sea_y"] + 6, max_days=40)
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
