/* Smart Trainers: the decision core (st_core.h).
 *
 * The idea, from the user's design:
 * - The trainer knows its own team exactly.
 * - Of the player's Pokemon it starts from public knowledge (species, level,
 *   base stats, types, the moves it could learn) and learns from what it
 *   sees: each hit it takes is turned into an estimate of that Pokemon's
 *   attacking power, each hit it deals into an estimate of its defences,
 *   each turn order into bounds on its speed, each move choice into odds on
 *   the moves it does not have (a Serperior that uses Scratch when a grass
 *   move would have done more probably has no grass move).
 * - The estimates are re-applied to every own Pokemon with its own stats,
 *   types and HP, so the trainer can ask "what would that do to my bench".
 * - It never reads the player's real stats, IVs, EVs, nature, held item or
 *   unrevealed moves.
 *
 * A choice compares, for each action, how many turns each side needs to
 * knock the other out (with speed and the next hit), the same yardstick for
 * attacking, setting up, statusing, healing and switching.
 */
#include "st_core.h"

#define MAXFOES 12
#define MAXOBS 48
#define BIG 99
#define RACE_CAP 30

int32_t st_dbg[8];   /* the last setup look-ahead, for the debug log */

/* ------------------------------------------------------------- the chart */

static const signed char CHART[][3] = {
    {ST_NORMAL, ST_ROCK, 5}, {ST_NORMAL, ST_GHOST, 0}, {ST_NORMAL, ST_STEEL, 5},
    {ST_FIGHTING, ST_NORMAL, 20}, {ST_FIGHTING, ST_FLYING, 5}, {ST_FIGHTING, ST_POISON, 5},
    {ST_FIGHTING, ST_ROCK, 20}, {ST_FIGHTING, ST_BUG, 5}, {ST_FIGHTING, ST_GHOST, 0},
    {ST_FIGHTING, ST_STEEL, 20}, {ST_FIGHTING, ST_PSYCHIC, 5}, {ST_FIGHTING, ST_ICE, 20},
    {ST_FIGHTING, ST_DARK, 20},
    {ST_FLYING, ST_FIGHTING, 20}, {ST_FLYING, ST_ROCK, 5}, {ST_FLYING, ST_BUG, 20},
    {ST_FLYING, ST_STEEL, 5}, {ST_FLYING, ST_GRASS, 20}, {ST_FLYING, ST_ELECTRIC, 5},
    {ST_POISON, ST_POISON, 5}, {ST_POISON, ST_GROUND, 5}, {ST_POISON, ST_ROCK, 5},
    {ST_POISON, ST_GHOST, 5}, {ST_POISON, ST_STEEL, 0}, {ST_POISON, ST_GRASS, 20},
    {ST_GROUND, ST_FLYING, 0}, {ST_GROUND, ST_POISON, 20}, {ST_GROUND, ST_ROCK, 20},
    {ST_GROUND, ST_BUG, 5}, {ST_GROUND, ST_STEEL, 20}, {ST_GROUND, ST_FIRE, 20},
    {ST_GROUND, ST_GRASS, 5}, {ST_GROUND, ST_ELECTRIC, 20},
    {ST_ROCK, ST_FIGHTING, 5}, {ST_ROCK, ST_FLYING, 20}, {ST_ROCK, ST_GROUND, 5},
    {ST_ROCK, ST_BUG, 20}, {ST_ROCK, ST_STEEL, 5}, {ST_ROCK, ST_FIRE, 20}, {ST_ROCK, ST_ICE, 20},
    {ST_BUG, ST_FIGHTING, 5}, {ST_BUG, ST_FLYING, 5}, {ST_BUG, ST_POISON, 5},
    {ST_BUG, ST_GHOST, 5}, {ST_BUG, ST_STEEL, 5}, {ST_BUG, ST_FIRE, 5},
    {ST_BUG, ST_GRASS, 20}, {ST_BUG, ST_PSYCHIC, 20}, {ST_BUG, ST_DARK, 20},
    {ST_GHOST, ST_NORMAL, 0}, {ST_GHOST, ST_GHOST, 20}, {ST_GHOST, ST_STEEL, 5},
    {ST_GHOST, ST_PSYCHIC, 20}, {ST_GHOST, ST_DARK, 5},
    {ST_STEEL, ST_ROCK, 20}, {ST_STEEL, ST_STEEL, 5}, {ST_STEEL, ST_FIRE, 5},
    {ST_STEEL, ST_WATER, 5}, {ST_STEEL, ST_ELECTRIC, 5}, {ST_STEEL, ST_ICE, 20},
    {ST_FIRE, ST_ROCK, 5}, {ST_FIRE, ST_BUG, 20}, {ST_FIRE, ST_STEEL, 20},
    {ST_FIRE, ST_FIRE, 5}, {ST_FIRE, ST_WATER, 5}, {ST_FIRE, ST_GRASS, 20},
    {ST_FIRE, ST_ICE, 20}, {ST_FIRE, ST_DRAGON, 5},
    {ST_WATER, ST_GROUND, 20}, {ST_WATER, ST_ROCK, 20}, {ST_WATER, ST_FIRE, 20},
    {ST_WATER, ST_WATER, 5}, {ST_WATER, ST_GRASS, 5}, {ST_WATER, ST_DRAGON, 5},
    {ST_GRASS, ST_FLYING, 5}, {ST_GRASS, ST_POISON, 5}, {ST_GRASS, ST_GROUND, 20},
    {ST_GRASS, ST_ROCK, 20}, {ST_GRASS, ST_BUG, 5}, {ST_GRASS, ST_STEEL, 5},
    {ST_GRASS, ST_FIRE, 5}, {ST_GRASS, ST_WATER, 20}, {ST_GRASS, ST_GRASS, 5},
    {ST_GRASS, ST_DRAGON, 5},
    {ST_ELECTRIC, ST_FLYING, 20}, {ST_ELECTRIC, ST_GROUND, 0}, {ST_ELECTRIC, ST_WATER, 20},
    {ST_ELECTRIC, ST_GRASS, 5}, {ST_ELECTRIC, ST_ELECTRIC, 5}, {ST_ELECTRIC, ST_DRAGON, 5},
    {ST_PSYCHIC, ST_FIGHTING, 20}, {ST_PSYCHIC, ST_POISON, 20}, {ST_PSYCHIC, ST_STEEL, 5},
    {ST_PSYCHIC, ST_PSYCHIC, 5}, {ST_PSYCHIC, ST_DARK, 0},
    {ST_ICE, ST_FLYING, 20}, {ST_ICE, ST_GROUND, 20}, {ST_ICE, ST_STEEL, 5},
    {ST_ICE, ST_FIRE, 5}, {ST_ICE, ST_WATER, 5}, {ST_ICE, ST_GRASS, 20},
    {ST_ICE, ST_ICE, 5}, {ST_ICE, ST_DRAGON, 20},
    {ST_DRAGON, ST_STEEL, 5}, {ST_DRAGON, ST_DRAGON, 20},
    {ST_DARK, ST_FIGHTING, 5}, {ST_DARK, ST_GHOST, 20}, {ST_DARK, ST_STEEL, 5},
    {ST_DARK, ST_PSYCHIC, 20}, {ST_DARK, ST_DARK, 5},
};

static int one(int a, int d)
{
    unsigned i;
    if (a < 0 || a >= ST_NTYPES || d < 0 || d >= ST_NTYPES)
        return 10;
    for (i = 0; i < sizeof CHART / sizeof CHART[0]; i++)
        if (CHART[i][0] == a && CHART[i][1] == d)
            return CHART[i][2];
    return 10;
}

int32_t st_type_mult(int32_t atk, int32_t def1, int32_t def2)
{
    int m = one(atk, def1);
    if (def2 != def1)
        m = m * one(atk, def2) / 10;
    return m;
}

/* ------------------------------------------------------------- memory */

typedef struct Obs {
    int32_t move, own_key, s_att, s_def;
    float frac;          /* the share of the own Pokemon's max HP it took */
} Obs;

typedef struct FoeMem {
    int32_t used, key;
    float atk_r[2], atk_w[2];   /* learned ratio to the base estimate, weight */
    float def_r[2], def_w[2];
    float spe_lo, spe_hi;       /* bounds on the real speed stat */
    int32_t ability, item;
    float p_type[ST_NTYPES];    /* odds it has a damaging move of the type */
    StMove cand[ST_NTYPES];
    int32_t has_cand[ST_NTYPES];
    StMove seen[8];
    int32_t nseen;
    Obs obs[MAXOBS];
    int32_t nobs;
} FoeMem;

static FoeMem g_foe[MAXFOES];
static StConfig g_cfg = {1, 1, 1, 250, 200};
static int32_t g_switch_streak;
/* The player's training as a whole: how much harder its Pokemon hit and
 * how much more they take than untrained ones. A Pokemon not seen yet
 * starts from this. */
static float g_atk_r[2] = {1, 1}, g_def_r[2] = {1, 1}, g_atk_w[2], g_def_w[2];

/*
 * The field, as the trainer knows it: what its own moves set up and what it
 * saw the foe's moves set up.  A move is assumed to work (the core cannot
 * see a miss of a status move); the turns run on the view's turn number.
 */
typedef struct Field {
    int turn;                       /* the last turn the core chose on */
    int reflect_until, screen_until, tailwind_until;   /* own side */
    int foe_reflect_until, foe_screen_until;
    int sub_until;                  /* own Substitute, about two hits */
    int hz_foe[3], hz_own[3];       /* rocks, spikes layers, toxic spikes layers */
    int seeded_key, own_seeded;     /* the foe we seeded (-1); seeded ourselves */
    int conf_key, conf_until;       /* the foe we confused */
    int yawn_key;
    int foe_active_key;             /* the foe on the field when the core last chose */
    int lock_key, lock_move;        /* that foe used lock_move since it came in (a Choice item holds it) */
    int bad_key;                    /* the foe that was losing its matchup with a counter on its bench */
    int taunted_until;              /* we cannot use status moves */
    int stockpile;
    int ring;                       /* Aqua Ring or Ingrain on the active one */
} Field;

static Field g_f;

static void field_reset(void)
{
    char *p = (char *)&g_f;
    int j;
    for (j = 0; j < (int)sizeof g_f; j++)
        p[j] = 0;
    g_f.seeded_key = g_f.conf_key = g_f.yawn_key = g_f.lock_key = g_f.foe_active_key = g_f.bad_key = -1;
}

/* The moves the core knows by their number (the national move numbers, the
 * same in the game and in Showdown) beyond what the move data says. */
enum {
    R_NONE, R_REFLECT, R_LSCREEN, R_ROCKS, R_SPIKES, R_TSPIKES, R_WEATHER,
    R_PHAZE, R_HAZE, R_SEED, R_SUB, R_CONFUSE, R_SWAGGER, R_YAWN, R_TAUNT,
    R_OHKO, R_DRUM, R_RING, R_REST, R_DBOND, R_STOCKPILE, R_HEAL50,
    R_SWALLOW, R_TAILWIND, R_USELESS
};

static int rule_of(int id, int *arg)
{
    *arg = 0;
    switch (id) {
    case 115: return R_REFLECT;
    case 113: return R_LSCREEN;
    case 446: return R_ROCKS;
    case 191: return R_SPIKES;
    case 390: return R_TSPIKES;
    case 240: *arg = ST_W_RAIN; return R_WEATHER;
    case 241: *arg = ST_W_SUN; return R_WEATHER;
    case 201: *arg = ST_W_SAND; return R_WEATHER;
    case 258: *arg = ST_W_HAIL; return R_WEATHER;
    case 46: case 18: return R_PHAZE;
    case 114: return R_HAZE;
    case 73: return R_SEED;
    case 164: return R_SUB;
    case 109: case 48: case 186: case 298: return R_CONFUSE;
    case 207: *arg = 0; return R_SWAGGER;      /* raises the foe's attack */
    case 260: *arg = 1; return R_SWAGGER;      /* Flatter: its special attack */
    case 281: return R_YAWN;
    case 269: return R_TAUNT;
    case 90: case 12: case 32: case 329: return R_OHKO;
    case 187: return R_DRUM;
    case 392: case 275: return R_RING;
    case 156: return R_REST;
    case 194: return R_DBOND;
    case 254: return R_STOCKPILE;
    case 236: case 234: case 235: case 273: return R_HEAL50;
    case 256: return R_SWALLOW;
    case 366: return R_TAILWIND;
    /* nothing in a single battle against one foe */
    case 270: case 495: case 150: case 289: case 382: case 300: case 346:
    case 381: case 373: case 259:
        return R_USELESS;
    default: return R_NONE;
    }
}

