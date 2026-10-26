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

#define SIM_NEWS_MAX	256	/* most recent news kept in the state */
#define SIM_NO_TOWN	(-1)

/* News codes: the entries of the game's news list (CSTN group 20). */
enum news_code {
	NEWS_AMNESTY = 10,	/* a: nation */
	NEWS_INDIANS = 11,	/* a: town */
	NEWS_GOLD_MINE = 12,
	NEWS_MALARIA = 13,
	NEWS_GOVERNOR = 14,
	NEWS_PLUNDER = 15,
	NEWS_WAR = 16,		/* a, b: nations */
	NEWS_PEACE = 17,
	NEWS_ALLIANCE = 18,
	NEWS_CAPTURE = 19	/* a: nation, b: town */
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
	uint16_t	id;
	uint16_t	sea_x, sea_y;	/* fields 26, 27: water outside the
					 * harbour, 0 for inland towns */
};

struct sim_news {
	int32_t		day;
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
	int		gunnery;	/* player block +0x18, not identified;
					 * sets the reload time */
	int32_t		x, y;		/* world pixels */
	uint8_t		fx, fy;		/* 1/8 pixel remainders */
	uint8_t		heading;	/* 0 north, 0x40 east... */
	int		speed;		/* 4..12 once under way */
	uint32_t	ticks;		/* sailing ticks, 64 per day */
	int		leaving;	/* town just left, or -1 */
};

/* Why sim_sail() stopped. */
enum sail_stop {
	SAIL_TICKS,	/* all requested ticks done */
	SAIL_LAND,	/* the next move would run aground on land */
	SAIL_TOWN,	/* arrived at a town: sim_sail()'s "town" says which */
	SAIL_ENCOUNTER	/* a sail is sighted: see sim.encounter */
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
	BAT_DEFEATED		/* boarding: the player's men gave up */
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
	struct battle_ship you, foe;
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
	int		recruits;	/* men offering to sign on */
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

/* Entering the tavern: count the men who would sign on. */
void	sim_tavern_open(struct sim *s);
/* Sign them up (or not); the crew is then gone until next time. */
void	sim_recruit(struct sim *s, int accept);

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
