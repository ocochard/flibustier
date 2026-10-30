#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""data/sword: the sword fight tables.

Designed from the rules that read them (src/sim/sword.c, the sword
screen in src/ui/screens.c) and the open pack's fencer sheets (frames
per move), nothing else.  How the rules use the fields:

  - Moves: 0-2 guard middle, low, high; 3-5 the same stepping back; 6-8
    slash and 9-11 lunge, middle, low, high; 12 advance, 13 retreat, 14
    the fall; 15 is never chosen by the rules.
  - A move lasts frames[move] fight ticks; its row per frame says the
    lines guarded, the line struck (0 none), the step toward the
    opponent and away.  A strike lands when the other side's guard does
    not cover its line, the distance is within reach and a roll of
    rand & 255 reaches the hit threshold.  The rules then fix the damage
    by weapon and by move: a lunge moves the strength bar half as far as
    a slash, and the cutlass five times as far as the rapier.
  - Up close (under 20 apart) the opponent plays its script, or guards
    low when rand(16) <= caution; farther it picks from far_ai.
"""

# Lines, in move order: a move's line is (move % 3) for moves 0-11.
MID, LOW, HIGH = 0, 1, 2
# The guard and strike bits of each line.
BIT = {LOW: 1, MID: 2, HIGH: 4}
ALL = 7
LINE_NAME = {MID: "middle", LOW: "low", HIGH: "high"}

# Body areas (the wound layers): 1 head, 2 chest, 3 legs.
AREA = {HIGH: 1, MID: 2, LOW: 3}

GUARD, BACK, SLASH, LUNGE = 0, 3, 6, 9
ADVANCE, RETREAT, FALL, SPARE = 12, 13, 14, 15
MOVES, FRAMES = 16, 6

# Frames in the open art's sheets (grp/mybody ... grp/hecutlas): 6 per
# move, 4 for the lunges and 10 for the fall.  A move may use fewer.
ART_FRAMES = [6] * 9 + [4] * 3 + [6, 6, 10, 6]


def frame(guard=0, strike=0, step_in=0, step_back=0):
    return {"guard": guard, "strike": strike, "step_in": step_in,
            "step_back": step_back}


def guard_move(line):
    # Two ticks of a steady guard on one line: short, so a guard is
    # the quickest way to answer what the opponent starts.
    return [frame(guard=BIT[line])] * 2


# The open pack's fencer frames plant their feet on the steps of the
# back moves (3-5), the advance (12) and the retreat (13): tools/artgen
# reads step_in and step_back from this table when it renders them.
# Change those steps, and the fencers must be rendered again (artgen.py
# fencer/my, fencer/he) or they slide.


def back_move(line):
    # The same guard held while giving ground, 3 pixels a tick (12 in
    # all): a step back costs a little more time than a plain guard.
    return [frame(guard=BIT[line], step_back=3)] * 4


def slash_move(line):
    # Six ticks: the guard is kept while drawing back (tick 0), the
    # blade is raised and the body open (1-3), it cuts on tick 4, and
    # the guard comes back as it recovers (5).  Slow and readable: the
    # raised blade shows the line three ticks before the cut (0.5 s at
    # the easy levels, 0.4 s at the middle one, 0.2 s at level 3: see
    # DELAY), time to parry it; it moves the bar twice as far as a lunge.
    return [frame(guard=BIT[line]), frame(), frame(), frame(),
            frame(strike=BIT[line]), frame(guard=BIT[line])]


def lunge_move(line):
    # Four ticks, the open art's lunge: a long step in (tick 1, 10
    # pixels, which reaches farther than standing still), the point
    # lands on tick 2, the recovery steps half back (tick 3).  Quick
    # and far reaching, but light: half a slash's damage.
    return [frame(guard=BIT[MID]), frame(step_in=10),
            frame(strike=BIT[line]), frame(step_back=5, guard=BIT[MID])]


def advance_move():
    # Four ticks of 4 pixels each, guarding the middle.
    return [frame(guard=BIT[MID], step_in=4)] * 4


def retreat_move():
    return [frame(guard=BIT[MID], step_back=4)] * 4


def fall_move():
    # The fight ends after the fall's frame 8: it needs 10 frames, all
    # of the open art's, and no guard (the beaten side is open).
    return [frame()] * 10


def spare_move():
    # Never chosen: a plain middle guard, so a stray move 15 is harmless.
    return [frame(guard=BIT[MID])] * 6


def move_defs():
    d = {}
    for line in (MID, LOW, HIGH):
        d[GUARD + line] = guard_move(line)
        d[BACK + line] = back_move(line)
        d[SLASH + line] = slash_move(line)
        d[LUNGE + line] = lunge_move(line)
    d[ADVANCE] = advance_move()
    d[RETREAT] = retreat_move()
    d[FALL] = fall_move()
    d[SPARE] = spare_move()
    return d


# Reach, in pixels between the fighters' x.  Up close the fighters
# stand some 0-20 apart: steps in stop under 10, so they come nearer
# only by a lunge's 10 pixel step or a blow's push.  Measured by bot
# fights, the strikes bunch at 9 apart (where steps in stop) and at 4,
# the rest spread thinly below.  The rules make the longsword's blows
# move the bar three times as far as the rapier's and the cutlass's five
# times, so reach pays for weight: the rapier (30) strikes from anywhere
# in the fight, while a heavy blade (4 and 3) lands only when the lunge
# has carried it right in, about a fifth of its strikes, where the
# opponent's blade reaches it too.  A reach of 5 or more takes in the
# bunch at 4 and makes the heavy blades far the strongest.
# The arm is one pixel longer at the two easy levels: a cautious
# opponent there often stands guarding, and the heavy blades, which
# then land only from inside 4, dragged on until the crews' melee
# decided; with it, the apprentice's boarding cutlass wins like the
# rapier (better when outnumbered).  Level 1 is also where an
# apprentice boards a named captain (a level or two harder): without
# the pixel, random lunges with a cutlass won only a third of those
# boardings with a crew two and a half times smaller than the enemy's
# (the strength bar near 16 of 47), and the career bot lost nearly half
# its boardings of named captains while outnumbered, each one a prison
# term and an injury.  With it they win nine in ten there, two in five
# when outnumbered five to one (bar 13), where a parrying player still
# wins four in five.
# The rapier, whose reach does not hang on the pixel, is unchanged: it
# fares worse outnumbered at level 1, where its light blows move the
# bar too slowly against the melee.  The arm does not shorten at the
# hard levels, as it used to: a pixel is a large share of the heavy
# blades' chances and none of the rapier's, so it made the hard levels
# lopsided by weapon; the hit thresholds alone make them hard.
REACH_LEVEL = [3, 3, 2, 2, 2]
REACH_WEAPON = [28, 2, 1]              # rapier, longsword, cutlass
# The opponent's, by its blade.  Its heavy blades keep a long reach: a
# shorter one mirroring the player's made crew fights against them easy
# with a rapier and lopsided by weapon.
FOE_REACH = [26, 22, 18]

# Hit thresholds against rand & 255 (lower hits more often).  From the
# easiest level to the hardest the player lands 91% down to 41% of the
# strikes that get through, the opponent 9% up to 75%; at the middle
# level 70% against 42%.  The player's practice adds to his roll.
# Crew fights are much harder than duels at the same thresholds: the
# crews' opponent swings a longsword or cutlass (two or three times a
# rapier's blow) and the melee wears the weaker crew down, so the
# thresholds are set for boarding: at the two easy levels a crew as
# strong as the enemy's wins nearly every boarding even fighting
# carelessly, from the middle level on it takes a fencer.
HIT = [24, 48, 76, 112, 152]
FOE_HIT = [232, 192, 148, 108, 64]

# Up close the opponent guards low instead of following its script when
# rand(16) <= caution: an easy opponent hesitates often, which also
# leaves fewer attacks to the named captains a level or two harder.
CAUTION = [10, 8, 6, 2, 0]

# Timer units (screen frames at 60 Hz, one per sword screen tick) per
# fight tick, by level & 3; a tick lasts DELAY/60 s.  Set from how long
# a player has to parry: a key is read when his move ends, so a guard
# must be keyed a tick before the cut, and a slash's blade, raised on
# its tick 1, gives 2 ticks to see it and press (3 from the draw back
# on tick 0); a lunge's step in, on its tick 1, gives none.  For a
# reaction of 0.25 s (r):
#   easy (0, 1), comfortable: 2 ticks >= 4/3 r = 0.33 s, DELAY >= 10;
#   middle (2), with attention: 2 ticks >= r, DELAY >= 7.5, so 8;
#   hard (3), only by anticipating: even the 3 ticks from the raised
#   blade to the cut under r, DELAY < 5, so 4.
# Seconds a tick: 0.167, 0.167, 0.133, 0.067.  Level 4 shares entry 0
# and plays at the easy pace; its thresholds make it the hardest, but
# its fights last longest (a good fencer's duel takes 1.5 min).
DELAY = [10, 10, 8, 4]

# Far apart (more than 59, or the opponent past the player) an attack
# key advances (it keeps advancing while held, then attacks in range);
# guards and steps back stay what they are.
FAR_MOVE = [GUARD + MID, GUARD + LOW, GUARD + HIGH,
            BACK + MID, BACK + LOW, BACK + HIGH,
            ADVANCE, ADVANCE, ADVANCE, ADVANCE, ADVANCE, ADVANCE]

# Inputs taken once fall back to the middle guard when the move ends.
# The front end sends a key when it goes down and again only on the
# system's key repeat, so a guard is kept (a held guard does not drop
# before the repeat starts; it lasts until another key), while an
# attack or a step back is one move per key press.  The advance repeats,
# so an attack asked for from afar is carried out once in reach.
ONCE = [0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0]

# The opponent's moves from 20 apart and more: mostly advancing, now
# and then a lunge (its step in closes the gap and may land) or a guard.
FAR_AI = [ADVANCE, ADVANCE, ADVANCE, LUNGE + MID, ADVANCE, LUNGE + LOW,
          GUARD + HIGH, LUNGE + HIGH]

# Script counters: a move chosen against the player's current move.
PARRY = 0x90        # guard the line the player is on
GIVE = 0x91         # step back guarding that line
CUT_ELSE = 0x92     # slash on another line than the player's
THRUST_ELSE = 0x93  # lunge on another line

G, B, S, L = GUARD, BACK, SLASH, LUNGE

# Bank 0 (level 0): plain patterns a beginner learns to read, few
# counters, many guards.
BANK_EASY = [
    [G + MID, S + MID, G + MID, G + MID, S + HIGH, G + HIGH, G + MID, B + MID],
    [G + HIGH, S + HIGH, G + HIGH, G + LOW, S + LOW, G + LOW, G + MID, S + MID],
    [G + MID, L + MID, G + MID, B + MID, G + LOW, L + LOW, G + LOW, G + MID],
    [G + LOW, G + LOW, S + LOW, G + MID, G + MID, S + MID, PARRY, G + MID],
    [S + MID, G + MID, G + MID, G + HIGH, S + HIGH, G + HIGH, B + HIGH, G + MID],
    [G + MID, G + MID, L + HIGH, G + HIGH, PARRY, G + MID, S + LOW, G + LOW],
    [B + MID, G + MID, S + MID, G + MID, G + LOW, S + LOW, G + LOW, PARRY],
    [G + HIGH, L + MID, G + MID, G + MID, S + HIGH, G + HIGH, B + LOW, G + LOW],
]

# Bank 1 (level 1): attacks more often, parries now and then.
BANK_MID = [
    [S + MID, PARRY, S + HIGH, G + HIGH, L + LOW, PARRY, G + MID, CUT_ELSE],
    [PARRY, S + LOW, G + LOW, L + MID, G + MID, PARRY, S + HIGH, B + HIGH],
    [L + MID, G + MID, PARRY, CUT_ELSE, G + MID, S + MID, GIVE, L + HIGH],
    [G + HIGH, S + HIGH, PARRY, S + MID, G + LOW, L + LOW, PARRY, G + MID],
    [S + LOW, G + LOW, PARRY, L + HIGH, G + HIGH, CUT_ELSE, G + MID, B + MID],
    [PARRY, PARRY, S + MID, L + MID, G + MID, S + HIGH, G + HIGH, THRUST_ELSE],
    [L + LOW, B + LOW, PARRY, S + HIGH, G + MID, CUT_ELSE, PARRY, G + MID],
    [G + MID, CUT_ELSE, PARRY, S + LOW, G + LOW, L + MID, GIVE, S + MID],
]

# Bank 2 (levels 2-4): reads the player, answers his line, feints by
# attacking where he is not.
BANK_HARD = [
    [PARRY, CUT_ELSE, PARRY, THRUST_ELSE, S + MID, PARRY, CUT_ELSE, G + HIGH],
    [THRUST_ELSE, PARRY, S + HIGH, CUT_ELSE, PARRY, L + LOW, PARRY, CUT_ELSE],
    [PARRY, PARRY, CUT_ELSE, GIVE, THRUST_ELSE, S + LOW, PARRY, S + HIGH],
    [CUT_ELSE, PARRY, L + MID, PARRY, CUT_ELSE, G + MID, THRUST_ELSE, PARRY],
    [S + HIGH, PARRY, CUT_ELSE, PARRY, L + MID, GIVE, CUT_ELSE, PARRY],
    [PARRY, THRUST_ELSE, PARRY, S + MID, CUT_ELSE, PARRY, S + LOW, THRUST_ELSE],
    [L + HIGH, PARRY, CUT_ELSE, CUT_ELSE, PARRY, GIVE, THRUST_ELSE, S + MID],
    [PARRY, S + LOW, PARRY, CUT_ELSE, L + HIGH, PARRY, CUT_ELSE, G + LOW],
]

# The flags' height on screen by bar / 8 (0 weakest .. 5 strongest), the
# screen y of the flag frame's top (its sheet frames have no offset): the
# stronger side's flag flies higher, up the narrow side columns of the
# open pack's bottom panel (the foe's flag at x 10 in the left column,
# the player's at x 584 in the right one). Measured on the open pack's
# art, drawn for a 1080-high screen: the panel is the same on
# art/shipdeck.png, alley.png and fort.png; the columns' recess starts
# under its top bevel's highlight (rows 729-738) and ends on its bottom
# shadow line (from row 1060); the flags of art/miscswrd.sheet (frames
# 6-53) are 63 high, opaque top to bottom. The flag's top steps evenly
# from the recess's foot, a frame's height above its bottom, to its head.
FLAG_ART_HEIGHT = 1080
FLAG_RECESS_TOP = 739           # first row inside the columns' recess
FLAG_RECESS_BOTTOM = 1060       # first row of its bottom shadow line
FLAG_FRAME_HEIGHT = 63


def _screen(art):
    return round(art * 480 / FLAG_ART_HEIGHT)


def _flag_heights():
    top = _screen(FLAG_RECESS_TOP)
    foot = _screen(FLAG_RECESS_BOTTOM) - _screen(FLAG_FRAME_HEIGHT)
    return [round(foot - (foot - top) * i / 5) for i in range(6)]


FLAG_Y = _flag_heights()

# The edges: the player starts at x 320-340 and is driven back to the
# right, the opponent starts at 220-240 and is driven off to the left.
PLAYER_EDGE = 440
FOE_EDGE = 120


def build_moves():
    defs = move_defs()
    frames = []
    tables = {k: [] for k in ("guard", "strike", "step_in", "step_back")}
    for m in range(MOVES):
        rows = defs[m]
        # A move fits its animation in the open art.
        assert 1 <= len(rows) <= ART_FRAMES[m], m
        frames.append(len(rows))
        for k in tables:
            # Past the table's 6 frames the rules read frame 5.
            vals = [r[k] for r in rows[:FRAMES]]
            vals += [vals[-1]] * (FRAMES - len(vals))
            tables[k].append(vals)
    return frames, tables


def check_scripts(banks):
    for bank in banks:
        assert len(bank) == 8
        for s in bank:
            assert len(s) == 8
            for v in s:
                assert v < ADVANCE or PARRY <= v <= THRUST_ELSE, v


def build():
    frames, t = build_moves()
    banks = [BANK_EASY, BANK_MID, BANK_HARD]
    check_scripts(banks)
    line = [m % 3 if m < ADVANCE else MID for m in range(MOVES)]
    area = [AREA[m % 3] if SLASH <= m < ADVANCE else 0
            for m in range(MOVES)]
    value = {
        "flag_y": FLAG_Y,
        "frames": frames,
        "reach_level": REACH_LEVEL,
        "reach_weapon": REACH_WEAPON,
        "foe_reach": FOE_REACH,
        "delay": DELAY,
        "player_edge": PLAYER_EDGE,
        "foe_edge": FOE_EDGE,
        "hit": HIT,
        "foe_hit": FOE_HIT,
        "far_move": FAR_MOVE,
        "once": ONCE,
        "guard": t["guard"],
        "strike": t["strike"],
        "step_in": t["step_in"],
        "step_back": t["step_back"],
        "far_ai": FAR_AI,
        "caution": CAUTION,
        "area": area,
        "scripts": banks,
        "line": line,
    }
    notes = (
        "Designed anew from the rules in src/sim/sword.c and the open "
        "fencer art's frame counts. Each guard covers only its own line "
        "for 2 ticks; a step back holds the guard while giving 12 pixels. "
        "A slash is slow (6 ticks, the cut on tick 4, the body open "
        "while the blade is raised, time to read and parry it) and moves "
        "the bar twice as far as a lunge, which is quick (4 ticks, a 10 "
        "pixel step in before the point lands). Reach pays for the "
        "weight the rules give heavy blades: the rapier strikes from 30 "
        "pixels, the longsword and cutlass only from 4 and 3 (one more "
        "at the two easy levels, where a hesitant opponent would otherwise "
        "keep them out, and where an apprentice boards named captains, "
        "a level harder), once a lunge has carried them right in. Harder "
        "levels raise the player's hit threshold, lower the opponent's "
        "and its caution; the arm keeps its length, which would favour "
        "the rapier. The "
        "opponent's scripts go from plain guard-and-cut patterns (level "
        "0) to parries of the player's line and attacks on the lines he "
        "leaves open (levels 2-4). Attack keys advance from afar and "
        "strike once in reach; a guard is kept until another key, an "
        "attack or step back is one per key press. Edges and flag "
        "heights fit the 640x480 sword screen. The pace leaves a player "
        "with a quarter second reaction time to parry a raised blade "
        "comfortably at the easy levels (a tick lasts 1/6 s), with "
        "attention at the middle level (2/15 s), and only by "
        "anticipating at level 3 (1/15 s); a lunge is hard to parry by "
        "reaction at every level. Level 4 shares the easy pace. "
        "Tuned by bot fights through flibustier-sim at every level, "
        "weapon and crew ratio, with random lunges (the career bot's) "
        "and a player who parries the line he sees raised 2 ticks late: "
        "with a crew as strong as the enemy's, random lunges win "
        "boardings at the two easy levels nearly always and about half "
        "at the middle level, the parrying player nine in ten there; at "
        "the easiest level boardings are won even outnumbered three to "
        "one. Duels are won at the easy and middle levels; at level 3 "
        "random lunges win two in three, at level 4 almost none, the "
        "parrying player two in three. Over eight career bot careers "
        "the bot won 99.5% of its sword fights (1035 of 1040), up from "
        "63%, and went to prison 3 times. The pace does not change "
        "these: the bot plays tick by tick. Clean-room "
        "note: after these values were final, a search of the docs "
        "printed seven lines of the engine's rules spec on the weapons' "
        "reach (no game-data numbers); nothing was changed after it.")
    return value, notes


TABLES = [("data/sword", "sword.json", build)]