void st_reset(void)
{
    int i;
    for (i = 0; i < MAXFOES; i++)
        g_foe[i].used = 0;
    field_reset();
    g_switch_streak = 0;
    g_atk_r[0] = g_atk_r[1] = g_def_r[0] = g_def_r[1] = 1;
    g_atk_w[0] = g_atk_w[1] = g_def_w[0] = g_def_w[1] = 0;
}

void st_config(const StConfig *c)
{
    g_cfg = *c;
}

static FoeMem *mem(int32_t key)
{
    int i, j;
    for (i = 0; i < MAXFOES; i++)
        if (g_foe[i].used && g_foe[i].key == key)
            return &g_foe[i];
    for (i = 0; i < MAXFOES; i++)
        if (!g_foe[i].used)
            break;
    if (i == MAXFOES)
        i = 0;   /* full: reuse the first */
    {
        FoeMem *f = &g_foe[i];
        char *p = (char *)f;
        for (j = 0; j < (int)sizeof *f; j++)
            p[j] = 0;
        f->used = 1;
        f->key = key;
        f->atk_r[0] = g_atk_r[0]; f->atk_r[1] = g_atk_r[1];
        f->def_r[0] = g_def_r[0]; f->def_r[1] = g_def_r[1];
        f->spe_lo = 0;
        f->spe_hi = 10000;
        for (j = 0; j < ST_NTYPES; j++)
            f->p_type[j] = -1;   /* no prior yet: set when the view is known */
        return f;
    }
}

/* ------------------------------------------------------------- numbers */

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static float fmin_(float a, float b) { return a < b ? a : b; }
static float fmax_(float a, float b) { return a > b ? a : b; }

static float stage_mult(int s)
{
    if (s > 6) s = 6;
    if (s < -6) s = -6;
    return s >= 0 ? (2.0f + s) / 2.0f : 2.0f / (2.0f - s);
}

/* A stat from the base, for an unknown Pokemon: average IVs, no EVs, a
 * neutral nature (what "anyone knows"). */
static float base_stat(int base, int level, int hp)
{
    float v = (float)((2 * base + 15) * level / 100);
    return hp ? v + level + 10 : v + 5;
}

/* Turns to take away hp (a share) at d per turn. */
static int turns(float hp, float d)
{
    int n;
    if (d <= 0.001f)
        return BIG;
    n = (int)(hp / d);
    if ((float)n * d < hp - 0.0001f)
        n++;
    return n < 1 ? 1 : imin(n, BIG);
}

/* ------------------------------------------------------------- combatants */

typedef struct Cb {
    int level, t1, t2, ability, item, status, foe;
    float stat[6];       /* own: real; foe: estimates */
    int stage[ST_NSTAGES];
    float hp;            /* share of max HP left, 0..1 */
    float maxhp;
    const StMon *mon;
    FoeMem *m;           /* foe only */
    int maybe[3];        /* foe: possible abilities (0 unknown) */
} Cb;

static void cb_own(Cb *c, const StMon *s)
{
    int i;
    c->level = s->level; c->t1 = s->type1; c->t2 = s->type2;
    c->ability = s->ability; c->item = s->item; c->status = s->status; c->foe = 0;
    for (i = 0; i < 6; i++) c->stat[i] = (float)s->stat[i];
    for (i = 0; i < ST_NSTAGES; i++) c->stage[i] = s->stage[i];
    c->maxhp = (float)(s->maxhp > 0 ? s->maxhp : 1);
    c->hp = s->fainted ? 0 : (float)s->hp / c->maxhp;
    c->mon = s; c->m = 0;
    c->maybe[0] = c->maybe[1] = c->maybe[2] = 0;
}

static void cb_foe(Cb *c, const StMon *s)
{
    int i;
    FoeMem *m = mem(s->key);
    c->level = s->level; c->t1 = s->type1; c->t2 = s->type2;
    c->ability = s->ability ? s->ability : m->ability;
    c->item = s->item ? s->item : m->item;
    c->status = s->status; c->foe = 1;
    for (i = 0; i < 6; i++) c->stat[i] = base_stat(s->stat[i], s->level, i == ST_HP);
    if (g_cfg.learn) {
        c->stat[ST_ATK] *= m->atk_r[0];
        c->stat[ST_SPA] *= m->atk_r[1];
        c->stat[ST_DEF] *= m->def_r[0];
        c->stat[ST_SPD] *= m->def_r[1];
        c->stat[ST_SPE] = fmax_(m->spe_lo, fmin_(m->spe_hi, c->stat[ST_SPE]));
    }
    for (i = 0; i < ST_NSTAGES; i++) c->stage[i] = s->stage[i];
    c->maxhp = c->stat[ST_HP];
    c->hp = s->fainted ? 0 : (float)s->hp / 1000.0f;
    c->mon = s; c->m = m;
    for (i = 0; i < 3; i++) c->maybe[i] = c->ability ? (i == 0 ? c->ability : 0) : s->abilities[i];
}

/* Could the Pokemon have the ability: 2 surely, 1 maybe, 0 no. */
static int has_ab(const Cb *c, int ab)
{
    int i, n = 0, hit = 0;
    if (c->ability)
        return c->ability == ab ? 2 : 0;
    for (i = 0; i < 3; i++)
        if (c->maybe[i]) {
            n++;
            if (c->maybe[i] == ab) hit = 1;
        }
    if (!hit) return 0;
    return n == 1 ? 2 : 1;
}

static float speed(const Cb *c)
{
    float s = c->stat[ST_SPE] * stage_mult(c->stage[ST_S_SPE]);
    if (c->status == ST_ST_PARALYSIS) s *= 0.25f;
    /* the item: our own, or the foe's once it showed it */
    if (c->item == ST_IT_CHOICE_SCARF) s *= 1.5f;
    if (!c->foe && g_f.tailwind_until > g_f.turn) s *= 2.0f;   /* Tailwind on our side */
    return s;
}

/* The type a held item powers up by a fifth (Charcoal and the like), or -1. */
static int item_boost_type(int item)
{
    switch (item) {
    case 249: return ST_FIRE;     case 243: return ST_WATER;   case 239: return ST_GRASS;
    case 242: return ST_ELECTRIC; case 246: return ST_ICE;     case 241: return ST_FIGHTING;
    case 245: return ST_POISON;   case 237: return ST_GROUND;  case 244: return ST_FLYING;
    case 248: return ST_PSYCHIC;  case 222: return ST_BUG;     case 238: return ST_ROCK;
    case 247: return ST_GHOST;    case 250: return ST_DRAGON;  case 240: return ST_DARK;
    case 233: return ST_STEEL;    case 251: return ST_NORMAL;
    default: return -1;
    }
}

/* What a Pokemon's item heals each turn (Leftovers; Black Sludge on a Poison
 * type), as a share of its HP. */
static float item_regen(const Cb *c)
{
    if (c->item == ST_IT_LEFTOVERS) return 0.0625f;
    if (c->item == ST_IT_BLACK_SLUDGE) return (c->t1 == ST_POISON || c->t2 == ST_POISON) ? 0.0625f : -0.125f;
    return 0;
}

/* Turns for hits of d to knock out c: its item's healing, a Sitrus Berry's
 * quarter, and a Focus Sash or Sturdy at full HP (one hit cannot do it). */
static int ko_turns(const Cb *c, float d)
{
    float hp = c->hp, per = d - item_regen(c);
    int n;
    if (c->item == ST_IT_SITRUS_BERRY && hp > 0.5f && d < hp) hp += 0.25f;
    /* poison and a burn take an eighth a turn (Leftovers is already off) */
    if (c->status == ST_ST_POISON || c->status == ST_ST_BURN || c->status == ST_ST_TOXIC) per += 0.125f;
    if (per < 0.005f) per = 0.005f;
    n = turns(hp, per);
    if (n == 1 && c->hp >= 0.999f && (c->item == ST_IT_FOCUS_SASH || has_ab(c, ST_AB_STURDY) == 2)) n = 2;
    return n;
}

/* The expected share of d's max HP that a's move m takes (accuracy and the
 * random factor averaged), with the weather w. */
static float damage(const Cb *a, const Cb *d, const StMove *m, int w)
{
    float A, D, base, mod, dmg;
    int tm, phys, imm;

    if (!m->id || m->cat == ST_STATUS)
        return 0;
    tm = st_type_mult(m->type, d->t1, d->t2);
    if (tm == 0)
        return 0;
    /* immunities from abilities: surely, or by the odds */
    imm = 0;
    if (m->type == ST_GROUND) imm = has_ab(d, ST_AB_LEVITATE);
    if (m->type == ST_WATER) imm = imax(has_ab(d, ST_AB_WATER_ABSORB), imax(has_ab(d, ST_AB_STORM_DRAIN), has_ab(d, ST_AB_DRY_SKIN)));
    if (m->type == ST_ELECTRIC) imm = imax(has_ab(d, ST_AB_VOLT_ABSORB), imax(has_ab(d, ST_AB_LIGHTNING_ROD), has_ab(d, ST_AB_MOTOR_DRIVE)));
    if (m->type == ST_FIRE) imm = has_ab(d, ST_AB_FLASH_FIRE);
    if (m->type == ST_GRASS) imm = imax(imm, has_ab(d, ST_AB_SAP_SIPPER));
    if (tm <= 10 && has_ab(d, ST_AB_WONDER_GUARD) == 2) return 0;
    if (imm == 2) return 0;

    if (m->fixed) {
        dmg = (float)(m->fixed < 0 ? a->level : m->fixed);
        return dmg / d->maxhp;
    }
    phys = m->cat == ST_PHYSICAL;
    A = a->stat[phys ? ST_ATK : ST_SPA];
    D = d->stat[phys ? ST_DEF : ST_SPD];
    if (!has_ab(d, ST_AB_UNAWARE))
        A *= stage_mult(a->stage[phys ? ST_S_ATK : ST_S_SPA]);
    if (!has_ab(a, ST_AB_UNAWARE))
        D *= stage_mult(d->stage[phys ? ST_S_DEF : ST_S_SPD]);
    if (phys && (a->ability == ST_AB_HUGE_POWER || a->ability == ST_AB_PURE_POWER) && !a->foe)
        A *= 2;
    /* items: our own, or the foe's once shown */
    if (phys && a->item == ST_IT_CHOICE_BAND) A *= 1.5f;
    if (!phys && a->item == ST_IT_CHOICE_SPECS) A *= 1.5f;
    if (d->item == ST_IT_EVIOLITE) D *= 1.5f;   /* only a Pokemon that evolves holds it to use */
    if (m->type == ST_GROUND && d->item == ST_IT_AIR_BALLOON) return 0;
    if (D < 1) D = 1;
    base = (float)((2 * a->level) / 5 + 2) * (float)m->power * A / D / 50.0f + 2.0f;
    mod = 0.925f * 1.0625f * (float)tm / 10.0f;
    if (m->type == a->t1 || m->type == a->t2) mod *= 1.5f;
    if (phys && a->status == ST_ST_BURN) mod *= 0.5f;
    if (w == ST_W_RAIN) { if (m->type == ST_WATER) mod *= 1.5f; if (m->type == ST_FIRE) mod *= 0.5f; }
    if (w == ST_W_SUN) { if (m->type == ST_FIRE) mod *= 1.5f; if (m->type == ST_WATER) mod *= 0.5f; }
    if ((m->type == ST_FIRE || m->type == ST_ICE) && has_ab(d, ST_AB_THICK_FAT) == 2) mod *= 0.5f;
    if (a->item == ST_IT_LIFE_ORB) mod *= 1.3f;
    if (a->item == ST_IT_EXPERT_BELT && tm > 10) mod *= 1.2f;
    if (a->item == ST_IT_MUSCLE_BAND && phys) mod *= 1.1f;
    if (a->item == ST_IT_WISE_GLASSES && !phys) mod *= 1.1f;
    if (item_boost_type(a->item) == m->type) mod *= 1.2f;
    if (imm == 1) mod *= 0.5f;
    /* Reflect and Light Screen on the defender's side halve the hit */
    if (a->foe ? (phys ? g_f.reflect_until : g_f.screen_until) > g_f.turn
               : (phys ? g_f.foe_reflect_until : g_f.foe_screen_until) > g_f.turn)
        mod *= 0.5f;
    dmg = base * mod;
    if (m->hits > 10) dmg = dmg * (float)m->hits / 10.0f;
    if (m->acc > 0 && m->acc < 100) dmg = dmg * (float)m->acc / 100.0f;
    return dmg / d->maxhp;
}

