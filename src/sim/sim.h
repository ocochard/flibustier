/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef STMALO_SIM_H
#define STMALO_SIM_H

#include <stddef.h>
#include <stdint.h>

#include "assets/scenario.h"
#include "assets/towns.h"
#include "assets/world.h"
#include "rng.h"

/*
 * Simulation core: the whole game state and the rules that change it.
 * No I/O, no clock, no raylib: time only moves through sim_advance(), and
 * randomness only comes from the state's own generator, so a seed plus a
 * command log always replays to the same state.  The rules follow
 * aidd_docs/memory/internal/game-rules.md.
 */

#define SIM_MAPS	3	/* treasure maps */
#define SIM_VILLAINS	4
#define SIM_NEWS_MAX	256	/* most recent news kept in the state */
#define SIM_NO_TOWN	(-1)

/* News codes: the entries of the game's news list (CSTN group 20). */
enum news_code {
	NEWS_PLUNDERED = 3,	/* a: town, sacked by the player */
	NEWS_LOST_TREASURE = 7,	/* a: the civilisation (CSTN 93) */
	NEWS_TREASURE = 8,	/* buried treasure dug up */
	NEWS_VILLAIN = 9,	/* a: the villain, b: the town he is in */
	NEWS_AMNESTY = 10,	/* a: nation */
	NEWS_INDIANS = 11,	/* a: town */
	NEWS_GOLD_MINE = 12,
	NEWS_MALARIA = 13,
	NEWS_GOVERNOR = 14,
	NEWS_PLUNDER = 15,
	NEWS_WAR = 16,		/* a, b: nations */
	NEWS_PEACE = 17,
	NEWS_ALLIANCE = 18,
	NEWS_CAPTURE = 19,	/* a: nation, b: town */
	NEWS_TAKEN = 22,	/* a: town, b: nation it was taken for */
	NEWS_NOT_TAKEN = 23,	/* a: town */
	NEWS_RESCUED = 26,	/* a: the relative (CSTN 37) */
	NEWS_LETTER = 28,	/* a: the town to deliver to */
	NEWS_PAPERS = 29,
	NEWS_PIRATE_OFF = 30,	/* a: the pirate's id, b: the town */
	NEWS_RESCUE = 31,	/* a: the town holding the son */
	NEWS_DELIVERED_LETTER = 32,	/* a: the town */
	NEWS_DELIVERED_PAPERS = 33,
	NEWS_PIRATE_TAKEN = 34,	/* a: the pirate's id */
	NEWS_SON_FREED = 35,	/* a: the town */
	NEWS_DISPLEASED = 36	/* a mission failed */
};

struct sim_town {
	char		name[17];
	uint16_t	flags;
	uint16_t	x, y;
	uint16_t	nation;
	uint16_t	forts;
	uint16_t	soldiers;
	uint16_t	citizens;
	uint16_t	gold;
	uint16_t	wealth;
	uint16_t	standing;	/* field 9, reset by a new governor */
	uint16_t	governor;	/* field 11 */
	uint16_t	family;		/* field 12: 0x40 married into the
					 * merchant's family */
	uint16_t	id;		/* field 14: its battle screen */
	uint16_t	fort_x, fort_y;	/* fields 17, 18: the fort's cell on
					 * that screen */
	uint16_t	ship_x, ship_y;	/* fields 19, 20: the attacker's */
	uint16_t	ship_heading;	/* field 21 */
	uint16_t	sea_heading;	/* field 28: leaving, x 32 */
	uint16_t	barmaid;	/* field 13: her fondness, 0..11 */
	uint16_t	garrison;	/* field 10: more defenders, x 128 */
	uint16_t	land_x, land_y;	/* fields 15, 16: its land battle cell */
	uint16_t	party_x, party_y;	/* fields 22, 23: the attackers' */
	uint16_t	sea_x, sea_y;	/* fields 26, 27: water outside the
					 * harbour, 0 for inland towns */
};

struct sim_news {
	int32_t		day;
	int32_t		date;		/* year x 360 + day of the year */
	int		code;		/* enum news_code */
	int		a, b;
};

/* Special abilities, in the order of the game's list (CSTN group 83). */
enum ability {
	ABILITY_FENCING,
	ABILITY_CHARM,
	ABILITY_NAVIGATION,
	ABILITY_GUNNERY,
	ABILITY_MEDICINE
};

