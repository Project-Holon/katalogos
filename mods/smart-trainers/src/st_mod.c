/* Smart Trainers: the eidos mod. It fills the core's view from the battle
 * (st_core.h) and replaces the trainer AI's choices with the core's.
 *
 * What it reads of the player's Pokemon is what a player sees: species,
 * level, types, the HP bar, stat stages, status; and what the battle shows:
 * the moves used, the abilities and items that announce themselves, the
 * damage as the HP bar moves. The trainer's own Pokemon are read in full.
 */
#include "eidos_wasm.h"
#include "st_core.h"

#define SIDE_PLAYER 0
#define SIDE_AI 1

static StView g_v;
static int g_in_battle;
static int g_turn_first;          /* side that moved first this turn, -1 none */
static uint16_t g_turn_move[2];   /* the move each side used first this turn */
static uint32_t g_seen_keys[12];
static int g_nseen;
static int g_choice_turn = -1;
static StChoice g_choice;

/* ------------------------------------------------------------ logging */

static char g_buf[200];
static unsigned g_pos;
static void put(const char *s) { while (*s && g_pos < sizeof g_buf - 1) g_buf[g_pos++] = *s++; }
static void num(long v)
{
    char t[16];
    int n = 0;
    if (v < 0) { put("-"); v = -v; }
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 15);
    while (n) { char c[2] = { t[--n], 0 }; put(c); }
}
static void out(void) { eidos_log(g_buf, g_pos); g_pos = 0; }

/* ------------------------------------------------------------ reads */

static uint32_t bget(const EidosMonRef *r, uint32_t field)
{
    uint32_t v = 0;
    eidos_battle_get(r->handle, r->place, r->side, r->box, r->slot, field, &v);
    return v;
}

static uint32_t mv(uint32_t move, uint32_t field)
{
    uint32_t v = 0;
    eidos_move_value(move, field, &v);
    return v;
}

static uint32_t sp(uint32_t species, uint32_t field)
{
    uint32_t v = 0;
    eidos_species_value(species, field, &v);
    return v;
}

/* The game's condition id (1 paralysis, 2 sleep, 3 freeze, 4 burn,
 * 5 poison) to the core's. */
static int32_t status_of(uint32_t s)
{
    switch (s) {
    case 1: return ST_ST_PARALYSIS;
    case 2: return ST_ST_SLEEP;
    case 3: return ST_ST_FREEZE;
    case 4: return ST_ST_BURN;
    case 5: return ST_ST_POISON;
    default: return ST_ST_NONE;
    }
}