/* ------------------------------------------------------------- the foe's moves */

static void prior_types(FoeMem *m, const StMon *s)
{
    int t;
    if (m->p_type[0] >= 0)
        return;
    for (t = 0; t < ST_NTYPES; t++) {
        if (!m->has_cand[t]) m->p_type[t] = 0;
        else if (t == s->type1 || t == s->type2) m->p_type[t] = 0.85f;
        else m->p_type[t] = 0.3f;
    }
}

/* What the foe f can do to own o: the most damaging move it has shown, or
 * a candidate by its odds. Writes the move it would use. */
static float threat(const Cb *f, const Cb *o, int w, StMove *best_out)
{
    FoeMem *m = f->m;
    float best = 0, d;
    int i, t;
    StMove none = {0};

    if (best_out) *best_out = none;
    /* a foe locked by its Choice item can only repeat the move it used */
    if ((f->item == ST_IT_CHOICE_BAND || f->item == ST_IT_CHOICE_SPECS || f->item == ST_IT_CHOICE_SCARF) &&
        g_f.lock_key == f->mon->key && g_f.lock_move)
        for (i = 0; i < m->nseen; i++)
            if (m->seen[i].id == g_f.lock_move) {
                if (best_out) *best_out = m->seen[i];
                return damage(f, o, &m->seen[i], w);
            }
    prior_types(m, f->mon);
    for (i = 0; i < m->nseen; i++) {
        d = damage(f, o, &m->seen[i], w);
        /* what this exact situation did before, if remembered */
        if (g_cfg.learn) {
            int k;
            for (k = 0; k < m->nobs; k++) {
                const Obs *ob = &m->obs[k];
                if (ob->move == m->seen[i].id && ob->own_key == o->mon->key &&
                    ob->s_att == f->stage[m->seen[i].cat == ST_PHYSICAL ? ST_S_ATK : ST_S_SPA] &&
                    ob->s_def == o->stage[m->seen[i].cat == ST_PHYSICAL ? ST_S_DEF : ST_S_SPD])
                    d = fmax_(d, ob->frac);
            }
        }
        if (d > best) { best = d; if (best_out) *best_out = m->seen[i]; }
    }
    for (t = 0; t < ST_NTYPES; t++) {
        float p = g_cfg.learn ? m->p_type[t] : (m->has_cand[t] && (t == f->t1 || t == f->t2) ? 0.85f : 0.3f);
        if (!m->has_cand[t] || p <= 0.05f) continue;
        d = damage(f, o, &m->cand[t], w);
        /* a likely move counts in full, an unlikely one in part */
        d = p >= 0.6f ? d : d * p * 1.4f;
        if (d > best) { best = d; if (best_out) *best_out = m->cand[t]; }
    }
    return best;
}

void st_foe_candidate(int32_t foe_key, const StMove *mv)
{
    FoeMem *m = mem(foe_key);
    int t = mv->type;
    float score, cur;
    if (mv->cat == ST_STATUS || t < 0 || t >= ST_NTYPES || mv->power <= 0)
        return;
    score = (float)mv->power * (mv->acc > 0 ? mv->acc : 100) * (mv->hits > 10 ? mv->hits : 10);
    if (mv->recoil || mv->self_ko) score *= 0.8f;
    cur = m->has_cand[t] ? (float)m->cand[t].power * (m->cand[t].acc > 0 ? m->cand[t].acc : 100) *
                               (m->cand[t].hits > 10 ? m->cand[t].hits : 10)
                         : -1;
    if (score > cur) {
        m->cand[t] = *mv;
        m->has_cand[t] = 1;
    }
}

void st_observe_foe_move(int32_t foe_key, const StMove *mv)
{
    FoeMem *m = mem(foe_key);
    int i, arg;

    /* the move a Choice item would hold it to */
    g_f.lock_key = foe_key;
    g_f.lock_move = mv->id;
    /* what the foe set up on the field */
    switch (rule_of(mv->id, &arg)) {
    case R_REFLECT: g_f.foe_reflect_until = g_f.turn + 5; break;
    case R_LSCREEN: g_f.foe_screen_until = g_f.turn + 5; break;
    case R_ROCKS: g_f.hz_own[0] = 1; break;
    case R_SPIKES: if (g_f.hz_own[1] < 3) g_f.hz_own[1]++; break;
    case R_TSPIKES: if (g_f.hz_own[2] < 2) g_f.hz_own[2]++; break;
    case R_SEED: g_f.own_seeded = 1; break;
    case R_TAUNT: g_f.taunted_until = g_f.turn + 3; break;
    case R_HAZE: break;
    default: break;
    }
    for (i = 0; i < m->nseen; i++)
        if (m->seen[i].id == mv->id)
            return;
    if (m->nseen < 8)
        m->seen[m->nseen++] = *mv;
    if (mv->cat != ST_STATUS && mv->type >= 0 && mv->type < ST_NTYPES)
        m->p_type[mv->type] = 1;
}

void st_observe_foe_ability(int32_t foe_key, int32_t ability) { mem(foe_key)->ability = ability; }
void st_observe_foe_item(int32_t foe_key, int32_t item) { mem(foe_key)->item = item; }

static int find_foe(const StView *v, int32_t key)
{
    int i;
    for (i = 0; i < v->nfoe; i++)
        if (v->foe[i].present && v->foe[i].key == key)
            return i;
    return -1;
}

static int active_own(const StView *v)
{
    int i;
    for (i = 0; i < v->nown; i++)
        if (v->own[i].present && v->own[i].active && !v->own[i].fainted)
            return i;
    return -1;
}

static int active_foe(const StView *v)
{
    int i;
    for (i = 0; i < v->nfoe; i++)
        if (v->foe[i].present && v->foe[i].active && !v->foe[i].fainted)
            return i;
    return -1;
}

/* The foe hit own: learn its attacking power, and what its choice says
 * about the moves it does not have. */
void st_observe_damage_taken(const StView *v, int32_t foe_key, const StMove *mv,
                             int32_t own_index, int32_t dmg, int32_t crit)
{
    Cb f, o;
    int fi = find_foe(v, foe_key), t, c;
    float pred, ratio, w;
    FoeMem *m;

    if (fi < 0 || own_index < 0 || own_index >= v->nown || mv->cat == ST_STATUS || mv->fixed)
        return;
    cb_foe(&f, &v->foe[fi]);
    cb_own(&o, &v->own[own_index]);
    m = f.m;
    c = mv->cat == ST_PHYSICAL ? 0 : 1;
    if (m->nobs < MAXOBS) {
        Obs *ob = &m->obs[m->nobs++];
        ob->move = mv->id;
        ob->own_key = v->own[own_index].key;
        ob->s_att = f.stage[c ? ST_S_SPA : ST_S_ATK];
        ob->s_def = o.stage[c ? ST_S_SPD : ST_S_DEF];
        ob->frac = (float)dmg / o.maxhp;
    }
    /* the attacking power: damage scales about linearly with the stat; the
     * hit was capped by the HP left, so only learn from hits that did not
     * knock out */
    {
        StMove one = *mv;
        one.acc = 0;   /* it hit */
        pred = damage(&f, &o, &one, v->weather) * o.maxhp;
    }
    if (crit) dmg = (int32_t)((float)dmg / 2.0f * 1.0625f);
    if (pred > 1 && dmg > 0 && dmg < v->own[own_index].hp) {
        ratio = fmin_(3.0f, fmax_(0.4f, (float)dmg / pred));
        w = m->atk_w[c];
        /* pred already used the current estimate: move it toward what the
         * hit showed, less with each hit seen */
        m->atk_r[c] = m->atk_r[c] * (1.0f + (ratio - 1.0f) / (w + 1.5f));
        m->atk_w[c] = w + 1.0f;
        /* the same hit, relative to the untrained estimate, for the
         * player's training as a whole */
        {
            float abs_r = m->atk_r[c];
            g_atk_r[c] += (abs_r - g_atk_r[c]) / (g_atk_w[c] + 2.0f);
            if (g_atk_w[c] < 12) g_atk_w[c] += 1.0f;
        }
    }
    /* odds on other types: a move type that would clearly have done more
     * to this target was probably not there */
    prior_types(m, &v->foe[fi]);
    for (t = 0; t < ST_NTYPES; t++) {
        float dt;
        if (!m->has_cand[t] || t == mv->type || m->p_type[t] >= 1) continue;
        dt = damage(&f, &o, &m->cand[t], v->weather);
        if (dt > damage(&f, &o, mv, v->weather) * 1.3f + 0.05f)
            m->p_type[t] *= 0.45f;
    }
}

/* For tests: the core's prediction, in 1/1000 of the own Pokemon's max HP,
 * of the foe's move m against own_index (accuracy left out). */
int32_t st_predict_taken(const StView *v, int32_t foe_key, const StMove *mv, int32_t own_index)
{
    Cb f, o;
    StMove one = *mv;
    int fi = find_foe(v, foe_key);
    if (fi < 0 || own_index < 0 || own_index >= v->nown) return -1;
    cb_foe(&f, &v->foe[fi]);
    cb_own(&o, &v->own[own_index]);
    one.acc = 0;
    return (int32_t)(damage(&f, &o, &one, v->weather) * 1000.0f);
}

void st_observe_damage_dealt(const StView *v, int32_t own_index, int32_t foe_key,
                             const StMove *mv, int32_t before, int32_t after, int32_t crit)
{
    Cb f, o;
    int fi = find_foe(v, foe_key), c;
    float pred, got, ratio, w;
    FoeMem *m;
    StMove one;

    if (fi < 0 || own_index < 0 || own_index >= v->nown || mv->cat == ST_STATUS || mv->fixed)
        return;
    if (after <= 0) return;   /* knocked out: capped */
    cb_foe(&f, &v->foe[fi]);
    cb_own(&o, &v->own[own_index]);
    m = f.m;
    c = mv->cat == ST_PHYSICAL ? 0 : 1;
    one = *mv;
    one.acc = 0;
    pred = damage(&o, &f, &one, v->weather);
    got = (float)(before - after) / 1000.0f;
    if (crit) got = got / 2.0f * 1.0625f;
    if (pred > 0.005f && got > 0.005f) {
        /* more damage than expected: lower defence */
        ratio = fmin_(2.5f, fmax_(0.4f, pred / got));
        w = m->def_w[c];
        m->def_r[c] = m->def_r[c] * (1.0f + (ratio - 1.0f) / (w + 1.5f));
        m->def_w[c] = w + 1.0f;
        g_def_r[c] += (m->def_r[c] - g_def_r[c]) / (g_def_w[c] + 2.0f);
        if (g_def_w[c] < 12) g_def_w[c] += 1.0f;
    }
}