/* Who the player is: set by the start screens, kept in saves. */
struct sim_player {
	char		name[41];	/* UTF-8 */
	char		title[24];	/* start scenario, "English Buccaneer" */
	int		nation;		/* town nation order */
	int		difficulty;	/* 0..3 */
	int		ability;	/* 0..4 */
	int		expedition;	/* -1, or famous expedition 0..5 */
};

/* The player's fleet and its flagship's position at sea. */
struct sim_fleet {
	int		nships;
	uint8_t		ships[SCENARIO_SHIPS];	/* types, 0 = pinnace */
	uint8_t		damage[SCENARIO_SHIPS];	/* hull damage per ship */
	int		crew;
	int		guns;
	long		gold;
	int		food, goods, sugar;	/* cargo, tons */
	int		capacity;	/* hold, tens of tons: 2 x class + 2
					 * per ship */
	int		mood;		/* the crew's (VA 0x45faa): see
					 * sim_crew_mood */
	int32_t		x, y;		/* world pixels */
	uint8_t		fx, fy;		/* 1/8 pixel remainders */
	uint8_t		heading;	/* 0 north, 0x40 east... */
	int		speed;		/* 4..12 once under way */
	uint32_t	ticks;		/* sailing ticks, 64 per day */
	int		leaving;	/* town just left, or -1 */
	/* A landing party (VA 0x231b8): on foot at x, y while the ship
	 * waits at ship_x, ship_y; land_x, land_y where it went ashore
	 * (or, at sea, the land the ship last ran into). */
	int		ashore;
	int32_t		ship_x, ship_y;
	int32_t		land_x, land_y;
};

/* Why sim_sail() stopped. */
enum sail_stop {
	SAIL_TICKS,	/* all requested ticks done */
	SAIL_LAND,	/* the next move would run aground on land */
	SAIL_TOWN,	/* arrived at a town: sim_sail()'s "town" says which */
	SAIL_ENCOUNTER,	/* a sail is sighted: see sim.encounter */
	SAIL_BOARDED,	/* the landing party is back aboard */
	SAIL_CREW	/* a month passed and the crew speaks: crew_event */
};

/* What the crew says when a month passes at sea (VA 0x1dd0e). */
enum crew_event {
	CREW_NONE,
	CREW_RESTLESS,	/* mood below 0: "...they'll mutiny!" (CSTR 140) */
	CREW_ANXIOUS,	/* the same after 12 months: "...dividin' up the
			 * plunder" (CSTR 210) */
	CREW_MUTINY	/* below -3: a third leave, their leader duels the
			 * player (CSTR 139) */
};

#define NATION_PIRATE	4	/* in encounter records */

/* Steps of an encounter, as the dialogs reveal the other ship. */
enum encounter_stage {
	ENC_NONE,
	ENC_SIGHTED,	/* "Sail ho!" */
	ENC_IDENTIFIED,	/* her type is known */
	ENC_COLOURS	/* her nation is known */
};

enum encounter_action {
	ACT_INVESTIGATE,
	ACT_IGNORE,	/* continue the voyage, or sail away */
	ACT_BATTLE,	/* close for battle */
	ACT_HAIL	/* hail for news */
};

enum encounter_result {
	RES_CONTINUE,	/* the next dialog: see sim.encounter.stage */
	RES_ESCAPED,	/* the encounter ends without a fight */
	RES_NEWS,	/* hailed; the encounter ends */
	RES_BATTLE,	/* the player closes for battle */
	RES_OVERTAKEN,	/* she catches the fleeing player: battle */
	RES_UNANSWERED	/* hail refused: battle */
};

/* How a sea battle ended. */
enum battle_outcome {
	BAT_NONE,		/* still fighting */
	BAT_BOARDING,		/* the ships crashed together */
	BAT_ESCAPED,		/* out of range of each other */
	BAT_SUNSET,		/* the time limit */
	BAT_SURRENDER,		/* the enemy struck her colours */
	BAT_PLAYER_SUNK,
	BAT_ENEMY_SUNK,
	BAT_REPELLED,		/* boarding: the player was driven back */
	BAT_DEFEATED,		/* boarding: the player's men gave up */
	BAT_BEACHED,		/* the player ran onto land (CSTR 173) */
	BAT_LANDED		/* landed by the fort: the assault */
};

/* Sword fights: who fights, and how it ended. */
enum sword_kind {
	SWORD_CREW = 0,		/* boarding or storming */
	SWORD_DUEL = 1,
	SWORD_PRACTICE = -2,
	SWORD_STORY = -1	/* the career story's: no choice, no stakes */
};

enum sword_result {
	SWORD_NONE = 0,
	SWORD_WON = 1,
	SWORD_REPELLED = 2,	/* the player was driven off his edge */
	SWORD_LOST = 3
};

