/* Smart Trainers: the decision core.
 *
 * Portable C, no eidos and no libc beyond what freestanding wasm has. An
 * adapter (the eidos mod, or the Showdown bench) fills an StView with what
 * the trainer may know: its own team in full, and of the player's Pokemon
 * only what a player at the screen could see (species, level, types, the
 * HP bar, stat stages, status, moves and abilities shown). It feeds the
 * core what it observes (st_observe_*), then asks for a choice.
 *
 * Every field is an int32_t so the bench can fill the struct from
 * JavaScript as a flat Int32Array.
 */
#ifndef ST_CORE_H
#define ST_CORE_H

#include <stdint.h>

/* Types, in the game's order. */
enum {
    ST_NORMAL, ST_FIGHTING, ST_FLYING, ST_POISON, ST_GROUND, ST_ROCK, ST_BUG,
    ST_GHOST, ST_STEEL, ST_FIRE, ST_WATER, ST_GRASS, ST_ELECTRIC, ST_PSYCHIC,
    ST_ICE, ST_DRAGON, ST_DARK, ST_NTYPES
};

/* Move categories. */
enum { ST_PHYSICAL, ST_SPECIAL, ST_STATUS };

/* What a status move (or a damaging move's main side effect) does. */
enum {
    ST_EFF_NONE,
    ST_EFF_BOOST,      /* raises the user's stats: eff_stat mask, eff_stages */
    ST_EFF_DROP,       /* lowers the target's stats: eff_stat mask, eff_stages (positive count) */
    ST_EFF_STATUS,     /* gives the target eff_status */
    ST_EFF_HEAL,       /* heals the user heal % of its max HP */
    ST_EFF_PROTECT,
    ST_EFF_OTHER
};

/* Stats and stages: index order. */
enum { ST_HP, ST_ATK, ST_DEF, ST_SPA, ST_SPD, ST_SPE };
enum { ST_S_ATK, ST_S_DEF, ST_S_SPA, ST_S_SPD, ST_S_SPE, ST_S_ACC, ST_S_EVA, ST_NSTAGES };

/* Major status. */
enum { ST_ST_NONE, ST_ST_SLEEP, ST_ST_POISON, ST_ST_BURN, ST_ST_FREEZE, ST_ST_PARALYSIS, ST_ST_TOXIC };

/* Weather. */
enum { ST_W_NONE, ST_W_SUN, ST_W_RAIN, ST_W_SAND, ST_W_HAIL };

/* Abilities and items the core understands, by the game's ids. */
#define ST_AB_STURDY 5
#define ST_AB_VOLT_ABSORB 10
#define ST_AB_WATER_ABSORB 11
#define ST_AB_FLASH_FIRE 18
#define ST_AB_INTIMIDATE 22
#define ST_AB_WONDER_GUARD 25
#define ST_AB_LEVITATE 26
#define ST_AB_CLEAR_BODY 29
#define ST_AB_LIGHTNING_ROD 31
#define ST_AB_HUGE_POWER 37
#define ST_AB_THICK_FAT 47
#define ST_AB_HYPER_CUTTER 52
#define ST_AB_WHITE_SMOKE 73
#define ST_AB_PURE_POWER 74
#define ST_AB_MOTOR_DRIVE 78
#define ST_AB_SIMPLE 86
#define ST_AB_DRY_SKIN 87
#define ST_AB_UNAWARE 109
#define ST_AB_STORM_DRAIN 114
#define ST_AB_CONTRARY 126
#define ST_AB_DEFIANT 128
#define ST_AB_SAP_SIPPER 157
#define ST_AB_MAGIC_BOUNCE 156
#define ST_AB_COMPETITIVE 172

#define ST_IT_CHOICE_BAND 220
#define ST_IT_LEFTOVERS 234
#define ST_IT_LIFE_ORB 270
#define ST_IT_FOCUS_SASH 275
#define ST_IT_CHOICE_SCARF 287
#define ST_IT_CHOICE_SPECS 297