void st_observe_order(const StView *v, int32_t foe_key, int32_t foe_first, int32_t prio)
{
    Cb f, o;
    int fi = find_foe(v, foe_key), oi = active_own(v);
    float mine, fm;
    (void)prio;
    if (fi < 0 || oi < 0) return;
    cb_foe(&f, &v->foe[fi]);
    cb_own(&o, &v->own[oi]);
    mine = speed(&o);
    fm = stage_mult(f.stage[ST_S_SPE]) * (f.status == ST_ST_PARALYSIS ? 0.25f : 1.0f);
    if (fm <= 0) return;
    if (foe_first) f.m->spe_lo = fmax_(f.m->spe_lo, mine / fm);
    else f.m->spe_hi = fmin_(f.m->spe_hi, mine / fm);
    if (f.m->spe_lo > f.m->spe_hi) f.m->spe_hi = f.m->spe_lo;   /* a Scarf or a tie: keep the newest */
}

/* ------------------------------------------------------------- choosing */

/* The own side's view of a matchup: x against the foe f with x's HP share
 * hpx and f's share hpf. Higher is better for the trainer. */
typedef struct Match {
    int n_me, n_foe, faster;
    float d_me, d_foe;
} Match;

static float best_move_dmg(const Cb *x, const Cb *f, int w, int *idx)
{
    const StMon *s = x->mon;
    float best = 0, d;
    int i;
    if (idx) *idx = -1;
    for (i = 0; i < s->nmoves; i++) {
        if (!s->moves[i].id || s->moves[i].pp <= 0) continue;
        if (s->choice_lock >= 0 && s->choice_lock != i && x->mon->active) continue;
        d = damage(x, f, &s->moves[i], w);
        if (d > best) { best = d; if (idx) *idx = i; }
    }
    return best;
}

static Match match(const Cb *x, const Cb *f, int w)
{
    Match r;
    r.d_me = best_move_dmg(x, f, w, 0);
    r.d_foe = threat(f, x, w, 0);
    /* Life Orb costs its holder a tenth a hit */
    r.n_me = ko_turns(f, r.d_me + (f->item == ST_IT_LIFE_ORB ? 0.1f : 0.0f));
    r.n_foe = ko_turns(x, r.d_foe + (x->item == ST_IT_LIFE_ORB ? 0.1f : 0.0f));
    r.faster = speed(x) > speed(f);
    return r;
}

/* The yardstick: who wins the race to the knock out, and by how much. */
static int value(int n_me, int n_foe, int faster, float hp_left)
{
    int win = faster ? n_me <= n_foe : n_me < n_foe;
    /* a long race is still a race: a boost that turns 9 hits into 6 is worth
     * as much as one that turns 4 into 1 (a cap at 6 made it look worthless
     * and the trainer never set up in slow early-game battles) */
    int v = 100 * (imin(n_foe, RACE_CAP) - imin(n_me, RACE_CAP));
    if (win) v += 300;
    v += (int)(hp_left * 50);
    return v;
}

static int blocks_drop(const Cb *f, int statmask)
{
    if (has_ab(f, ST_AB_CONTRARY) == 2 || has_ab(f, ST_AB_DEFIANT) == 2 || has_ab(f, ST_AB_COMPETITIVE) == 2)
        return 2;   /* backfires */
    if (has_ab(f, ST_AB_CLEAR_BODY) == 2 || has_ab(f, ST_AB_WHITE_SMOKE) == 2 || has_ab(f, ST_AB_MAGIC_BOUNCE) == 2)
        return 2;
    if ((statmask & (1 << ST_S_ATK)) && has_ab(f, ST_AB_HYPER_CUTTER) == 2)
        return 2;
    if (has_ab(f, ST_AB_CONTRARY) || has_ab(f, ST_AB_DEFIANT) || has_ab(f, ST_AB_COMPETITIVE))
        return 1;
    return 0;
}

static int status_immune(const Cb *f, const StMove *m)
{
    int st = m->eff_status;
    if (f->status) return 1;
    if (st_type_mult(m->type, f->t1, f->t2) == 0 && m->cat == ST_STATUS && m->type == ST_ELECTRIC) return 1;
    if ((st == ST_ST_POISON || st == ST_ST_TOXIC) &&
        (f->t1 == ST_POISON || f->t2 == ST_POISON || f->t1 == ST_STEEL || f->t2 == ST_STEEL)) return 1;
    if (st == ST_ST_BURN && (f->t1 == ST_FIRE || f->t2 == ST_FIRE)) return 1;
    if (st == ST_ST_FREEZE && (f->t1 == ST_ICE || f->t2 == ST_ICE)) return 1;
    if (has_ab(f, ST_AB_MAGIC_BOUNCE) == 2) return 1;
    return 0;
}

/*
 * The moves of rule_of, scored on the race's scale.  The yardstick is a
 * lost turn: what spending this turn without damage does to the race
 * (`lost`); each rule adds what its effect is worth on top of that, or says
 * it would do nothing.
 */
static int foe_bench(const StView *v);
static int own_bench(const StView *v);

static int score_rule(const StView *v, const Cb *a, const Cb *f, const StMove *m, int r, int arg,
                      float t, float hp_after, int base_n_me, float dme0, int faster)
{
    int w = v->weather, k, sum;
    int lost = 1000 + value(base_n_me + 1, turns(hp_after, t), faster, hp_after);
    int fkey = f->mon->key;
    float acc = m->acc > 0 && m->acc < 100 ? (float)m->acc / 100.0f : 1.0f;

    if (r == R_USELESS) return lost - 400;
    if (hp_after <= 0.0f && r != R_DBOND) return -1500;
    switch (r) {
    case R_REFLECT: case R_LSCREEN: {
        StMove bm;
        int until = r == R_REFLECT ? g_f.reflect_until : g_f.screen_until;
        if (until > g_f.turn) return lost - 400;
        threat(f, a, w, &bm);
        if (!bm.id || bm.cat != (r == R_REFLECT ? ST_PHYSICAL : ST_SPECIAL)) return lost - 300;
        return 1000 + value(base_n_me + 1, turns(hp_after, t * 0.5f), faster, hp_after) + 60 * own_bench(v) - 50;
    }
    case R_ROCKS:
        if (g_f.hz_foe[0] || foe_bench(v) == 0) return lost - 400;
        return lost + 120 * foe_bench(v) - 100;
    case R_SPIKES:
        if (g_f.hz_foe[1] >= 3 || foe_bench(v) == 0) return lost - 400;
        return lost + (g_f.hz_foe[1] == 0 ? 90 : 50) * foe_bench(v) - 100;
    case R_TSPIKES:
        if (g_f.hz_foe[2] >= 2 || foe_bench(v) == 0) return lost - 400;
        return lost + 60 * foe_bench(v) - 100;
    case R_WEATHER: {
        float dme_w, t_w, chip_f = 0, chip_a = 0;
        if (w == arg) return lost - 400;
        dme_w = best_move_dmg(a, f, arg, 0);
        t_w = threat(f, a, arg, 0);
        if (arg == ST_W_SAND) {
            if (f->t1 != ST_ROCK && f->t2 != ST_ROCK && f->t1 != ST_GROUND && f->t2 != ST_GROUND &&
                f->t1 != ST_STEEL && f->t2 != ST_STEEL) chip_f = 0.0625f;
            if (a->t1 != ST_ROCK && a->t2 != ST_ROCK && a->t1 != ST_GROUND && a->t2 != ST_GROUND &&
                a->t1 != ST_STEEL && a->t2 != ST_STEEL) chip_a = 0.0625f;
        }
        if (arg == ST_W_HAIL) {
            if (f->t1 != ST_ICE && f->t2 != ST_ICE) chip_f = 0.0625f;
            if (a->t1 != ST_ICE && a->t2 != ST_ICE) chip_a = 0.0625f;
        }
        return 1000 + value(turns(f->hp, dme_w + chip_f) + 1, turns(hp_after, t_w + chip_a), faster, hp_after) +
               40 * own_bench(v) - 50;
    }
    case R_TAILWIND:
        if (g_f.tailwind_until > g_f.turn || faster) return lost - 300;
        return 1000 + value(base_n_me + 1, turns(hp_after, t), 1, hp_after) - 50;
    case R_PHAZE: case R_HAZE: {
        Cb f2 = *f;
        sum = 0;
        for (k = 0; k <= ST_S_SPE; k++) {
            if (f->stage[k] > 0) sum += f->stage[k];
            f2.stage[k] = 0;
        }
        if (r == R_HAZE)
            for (k = 0; k <= ST_S_SPE; k++) if (a->stage[k] > 0) sum -= a->stage[k];
        if (r == R_PHAZE && foe_bench(v) == 0) return lost - 400;
        if (sum < 2) return lost - 200 + (r == R_PHAZE ? 40 * (g_f.hz_foe[0] + g_f.hz_foe[1]) : 0);
        return 1000 + value(turns(f->hp, best_move_dmg(a, &f2, w, 0)) + 1, turns(hp_after, threat(&f2, a, w, 0)),
                            speed(a) > speed(&f2), hp_after) + 50 * sum;
    }
    case R_SEED: {
        float t_eff;
        int v_hit;
        if (f->t1 == ST_GRASS || f->t2 == ST_GRASS || g_f.seeded_key == fkey) return lost - 400;
        t_eff = fmax_(0.005f, t - 0.125f * f->maxhp / a->maxhp);
        v_hit = 1000 + value(turns(f->hp, dme0 + 0.125f) + 1, turns(hp_after, t_eff), faster, hp_after) - 30;
        return (int)(acc * (float)v_hit + (1.0f - acc) * (float)lost);
    }
    case R_SUB:
        if (g_f.sub_until > g_f.turn || a->hp <= 0.3f) return lost - 400;
        if (t >= 0.25f) return lost - 200;
        return 1000 + value(base_n_me + 1, 1 + turns(a->hp - 0.25f, t), faster, a->hp - 0.25f) - 50;
    case R_CONFUSE: case R_SWAGGER: {
        Cb f2 = *f;
        float t2;
        int v_hit;
        if (g_f.conf_key == fkey && g_f.conf_until > g_f.turn) return lost - 400;
        if (r == R_SWAGGER) {
            if (arg == 0) f2.stage[ST_S_ATK] = imin(6, f2.stage[ST_S_ATK] + 2);
            else f2.stage[ST_S_SPA] = imin(6, f2.stage[ST_S_SPA] + 1);
        }
        t2 = threat(&f2, a, w, 0);
        /* confused for about three turns, hitting itself half the time */
        v_hit = 1000 + value(turns(f->hp, dme0 + 0.05f) + 1, turns(hp_after, t2 * 0.5f), faster, hp_after) - 40;
        return (int)(acc * (float)v_hit + (1.0f - acc) * (float)lost);
    }
    case R_YAWN:
        if (f->status || g_f.yawn_key == fkey) return lost - 400;
        return 1000 + value(base_n_me + 1, turns(hp_after, t) + 2, faster, hp_after) - 120;
    case R_TAUNT: {
        int has_status = 0;
        for (k = 0; k < f->m->nseen; k++)
            if (f->m->seen[k].cat == ST_STATUS && f->m->seen[k].eff != ST_EFF_NONE) has_status = 1;
        return has_status ? lost + 120 : lost - 250;
    }
    case R_OHKO: {
        float p;
        int n;
        if (f->level > a->level || has_ab(f, ST_AB_STURDY) == 2) return -3000;
        p = 0.30f + (float)(a->level - f->level) / 100.0f;
        n = (int)(1.0f / p + 0.5f);
        if (n >= base_n_me) return lost - 200;
        return 1000 + value(n, turns(a->hp, t), faster, a->hp) - 150;
    }
    case R_DRUM: {
        Cb a2 = *a;
        float hp2 = a->hp - 0.5f - t;
        if (!g_cfg.setup || hp2 <= 0.0f || a->stage[ST_S_ATK] >= 4) return -1500;
        a2.stage[ST_S_ATK] = 6;
        a2.hp = hp2;
        return 1000 + value(1 + turns(f->hp, best_move_dmg(&a2, f, w, 0)), 1 + turns(hp2, t), faster, hp2) - 100;
    }
    case R_RING:
        if (g_f.ring) return lost - 400;
        return 1000 + value(base_n_me + 1, turns(hp_after, fmax_(0.005f, t - 0.0625f)), faster, hp_after) - 30;
    case R_REST: {
        float hp_rest = 1.0f - 2.0f * t;
        if (a->hp > 0.5f || hp_rest <= 0.0f) return -500;
        return 1000 + value(base_n_me + 3, 3 + turns(hp_rest, t), faster, hp_rest) - 50;
    }
    case R_DBOND:
        /* it goes down this turn anyway: the foe goes with it */
        if (!faster && t >= a->hp && dme0 < f->hp) return 1000 + 400;
        return lost - 400;
    case R_STOCKPILE: {
        Cb a2 = *a;
        if (g_f.stockpile >= 3) return lost - 400;
        a2.stage[ST_S_DEF] = imin(6, a2.stage[ST_S_DEF] + 1);
        a2.stage[ST_S_SPD] = imin(6, a2.stage[ST_S_SPD] + 1);
        return 1000 + value(base_n_me + 1, 1 + turns(hp_after, threat(f, &a2, w, 0)), faster, hp_after) - 60;
    }
    case R_HEAL50: case R_SWALLOW: {
        float heal = 0.5f, hp2;
        if (r == R_SWALLOW) {
            if (g_f.stockpile == 0) return -3000;
            heal = g_f.stockpile == 1 ? 0.25f : g_f.stockpile == 2 ? 0.5f : 1.0f;
        }
        if (a->hp > 0.6f) return -500;
        hp2 = fmin_(1.0f, a->hp + heal) - t;
        if (hp2 <= a->hp - t + 0.05f || hp2 <= 0) return -500;
        return 1000 + value(base_n_me + 1, turns(hp2, t), faster, hp2) - 50;
    }
    default:
        return lost - 250;
    }
}