/* Moves (rows of the game's tables). */
#define SWM_GUARD	0	/* 0-2: guard middle, low, high */
#define SWM_BACK	3	/* 3-5: the same, stepping back */
#define SWM_SLASH	6	/* 6-8: slash middle, low, high */
#define SWM_LUNGE	9	/* 9-11: lunge middle, low, high */
#define SWM_ADVANCE	12
#define SWM_RETREAT	13
#define SWM_FALL	14

struct sword_side {
	int		x;		/* on the fighting line, pixels */
	int		move, frame;	/* move -1: none yet */
	int		men;
	int		bar;		/* strength bar share */
	int		weapon;		/* 0 rapier, 1 longsword, 2 cutlass */
	int		guard;		/* lines the current frame covers */
	int		wounds[3];	/* hits taken: high, middle, low */
};

struct sim_sword {
	int		active;
	int		kind;		/* enum sword_kind */
	int		level;		/* 0..4 */
	int		title, nation;	/* the opponent's, CSTN 74 and 71 */
	uint32_t	counter;	/* fight ticks, from 1 */
	int		result;		/* enum sword_result */
	int		beaten;		/* 1 the opponent, 3 the player */
	int		input;		/* the player's pending move, 0-11 */
	int		script, step;	/* the opponent's plan */
	int		you_hits, foe_hits;	/* since the plan changed */
	int		men_start;	/* the player's men at the start */
	int		scene;		/* 0 alley, 1-2 fort, 3-4 deck */
	struct sword_side you, foe;
};

/* What a caller sets up for a fight. */
struct sword_setup {
	int		kind;		/* enum sword_kind */
	int		you_men, you_factor;
	int		foe_men, foe_factor;
	int		title, nation, extra;
	int		weapon;		/* the player's choice */
};

/* Events of one sword fight tick, as bits. */
#define SEV_HIT_FOE	0x01
#define SEV_HIT_YOU	0x02
#define SEV_CLASH	0x04
#define SEV_FOE_MEN	0x08	/* the opponent's crew lost men */
#define SEV_YOU_MEN	0x10

/* Events of one battle tick, as bits. */
#define BEV_FIRED	0x01	/* the player fired */
#define BEV_FOE_FIRED	0x02
#define BEV_HIT_FOE	0x04
#define BEV_HIT_YOU	0x08
#define BEV_RELOADED	0x10	/* the player's guns are ready again */
#define BEV_SAILS	0x20	/* the player's sails changed */
#define BEV_WIND	0x40	/* the wind changed */
#define BEV_AGROUND	0x80	/* the player's ship scraped the shallows */
#define BEV_FOE_AGROUND	0x100
#define BEV_SCROLLED	0x200	/* the field moved on */

struct battle_shot {
	int		ticks;		/* flight ticks left; 0: none */
	int32_t		x, y;		/* 1/256 pixels */
	uint8_t		heading;
};

struct battle_ship {
	int32_t		x, y;		/* battle field, 1/256 pixels */
	uint8_t		heading;
	uint8_t		target;		/* heading the enemy steers for */
	int		speed;
	int		type;		/* 0 pinnace .. 8 fast galleon */
	int		guns, crew, damage;
	int		full_sails;	/* else battle sails */
	int		sail_timer;	/* ticks until the sails change */
	int		reload;		/* ticks until the guns are ready */
	int		turn_rate;	/* 3 small .. 1 big */
	int		role;		/* the enemy's, for its decisions */
	struct battle_shot shot;
};

struct sim_battle {
	int		active;
	uint32_t	tick;
	uint8_t		wind;		/* direction it blows toward */
	int		wind_strength;	/* 0..2 */
	int		decision;	/* the enemy's: 0 close, 1 broadside,
					 * 2 flee */
	int		outcome;	/* enum battle_outcome */
	int		crew_ashore;	/* crew and guns left with the fleet */
	int		guns_ashore;
	int		fort;		/* against a town's fort */
	int		fort_damage;	/* hits on the fort */
	int		set;		/* battle screen set (VA 0x325ac) */
	int		piece;		/* the set's screen first shown */
	int		scroll_x;	/* screens scrolled (VA 0x325a0) */
	int		scroll_y;
	int		screen;		/* SBTL screen shown */
	int		you_scrape;	/* ticks before the shallows can hurt */
	int		foe_scrape;
	struct battle_ship you, foe;
};

