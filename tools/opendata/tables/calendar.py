#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""data/calendar: the length of a day at sea and on foot.

A ship sails at most 6 map cells a day at the original's 64 ticks a day
(the engine's top speed).  On the open world, a faithful Mercator map
of the Caribbean, the sea routes between ports are (in cells, 8-way
steps): Port Royal-Tortuga 64, St Augustine-Havana 119, Port
Royal-Cartagena 122, Tortuga-Curacao 138, Havana-Cartagena 230,
Portobelo-Havana 244, Barbados-Veracruz 576.  The targets for play are
3-5 days between neighbouring ports, 4-7 for short crossings, 12-20
for long ones and three to five weeks across the whole map
(aidd_docs/plans/open-game-data.md, G4): three times the original's
day meets them all but Tortuga-Curacao, a little over a week.
"""

ORIGINAL_DAY = 64
DAY_FACTOR = 3


def build():
    return ({"ticks_per_day": ORIGINAL_DAY * DAY_FACTOR},
            "Three of the original's days (%d ticks): at the top speed of "
            "6 cells per 64 ticks, the open map's sea routes take 3.6 days "
            "(Port Royal-Tortuga) to 32 (Barbados-Veracruz), within the "
            "voyage targets of open-game-data.md but Tortuga-Curacao at "
            "7.7 days." % (ORIGINAL_DAY * DAY_FACTOR))


TABLES = [("data/calendar", "calendar.json", build)]