/* What the trainer's own choice sets up on the field (assumed to work). */
static void note_choice(const StView *v, const StChoice *ch)
{
    int ai = active_own(v), fi = active_foe(v), arg;
    if (ch->kind == 1) {
        g_f.own_seeded = 0; g_f.sub_until = 0; g_f.ring = 0; g_f.stockpile = 0;
        return;
    }
    if (ai < 0 || ch->index < 0 || ch->index >= v->own[ai].nmoves) return;
    switch (rule_of(v->own[ai].moves[ch->index].id, &arg)) {
    case R_REFLECT: g_f.reflect_until = g_f.turn + 5; break;
    case R_LSCREEN: g_f.screen_until = g_f.turn + 5; break;
    case R_ROCKS: g_f.hz_foe[0] = 1; break;
    case R_SPIKES: if (g_f.hz_foe[1] < 3) g_f.hz_foe[1]++; break;
    case R_TSPIKES: if (g_f.hz_foe[2] < 2) g_f.hz_foe[2]++; break;
    case R_SEED: if (fi >= 0) g_f.seeded_key = v->foe[fi].key; break;
    case R_SUB: g_f.sub_until = g_f.turn + 2; break;
    case R_CONFUSE: case R_SWAGGER: if (fi >= 0) { g_f.conf_key = v->foe[fi].key; g_f.conf_until = g_f.turn + 3; } break;
    case R_YAWN: if (fi >= 0) g_f.yawn_key = v->foe[fi].key; break;
    case R_RING: g_f.ring = 1; break;
    case R_STOCKPILE: g_f.stockpile++; break;
    case R_SWALLOW: g_f.stockpile = 0; break;
    case R_TAILWIND: g_f.tailwind_until = g_f.turn + 4; break;
    default: break;
    }
}

static int score_move(const StView *v, const Cb *a, const Cb *f, int i)
{
    const StMove *m = &a->mon->moves[i];
    int w = v->weather;
    float t = threat(f, a, w, 0), d;
    int faster, n_me, n_foe, sc;
    StMove fm;

    if (!m->id || m->pp <= 0) return -100000;
    if (a->mon->choice_lock >= 0 && a->mon->choice_lock != i) return -100000;
    faster = speed(a) > speed(f);
    if (m->prio > 0) faster = 1;
    (void)fm;

    if (m->cat != ST_STATUS) {
        d = damage(a, f, m, w);
        if (d <= 0) return -5000;
        if (m->self_ko) {
            /* only when this Pokemon is lost anyway */
            if (turns(a->hp, t) > 1 && !(d >= f->hp)) return -3000;
        }
        if (faster && d >= f->hp && ko_turns(f, d) == 1)
            return 5000 + (int)(d * 100) - (m->acc > 0 && m->acc < 100 ? (100 - m->acc) * 20 : 0);
        n_me = ko_turns(f, d + (f->item == ST_IT_LIFE_ORB ? 0.1f : 0.0f));
        n_foe = ko_turns(a, t + (a->item == ST_IT_LIFE_ORB ? 0.1f : 0.0f));
        sc = 1000 + value(n_me, n_foe, faster, a->hp) + (int)(d * 100);
        if (m->recoil) sc -= (int)(d * m->recoil);
        if (m->eff == ST_EFF_DROP && blocks_drop(f, m->eff_stat) == 2 && m->eff_chance >= 100) sc -= 150;
        return sc;
    }

    {
        Cb a2 = *a, f2 = *f;
        float hp_after = a->hp - t;   /* the hit taken while not attacking */
        int s, k, base_n_me;
        float dme0 = best_move_dmg(a, f, w, 0);
        base_n_me = turns(f->hp, dme0);
        if (g_f.taunted_until > g_f.turn) return -4000;   /* Taunt: it would fail */
        /* a Choice item locks it into the move: a status move would be all it
         * could use until it switches (Trick, 271, hands the item over) */
        if ((a->item == ST_IT_CHOICE_BAND || a->item == ST_IT_CHOICE_SPECS || a->item == ST_IT_CHOICE_SCARF) &&
            a->mon->choice_lock < 0 && m->id != 271) return -2500;
        {
            int arg, r = rule_of(m->id, &arg);
            if (r != R_NONE)
                return score_rule(v, a, f, m, r, arg, t, hp_after, base_n_me, dme0, faster);
        }
        switch (m->eff) {
        case ST_EFF_BOOST:
            if (!g_cfg.setup) return -2000;
            if (has_ab(f, ST_AB_UNAWARE) == 2) return -1000;
            s = m->eff_stages;
            if (a->ability == ST_AB_CONTRARY) s = -s;
            if (a->ability == ST_AB_SIMPLE) s *= 2;
            if (s <= 0) return -2000;
            for (k = 0; k < ST_NSTAGES; k++)
                if ((m->eff_stat & (1 << k)) && a2.stage[k] >= 4) return -800;   /* enough */
            if (hp_after <= 0.0f) return -1500;
            /* boost 1 to 3 times in a row while it lives through the hits,
             * then attack: the best of these races against attacking now */
            {
                int times, best = -100000, best_total = BIG;
                float hp_k = a->hp;
                for (times = 1; times <= 3; times++) {
                    int total, nf, any = 0;
                    float t_k;
                    for (k = 0; k < ST_NSTAGES; k++)
                        if (m->eff_stat & (1 << k) && a2.stage[k] < 6) {
                            a2.stage[k] = imin(6, a2.stage[k] + s);
                            any = 1;
                        }
                    if (!any) break;
                    t_k = threat(f, &a2, w, 0);
                    hp_k -= t_k;
                    if (hp_k <= 0.0f) break;
                    a2.hp = hp_k;
                    total = times + turns(f->hp, best_move_dmg(&a2, f, w, 0));
                    nf = times + turns(hp_k, t_k);
                    if (times == 1) {
                        StMove tb;
                        threat(f, a, w, &tb);
                        st_dbg[7] = tb.id * 100 + tb.type;
                        st_dbg[0] = m->eff_stat * 1000 + base_n_me; st_dbg[1] = total; st_dbg[2] = nf;
                        st_dbg[3] = (int32_t)(dme0 * 1000); st_dbg[4] = (int32_t)(best_move_dmg(&a2, f, w, 0) * 1000);
                        st_dbg[5] = (int32_t)(t_k * 1000); st_dbg[6] = (int32_t)(t * 1000);
                    }
                    sc = 1000 + value(total, nf, speed(&a2) > speed(f), hp_k);
                    if (sc > best) { best = sc; best_total = total; }
                }
                if (best == -100000) return -1500;
                if (best_total >= base_n_me) best -= g_cfg.setup_penalty;
                return best;
            }
        case ST_EFF_DROP:
            k = blocks_drop(f, m->eff_stat);
            if (k == 2) return -3000;   /* Contrary, Defiant, Clear Body ... */
            for (s = 0; s < ST_NSTAGES; s++)
                if (m->eff_stat & (1 << s)) {
                    if (f2.stage[s] <= -4) return -800;
                    f2.stage[s] = imax(-6, f2.stage[s] - (m->eff_stages > 0 ? m->eff_stages : 1));
                }
            if (hp_after <= 0.0f) return -1500;
            n_me = turns(f->hp, best_move_dmg(a, &f2, w, 0));
            n_foe = turns(hp_after, threat(&f2, a, w, 0));
            sc = 1000 + value(n_me + 1, n_foe + 1, speed(a) > speed(&f2), hp_after) - 100;
            if (k == 1) sc -= 250;
            return sc;
        case ST_EFF_STATUS:
            if (status_immune(f, m)) return -3000;
            if (hp_after <= 0.0f) return -1500;
            n_me = base_n_me;
            n_foe = turns(hp_after, t);
            switch (m->eff_status) {
            case ST_ST_SLEEP: n_foe += 2; break;
            case ST_ST_PARALYSIS: f2.status = ST_ST_PARALYSIS; if (speed(a) > speed(&f2)) n_foe += 1; break;
            case ST_ST_BURN: {
                StMove bm;
                threat(f, a, w, &bm);
                if (bm.cat == ST_PHYSICAL) n_foe = turns(hp_after, t * 0.5f);
                n_me = turns(f->hp, dme0 + 0.0625f);
                break;
            }
            case ST_ST_TOXIC: case ST_ST_POISON: n_me = turns(f->hp, dme0 + 0.1f); break;
            default: break;
            }
            sc = 1000 + value(n_me + 1, n_foe, speed(a) > speed(f), hp_after) - 50;
            if (m->acc > 0 && m->acc < 100) sc -= (100 - m->acc) * 3;
            return sc;
        case ST_EFF_HEAL:
            if (a->hp > 0.6f) return -500;
            {
                float hp2 = fmin_(1.0f, a->hp + (float)m->heal / 100.0f) - t;
                if (hp2 <= a->hp - t + 0.05f || hp2 <= 0) return -500;
                n_me = base_n_me;
                n_foe = turns(hp2, t);
                return 1000 + value(n_me + 1, n_foe, faster, hp2) - 50;
            }
        case ST_EFF_RETURN: {
            /* the foe's next hits come back multiplied, if this Pokemon
             * lives through them and the foe's best move is of the kind it
             * returns; worth it when it wins the race attacking would lose */
            StMove bm;
            float hit, back, hp_left;
            int hits = m->eff_stages > 0 ? m->eff_stages : 1;
            threat(f, a, w, &bm);
            if (!bm.id || bm.cat == ST_STATUS) return -1000;
            if (m->eff_stat == 1 && bm.cat != ST_PHYSICAL) return -1000;
            if (m->eff_stat == 2 && bm.cat != ST_SPECIAL) return -1000;
            hit = t;
            hp_left = a->hp - hit * (float)hits;
            if (hp_left <= 0.0f) return -1500;
            back = hit * (float)hits * (float)m->eff_chance / 10.0f * a->maxhp / f->maxhp;
            if (back >= f->hp) return 1000 + value(hits, turns(a->hp, t), 1, hp_left) + 200;
            n_me = hits + turns(f->hp - back, dme0);
            n_foe = turns(a->hp, t);
            return 1000 + value(n_me, n_foe, faster, hp_left) - 50;
        }
        case ST_EFF_PROTECT:
        default:
            /* a move the core does not model: a turn spent without damage.
             * It is scored on the race's own scale, a turn behind attacking;
             * a flat score here beat every attack once the race was lost,
             * and the trainer used Helping Hand or Gastro Acid turn after
             * turn while it lost */
            if (hp_after <= 0.0f) return -1500;
            return 1000 + value(base_n_me + 1, turns(hp_after, t), faster, hp_after) -
                   (m->eff == ST_EFF_PROTECT ? 150 : 250);
        }
    }
}