/* Land battles (VA 0x5a80): units 1-3 the player's, 4-7 the town's. */
#define LAND_UNITS	8
#define LAND_PLAYER	1	/* unit types (flags & 7) */
#define LAND_SOLDIERS	2
#define LAND_FORT	3
#define LAND_HORSE	4
#define LAND_EXPOSED	0x80	/* off cover, or just engaged */
struct land_unit {
	int		x, y;		/* field pixels */
	int		men;
	int		flags;		/* type, merge target << 4, exposed */
	int		muskets;
	int		morale;		/* 0..79 */
	int		tx, ty;		/* where it heads; tx 0: nowhere */
};
struct sim_land {
	int		active;
	int		town;
	int		screen;		/* LBTL screen: town field 14 */
	uint32_t	pass;
	int		idle;		/* passes since the last fire */
	int		selected;	/* the player's unit (1-3) */
	int		shown;		/* the enemy unit on the panel */
	int		fort;		/* the fort's unit, or 0 */
	int		rally_x, rally_y, rally_unit;
	int		ahead;		/* the town outnumbers the open party */
	int		held;		/* the button is held */
	int		result;		/* 0, 2 won, 3 lost */
	struct land_unit u[LAND_UNITS];
};

struct sim_encounter {
	int		stage;		/* enum encounter_stage */
	int		type;		/* 0 pinnace .. 8 fast galleon */
	int		nation;		/* 0..3, or NATION_PIRATE */
	int		crew;
	int		guns;
	int		role;
	int		cargo, gold;	/* record bytes 5 and 6, not decoded */
	int		hostile;	/* she chases the player */
	int		town;		/* nearest town when sighted */
	int		named;		/* the pirate a mission hunts */
};

struct sim {
	struct rng	rng;
	struct sim_player player;
	struct sim_fleet fleet;
	uint8_t		wind;		/* direction it blows toward */
	int		encounters;	/* 1: sails may be sighted (0 for tests) */
	int		heat;		/* lingering counter (VA 0x4c09e) */
	int		heat_town;	/* nearest town it counts for */
	struct sim_encounter encounter;
	struct sim_battle battle;
	struct sim_land	land;
	struct sim_sword sword;
	int		practice;	/* fencing practice, 0..30 */
	const struct world *world;	/* not saved; NULL: no sailing */
	int		era_year;	/* era of the towns, 1560..1680 */
	int		year;		/* calendar year */
	int		day_of_year;	/* 0..359: twelve 30-day months */
	int		month;		/* month passed to world updates */
	int32_t		day;		/* days since the start, for news */
	int		diplomacy_range; /* unknown original value */
	int		player_town;	/* town the player is in, or -1 */
	int		visit_town;	/* town of the visit flags, or -1 */
	int		visit_flags;	/* VISIT_* for the current visit */
	int		wright_gold;	/* the shipwright's, -1 until known */
	int		merchant_gold;	/* the merchant's, -1 until known */
	int		wright_cannon;	/* guns the shipwright holds */
	int		reputation;	/* -128..127: captures, failed boardings */
	unsigned	tavern_spots;	/* people still there, bit per spot */
	int		recruits;
	int		news_town;	/* the traveller's town, and price */
	int		news_price;
	int		barmaid_mood;	/* rand(6) on meeting her */
	/* Treasure maps (VA 0x47186): the pirate's, the lost relative's,
	 * the lost treasure's. */
	struct sim_map {
		uint16_t	pieces;	/* MAP_* quarters, MAP_SHOWN */
		int16_t		x, y;	/* the spot, in map cells */
		int16_t		value;	/* the lost treasure's people */
		int16_t		town;	/* the town it is near */
	} maps[SIM_MAPS];
	int		relative;	/* the next lost relative, 0..4 */
	int		months;		/* since the plunder was divided (VA
					 * 0x45e79) */
	int		crew_event;	/* enum crew_event, until handled */
	int		deserted;	/* men who left on leaving town */
	/* The villains who know where the relatives are (VA 0x471da). */
	struct sim_villain {
		int	active;
		int	surname;	/* CSTN 2 + nation, 0..15 */
		int	rank;		/* CSTN 78, 0..7 */
		int	nation;		/* CSTN 97: Spaniard... */
		int	town;
	} villains[SIM_VILLAINS];
	int		villain_duel;	/* the villain fought, or -1 */
	/* The governor's mission (VA 0x471aa): a type, negative once
	 * done; the giving town and nation, the destination, the pirate
	 * hunted; "offer" is the one proposed. */
	struct sim_mission {
		int	type;		/* enum mission_type, -type when done */
		int	giver, dest, nation;
		int	pirate;		/* a captain id (VA 0x19fbb) */
	} mission, mission_offer;
	struct sim_market {
		int	open;
		int	stock[3];	/* food, goods, sugar */
		int	price[3];
		long	gold_at_open;	/* the player's */
	} market;
	int8_t		relations[4][4];
	uint8_t		rank[5];	/* title per nation: CSTN 78 */
	int		acres;		/* land granted by governors */
	long		fortune;	/* the player's own, from divided plunder */
	int8_t		standing[5];	/* player standing per nation, and
					 * pirates (always -1 so far) */
	size_t		ntowns;
	struct sim_town	towns[TOWN_MAX];
	size_t		nnews;		/* total logged, may exceed the ring */
	struct sim_news	news[SIM_NEWS_MAX];
};