/* A move from the game's move record (waza_tool.c's fields). */
static void fill_move(StMove *m, uint32_t id)
{
    int i;
    uint32_t dmg, sick, hmin, hmax;
    char *p = (char *)m;
    for (i = 0; i < (int)sizeof *m; i++) p[i] = 0;
    if (!id) return;
    m->id = (int32_t)id;
    m->type = (int32_t)mv(id, 0);
    dmg = mv(id, 2);                        /* 0 none, 1 physical, 2 special */
    m->cat = dmg == 1 ? ST_PHYSICAL : dmg == 2 ? ST_SPECIAL : ST_STATUS;
    m->power = (int32_t)mv(id, 3);
    m->acc = (int32_t)mv(id, 4);
    if (m->acc >= 100) m->acc = 0;
    m->prio = (int32_t)(int8_t)mv(id, 6);
    m->pp = 10;
    hmax = mv(id, 8); hmin = mv(id, 9);
    m->hits = hmax > 1 ? (hmin == 2 && hmax == 5 ? 31 : (int32_t)(hmin + hmax) * 5) : 10;
    m->heal = (int32_t)mv(id, 25);           /* drain */
    m->recoil = (int32_t)mv(id, 30);
    if (m->cat != ST_STATUS && m->power <= 1) {
        switch (id) {
        /* power from the weight, HP, speed, friendship...: about 60 */
        case 67: case 447: case 360: case 486: case 216: case 218: case 175: case 179:
        case 484: case 535: case 386: case 376: case 378: case 462: case 237: case 363:
        case 222: case 217: case 374: case 251:
            m->power = 60;
            break;
        case 69: case 101: m->fixed = -1; break;   /* Seismic Toss, Night Shade: the level */
        case 49: m->fixed = 20; break;   /* Sonic Boom */
        case 82: m->fixed = 40; break;   /* Dragon Rage */
        default:
            /* Bide, Counter, Mirror Coat, Endeavor, Super Fang and the
             * like: no damage estimate */
            m->cat = ST_STATUS;
            m->power = 0;
            m->eff = ST_EFF_OTHER;
            break;
        }
    }
    if (id == 69 || id == 101) { m->fixed = -1; m->power = 0; }   /* Seismic Toss, Night Shade */
    if (id == 120 || id == 153) m->self_ko = 1;                   /* Selfdestruct, Explosion */
    sick = mv(id, 11);
    if (m->cat == ST_STATUS) {
        int32_t heal = (int32_t)mv(id, 26);
        int32_t r0 = (int32_t)(int8_t)mv(id, 17);
        if (id == 182 || id == 197) m->eff = ST_EFF_PROTECT;
        else if (heal > 0) { m->eff = ST_EFF_HEAL; m->heal = heal; }
        else if (sick >= 1 && sick <= 5) {
            m->eff = ST_EFF_STATUS;
            m->eff_status = id == 92 ? ST_ST_TOXIC : status_of(sick);
        } else if (r0 != 0) {
            int k, mask = 0, st = 0;
            for (k = 0; k < 3; k++) {
                int32_t t = (int32_t)mv(id, 16 + 3 * k), v = (int32_t)(int8_t)mv(id, 17 + 3 * k);
                if (t >= 1 && t <= 7 && v) { mask |= 1 << (t - 1); if ((v < 0 ? -v : v) > st) st = v < 0 ? -v : v; }
            }
            m->eff = r0 > 0 ? ST_EFF_BOOST : ST_EFF_DROP;
            m->eff_stat = mask;
            m->eff_stages = st;
        } else m->eff = ST_EFF_OTHER;
    } else {
        int32_t t = (int32_t)mv(id, 16), v = (int32_t)(int8_t)mv(id, 17);
        if (t >= 1 && t <= 7 && v < 0) {
            m->eff = ST_EFF_DROP;
            m->eff_stat = 1 << (t - 1);
            m->eff_stages = -v;
            m->eff_chance = (int32_t)mv(id, 18);
        }
    }
}

/* The HP bar as a player sees it: 48 pixels, in 1/1000. */
static int32_t bar(uint32_t hp, uint32_t max)
{
    uint32_t px;
    if (!hp || !max) return 0;
    px = (hp * 48u + max / 2u) / max;
    if (px < 1) px = 1;
    return (int32_t)(px * 1000u / 48u);
}

static int seen_before(uint32_t key)
{
    int i;
    for (i = 0; i < g_nseen; i++)
        if (g_seen_keys[i] == key) return 1;
    if (g_nseen < 12) g_seen_keys[g_nseen++] = key;
    return 0;
}

/* The moves a player's Pokemon could know: its level-up moves up to its
 * level (public knowledge); the core keeps the strongest per type. */
static void candidates(uint32_t key, uint32_t species, uint32_t level)
{
    uint32_t m;
    StMove x;
    for (m = 1; m <= 559; m++) {
        if (eidos_species_learns(species, 0, m, level) <= 0) continue;
        if (mv(m, 2) == 0 || mv(m, 3) <= 1) continue;
        fill_move(&x, m);
        st_foe_candidate((int32_t)key, &x);
    }
}

