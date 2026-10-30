# Flibustier manual

The keys are the original game's, with a few additions marked
*(engine)*. Letter keys work in either case. The mouse works
everywhere: point and click to choose, and click to go on after a
message.

## Anywhere

These keys work on every screen, except while a screen fades in or out
and while you type your captain's name, where they are plain letters.

| Key | Action |
| --- | --- |
| P | Pause. The "Game Paused" plaque shows and the game and the music stop; press P again to go on. |
| Q | "Quit (Y/N)": Y closes the program, N (or Escape *(engine)*) goes on. |
| V | Sound level, in turn: music and effects, effects only, then neither. The digitised effects, such as the broadside, still play. |
| F2 | CRT look on or off *(engine)*. |

P and V are ignored while the "Quit (Y/N)" prompt is up, and Q and V
while the game is paused.

## Menus, boards and messages

| Key | Action |
| --- | --- |
| Arrows | Move through the choices. |
| Enter | Take the selected choice. |
| Escape | Back out, or go on. |
| Y / N | Answer a yes or no question. In French and German, the first letter of the language's "yes" (O, J) works too *(engine)*. |
| Any key | Go on after a message. After about seven seconds, a "Click to continue" plaque reminds you. |

In a town or a tavern, Space opens the captain's cabin.

## Sailing

| Key | Action |
| --- | --- |
| Left / Right | Steer. |
| T | Faster pace on or off. |
| Space | Open the captain's cabin: log book, maps, the map of the Caribbean, search, save, exit. Escape too *(engine)*. |

Sailing onto land outside a town asks "Disembark (Y/N)". Enter answers
yes and Escape no *(engine)*.

## On foot

| Key | Action |
| --- | --- |
| Arrows, numeric keypad | Walk, in eight directions with the keypad's corners. |
| S | Search for buried treasure right here, without opening the cabin. Three days pass. |
| T | Faster pace on or off. |
| Space | Open the captain's cabin. Escape too *(engine)*. |

## Captain's cabin

| Key | Action |
| --- | --- |
| Left / Right (or Up / Down) | Move through the cabin's choices. |
| Enter | Take the selected one. |
| Space, Escape | Go back to the voyage. |
| L | Change the language: English, French or German. |

## Sea battles

| Key | Action |
| --- | --- |
| Left / Right | Steer. |
| Enter, Space | Fire a broadside. |
| Up / Down | Full sails or battle sails. |

Sailing into the enemy boards her.

## Sword fights

| Key | Action |
| --- | --- |
| Left | Attack with a lunge. |
| Shift + Left | Attack with a slash. |
| Up / Down | Pick the line. |
| Right | Step back. |
| Numeric keypad | The same, with its corners as diagonals. |

With the mouse, click on the side of the screen you want to move to;
the right button slashes. Before the fight, Left and Right pick a
blade and Enter takes it.

## Land battles

| Key | Action |
| --- | --- |
| Arrows, numeric keypad | Move the selected unit. |
| Space, Enter, click | Select the next unit; held, every unit without a goal follows the arrows. |

## Difficulty

A new career asks for one of four difficulty levels, counted here from
the easiest (first) to the hardest (fourth). The harder the level:

- **Fights are harder.** Sea battles and sword fights are fought at the
  difficulty's level (named captains add to it). Your broadsides hit
  a smaller target, and at the first level they do half again as much
  damage. In a sword fight your blade must come closer to reach and
  lands less often, the opponent hesitates less when you close in, and
  the fight runs faster.
  A town's soldiers carry more muskets in land battles.
- **The crew is harder to please.** Its mood drops one step per level,
  and it sets how fast your guns reload and how well your men fight. In
  towns, fewer men sign on.
- **The sea is rougher.** Your ship sails a little slower at the third
  and fourth levels, and storm clouds catch you from farther away (not
  at all at the first level).
- **Ships are less easy to avoid.** A captain you try to hail or leave
  overtakes you and forces a battle one time in three per level above
  the first. At the first two levels the lookout also says whose waters
  you are in.
- **Villains hide farther.** A villain looks for his next hideout over
  a wider area.
- **Treasure maps come whole.** At the first level, any map you get is
  complete at once; above it, you gather it piece by piece.
- **But you earn more.** When the plunder is divided, you take more
  portions, and a bigger share of the gold, the higher the level.
- **Romance is easier.** A governor's daughter or niece is a little more
  interested in you.