/* Placeholder until the original's value is traced. */
#define SIM_DIPLOMACY_RANGE	100

/* A town field an older state lacks, until sim_restore_towns. */
#define TOWN_UNSET	0xFFFF

/* Fills the town fields an older state lacked from the era's table. */
void	sim_restore_towns(struct sim *s, const struct era_towns *era);
void	sim_new_game(struct sim *s, const struct era_towns *era,
	    const struct scenario *scn, uint64_t seed);

/*
 * Let "days" pass: one world update per 30-day month boundary, and the
 * year turns every 360 days.
 */
void	sim_advance(struct sim *s, int32_t days);

/*
 * Sail for up to "ticks" ticks with a turn input of -1, 0 or +1, stopping
 * early at land or a town.  Days pass every 64 ticks.  Needs s->world.
 * Returns why it stopped; "town" receives the town index on SAIL_TOWN.
 */
enum sail_stop sim_sail(struct sim *s, int turn, int ticks, int *town);

/*
 * Answer the current encounter's dialog.  A battle result starts a sea
 * battle (see sim.battle).
 */
enum encounter_result sim_encounter_act(struct sim *s,
	    enum encounter_action action);

/* Visit flags, per stay in a town (the game's VA 0x45f0a). */
#define VISIT_GOVERNOR	0x01	/* seen the governor this stay */
#define VISIT_KEPT	0x02	/* kept on a return; not identified */
#define VISIT_SNEAKED	0x08	/* came in by stealth: no bank */

/*
 * Landing parties.  After SAIL_LAND the player is asked to go ashore
 * (VA 0x315d): yes puts the party on the land the ship ran into, the
 * ship waiting where it stood; no turns the ship about.  On foot the
 * party walks dx, dy (-1..1) half a pixel a tick; on water within 11
 * pixels of where it landed it boards again, elsewhere it is stopped.
 * Returns SAIL_TICKS, SAIL_TOWN (*town) or SAIL_BOARDED.
 */
void		sim_go_ashore(struct sim *s);
void		sim_turn_back(struct sim *s);
enum sail_stop	sim_march(struct sim *s, int dx, int dy, int ticks, int *town);

/* Enter a town (arriving or returning to it), and leave it. */
void	sim_enter_town(struct sim *s, int town);
/* At the arrival menu (VA 0x18fa9): the stay's memory of the town is
 * kept for the same town, forgotten for another. */
void	sim_arrive(struct sim *s, int town);

/* Lose fleet ship "i" (VA 0x1f2a1); "keep": its cargo stays with the
 * fleet, otherwise its share of it is lost. */
void	sim_drop_ship(struct sim *s, int i, int keep);

/* The ways into a town from the arrival menu. */
enum arrival {
	ARRIVE_ENTER,		/* in */
	ARRIVE_FIRED,		/* the fort drives the fleet off (CSTR 116) */
	ARRIVE_SUNK,		/* ... and sinks a ship (CSTR 117) */
	ARRIVE_CAUGHT		/* a guard recognizes the player (CSTR 163) */
};
/* Sail into the harbour; a sunk ship takes its share of the cargo. */
enum arrival	sim_sail_in(struct sim *s, int town);
/* Sneak into town; when caught, a duel with a guard follows. */
enum arrival	sim_sneak_in(struct sim *s, int town);
/* The duel with the town's guard (VA 0x161ba), with the player's
 * weapon. */
void	sim_guard_duel(struct sim *s, int town, int weapon);
/* After a duel: a loss or a retreat disgraces the player (CSTR 174,
 * reputation -1). Returns 1 when disgraced. */
int	sim_duel_end(struct sim *s);
void	sim_leave_town(struct sim *s);