/* What the hazards on our side take from a Pokemon coming in (a share of
 * its HP); Toxic Spikes poison it. */
static int grounded(const Cb *c)
{
    return c->t1 != ST_FLYING && c->t2 != ST_FLYING && c->ability != ST_AB_LEVITATE;
}

static float hazard_hit(Cb *c)
{
    float h = 0;
    if (g_f.hz_own[0])
        h += 0.125f * (float)st_type_mult(ST_ROCK, c->t1, c->t2) / 10.0f;
    if (g_f.hz_own[1] && grounded(c))
        h += g_f.hz_own[1] == 1 ? 0.125f : g_f.hz_own[1] == 2 ? 0.1667f : 0.25f;
    if (g_f.hz_own[2] && grounded(c) && !c->status && c->t1 != ST_POISON && c->t2 != ST_POISON &&
        c->t1 != ST_STEEL && c->t2 != ST_STEEL)
        c->status = ST_ST_POISON;
    return h;
}

/* The foe's bench as the trainer sees it: the ones not on the field and not
 * fainted, counting the ones not seen yet as the party's six. */
static int foe_bench(const StView *v)
{
    int i, n = 0, seen = 0;
    for (i = 0; i < v->nfoe; i++)
        if (v->foe[i].present) {
            seen++;
            if (!v->foe[i].active && !v->foe[i].fainted) n++;
        }
    return n + (6 - seen > 0 ? (6 - seen) / 2 : 0);   /* half of the unseen: a trainer's party is often smaller */
}

static int own_bench(const StView *v)
{
    int i, n = 0;
    for (i = 0; i < v->nown; i++)
        if (v->own[i].present && !v->own[i].active && !v->own[i].fainted) n++;
    return n;
}

static int switch_value(const StView *v, const Cb *a, const Cb *f, int x, int entering_hit)
{
    Cb c;
    Match r;
    float hit = 0;
    cb_own(&c, &v->own[x]);
    c.hp -= hazard_hit(&c);
    if (c.hp <= 0) return -4000;
    if (entering_hit) {
        StMove bm;
        threat(f, a, v->weather, &bm);   /* the move the foe aims at the active one */
        hit = bm.id ? damage(f, &c, &bm, v->weather) : 0;
        c.hp -= hit;
        if (c.hp <= 0) return -4000;
    }
    r = match(&c, f, v->weather);
    return value(r.n_me + (entering_hit ? 1 : 0), r.n_foe, r.faster, c.hp);
}

/* ------------------------------------------------------------- the search */

/*
 * A two-turn look-ahead.  The one-turn scores above judge a move by the race
 * it starts; they cannot see a foe that sets up while we chip at it, a
 * switch that walks into a hit, or a status that pays off a turn later.  The
 * search plays our options against the foe's likely replies for two turns
 * on a small model of the battle (expected damage, the stat stages, the
 * major status, faints and the replacement) and scores where each line
 * ends.  The foe is modelled from what the trainer could know: the moves it
 * has shown and the likely ones of its species.
 */

static int g_search = 0;            /* turns looked ahead; 0 the one-turn scores (the
                                     * search does not beat them yet) */
/* the search's knobs, x100: the worst reply's share, the race's weight at a
 * leaf, the odds of the foe's best hit, the switch margin */
static int g_sp[4] = {25, 60, 60, 250};
void st_set_search_param(int32_t i, int32_t v) { if (i >= 0 && i < 4) g_sp[i] = v; }

void st_set_search(int32_t depth) { g_search = depth < 0 ? 0 : depth > 3 ? 3 : depth; }

typedef struct SState {
    Cb own[6], foe[6];
    int n_own, n_foe, a_own, a_foe;
    int w;
} SState;

typedef struct FoeOpt { StMove m; float p; } FoeOpt;

#define S_MAX_FOE_OPTS 4

/* The foe's likely replies: its most damaging move (most of the time), the
 * other damaging ones it has or likely has, and the status moves it has
 * shown. */
static int foe_options(const Cb *f, const Cb *o, int w, FoeOpt *out)
{
    FoeMem *m = f->m;
    StMove cand[16];
    float dmg[16];
    int n = 0, i, t, best = -1, k = 0;

    prior_types(m, f->mon);
    for (i = 0; i < m->nseen && n < 16; i++) {
        cand[n] = m->seen[i];
        dmg[n] = damage(f, o, &m->seen[i], w);
        n++;
    }
    for (t = 0; t < ST_NTYPES && n < 16; t++) {
        float p = g_cfg.learn ? m->p_type[t] : (m->has_cand[t] && (t == f->t1 || t == f->t2) ? 0.85f : 0.3f);
        int dup = 0;
        if (!m->has_cand[t] || p < 0.6f) continue;
        for (i = 0; i < n; i++) if (cand[i].id == m->cand[t].id) dup = 1;
        if (dup) continue;
        cand[n] = m->cand[t];
        dmg[n] = damage(f, o, &m->cand[t], w);
        n++;
    }
    for (i = 0; i < n; i++)
        if (cand[i].cat != ST_STATUS && (best < 0 || dmg[i] > dmg[best])) best = i;
    if (best >= 0) { out[k].m = cand[best]; out[k].p = 0; k++; }
    /* the next most damaging ones */
    while (k < 3) {
        int nb = -1;
        for (i = 0; i < n; i++) {
            int used = 0, j;
            if (cand[i].cat == ST_STATUS || dmg[i] <= 0) continue;
            for (j = 0; j < k; j++) if (out[j].m.id == cand[i].id) used = 1;
            if (!used && (nb < 0 || dmg[i] > dmg[nb])) nb = i;
        }
        if (nb < 0) break;
        out[k].m = cand[nb]; out[k].p = 0; k++;
    }
    /* a status move it has shown (a setup, a status, a drop) */
    for (i = 0; i < m->nseen && k < S_MAX_FOE_OPTS; i++)
        if (m->seen[i].cat == ST_STATUS && m->seen[i].eff != ST_EFF_NONE && m->seen[i].eff != ST_EFF_OTHER) {
            out[k].m = m->seen[i]; out[k].p = 0; k++;
            break;
        }
    if (k == 0) {
        StMove none = {0};
        out[0].m = none; out[0].p = 1; return 1;
    }
    /* the odds: the best hit most of the time, the rest share the remainder */
    out[0].p = k == 1 ? 1.0f : (float)g_sp[2] / 100.0f;
    for (i = 1; i < k; i++) out[i].p = (1.0f - out[0].p) / (float)(k - 1);
    return k;
}

static int alive(const Cb *c) { return c->hp > 0.0005f; }

/* One move's effect, x on y.  dealt: what y took, for Bide and the like. */
static void apply_move(SState *s, Cb *x, Cb *y, const StMove *m, int y_protected, float taken_by_x)
{
    int k, st;
    float d;

    if (!m->id) return;
    if (x->status == ST_ST_SLEEP || x->status == ST_ST_FREEZE) return;   /* asleep this turn */
    if (m->self_ko) x->hp = 0;
    if (m->cat != ST_STATUS) {
        if (y_protected) return;
        d = damage(x, y, m, s->w);
        if (d > y->hp) d = y->hp;
        y->hp -= d;
        if (m->heal > 0) x->hp = fmin_(1.0f, x->hp + d * (float)m->heal / 100.0f * y->maxhp / x->maxhp);
        if (m->recoil > 0) x->hp = fmax_(0.0f, x->hp - d * (float)m->recoil / 100.0f * y->maxhp / x->maxhp);
        if (m->eff == ST_EFF_DROP && m->eff_chance >= 100 && alive(y) && !blocks_drop(y, m->eff_stat))
            for (k = 0; k < ST_NSTAGES; k++)
                if (m->eff_stat & (1 << k)) y->stage[k] = imax(-6, y->stage[k] - (m->eff_stages > 0 ? m->eff_stages : 1));
        return;
    }
    switch (m->eff) {
    case ST_EFF_BOOST:
        st = m->eff_stages;
        if (has_ab(x, ST_AB_CONTRARY) == 2) st = -st;
        if (has_ab(x, ST_AB_SIMPLE) == 2) st *= 2;
        for (k = 0; k < ST_NSTAGES; k++)
            if (m->eff_stat & (1 << k)) x->stage[k] = imax(-6, imin(6, x->stage[k] + st));
        break;
    case ST_EFF_DROP:
        if (y_protected || blocks_drop(y, m->eff_stat)) break;
        for (k = 0; k < ST_NSTAGES; k++)
            if (m->eff_stat & (1 << k)) y->stage[k] = imax(-6, y->stage[k] - (m->eff_stages > 0 ? m->eff_stages : 1));
        break;
    case ST_EFF_STATUS:
        if (y_protected || status_immune(y, m)) break;
        /* a miss is likely enough to matter: below 80% it is left out */
        if (m->acc > 0 && m->acc < 80) break;
        y->status = m->eff_status == ST_ST_TOXIC ? ST_ST_POISON : m->eff_status;
        break;
    case ST_EFF_HEAL:
        x->hp = fmin_(1.0f, x->hp + (float)m->heal / 100.0f);
        break;
    case ST_EFF_RETURN:
        if (taken_by_x > 0 && alive(x)) {
            d = taken_by_x * (float)m->eff_chance / 10.0f * x->maxhp / y->maxhp;
            y->hp = fmax_(0.0f, y->hp - d);
        }
        break;
    default:
        break;
    }
}

/* The best of the own bench to send in for a fainted one (the same choice
 * the trainer makes). */
static int pick_own_replacement(const SState *s)
{
    int i, best = -1, bv = -1000000;
    for (i = 0; i < s->n_own; i++) {
        Match r;
        int val;
        if (i == s->a_own || !alive(&s->own[i])) continue;
        r = match(&s->own[i], &s->foe[s->a_foe], s->w);
        val = value(r.n_me, r.n_foe, r.faster, s->own[i].hp);
        if (val > bv) { bv = val; best = i; }
    }
    return best;
}

/* The foe's: the one of its known Pokemon that does best here. */
static int pick_foe_replacement(const SState *s)
{
    int i, best = -1, bv = -1000000;
    for (i = 0; i < s->n_foe; i++) {
        Match r;
        int val;
        if (i == s->a_foe || !alive(&s->foe[i])) continue;
        r = match(&s->own[s->a_own], &s->foe[i], s->w);
        val = -value(r.n_me, r.n_foe, r.faster, s->own[s->a_own].hp);
        if (val > bv) { bv = val; best = i; }
    }
    return best;
}

/* One turn: our option (a move index, or 100 + a party index to switch)
 * against the foe's move. */