typedef struct StMove {
    int32_t id;          /* the game's move id, 0 for none */
    int32_t type, cat, power, acc;   /* acc 0 never misses */
    int32_t prio;
    int32_t pp;          /* own moves: PP left; 0 means it cannot be used */
    int32_t eff, eff_stat, eff_stages, eff_status, eff_chance;
    int32_t heal;        /* ST_EFF_HEAL: % healed; damaging: drain % */
    int32_t recoil;      /* % of the damage dealt */
    int32_t hits;        /* expected hits x10 (10 one hit) */
    int32_t fixed;       /* fixed damage (Seismic Toss: -1 = the level), 0 none */
    int32_t self_ko;     /* Explosion and the like */
} StMove;                /* 17 ints */

#define ST_MOVE_INTS 17

typedef struct StMon {
    int32_t present;     /* 1 a Pokemon is here */
    int32_t key;         /* a stable id: own index, or the foe's identity */
    int32_t species, level, type1, type2;
    int32_t ability;     /* own: the real one; foe: shown one, 0 unknown */
    int32_t item;        /* own: the real one; foe: shown, 0 unknown */
    int32_t hp, maxhp;   /* own: real; foe: hp = the bar in 1/1000 (maxhp 1000) */
    int32_t stat[6];     /* own: real stats; foe: base stats */
    int32_t stage[ST_NSTAGES];
    int32_t status;
    int32_t active;      /* 1 on the field */
    int32_t fainted;
    int32_t choice_lock; /* own: the move index a Choice item locks, -1 none */
    int32_t nmoves;
    StMove moves[4];     /* own: real; foe: shown moves */
    int32_t abilities[3];/* foe: the species' possible abilities (public) */
} StMon;

typedef struct StView {
    int32_t turn, weather;
    int32_t request;     /* 0 a move or a switch, 1 a replacement after a faint */
    int32_t trapped;     /* the active Pokemon cannot switch */
    int32_t nown, nfoe;
    StMon own[6];
    StMon foe[6];
} StView;

/* The choice: kind 0 a move (index into own active's moves), 1 a switch
 * (index into own). */
typedef struct StChoice {
    int32_t kind, index;
    int32_t score;       /* for logs */
} StChoice;

/* Settings. */
typedef struct StConfig {
    int32_t learn;       /* 1 remember damage and infer (the point), 0 base estimates only */
    int32_t switching;   /* 1 may switch to a counter */
    int32_t setup;       /* 1 may set up */
    int32_t switch_margin;   /* how much better a switch must look (250) */
    int32_t setup_penalty;   /* taken off a boost that does not speed up the knock out (200) */
} StConfig;

void st_reset(void);                 /* forget everything (a new battle) */
void st_config(const StConfig *c);

/* Observations. The foe is named by its key; own Pokemon by own index. */
void st_observe_foe_move(int32_t foe_key, const StMove *m);
/* The foe hit an own Pokemon: damage in HP, with both sides' stages, the
 * defender's state as the view had it before the hit. crit: 1 if shown. */
void st_observe_damage_taken(const StView *v, int32_t foe_key, const StMove *m,
                             int32_t own_index, int32_t damage, int32_t crit);
/* An own Pokemon hit the foe: the bar fell from before to after (1/1000). */
void st_observe_damage_dealt(const StView *v, int32_t own_index, int32_t foe_key,
                             const StMove *m, int32_t before, int32_t after, int32_t crit);
void st_observe_foe_ability(int32_t foe_key, int32_t ability);
void st_observe_foe_item(int32_t foe_key, int32_t item);
/* Turn order: the foe moved before (1) or after (0) the own active, both
 * with moves of priority prio. */
void st_observe_order(const StView *v, int32_t foe_key, int32_t foe_first, int32_t prio);
/* A candidate move the foe could have (learnset, public); call once per
 * foe and move, the adapter picks the strongest per type. */
void st_foe_candidate(int32_t foe_key, const StMove *m);

StChoice st_choose(const StView *v);

/* The type chart, x10 (0, 5, 10, 20 per type; product for two). */
int32_t st_type_mult(int32_t atk, int32_t def1, int32_t def2);

#endif