/* The shipwright (VA 0x218c2). */
enum repair_result { REPAIR_DONE, REPAIR_NOTHING, REPAIR_TOO_DEAR };
enum sell_result { SELL_DONE, SELL_NOTHING, SELL_ONLY_SHIP, SELL_SHORT };

/* "We do not trade with pirates!" */
int	sim_shipwright_refuses(const struct sim *s);
/* The shipwright's gold this stay. */
int	sim_shipwright_gold(struct sim *s);
/* Ship "i": the cost of its repair, and its price. */
int	sim_repair_cost(const struct sim *s, int i);
int	sim_ship_price(const struct sim *s, int i);
/* Sell (n < 0) or buy back (n > 0) cannon at the shipwright, 50 gold
 * each, one at a time while the rules allow (VA 0x22c76); returns the
 * guns traded. */
#define CANNON_PRICE	50
int	sim_trade_cannon(struct sim *s, int n);
/* Repair the ships in "mask" (bit i: ship i), paying for them. */
int	sim_repair(struct sim *s, unsigned mask);
/* Sell the ships in "mask", in fleet order, while his gold lasts; one
 * ship always stays. */
int	sim_sell_ships(struct sim *s, unsigned mask);

/* The merchant (VA 0x21e56).  Goods: 0 food, 1 goods, 2 sugar. */
#define GOODS		3

/* Free space in the hold, in tons; guns take half a ton. */
int	sim_hold_space(const struct sim *s);
int	sim_merchant_refuses(const struct sim *s);
/* The greeting, a CSTR number, by the player's gold. */
int	sim_merchant_greeting(const struct sim *s);
/* Open the shop: stock and prices are rolled for this visit. */
void	sim_merchant_open(struct sim *s);
/* Buy (n > 0) or sell (n < 0) units of a good, one at a time while
 * the rules allow; returns the units traded. */
int	sim_trade(struct sim *s, int good, int n);
/* Close the shop: trading moves the town's wealth. */
void	sim_merchant_close(struct sim *s);

/* The tavern (VA 0x11867): ten spots, all back on each arrival. */
#define TAVERN_SPOTS	10
#define TAVERN_ALL	0x3FFu
#define TAVERN_CREW	((1u << 2) | (1u << 7) | (1u << 8) | (1u << 9))
#define TAVERN_MAPS	(1u << 0)	/* the map seller */
#define TAVERN_NEWS	(1u << 1)	/* the traveller */
#define TAVERN_MAID	(1u << 4)
#define TAVERN_MYSTERY	(1u << 5)
#define MAP_PRICE	50	/* the map seller's text says 500 */

/*
 * Treasure maps.  sim_map_piece (VA 0x1f08a) gives a quarter of map
 * "map": the first piece draws the spot, one of the world's 256, kept
 * away from the town the player is in and the other maps' towns; on the
 * easiest level the whole map comes at once.  sim_search (VA 0xa801),
 * ashore, takes 3 days and digs at every map's spot within a cell, held
 * pieces or not; it fills "found" (one per map dug up) and returns how
 * many.
 */
enum { MAP_PIRATE, MAP_RELATIVE, MAP_LOST };
#define MAP_WHOLE	0x0F	/* the four quarters: TL, TR, BL, BR */
#define MAP_SHOWN	0x10
struct sim_find {
	int	map;
	long	gold;
	int	value;		/* the relative, or the treasure's people */
};
void	sim_map_piece(struct sim *s, int map);
int	sim_search(struct sim *s, struct sim_find found[SIM_MAPS]);

/* Entering the tavern: count the men who would sign on. */
void	sim_tavern_open(struct sim *s);
/* Sign them up (or not); the crew is then gone until next time. */
void	sim_recruit(struct sim *s, int accept);
/* The map seller (VA 0x112ef): a piece of a treasure map for 50 gold;
 * 0 when the player cannot pay. */
int	sim_buy_map(struct sim *s);
/* The traveller (VA 0x1116f): news of his town for his price; returns
 * the town now known, or -1 when the player cannot pay. */
int	sim_buy_news(struct sim *s);
/* The barmaid (VA 0x114d4).  sim_barmaid_meet: 0 she offers a drink
 * (CSTR 301), 1 she asks what the player wants (CSTR 302), 2 she
 * reports (CSTR 305-308). */
enum barmaid_answer { BARMAID_NOTHING, BARMAID_DRINK, BARMAID_COMPANY };
int	sim_barmaid_meet(struct sim *s);
/* A drink (1 gold) or company (5 gold, fondness +1): returns 1 when
 * her heart is won (fondness 10), 0 otherwise, -1 without the gold. */