static void play_turn(SState *s, int opt, const StMove *fm)
{
    Cb *o = &s->own[s->a_own], *f = &s->foe[s->a_foe];
    const StMove *om = 0;
    int own_first, own_prot = 0, foe_prot = 0, k;
    float o_hp0, f_hp0;

    if (opt >= 100) {
        /* the switch comes first; the foe's move lands on the new one */
        int to = opt - 100;
        for (k = 0; k < ST_NSTAGES; k++) o->stage[k] = 0;
        s->a_own = to;
        o = &s->own[s->a_own];
        apply_move(s, f, o, fm, 0, 0);
    } else {
        om = &o->mon->moves[opt];
        own_prot = om->eff == ST_EFF_PROTECT;
        foe_prot = fm->eff == ST_EFF_PROTECT;
        if (om->prio != fm->prio) own_first = om->prio > fm->prio;
        else own_first = speed(o) > speed(f);   /* a tie goes to the foe */
        o_hp0 = o->hp; f_hp0 = f->hp;
        if (own_first) {
            apply_move(s, o, f, om, foe_prot, 0);
            if (alive(f)) apply_move(s, f, o, fm, own_prot, 0);
        } else {
            apply_move(s, f, o, fm, own_prot, 0);
            if (alive(o)) apply_move(s, o, f, om, foe_prot, o_hp0 - o->hp);
        }
        (void)f_hp0;
    }
    /* the end of the turn: burn and poison */
    for (k = 0; k < 2; k++) {
        Cb *c = k == 0 ? o : f;
        if (alive(c) && (c->status == ST_ST_BURN || c->status == ST_ST_POISON))
            c->hp = fmax_(0.0f, c->hp - 0.125f);
    }
    /* faints: the replacements come in */
    if (!alive(o)) {
        int r = pick_own_replacement(s);
        if (r >= 0) s->a_own = r;
    }
    if (!alive(f)) {
        int r = pick_foe_replacement(s);
        if (r >= 0) s->a_foe = r;
    }
}

/* Where a line ends: the Pokemon and the HP each side has left, and the
 * race of the matchup on the field. */
static float eval_state(const SState *s)
{
    float e = 0;
    int i;
    for (i = 0; i < s->n_own; i++)
        if (alive(&s->own[i])) e += 1000.0f * (0.5f + s->own[i].hp);
    for (i = 0; i < s->n_foe; i++)
        if (alive(&s->foe[i])) e -= 1000.0f * (0.5f + s->foe[i].hp);
    if (alive(&s->own[s->a_own]) && alive(&s->foe[s->a_foe])) {
        Match r = match(&s->own[s->a_own], &s->foe[s->a_foe], s->w);
        e += (float)g_sp[1] / 100.0f * (float)value(r.n_me, r.n_foe, r.faster, s->own[s->a_own].hp);
    }
    return e;
}

/* The options on the field: the usable moves and the switches. */
static int own_options(const SState *s, int *out, int allow_switch, int trapped)
{
    const Cb *o = &s->own[s->a_own];
    int n = 0, i;
    if (!alive(o)) return 0;
    for (i = 0; i < o->mon->nmoves; i++) {
        const StMove *m = &o->mon->moves[i];
        if (!m->id || m->pp <= 0) continue;
        if (o->mon->choice_lock >= 0 && o->mon->choice_lock != i) continue;
        out[n++] = i;
    }
    if (allow_switch && !trapped)
        for (i = 0; i < s->n_own; i++)
            if (i != s->a_own && alive(&s->own[i])) out[n++] = 100 + i;
    return n;
}

/* The value of an option: the foe's replies weighted by their odds, mixed
 * with the worst of them (a trainer that only plays the odds walks into
 * the reply that loses the game). */
static float search_value(const SState *s0, int opt, int depth)
{
    FoeOpt fo[S_MAX_FOE_OPTS];
    int nf, j;
    float sum = 0, worst = 1e30f;

    nf = foe_options(&s0->foe[s0->a_foe], &s0->own[opt >= 100 ? opt - 100 : s0->a_own], s0->w, fo);
    for (j = 0; j < nf; j++) {
        SState s = *s0;
        float v;
        play_turn(&s, opt, &fo[j].m);
        if (depth > 1 && alive(&s.own[s.a_own]) && alive(&s.foe[s.a_foe])) {
            int opts[12], no = own_options(&s, opts, 0, 0), i;
            float best = -1e30f;
            for (i = 0; i < no; i++) {
                float sv = search_value(&s, opts[i], depth - 1);
                if (sv > best) best = sv;
            }
            v = no > 0 ? best : eval_state(&s);
        } else {
            v = eval_state(&s);
        }
        sum += fo[j].p * v;
        if (v < worst) worst = v;
    }
    return (1.0f - (float)g_sp[0] / 100.0f) * sum + (float)g_sp[0] / 100.0f * worst;
}

static int search_choose(const StView *v, StChoice *ch)
{
    SState s;
    int i, opts[12], no;
    float best = -1e30f;

    s.n_own = v->nown; s.n_foe = v->nfoe; s.w = v->weather;
    s.a_own = active_own(v); s.a_foe = active_foe(v);
    if (s.a_own < 0 || s.a_foe < 0) return 0;
    for (i = 0; i < v->nown; i++) {
        if (v->own[i].present) cb_own(&s.own[i], &v->own[i]);
        else { Cb z = {0}; s.own[i] = z; }
    }
    for (i = 0; i < v->nfoe; i++) {
        if (v->foe[i].present) cb_foe(&s.foe[i], &v->foe[i]);
        else { Cb z = {0}; s.foe[i] = z; }
    }
    no = own_options(&s, opts, g_cfg.switching, v->trapped);
    for (i = 0; i < no; i++) {
        float val = search_value(&s, opts[i], g_search);
        /* a switch gives up the turn's momentum: only for a clear gain */
        if (opts[i] >= 100) val -= (float)g_sp[3];
        if (val > best) {
            best = val;
            ch->kind = opts[i] >= 100 ? 1 : 0;
            ch->index = opts[i] >= 100 ? opts[i] - 100 : opts[i];
            ch->score = (int32_t)val;
        }
    }
    return no > 0;
}

int32_t st_debug_score(const StView *v, int i)
{
    int ai = active_own(v), fi = active_foe(v);
    Cb a, f;
    if (ai < 0 || fi < 0) return -1;
    cb_own(&a, &v->own[ai]);
    cb_foe(&f, &v->foe[fi]);
    return score_move(v, &a, &f, i);
}

/*
 * Will the player switch?  When its Pokemon is losing the race on the field
 * (we knock it out first, within three hits) and one it has shown is waiting
 * on its bench that does better against ours, it may bring that one in.
 * The odds are this player's own: how often it switched in that spot before
 * (a quarter until it has shown its habit).  Writes the likely switch-in.
 */
static int g_pl_sw, g_pl_opp;   /* the player's habit, kept from battle to battle */

static float predict_switch(const StView *v, const Cb *a, const Cb *f, Cb *cand)
{
    Match r = match(a, f, v->weather);
    int foe_losing = r.faster ? r.n_me <= r.n_foe : r.n_me < r.n_foe;
    int i, best = -1, stay, bv = -1000000;

    g_f.bad_key = -1;
    if (!foe_losing || r.n_me > 3) return 0;
    stay = -value(r.n_me, r.n_foe, r.faster, a->hp);   /* the race as the player sees it */
    for (i = 0; i < v->nfoe; i++) {
        Cb c;
        Match q;
        int fv;
        if (!v->foe[i].present || v->foe[i].active || v->foe[i].fainted) continue;
        cb_foe(&c, &v->foe[i]);
        q = match(a, &c, v->weather);
        fv = -value(q.n_me, q.n_foe, q.faster, a->hp);
        if (fv > bv) { bv = fv; best = i; }
    }
    if (best < 0 || bv < stay + 100) return 0;
    cb_foe(cand, &v->foe[best]);
    g_f.bad_key = f->mon->key;
    return ((float)g_pl_sw + 0.5f) / ((float)g_pl_opp + 2.0f);
}

/* At the next choice: did the player take the switch it had? */
static void learn_switch(const StView *v)
{
    int fi, bi;
    if (g_f.bad_key < 0) return;
    fi = active_foe(v);
    bi = find_foe(v, g_f.bad_key);
    g_pl_opp++;
    if (fi >= 0 && v->foe[fi].key != g_f.bad_key && bi >= 0 && !v->foe[bi].fainted) g_pl_sw++;
    g_f.bad_key = -1;
}

/* ------------------------------------------------------------- the learned correction */

/*
 * Every option the trainer has (each move, each switch) gets the hand-made
 * score above and a feature vector: what the core already worked out about
 * it, the race, the field.  A small network (ST_NF inputs, two hidden
 * layers of 64 and 32, one output) adds a correction, in thousands of score
 * points, to the hand-made score; with no network loaded the correction is
 * 0 and the choice is the hand-made one.  For self-play the choice can be
 * drawn from the scores (a softmax at a temperature) instead of the best,
 * and the options of the last choice can be read back for training.
 */
#define ST_NF 48
#define ST_H1 64
#define ST_H2 32
#define ST_MAXOPT 12
#define ST_NET_SIZE (ST_NF * ST_H1 + ST_H1 + ST_H1 * ST_H2 + ST_H2 + ST_H2 + 1)

static float g_net[ST_NET_SIZE];
static int g_net_on, g_log_on;
static float g_temp;
static uint32_t g_rng = 2463534242u;

/* The last choice's options, for the trainer: count, chosen, then for each
 * option its hand score and features. */
static int32_t g_opt_n, g_opt_chosen;
static float g_opt_hand[ST_MAXOPT];
static int32_t g_opt_kind[ST_MAXOPT], g_opt_idx[ST_MAXOPT];
int32_t *st_opt_kind(void) { return g_opt_kind; }
int32_t *st_opt_idx(void) { return g_opt_idx; }
static float g_opt_feat[ST_MAXOPT * ST_NF];

float *st_net_weights(void) { return g_net; }
int32_t st_net_size(void) { return ST_NET_SIZE; }
void st_net_enable(int32_t on) { g_net_on = on; }
void st_set_policy(int32_t temp_x100, int32_t seed, int32_t log_on)
{
    g_temp = (float)temp_x100 / 100.0f;
    g_rng = seed ? (uint32_t)seed : 2463534242u;
    g_log_on = log_on;
}
int32_t st_opt_count(void) { return g_opt_n; }
int32_t st_opt_chosen(void) { return g_opt_chosen; }
float *st_opt_hand(void) { return g_opt_hand; }
float *st_opt_features(void) { return g_opt_feat; }

static float net_forward(const float *x)
{
    const float *w1 = g_net, *b1 = w1 + ST_NF * ST_H1;
    const float *w2 = b1 + ST_H1, *b2 = w2 + ST_H1 * ST_H2;
    const float *w3 = b2 + ST_H2, *b3 = w3 + ST_H2;
    float h1[ST_H1], h2[ST_H2], o = *b3;
    int i, j;
    for (j = 0; j < ST_H1; j++) {
        float s = b1[j];
        for (i = 0; i < ST_NF; i++) s += w1[j * ST_NF + i] * x[i];
        h1[j] = s > 0 ? s : 0;
    }
    for (j = 0; j < ST_H2; j++) {
        float s = b2[j];
        for (i = 0; i < ST_H1; i++) s += w2[j * ST_H1 + i] * h1[i];
        h2[j] = s > 0 ? s : 0;
    }
    for (i = 0; i < ST_H2; i++) o += w3[i] * h2[i];
    return o;
}

static float rnd01(void)
{
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5;
    return (float)(g_rng >> 8) / 16777216.0f;
}

static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

/* exp for the softmax, without libm: e^x for x <= 0 */
static float expneg(float x)
{
    float r = 1, t = 1;
    int k, sq = 0;
    if (x < -30) return 0;
    while (x < -1) { x /= 2; sq++; }
    for (k = 1; k < 10; k++) { t *= x / (float)k; r += t; }
    while (sq-- > 0) r *= r;
    return r;
}