At the bank, below the fourth level, dividing the plunder also offers to
raise the difficulty one level. A spell in prison, or being marooned,
lowers it by one.

## Special abilities

A new career also asks for one special ability. Each gives one small
edge, for the whole career.

| Ability | Edge |
| --- | --- |
| Fencing | Sword fights (boarding, duels, practice) are fought one level easier. |
| Wit and charm | A governor's daughter or niece is twice as interested in you, so she is likelier to give you news and to accept a proposal. A merchant's daughter will marry you with 200 gold instead of 400. |
| Navigation | Your ships sail faster at the third difficulty level, and at every level but the first a storm cloud has to be nearer to catch you. |
| Gunnery | Sea battles are fought one level easier: your broadsides hit a wider target. |
| Medicine | Your health wears down a quarter more slowly with the years and wounds, so you can sail longer before age ends your career. |

Fencing and gunnery do nothing at the first level against ordinary
opponents, who are already as weak as they get there; they still help
against named captains. At the first, second and fourth levels,
navigation gives no extra speed, only the storms' shorter reach (none at
the first level). Wit and charm and medicine work the same at every
level.

## Debug mode

Started with `--debug` (the original's `RDM` argument). At sea and on
foot:

| Key | Action |
| --- | --- |
| N | No more sails sighted. |
| E | Sails sighted again. |
| R | Near invulnerable. |
| F | Vulnerable again. |

A right click on the cabin's map of the Caribbean moves the ship there.

## Advice

These tips come from the career bot (`tools/career-bot`), which plays
whole careers through the rules engine. Where a tip gives figures, it
was measured: unless noted, two versions of the bot over the same 500
careers, the score being the one retiring would give. Tips without
figures are what the bot does, or follow from the rules. The bot is a
mediocre captain, so read them as tendencies, not guarantees.

### Fighting

- **Board, and do not shy from bigger ships.** Boarding decides most
  fights. A bot that only took on ships at most one class above its own
  stayed small; letting it fight up to three classes above raised the
  mean score from 19.0 to 22.9 (from 12.2 to 17.5 starting in 1600,
  often in a pinnace), with 42% more captures, even though it lost more
  battles.
- **Fire only when the broadside will hit.** Lead your target, beat to
  windward when she lies upwind, and bring a fleeing ship abeam when
  your guns are loaded: a hit slows her and turns her into the wind.
- **Do not run at the first hit.** A bot that fled after its first
  damage and never fired captured 446 ships in 48 careers; one that
  aimed its shots and ran only when badly damaged captured 3115, lost
  its own ship 19 times instead of 106, and doubled its points.
- **Attack a fort only with hull to spare,** tack in under battle
  sails, and do not go back to one that beat you.

### At sea

- **Mind the shallows.** Reefs hide in them. Most
  wrecks came from sailing into a dead end between shallows on steps
  that each looked safe: keep a way out. A lone ship lost on a reef
  leaves you marooned.
- **Land on open water joined to your sea** when you go ashore to dig,
  not in a lagoon behind a reef.
- **Feed your crew.** Check the food before a long voyage or a dig, and
  turn back for port when the crew starts to go hungry.

### Crew and money

- **Divide the plunder in any town you enter openly,** not only your
  own nation's, before the crew turns mutinous: over 144 careers the
  mean score went from 14.0 to 15.5 and mutinies halved.
- **Recruit only the men your gold keeps content.** The crew's mood
  rises with the gold per man and falls with each month since the last
  division, twice as fast with more than 32767 gold aboard.
- A rich Spanish town refuses its merchant and shipwright to a low
  Spanish rank, even to a Spaniard: buy food and repairs elsewhere
  until your rank rises.

### Treasure and villains

- **Dig up every map you hold:** the lost relative's first, then the
  lost treasure the relative tells of, then the pirate's map.
- **Hunt the villains** named after each promotion: beating one gives
  the next relative's map. Hunting them in towns at war with you too
  scored 0.7 more, for a few more careers ending in prison.

### Playing Spain

At peace with Spain there are few ships worth taking near Spanish
towns. Hunt the nations at war with Spain instead, off their towns, and
take their small ones: Spain's mean score rose from 9.1 to 11.3.

### Retiring

The points grow in jumps, so timing your retirement on their recent
pace retires too early. Retiring at the bank at the first division of
the plunder after about ten years scored best: a mean of 13.7, against
12.8 when playing on to twelve years.
