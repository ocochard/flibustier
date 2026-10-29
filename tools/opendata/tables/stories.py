#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""data/stories: how the player came to command a ship, per scenario.

30 lists of steps in the order of data/scenarios: 24 careers (eras
1560 ... 1680, each English, French, Dutch, Spanish), then the 6 famous
expeditions.  Built only from the CSTR entries text-catalogue.md lists
as career story pages, as the open pack worded them.  None of those
pages uses a %s filler, so no step depends on the treasure, town, year
or name arguments.
"""

# The story pages by role (open pack wording, text-catalogue.md):
#
# Openings, one background each:
#    1, 31 a merchant captain seeks backing from men of rank
#    7  a dying seaman's tale of a lost silver galleon
#   13  poor emigrants wrecked on a reef
#   18  indentured to pay the passage
#   21  arrested for stealing bread
#   24  a governor offers a coast-guard ship
#   32  the family's lands seized, kin carried to the Indies
#   34  a trading company offers a ship
#   36  a youth of a seafaring family turns privateer
#   38  raised among buccaneers after fleeing bondage
#   39  a ship owner ruined by trade monopolies
# Middles (the original asked its knowledge question after these; here
# the player only turns the page):
#    2  an old lord backs the venture            (after 1, 31)
#    8  wary merchants would back it             (after 7)
#   14  cane fields, smugglers                   (after 13, 22)
#   19  slipping away to some sailors            (after 18, 22)
#   22  sentenced and transported                (after 21)
#   25  the governor would test you              (after 24)
#   33  penniless sailors invite you along       (after 7, 32)
#   37  a privateer captain signs on men         (after 32, 36, 38)
#    3  the voyage out to the Caribbean          (after 2, 8, 39)
# Lead-ins to the fight, each naming the foe who draws:
#    9  the rebel of a failing colony            (after 8)
#   15  the captain of a ship seized at anchor   (after 14, 38)
#   20  the buccaneers' captain                  (after 19)
#   26  a smuggler's captain                     (after 25)
#   35  the leader of a ship that boards yours   (after 2, 34, 39)
#   43  a bully among the crew                   (after 37)
#   44  a pirate captain at the watering place   (after 3, 33, 37)
# After the fight:
#   10  the men take you as captain, you take the ship (after 9, 20, 43)
#   17  the prize, a friendly port, a letter of marque (any ship taken)
#   23  the ship refitted and crewed, at last your own (a first ship)
#   27  the prize refitted, her men sign on       (a ship taken by one
#                                                  who had none)

FIGHT = "fight"


def career(*pages):
    """A career's story: each page shown until the player turns it;
    FIGHT marks the duel, which comes after its lead-in page was read.
    The story ends on the last page turned."""
    steps = []
    for p in pages:
        if p == FIGHT:
            steps.append("fight")
        else:
            steps += [{"text": p}, "page"]
    return steps


# Per era, the English, French, Dutch and Spanish careers.  Each nation
# keeps its own colour across the eras: England sends the poor and the
# convicted west (indenture, transport) and its sea dogs; France its
# emigrants, Huguenots and the buccaneers of the islands; Holland its
# merchants, companies and ship owners shut out of the Indies trade;
# Spain its men of rank, its coast-guard captains and fortune seekers.
# The eras add history where an entry fits: French colonists in Florida
# in the 1560s (the failing colony), the Dutch West India Company of the
# 1620s, the buccaneers of the 1640s-1660s, the guarda costas and the
# Huguenot exodus of the 1680s.
CAREERS = {
    1560: [
        career(36, 37, 44, FIGHT, 17),          # sea dog's son
        career(7, 8, 9, FIGHT, 10),             # silver tale, colony
        career(39, 44, FIGHT, 23),              # owner vs. monopoly
        career(1, 2, 35, FIGHT, 27),            # merchant at court
    ],
    1600: [
        career(7, 33, 44, FIGHT, 23),           # silver tale, sailors
        career(13, 14, 15, FIGHT, 17),          # wrecked emigrant
        career(39, 35, FIGHT, 17),              # owner vs. monopoly
        career(31, 2, 3, 44, FIGHT, 17),        # merchant at court
    ],
    1620: [
        career(18, 19, 20, FIGHT, 10),          # indentured servant
        career(32, 37, 43, FIGHT, 10),          # dispossessed noble
        career(34, 35, FIGHT, 27),              # company captain
        career(24, 25, 26, FIGHT, 27),          # coast guard
    ],
    1640: [
        career(21, 22, 14, 15, FIGHT, 17),      # convict, cane fields
        career(38, 15, FIGHT, 23),              # raised by buccaneers
        career(36, 37, 43, FIGHT, 10),          # sailor's son
        career(7, 8, 3, 44, FIGHT, 17),         # silver tale, backers
    ],
    1660: [
        career(38, 37, 44, FIGHT, 17),          # buccaneer, marque
        career(18, 19, 20, FIGHT, 10),          # engagé to Tortuga
        career(39, 3, 44, FIGHT, 17),           # owner vs. monopoly
        career(36, 37, 44, FIGHT, 27),          # sailor's son
    ],
    1680: [
        career(21, 22, 19, 20, FIGHT, 10),      # convict escapes
        career(32, 33, 44, FIGHT, 23),          # Huguenot exile
        career(31, 2, 35, FIGHT, 17),           # merchant at court
        career(24, 25, 26, FIGHT, 27),          # guarda costa
    ],
}

# A famous expedition is handed its command (40), then sails out (3);
# no fight, so it reaches the sea at once.
EXPEDITION = career(40, 3)

STORY_PAGES = {1, 2, 3, 7, 8, 9, 10, 13, 14, 15, 17, 18, 19, 20, 21, 22,
               23, 24, 25, 26, 27, 31, 32, 33, 34, 35, 36, 37, 38, 39,
               40, 43, 44}


def build():
    stories = [s for era in sorted(CAREERS) for s in CAREERS[era]]
    stories += [EXPEDITION] * 6
    assert len(stories) == 30
    for s in stories:
        assert len(s) <= 64 and s.count("fight") <= 1
        assert all(st in ("page", "wait", "fight") or
                   st["text"] in STORY_PAGES for st in s)
        assert not s or s[-1] == "page"
    notes = ("Each career story chains CSTR story pages the open pack "
             "wrote: an opening background, a middle page, a page "
             "leading into one sword fight that names the foe, and a "
             "closing page on how the ship was won; every page is shown "
             "until the player turns it, and the story ends on a turned "
             "page. Chains were picked so each reads in order (e.g. "
             "convict 21 -> transported 22 -> cane fields 14 -> raid 15 "
             "-> fight -> letter of marque 17). Nations keep a colour "
             "across eras: England the indentured, convicts and sea "
             "dogs; France emigrants, Huguenots, buccaneers; Holland "
             "merchants, the company and ship owners; Spain men of rank, "
             "coast guards, fortune seekers; eras add history where an "
             "entry fits (a failing colony in 1560 France, the company "
             "in 1620 Holland, the guarda costa in 1680 Spain). All 12 "
             "openings and all 7 fight lead-ins are used; no two careers "
             "of an era share a story. The Dutch 1560 career (an open "
             "home port, the towns' early stretch) is the ship owner "
             "shut out of the trade. The six famous expeditions show "
             "the command page (40) then the voyage out (3), no fight. "
             "No page used has a %s filler.")
    return {"stories": stories}, notes


TABLES = [("data/stories", "stories.json", build)]