/* The state part of the features, the same for every option. */
static void state_features(const StView *v, const Cb *a, const Cb *f, float p_switch, float *x)
{
    int k, sa = 0, sf = 0;
    Match r;
    if (a) {
        r = match(a, f, v->weather);
        for (k = 0; k <= ST_S_SPE; k++) { sa += a->stage[k]; sf += f->stage[k]; }
        x[0] = a->hp; x[2] = (float)r.faster;
        x[3] = (float)imin(r.n_me, 10) / 10.0f; x[4] = (float)imin(r.n_foe, 10) / 10.0f;
        x[7] = clampf((float)sa / 6.0f, -2, 2); x[9] = a->status ? 1.0f : 0.0f;
    }
    x[1] = f->hp;
    x[5] = (float)(own_bench(v) + (a ? 1 : 0)) / 6.0f;
    x[6] = (float)(foe_bench(v) + 1) / 6.0f;
    x[8] = clampf((float)sf / 6.0f, -2, 2);
    x[10] = f->status ? 1.0f : 0.0f;
    x[11] = v->weather ? 1.0f : 0.0f;
    x[12] = (float)g_f.hz_own[0] + 0.3f * (float)g_f.hz_own[1];
    x[13] = (float)g_f.hz_foe[0] + 0.3f * (float)g_f.hz_foe[1];
    x[14] = (g_f.reflect_until > g_f.turn || g_f.screen_until > g_f.turn) ? 1.0f : 0.0f;
    x[15] = (g_f.foe_reflect_until > g_f.turn || g_f.foe_screen_until > g_f.turn) ? 1.0f : 0.0f;
    x[16] = p_switch;
    x[17] = (float)imin(v->turn, 30) / 30.0f;
    x[47] = 1.0f;
}

static void move_features(const StView *v, const Cb *a, const Cb *f, const StMove *m, float *x)
{
    int arg, r = rule_of(m->id, &arg);
    float d = damage(a, f, m, v->weather);
    x[19] = m->cat != ST_STATUS ? 1.0f : 0.0f;
    x[20] = m->cat == ST_STATUS ? 1.0f : 0.0f;
    x[21] = clampf(d, 0, 2);
    x[22] = (m->cat != ST_STATUS && d >= f->hp && ko_turns(f, d) == 1) ? 1.0f : 0.0f;
    x[23] = m->acc > 0 ? (float)m->acc / 100.0f : 1.0f;
    x[24] = m->prio > 0 ? 1.0f : m->prio < 0 ? -1.0f : 0.0f;
    x[25] = (float)m->recoil / 100.0f;
    x[26] = (float)m->heal / 100.0f;
    x[27] = m->cat != ST_STATUS ? (float)imin(ko_turns(f, d), 10) / 10.0f : 1.0f;
    x[28] = m->eff == ST_EFF_BOOST; x[29] = m->eff == ST_EFF_DROP; x[30] = m->eff == ST_EFF_STATUS;
    x[31] = m->eff == ST_EFF_HEAL; x[32] = m->eff == ST_EFF_PROTECT; x[33] = m->eff == ST_EFF_RETURN;
    x[34] = r != R_NONE;
    x[35] = r == R_REFLECT || r == R_LSCREEN || r == R_ROCKS || r == R_SPIKES || r == R_TSPIKES ||
            r == R_WEATHER || r == R_TAILWIND;
    x[36] = r == R_CONFUSE || r == R_SWAGGER || r == R_YAWN || r == R_TAUNT || r == R_PHAZE || r == R_HAZE;
    x[37] = r == R_SEED || r == R_RING || r == R_SUB;
    x[38] = r == R_OHKO || r == R_DRUM || r == R_DBOND;
    x[39] = r == R_REST || r == R_HEAL50 || r == R_SWALLOW || r == R_STOCKPILE;
    x[40] = r == R_USELESS;
}

static void switch_features(const StView *v, const Cb *a, const Cb *f, int x_idx, int entering, float *x)
{
    Cb c;
    float h;
    cb_own(&c, &v->own[x_idx]);
    h = hazard_hit(&c);
    x[18] = 1.0f;
    x[41] = clampf((float)switch_value(v, a ? a : f, f, x_idx, entering) / 1000.0f, -5, 5);
    x[42] = c.hp;
    x[43] = h;
    if (entering && a) {
        StMove bm;
        threat(f, a, v->weather, &bm);
        x[44] = bm.id ? clampf(damage(f, &c, &bm, v->weather), 0, 2) : 0.0f;
    }
}

/* All the options, scored; the choice by the scores (best, or drawn). */
static StChoice choose_options(const StView *v, const Cb *a, const Cb *f, int ai)
{
    StChoice ch = {0, 0, -1000000};
    int kind[ST_MAXOPT], idx[ST_MAXOPT], n = 0, i, best = -1, top = -1000000;
    float hand[ST_MAXOPT], fin[ST_MAXOPT], feat[ST_MAXOPT * ST_NF], p = 0, base[ST_NF];
    Cb cand;

    for (i = 0; i < ST_NF; i++) base[i] = 0;
    if (a) p = predict_switch(v, a, f, &cand);
    state_features(v, a, f, p, base);

    if (a) {
        /* the moves, as the hand-made choice scores them */
        for (i = 0; i < v->own[ai].nmoves && n < ST_MAXOPT; i++) {
            int s = score_move(v, a, f, i);
            if (s <= -50000) continue;
            if (p > 0.0f) s = (int)((1.0f - p) * (float)s + p * (float)score_move(v, a, &cand, i));
            kind[n] = 0; idx[n] = i; hand[n] = (float)s;
            { int k; for (k = 0; k < ST_NF; k++) feat[n * ST_NF + k] = base[k]; }
            move_features(v, a, f, &v->own[ai].moves[i], &feat[n * ST_NF]);
            if (s > top) top = s;
            n++;
        }
        /* the switches: worth their gain over staying, on the moves' scale.
         * Every legal one is an option, so the network can learn the ones
         * the hand-made rule would not take; those carry a penalty that
         * keeps the hand-made choice when there is no network. */
        if (g_cfg.switching && !v->trapped) {
            Match stay = match(a, f, v->weather);
            int stay_v = value(stay.n_me, stay.n_foe, stay.faster, a->hp);
            int rule_ok = g_switch_streak < 2 && top < 5000;
            for (i = 0; i < v->nown && n < ST_MAXOPT; i++) {
                int s;
                if (i == ai || !v->own[i].present || v->own[i].fainted) continue;
                s = switch_value(v, a, f, i, 1);
                kind[n] = 1; idx[n] = i;
                hand[n] = (float)(top + s - stay_v - g_cfg.switch_margin - (rule_ok && s >= 300 ? 0 : 3000));
                { int k; for (k = 0; k < ST_NF; k++) feat[n * ST_NF + k] = base[k]; }
                switch_features(v, a, f, i, 1, &feat[n * ST_NF]);
                n++;
            }
        }
    } else {
        /* a replacement after a faint */
        for (i = 0; i < v->nown && n < ST_MAXOPT; i++) {
            if (!v->own[i].present || v->own[i].fainted || v->own[i].active) continue;
            kind[n] = 1; idx[n] = i; hand[n] = (float)switch_value(v, f, f, i, 0);
            { int k; for (k = 0; k < ST_NF; k++) feat[n * ST_NF + k] = base[k]; }
            switch_features(v, 0, f, i, 0, &feat[n * ST_NF]);
            n++;
        }
    }
    if (n == 0) return ch;
    {
        float hbest = hand[0];
        for (i = 1; i < n; i++) if (hand[i] > hbest) hbest = hand[i];
        for (i = 0; i < n; i++) {
            feat[i * ST_NF + 45] = clampf(hand[i] / 1000.0f, -10, 10);
            feat[i * ST_NF + 46] = clampf((hand[i] - hbest) / 1000.0f, -10, 0);
            fin[i] = hand[i] + (g_net_on ? 1000.0f * net_forward(&feat[i * ST_NF]) : 0.0f);
        }
    }
    for (i = 0; i < n; i++) if (best < 0 || fin[i] > fin[best]) best = i;
    if (g_temp > 0.0f && n > 1) {
        /* drawn by softmax(score / (1000 * temp)) */
        float wsum = 0, r, acc = 0, wt[ST_MAXOPT];
        for (i = 0; i < n; i++) { wt[i] = expneg((fin[i] - fin[best]) / (1000.0f * g_temp)); wsum += wt[i]; }
        r = rnd01() * wsum;
        for (i = 0; i < n; i++) { acc += wt[i]; if (r <= acc) { best = i; break; } }
    }
    if (g_log_on) {
        g_opt_n = n; g_opt_chosen = best;
        for (i = 0; i < n; i++) { g_opt_hand[i] = hand[i]; g_opt_kind[i] = kind[i]; g_opt_idx[i] = idx[i]; }
        for (i = 0; i < n * ST_NF; i++) g_opt_feat[i] = feat[i];
    }
    ch.kind = kind[best]; ch.index = idx[best]; ch.score = (int32_t)fin[best];
    g_switch_streak = (ch.kind == 1 && a) ? g_switch_streak + 1 : 0;
    return ch;
}

static StChoice choose_inner(const StView *v)
{
    StChoice ch = {0, 0, -1000000};
    int ai = active_own(v), fi = active_foe(v), i;
    Cb a, f;

    if (fi < 0) {
        /* no foe in view: the first usable move, or the first Pokemon */
        if (v->request == 1 || ai < 0) {
            for (i = 0; i < v->nown; i++)
                if (v->own[i].present && !v->own[i].fainted && !v->own[i].active) {
                    ch.kind = 1; ch.index = i; ch.score = 0;
                    return ch;
                }
        }
        return ch;
    }
    cb_foe(&f, &v->foe[fi]);

    if (v->request == 1 || ai < 0) {
        Cb dummy;
        cb_foe(&dummy, &v->foe[fi]);
        if (g_net_on || g_log_on || g_temp > 0.0f)
            return choose_options(v, 0, &f, -1);
        for (i = 0; i < v->nown; i++) {
            int s;
            if (!v->own[i].present || v->own[i].fainted || v->own[i].active) continue;
            s = switch_value(v, &dummy, &f, i, 0);
            if (s > ch.score) { ch.kind = 1; ch.index = i; ch.score = s; }
        }
        g_switch_streak = 0;
        return ch;
    }

    if (g_search > 0 && search_choose(v, &ch)) {
        g_switch_streak = ch.kind == 1 ? g_switch_streak + 1 : 0;
        return ch;
    }
    cb_own(&a, &v->own[ai]);
    if (g_net_on || g_log_on || g_temp > 0.0f)
        return choose_options(v, &a, &f, ai);
    {
        /* the player may switch this turn: the move is judged against the
         * one on the field and the likely switch-in, by the odds */
        Cb cand;
        float p = predict_switch(v, &a, &f, &cand);
        for (i = 0; i < v->own[ai].nmoves; i++) {
            int s = score_move(v, &a, &f, i);
            if (p > 0.0f && s > -50000) {
                int s2 = score_move(v, &a, &cand, i);
                /* a knock out of the one on the field still counts when it
                 * stays; against the switch-in the hit lands on entry */
                s = (int)((1.0f - p) * (float)s + p * (float)s2);
            }
            if (s > ch.score) { ch.kind = 0; ch.index = i; ch.score = s; }
        }
    }
    if (g_cfg.switching && !v->trapped && g_switch_streak < 2 && ch.score < 5000) {
        Match stay = match(&a, &f, v->weather);
        int stay_v = value(stay.n_me, stay.n_foe, stay.faster, a.hp);
        int best_x = -1, best_s = -1000000;
        for (i = 0; i < v->nown; i++) {
            int s;
            if (i == ai || !v->own[i].present || v->own[i].fainted) continue;
            s = switch_value(v, &a, &f, i, 1);
            if (s > best_s) { best_s = s; best_x = i; }
        }
        /* switch only for a clear gain: losing the race where we are and
         * winning it with the other one */
        if (best_x >= 0 && best_s > stay_v + g_cfg.switch_margin && best_s >= 300) {
            ch.kind = 1; ch.index = best_x; ch.score = best_s;
            g_switch_streak++;
            return ch;
        }
    }
    g_switch_streak = 0;
    return ch;
}

StChoice st_choose(const StView *v)
{
    StChoice ch;
    g_f.turn = v->turn;
    {
        /* a foe that came in since: no lock, whatever it used before */
        int fi = active_foe(v);
        int key = fi >= 0 ? v->foe[fi].key : -1;
        if (key != g_f.foe_active_key) { g_f.foe_active_key = key; g_f.lock_key = -1; }
    }
    learn_switch(v);
    ch = choose_inner(v);
    note_choice(v, &ch);
    return ch;
}