static void fill_mon(StMon *s, int side, int slot)
{
    EidosMonRef r;
    int i;
    char *p = (char *)s;
    for (i = 0; i < (int)sizeof *s; i++) p[i] = 0;
    if (eidos_battle_ref((uint32_t)side, (uint32_t)slot, &r) != 0) return;
    s->species = (int32_t)bget(&r, EIDOS_MON_FIELD_SPECIES);
    if (!s->species) return;
    s->present = 1;
    s->key = (int32_t)bget(&r, EIDOS_MON_FIELD_PERSONALITY);
    s->level = (int32_t)bget(&r, EIDOS_MON_FIELD_LEVEL);
    s->type1 = (int32_t)bget(&r, EIDOS_MON_FIELD_TYPE1);
    s->type2 = (int32_t)bget(&r, EIDOS_MON_FIELD_TYPE2);
    {
        uint32_t hp = bget(&r, EIDOS_MON_FIELD_HP), max = bget(&r, EIDOS_MON_FIELD_MAX_HP);
        s->fainted = hp == 0;
        if (side == SIDE_AI) { s->hp = (int32_t)hp; s->maxhp = (int32_t)max; }
        else { s->hp = bar(hp, max); s->maxhp = 1000; }
    }
    s->stage[ST_S_ATK] = (int32_t)bget(&r, EIDOS_MON_FIELD_STAGE_ATK) - 6;
    s->stage[ST_S_DEF] = (int32_t)bget(&r, EIDOS_MON_FIELD_STAGE_DEF) - 6;
    s->stage[ST_S_SPA] = (int32_t)bget(&r, EIDOS_MON_FIELD_STAGE_SPATK) - 6;
    s->stage[ST_S_SPD] = (int32_t)bget(&r, EIDOS_MON_FIELD_STAGE_SPDEF) - 6;
    s->stage[ST_S_SPE] = (int32_t)bget(&r, EIDOS_MON_FIELD_STAGE_SPEED) - 6;
    s->stage[ST_S_ACC] = (int32_t)bget(&r, EIDOS_MON_FIELD_STAGE_ACCURACY) - 6;
    s->stage[ST_S_EVA] = (int32_t)bget(&r, EIDOS_MON_FIELD_STAGE_EVASION) - 6;
    s->status = status_of(bget(&r, EIDOS_MON_FIELD_STATUS));
    s->active = slot == 0;   /* singles: the front position */
    s->choice_lock = -1;
    if (side == SIDE_AI) {
        /* its own Pokemon: everything */
        s->ability = (int32_t)bget(&r, EIDOS_MON_FIELD_BATTLE_ABILITY);
        s->item = (int32_t)bget(&r, EIDOS_MON_FIELD_ITEM);
        s->stat[ST_HP] = s->maxhp;
        s->stat[ST_ATK] = (int32_t)bget(&r, EIDOS_MON_FIELD_ATK);
        s->stat[ST_DEF] = (int32_t)bget(&r, EIDOS_MON_FIELD_DEF);
        s->stat[ST_SPA] = (int32_t)bget(&r, EIDOS_MON_FIELD_SPATK);
        s->stat[ST_SPD] = (int32_t)bget(&r, EIDOS_MON_FIELD_SPDEF);
        s->stat[ST_SPE] = (int32_t)bget(&r, EIDOS_MON_FIELD_SPEED);
        for (i = 0; i < 4; i++) {
            uint32_t m = bget(&r, EIDOS_MON_FIELD_MOVE1 + (uint32_t)i);
            if (!m) break;
            fill_move(&s->moves[i], m);
            s->nmoves = i + 1;
        }
    } else {
        /* the player's: public knowledge only */
        uint32_t spc = (uint32_t)s->species;
        s->stat[ST_HP] = (int32_t)sp(spc, 0);
        s->stat[ST_ATK] = (int32_t)sp(spc, 1);
        s->stat[ST_DEF] = (int32_t)sp(spc, 2);
        s->stat[ST_SPE] = (int32_t)sp(spc, 3);
        s->stat[ST_SPA] = (int32_t)sp(spc, 4);
        s->stat[ST_SPD] = (int32_t)sp(spc, 5);
        s->abilities[0] = (int32_t)sp(spc, 26);
        s->abilities[1] = (int32_t)sp(spc, 27);
        if (!seen_before((uint32_t)s->key))
            candidates((uint32_t)s->key, spc, (uint32_t)s->level);
    }
}

static void fill_view(int request)
{
    int i;
    g_v.weather = eidos_battle_weather();
    g_v.request = request;
    g_v.trapped = 0;
    g_v.nown = g_v.nfoe = 0;
    for (i = 0; i < 6; i++) {
        fill_mon(&g_v.own[i], SIDE_AI, i);
        if (g_v.own[i].present) g_v.nown = i + 1;
        fill_mon(&g_v.foe[i], SIDE_PLAYER, i);
        if (g_v.foe[i].present) g_v.nfoe = i + 1;
    }
}

static int own_index_of(const EidosMonRef *r)
{
    return r->side == SIDE_AI ? r->slot : -1;
}

/* ------------------------------------------------------------ events */