int	sim_barmaid(struct sim *s, int answer);
/* Her report: 0 nothing, 1 news of a town (*town), 2 a map piece. */
int	sim_barmaid_report(struct sim *s, int *town);

/* The governor (VA 0x13269). */
enum governor_offer {
	OFFER_NONE,		/* standing -4 or lower: nothing */
	OFFER_MARQUE,		/* a Letter of Marque (CSTR 147) */
	OFFER_PARDON,		/* forgiveness (CSTR 148) */
	OFFER_DAUGHTER		/* standing 2 or more: his daughter */
};

/* A blow against a nation (VA 0x1eaf5): "d" to its standing, and
 * through its alliances and wars to the others'. */
void	sim_attack_nation(struct sim *s, int nation, int d);
/* Promote the player if his standing earns it (VA 0x12549); returns the
 * new rank or 0, with the acres granted in "*acres". */
int	sim_governor_promote(struct sim *s, int *acres);
/* What the governor offers next, and for how much gold. */
enum governor_offer sim_governor_offer(const struct sim *s, long *price);
/* Pay for the Letter of Marque or the pardon; 0 if short of gold. */
int	sim_governor_pay(struct sim *s);

/*
 * The crew's mood (VA 0x1da9b): (int)(sqrt(gold / 10 / crew) x 2) -
 * months + 4 - difficulty, recomputed every month; a capture raises it
 * by 1, 2 for a warship.  sim_crew_leave: "n" men go with their share of
 * the gold (VA 0x1f044).  The mutineers' leader duels the player
 * (sim_mutiny_duel); afterwards sim_mutiny_end settles the duel and
 * lowers the reputation by 4.
 */
int	sim_crew_mood(const struct sim *s);
void	sim_crew_leave(struct sim *s, int n);
void	sim_mutiny_duel(struct sim *s, int weapon);
void	sim_mutiny_end(struct sim *s);

/*
 * The relatives' villains.  After a promotion, while a relative is
 * still lost, the governor names a new villain hiding in a Spanish town
 * (VA 0x12178): sim_villain_appear returns his slot, or -1.  Met in his
 * town (VA 0x18913: the tavern's mystery man, the governor's mansion, a
 * sacked town), he flees to a random town and duels the player
 * (sim_villain_duel); beaten, he gives a piece of the relative's map
 * (sim_villain_duel_end returns 1; a loss disgraces as any duel).
 */
int	sim_villain_appear(struct sim *s);
int	sim_villain_at(const struct sim *s, int town);
void	sim_villain_duel(struct sim *s, int villain, int weapon);
int	sim_villain_duel_end(struct sim *s);

/*
 * Governors' missions (VA 0x12e88).  At the giver's governor,
 * sim_mission_report ends a mission: 1 fulfilled, 2 the pirate brought
 * in (CSTR 133), -1 failed (CSTR 199), 0 none to report.  Otherwise
 * sim_mission_draw proposes one (mission_offer; returns its type, or 0)
 * and sim_mission_accept takes it.  sim_mission_done (VA 0x1f8f7)
 * fulfils it: a letter or papers handed to the mystery man in the
 * destination's tavern, the son freed in its sack, the pirate captured.
 */
enum mission_type { MISSION_NONE, MISSION_LETTER, MISSION_PAPERS,
    MISSION_PIRATE, MISSION_SON };
int	sim_mission_report(struct sim *s);
int	sim_mission_draw(struct sim *s);
void	sim_mission_accept(struct sim *s);
void	sim_mission_done(struct sim *s);
/* The mission's contact is in this town's tavern (VA 0x1108c). */
int	sim_mission_contact(const struct sim *s, int town);

/* The bank (VA 0x180c8): dividing the plunder ends an expedition. */
struct plunder {
	int	share;		/* each crew member's gold */
	int	mood;		/* 0 angry .. 3 happy (CSTN 72) */
	long	yours;		/* the captain's portions */
	int	portions;	/* how many: 2 x difficulty + 2 */
};
void	sim_divide_plunder(struct sim *s, struct plunder *out);
/* Plan another expedition (VA 0x1ef87): the flagship alone, a tenth of
 * the gold, a new crew; "advance" raises the difficulty first. */
void	sim_new_expedition(struct sim *s, int advance);

/* Start a sea battle against the current encounter's ship. */
void	sim_battle_start(struct sim *s);
/* Attack the fort of the town the fleet has reached (VA 0x18b3f, from
 * the sea): a battle on the town's own screen. */
