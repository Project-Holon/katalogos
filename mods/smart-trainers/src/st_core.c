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

void st_reset(void)
{
    int i;
    for (i = 0; i < MAXFOES; i++)
        g_foe[i].used = 0;
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
    if (!c->foe && c->item == ST_IT_CHOICE_SCARF) s *= 1.5f;
    return s;
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
    if (!a->foe && phys && a->item == ST_IT_CHOICE_BAND) A *= 1.5f;
    if (!a->foe && !phys && a->item == ST_IT_CHOICE_SPECS) A *= 1.5f;
    if (D < 1) D = 1;
    base = (float)((2 * a->level) / 5 + 2) * (float)m->power * A / D / 50.0f + 2.0f;
    mod = 0.925f * 1.0625f * (float)tm / 10.0f;
    if (m->type == a->t1 || m->type == a->t2) mod *= 1.5f;
    if (phys && a->status == ST_ST_BURN) mod *= 0.5f;
    if (w == ST_W_RAIN) { if (m->type == ST_WATER) mod *= 1.5f; if (m->type == ST_FIRE) mod *= 0.5f; }
    if (w == ST_W_SUN) { if (m->type == ST_FIRE) mod *= 1.5f; if (m->type == ST_WATER) mod *= 0.5f; }
    if ((m->type == ST_FIRE || m->type == ST_ICE) && has_ab(d, ST_AB_THICK_FAT) == 2) mod *= 0.5f;
    if (!a->foe && a->item == ST_IT_LIFE_ORB) mod *= 1.3f;
    if (imm == 1) mod *= 0.5f;
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
    int i;
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
    r.n_me = turns(f->hp, r.d_me);
    r.n_foe = turns(x->hp, r.d_foe);
    r.faster = speed(x) > speed(f);
    return r;
}

/* The yardstick: who wins the race to the knock out, and by how much. */
static int value(int n_me, int n_foe, int faster, float hp_left)
{
    int win = faster ? n_me <= n_foe : n_me < n_foe;
    int v = 100 * (imin(n_foe, 6) - imin(n_me, 6));
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
        if (faster && d >= f->hp) return 5000 + (int)(d * 100) - (m->acc > 0 && m->acc < 100 ? (100 - m->acc) * 20 : 0);
        n_me = turns(f->hp, d);
        n_foe = turns(a->hp, t);
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
        switch (m->eff) {
        case ST_EFF_BOOST:
            if (!g_cfg.setup) return -2000;
            if (has_ab(f, ST_AB_UNAWARE) == 2) return -1000;
            s = m->eff_stages;
            if (a->ability == ST_AB_CONTRARY) s = -s;
            if (a->ability == ST_AB_SIMPLE) s *= 2;
            if (s <= 0) return -2000;
            for (k = 0; k < ST_NSTAGES; k++)
                if (m->eff_stat & (1 << k)) {
                    if (a2.stage[k] >= 4) return -800;   /* enough */
                    a2.stage[k] = imin(6, a2.stage[k] + s);
                }
            if (hp_after <= 0.0f) return -1500;
            a2.hp = hp_after;
            n_me = turns(f->hp, best_move_dmg(&a2, f, w, 0));
            n_foe = turns(hp_after, threat(f, &a2, w, 0));
            /* worth it when the race after the boost (one turn later) is
             * better than attacking now */
            sc = 1000 + value(n_me + 1, n_foe + 1, speed(&a2) > speed(f), hp_after);
            if (n_me + 1 >= base_n_me) sc -= g_cfg.setup_penalty;
            return sc;
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
        case ST_EFF_PROTECT:
            return 300;
        default:
            return 200;
        }
    }
}

static int switch_value(const StView *v, const Cb *a, const Cb *f, int x, int entering_hit)
{
    Cb c;
    Match r;
    float hit = 0;
    cb_own(&c, &v->own[x]);
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

int32_t st_debug_score(const StView *v, int i)
{
    int ai = active_own(v), fi = active_foe(v);
    Cb a, f;
    if (ai < 0 || fi < 0) return -1;
    cb_own(&a, &v->own[ai]);
    cb_foe(&f, &v->foe[fi]);
    return score_move(v, &a, &f, i);
}

StChoice st_choose(const StView *v)
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
        for (i = 0; i < v->nown; i++) {
            int s;
            if (!v->own[i].present || v->own[i].fainted || v->own[i].active) continue;
            s = switch_value(v, &dummy, &f, i, 0);
            if (s > ch.score) { ch.kind = 1; ch.index = i; ch.score = s; }
        }
        g_switch_streak = 0;
        return ch;
    }

    cb_own(&a, &v->own[ai]);
    for (i = 0; i < v->own[ai].nmoves; i++) {
        int s = score_move(v, &a, &f, i);
        if (s > ch.score) { ch.kind = 0; ch.index = i; ch.score = s; }
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