static int32_t on_start(void *p)
{
    (void)p;
    st_reset();
    g_in_battle = 1;
    g_nseen = 0;
    g_choice_turn = -1;
    g_turn_first = -1;
    g_v.turn = 0;
    return EIDOS_EVENT_CONTINUE;
}

static int32_t on_turn(void *p)
{
    EidosBattleTurn *e = (EidosBattleTurn *)p;
    /* who moved first last turn, when both used priority 0 moves */
    if (g_turn_first >= 0 && g_turn_move[0] && g_turn_move[1] &&
        mv(g_turn_move[0], 6) == 0 && mv(g_turn_move[1], 6) == 0) {
        EidosMonRef r;
        fill_view(0);
        if (eidos_battle_ref(SIDE_PLAYER, 0, &r) == 0)
            st_observe_order(&g_v, (int32_t)bget(&r, EIDOS_MON_FIELD_PERSONALITY), g_turn_first == SIDE_PLAYER, 0);
    }
    g_turn_first = -1;
    g_turn_move[0] = g_turn_move[1] = 0;
    g_v.turn = e->turn;
    return EIDOS_EVENT_CONTINUE;
}

static int32_t on_move(void *p)
{
    EidosBattleMove *e = (EidosBattleMove *)p;
    int side = e->user.side;
    if (side > 1) return EIDOS_EVENT_CONTINUE;
    if (g_turn_first < 0) g_turn_first = side;
    if (!g_turn_move[side]) g_turn_move[side] = e->move;
    if (side == SIDE_PLAYER) {
        StMove m;
        fill_move(&m, e->move);
        st_observe_foe_move((int32_t)bget(&e->user, EIDOS_MON_FIELD_PERSONALITY), &m);
    }
    return EIDOS_EVENT_CONTINUE;
}

static int32_t on_damage(void *p)
{
    EidosBattleDamage *e = (EidosBattleDamage *)p;
    StMove m;
    uint32_t dmg = e->amount < e->target_hp ? e->amount : e->target_hp;
    fill_move(&m, e->move);
    fill_view(0);
    if (e->attacker.side == SIDE_PLAYER && e->target.side == SIDE_AI) {
        st_observe_damage_taken(&g_v, (int32_t)bget(&e->attacker, EIDOS_MON_FIELD_PERSONALITY), &m,
                                own_index_of(&e->target), (int32_t)dmg, e->critical);
    } else if (e->attacker.side == SIDE_AI && e->target.side == SIDE_PLAYER) {
        st_observe_damage_dealt(&g_v, own_index_of(&e->attacker),
                                (int32_t)bget(&e->target, EIDOS_MON_FIELD_PERSONALITY), &m,
                                bar(e->target_hp, e->target_max_hp),
                                bar(e->target_hp - dmg, e->target_max_hp), e->critical);
    }
    return EIDOS_EVENT_CONTINUE;
}

static int32_t on_ability(void *p)
{
    EidosBattleAbility *e = (EidosBattleAbility *)p;
    if (e->side == SIDE_PLAYER)
        st_observe_foe_ability((int32_t)bget(&e->mon, EIDOS_MON_FIELD_PERSONALITY), e->ability);
    return EIDOS_EVENT_CONTINUE;
}

static int32_t on_item(void *p)
{
    EidosBattleHeldItem *e = (EidosBattleHeldItem *)p;
    if (e->side == SIDE_PLAYER)
        st_observe_foe_item((int32_t)bget(&e->mon, EIDOS_MON_FIELD_PERSONALITY), e->item);
    return EIDOS_EVENT_CONTINUE;
}

int32_t st_debug_score(const StView *v, int i);

static void decide(void)
{
    if (g_choice_turn == g_v.turn) return;
    fill_view(0);
    g_choice = st_choose(&g_v);
    g_choice_turn = g_v.turn;
    if (eidos_setting_get("smarttrainers.debug") > 0) {
        const StMon *f = &g_v.foe[0], *a = &g_v.own[0];
        int i;
        put("view: foe sp "); num(f->species); put(" lv "); num(f->level); put(" t "); num(f->type1); put("/"); num(f->type2);
        put(" hp "); num(f->hp); put(" base "); num(f->stat[0]); put(","); num(f->stat[1]); put(","); num(f->stat[2]);
        put(" st def "); num(f->stage[ST_S_DEF]); put(" act "); num(f->active); out();
        put("view: own sp "); num(a->species); put(" lv "); num(a->level); put(" hp "); num(a->hp); put("/"); num(a->maxhp);
        put(" atk "); num(a->stat[1]); put(" moves");
        for (i = 0; i < a->nmoves; i++) {
            put(" "); num(a->moves[i].id); put(":"); num(a->moves[i].power); put("/"); num(a->moves[i].cat);
            put("="); num(st_debug_score(&g_v, i));
        }
        out();
    }
}