void	sim_fort_battle_start(struct sim *s, int town);
/* With several ships, the one to command (VA 0xf246), before the first
 * tick: it swaps into the flagship's place. */
void	sim_battle_command(struct sim *s, int ship);

/*
 * One battle tick: turn -1, 0 or +1; fire when nonzero; sails -1 for
 * battle sails, +1 for full sails, 0 to keep them.  Returns BEV_* bits;
 * sim.battle.outcome tells when it is over.
 */
int	sim_battle_tick(struct sim *s, int turn, int fire, int sails);

/* Start a sword fight; the opponent's weapon and plan are rolled. */
void	sim_sword_start(struct sim *s, const struct sword_setup *setup);

/*
 * The player's next move (0-11, SWM_*), kept until a move takes it.
 * Guards and attacks take it once; steps back repeat until replaced.
 */
void	sim_sword_input(struct sim *s, int move);

/* One fight tick.  Returns SEV_* bits; sim.sword.result ends it. */
int	sim_sword_tick(struct sim *s);

/* Fencing practice from the cabin (VA 0xbdae): 200 men of factor 32 a
 * side against the first mate, then the practice value rises by 5, to
 * at most 30. */
void	sim_practice_start(struct sim *s, int weapon);
void	sim_practice_end(struct sim *s);

/*
 * The assault on a town (VA 0x18b3f): the crew, but for "aboard" men
 * left with the fleet, against the garrison, soldiers x 10 + 5 men; the
 * scene is the fort, or the alley in a town without forts.  After a
 * fort battle, the garrison was first set from it.
 */
void	sim_assault_start(struct sim *s, int town, int aboard, int weapon);
/* The garrison after the fort battle (VA 0x18c2c). */
void	sim_fort_battle_garrison(struct sim *s, int town);
/* After the assault: 1 won (the sack follows), 0 repelled, -1 defeated
 * (prison).  A failure costs reputation and standing. */
int	sim_assault_end(struct sim *s, int town);

/* The sack (VA 0x22764). */
struct sack {
	long	gold;		/* found, already counted in tens */
	int	hidden;		/* -1, or CSTN 15: some, much, most hidden */
	int	goods[3];	/* food, goods, sugar to take for nothing */
	int	cannon;		/* forts x 6 */
	int	conquest;	/* 1: the player may install a governor */
	int	son_freed;	/* a governor's son was held there */
};
void	sim_sack(struct sim *s, int town, struct sack *out);
/*
 * The land battle before an assault on foot (VA 0x18c90): when the crew
 * is at most soldiers x 15, the town has forts and more than 9 soldiers.
 */
int	sim_land_battle_needed(const struct sim *s, int town);
void	sim_land_start(struct sim *s, int town);
/* A pass: dx, dy (-1..1) move the selected unit; "button" held moves
 * every unit without a goal, pressed anew selects the next.  Returns
 * LEV_* bits; land.result ends it. */
#define LEV_FIRE	0x01
#define LEV_SELECTED	0x02
int	sim_land_tick(struct sim *s, int dx, int dy, int button);
/* After it: the crew and the garrison as they stand; 1 won. */
int	sim_land_end(struct sim *s);
/* The log's "Failed to capture %s" (CSTN 20.23). */
void	sim_log_failure(struct sim *s, int town);

/* The plunder's goods on the trading board, for nothing; and done. */
void	sim_plunder_open(struct sim *s, const int goods[3]);
/* "Take all" (VA 0x20cd9): while the hold has room, a unit of the
 * selected good, or else of the first one left, bought while its price
 * is below the gold (the selected good's price is the one checked).
 * Returns the units taken. */
int	sim_take_all(struct sim *s, int good);
void	sim_plunder_close(struct sim *s);
/* Install a governor of "nation" in a sacked town (CSTR 194). */
void	sim_install_governor(struct sim *s, int town, int nation);

/* Board the enemy after BAT_BOARDING, with the player's weapon. */
void	sim_board(struct sim *s, int weapon);

/*
 * Apply a finished battle to the fleet and close it.  "keep" decides
 * whether a surrendered ship joins the fleet.  A boarding needs its sword
 * fight first (sim_board); until it is over, nothing is applied and
 * BAT_NONE is returned.  Returns the outcome.
 */
enum battle_outcome sim_battle_finish(struct sim *s, int keep);

/* One world update; exposed for tests. */
void	sim_world_update(struct sim *s);

/* News number i (0 = oldest still kept), or NULL. */
const struct sim_news *sim_news_at(const struct sim *s, size_t i);
size_t	sim_news_kept(const struct sim *s);

#endif