static int32_t on_ai(void *p)
{
    EidosBattleAi *e = (EidosBattleAi *)p;
    if (e->mon.side != SIDE_AI || eidos_battle_wild() == 1) return EIDOS_EVENT_CONTINUE;
    if (e->stage == EIDOS_AI_SWITCH) {
        decide();
        if (g_choice.kind == 1 && g_choice.index != e->mon.slot) {
            e->switch_to = (uint8_t)g_choice.index;
            put("turn "); num(g_v.turn); put(": switch to "); num(g_choice.index);
            put(" (score "); num(g_choice.score); put(")"); out();
        } else {
            e->switch_to = 6;
        }
    } else if (e->stage == EIDOS_AI_MOVE) {
        decide();
        if (g_choice.kind == 0 && g_choice.index >= 0 && g_choice.index < 4) {
            put("turn "); num(g_v.turn); put(": move "); num(g_choice.index);
            put(" (game picked "); num(e->move); put(", score "); num(g_choice.score); put(")"); out();
            e->move = (uint8_t)g_choice.index;
        }
    }
    return EIDOS_EVENT_CONTINUE;
}

static int32_t on_replace(void *p)
{
    EidosBattleAiReplace *e = (EidosBattleAiReplace *)p;
    StChoice c;
    int i, at = -1;
    if (eidos_battle_wild() == 1 || e->count < 2) return EIDOS_EVENT_CONTINUE;
    fill_view(1);
    c = st_choose(&g_v);
    if (c.kind != 1) return EIDOS_EVENT_CONTINUE;
    for (i = 0; i < e->count; i++)
        if (e->order[i] == c.index) at = i;
    if (at > 0) {
        uint8_t t = e->order[0];
        e->order[0] = e->order[at];
        e->order[at] = t;
        put("replace: "); num(c.index); put(" first (score "); num(c.score); put(")"); out();
    }
    return EIDOS_EVENT_CONTINUE;
}

static int32_t on_end(void *p)
{
    EidosBattleEnd *e = (EidosBattleEnd *)p;
    if (!g_in_battle) return EIDOS_EVENT_CONTINUE;
    g_in_battle = 0;
    put("battle end: trainer "); num(e->trainer_id[0]); put(" outcome "); num(e->outcome);
    put(" turns "); num(e->turns); out();
    return EIDOS_EVENT_CONTINUE;
}

int32_t eidos_mod_start(void)
{
    StConfig c = {1, 1, 1, 250, 200};
    st_config(&c);
    eidos_on(EIDOS_EVENT_BATTLE_START, (uint32_t)(uintptr_t)on_start);
    eidos_on(EIDOS_EVENT_BATTLE_TURN, (uint32_t)(uintptr_t)on_turn);
    eidos_on(EIDOS_EVENT_BATTLE_MOVE, (uint32_t)(uintptr_t)on_move);
    eidos_on(EIDOS_EVENT_BATTLE_DAMAGE, (uint32_t)(uintptr_t)on_damage);
    eidos_on(EIDOS_EVENT_BATTLE_ABILITY, (uint32_t)(uintptr_t)on_ability);
    eidos_on(EIDOS_EVENT_BATTLE_HELD_ITEM, (uint32_t)(uintptr_t)on_item);
    eidos_on(EIDOS_EVENT_BATTLE_AI, (uint32_t)(uintptr_t)on_ai);
    eidos_on(EIDOS_EVENT_BATTLE_AI_REPLACE, (uint32_t)(uintptr_t)on_replace);
    eidos_on(EIDOS_EVENT_BATTLE_END, (uint32_t)(uintptr_t)on_end);
    return 0;
}

void eidos_mod_stop(void) {}
