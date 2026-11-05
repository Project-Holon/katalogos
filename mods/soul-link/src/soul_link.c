/*
 * Soul Link: a Nuzlocke for two or more players, each in their own game of
 * the same eidos multiplayer session.
 *
 * - Each player may catch only the first wild Pokemon of each area (dupes
 *   and shiny clauses as options).
 * - The Pokemon the players catch in the same area are linked.  The link is
 *   the Pokemon's met location, which is the area's place name, so no list
 *   of pairs is kept: every game finds its own half by itself.
 * - When a linked Pokemon faints or is released, or a player's first
 *   encounter in an area fails (it ran or fainted without being caught),
 *   the area is lost: every player's Pokemon from it goes too.
 * - With "Keep pairs together", a pair is in the party or in the boxes on
 *   every side: when one player boxes theirs, the others' follow.
 *
 * The players share these numbers through the session's shared table
 * (eidos interface 5): `lost.<area>` (1 once the area is lost) and
 * `boxed.<area>` (1 boxed, 0 in the party).  Each game acts on its own
 * Pokemon only.  Outside a multiplayer session the mod does nothing.
 *
 * Built for wasm32 with no libc (build.sh); the imports are eidos_wasm.h.
 */
#include "eidos_wasm.h"

#define GRAVE_BOX   23u     /* box 24: where the fallen go with "box" */
#define PARTY_MAX   6u
#define BOX_SLOTS   30u
#define SWEEP_EVERY 30u     /* frames between two looks at the shared table */
#define HANDLED_MAX 64u

/* ------------------------------------------------------------------ */
/* Small helpers (no libc)                                             */
/* ------------------------------------------------------------------ */

static char g_line[160];
static unsigned g_len;

static void put(const char *s)
{
    while (*s != '\0' && g_len < sizeof g_line - 1u) {
        g_line[g_len++] = *s++;
    }
    g_line[g_len] = '\0';
}

static void put_num(uint32_t v)
{
    char t[12];
    int n = 0;

    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v != 0u && n < 11);
    while (n > 0 && g_len < sizeof g_line - 1u) {
        g_line[g_len++] = t[--n];
    }
    g_line[g_len] = '\0';
}

static const char *line_start(const char *s)
{
    g_len = 0;
    g_line[0] = '\0';
    put(s);
    return g_line;
}

static void log_line(void)
{
    eidos_log(g_line, g_len);
}

/* `<what>.<area>`, in a buffer of its own (the line buffer is for text). */
static const char *key(const char *what, uint32_t area)
{
    static char k[32];
    char t[12];
    unsigned n = 0;
    int d = 0;

    while (*what != '\0' && n < sizeof k - 13u) {
        k[n++] = *what++;
    }
    k[n++] = '.';
    do {
        t[d++] = (char)('0' + area % 10u);
        area /= 10u;
    } while (area != 0u && d < 11);
    while (d > 0) {
        k[n++] = t[--d];
    }
    k[n] = '\0';
    return k;
}

/* A shared value: 1 when the key holds `want`; 0 when it holds something
 * else or was never set; -1 outside a session. */
static int shared_is(const char *k, int32_t want)
{
    int32_t v = 0;
    int rc = eidos_shared_get(k, &v);

    if (rc < 0) {
        return -1;
    }
    return rc == 0 && v == want;
}

static int in_session(void)
{
    int32_t v;

    return eidos_shared_get("session", &v) >= 0;
}

static int setting(const char *k)
{
    return eidos_setting_get(k);
}

/* ------------------------------------------------------------------ */
/* The Pokemon                                                         */
/* ------------------------------------------------------------------ */

#define REF_ARGS(r) (r).handle, (r).place, (r).side, (r).box, (r).slot

static uint32_t party_field(const EidosMonRef *r, uint32_t field)
{
    uint32_t v = 0;

    (void)eidos_party_get(REF_ARGS(*r), field, &v);
    return v;
}

static uint32_t box_field(const EidosMonRef *r, uint32_t field)
{
    uint32_t v = 0;

    (void)eidos_box_get(REF_ARGS(*r), field, &v);
    return v;
}

/*
 * The links are remembered, not guessed: when a player catches or receives
 * a Pokemon, its game writes `mon.<personality>` = the area to the shared
 * table, which the host keeps with its game.  A Pokemon is linked when the
 * table holds it; one traded in, or one from before the mod, is not.
 */
static uint32_t linked_area(uint32_t personality)
{
    int32_t v = 0;

    if (eidos_shared_get(key("mon", personality), &v) == 0 && v > 0) {
        return (uint32_t)v;
    }
    return 0;
}

static void remember(uint32_t personality, uint32_t area)
{
    if (area != 0u && personality != 0u) {
        (void)eidos_shared_set(key("mon", personality), (int32_t)area);
    }
}

static uint32_t party_area(const EidosMonRef *r)
{
    return linked_area(party_field(r, EIDOS_MON_FIELD_PERSONALITY));
}

static uint32_t box_area(const EidosMonRef *r)
{
    return linked_area(box_field(r, EIDOS_MON_FIELD_PERSONALITY));
}

/* Whether this game holds a linked Pokemon (not an egg) of `area`. */
static int owns_from(uint32_t area)
{
    uint32_t n = eidos_party_count(), i, b, s, boxes = eidos_box_count();
    EidosMonRef r;

    for (i = 0; i < n; i++) {
        if (eidos_party_ref(i, &r) == 0 && !party_field(&r, EIDOS_MON_FIELD_IS_EGG) &&
            party_area(&r) == area) {
            return 1;
        }
    }
    for (b = 0; b < boxes; b++) {
        for (s = 0; s < BOX_SLOTS; s++) {
            if (eidos_box_ref(b, s, &r) == 0 && box_field(&r, EIDOS_MON_FIELD_SPECIES) != 0u &&
                !box_field(&r, EIDOS_MON_FIELD_IS_EGG) && box_area(&r) == area) {
                return 1;
            }
        }
    }
    return 0;
}

/* The Pokemon this game already sent away, by personality, so a sweep
 * does not send one twice while the game applies it. */
static uint32_t g_handled[HANDLED_MAX];
static uint32_t g_handled_count;

static int handled(uint32_t personality)
{
    uint32_t i;

    for (i = 0; i < g_handled_count; i++) {
        if (g_handled[i] == personality) {
            return 1;
        }
    }
    return 0;
}

static void mark_handled(uint32_t personality)
{
    if (g_handled_count < HANDLED_MAX) {
        g_handled[g_handled_count++] = personality;
    } else {
        g_handled[g_handled_count++ % HANDLED_MAX] = personality;
    }
}

/* An area is lost: this game's own Pokemon from it are released or go to
 * box 24 (the option). */
static void mark_lost(uint32_t area)
{
    if (area != 0u) {
        (void)eidos_shared_set(key("lost", area), 1);
        line_start("area lost: ");
        put_num(area);
        log_line();
    }
}

/* ------------------------------------------------------------------ */
/* The encounter in progress                                           */
/* ------------------------------------------------------------------ */

static int32_t g_area = -1;   /* the area of the wild battle, or -1 */
static int g_first;           /* it is this player's first encounter there */
static int g_caught;          /* the battle ended in a catch */
static int g_link_next;       /* the next caught Pokemon stored is a pair's */
static int g_told_no_session;

static int own_event(void)
{
    return eidos_event_player() == 0u;   /* not another player's */
}

/* The encounter rule starts once the player has a ball: an encounter met
 * with none in the bag cannot be caught, so it does not use the area.
 * The balls are items 1 to 16 and the Dream Ball, 576. */
static int has_balls(void)
{
    uint32_t i;

    for (i = 1; i <= 16u; i++) {
        if (eidos_item_count(i) > 0) {
            return 1;
        }
    }
    return eidos_item_count(576) > 0;
}

static int32_t on_encounter_wild(void *p)
{
    EidosEncounterWild *e = (EidosEncounterWild *)p;
    int32_t area;

    if (!own_event()) {
        return EIDOS_EVENT_CONTINUE;
    }
    area = eidos_area_of(e->zone);
    g_area = area;
    g_first = 0;
    g_caught = 0;
    if (!in_session()) {
        if (!g_told_no_session) {
            g_told_no_session = 1;
            eidos_message("Soul Link needs a multiplayer game: host or join one.");
        }
        g_area = -1;
        return EIDOS_EVENT_CONTINUE;
    }
    if (area <= 0) {
        return EIDOS_EVENT_CONTINUE;
    }
    if (!has_balls()) {
        g_area = -1;            /* no ball yet: the encounter does not count */
        return EIDOS_EVENT_CONTINUE;
    }
    if (shared_is(key("lost", (uint32_t)area), 1) == 1) {
        return EIDOS_EVENT_CONTINUE;             /* the area is gone for all */
    }
    if (owns_from((uint32_t)area)) {
        return EIDOS_EVENT_CONTINUE;             /* this player caught here */
    }
    if (setting("soul_link.dupes") == 1 && eidos_dex_caught(e->species) == 1) {
        return EIDOS_EVENT_CONTINUE;             /* a dupe: the next one counts */
    }
    g_first = 1;
    return EIDOS_EVENT_CONTINUE;
}

static int32_t on_catch_attempt(void *p)
{
    EidosCatchAttempt *e = (EidosCatchAttempt *)p;

    if (!own_event() || g_area < 0) {
        return EIDOS_EVENT_CONTINUE;
    }
    if (g_first || e->static_ != 0u ||
        (setting("soul_link.shiny") == 1 && e->shiny != 0u)) {
        return EIDOS_EVENT_CONTINUE;
    }
    eidos_message("Not this area's first encounter: it cannot be caught.");
    return EIDOS_EVENT_REFUSE;
}

static int32_t on_catch_result(void *p)
{
    EidosCatchResult *e = (EidosCatchResult *)p;

    if (own_event() && e->caught != 0u) {
        g_caught = 1;
        g_link_next = g_first;   /* stored a moment later (mon.receive) */
    }
    return EIDOS_EVENT_CONTINUE;
}

/* No healing a Pokemon whose pair is gone, in battle. */
static int doomed_battle(const EidosMonRef *m);

static int32_t on_heal_hp(void *p)
{
    EidosHealHp *e = (EidosHealHp *)p;

    if (own_event() && in_session() && e->mon.place == EIDOS_MON_BATTLE && doomed_battle(&e->mon)) {
        eidos_message("Its pair is gone: it cannot be healed.");
        return EIDOS_EVENT_REFUSE;
    }
    return EIDOS_EVENT_CONTINUE;
}

static int32_t on_battle_end(void *p)
{
    EidosBattleEnd *e = (EidosBattleEnd *)p;

    if (!own_event()) {
        return EIDOS_EVENT_CONTINUE;
    }
    if (e->wild != 0u && g_area > 0 && g_first && !g_caught &&
        e->outcome != EIDOS_OUTCOME_CAUGHT) {
        /* The first encounter got away: the area is lost for everyone. */
        mark_lost((uint32_t)g_area);
        eidos_message(line_start("The first encounter here got away. This area is lost for every player."));
    }
    g_area = -1;
    g_first = 0;
    g_caught = 0;
    return EIDOS_EVENT_CONTINUE;
}

/* A Pokemon caught or received: it is remembered with the area it came
 * from (the area of the zone it was received in). */
static int32_t on_mon_receive(void *p)
{
    EidosMonReceive *e = (EidosMonReceive *)p;
    uint32_t pers;
    int32_t area;

    if (!own_event() || !in_session()) {
        return EIDOS_EVENT_CONTINUE;
    }
    /* What is linked: the area's first encounter, caught, and the gifts
     * (the starter is one: every player gets theirs in the same town).  A
     * shiny or a story Pokemon caught outside the rule, an egg and a trade
     * are not part of a pair. */
    if (e->source == EIDOS_RECEIVE_CATCH) {
        int link = g_link_next;

        g_link_next = 0;
        if (!link) {
            return EIDOS_EVENT_CONTINUE;
        }
    } else if (e->source != EIDOS_RECEIVE_GIFT) {
        return EIDOS_EVENT_CONTINUE;
    }
    pers = e->mon.place == EIDOS_MON_BOX ? box_field(&e->mon, EIDOS_MON_FIELD_PERSONALITY)
                                         : party_field(&e->mon, EIDOS_MON_FIELD_PERSONALITY);
    area = eidos_area_of(e->zone);
    if (area > 0) {
        remember(pers, (uint32_t)area);
    }
    return EIDOS_EVENT_CONTINUE;
}

/* The area a battle Pokemon of the player's is linked to. */
static uint32_t battle_area(const EidosMonRef *m)
{
    uint32_t pers = 0;

    if (eidos_battle_get(REF_ARGS(*m), EIDOS_MON_FIELD_PERSONALITY, &pers) != 0) {
        return 0;
    }
    return linked_area(pers);
}

static int32_t on_battle_faint(void *p)
{
    EidosBattleFaint *e = (EidosBattleFaint *)p;

    if (!own_event() || e->side != 0u || !in_session()) {
        return EIDOS_EVENT_CONTINUE;
    }
    mark_lost(battle_area(&e->mon));
    return EIDOS_EVENT_CONTINUE;
}

static int32_t on_mon_release(void *p)
{
    EidosMonRelease *e = (EidosMonRelease *)p;

    if (!own_event() || !in_session()) {
        return EIDOS_EVENT_CONTINUE;
    }
    /* Releasing one half of a pair loses the other too. */
    if (e->mon.place == EIDOS_MON_PARTY) {
        mark_lost(party_area(&e->mon));
    } else if (e->mon.place == EIDOS_MON_BOX) {
        mark_lost(box_area(&e->mon));
    }
    return EIDOS_EVENT_CONTINUE;
}

static int32_t on_box_deposit(void *p)
{
    EidosBoxDeposit *e = (EidosBoxDeposit *)p;
    uint32_t area;

    if (!own_event() || !in_session() || setting("soul_link.together") != 1 ||
        e->box == GRAVE_BOX) {
        return EIDOS_EVENT_CONTINUE;
    }
    area = party_area(&e->mon);
    if (area != 0u) {
        (void)eidos_shared_set(key("boxed", area), 1);
    }
    return EIDOS_EVENT_CONTINUE;
}

static int32_t on_box_withdraw(void *p)
{
    EidosBoxWithdraw *e = (EidosBoxWithdraw *)p;
    uint32_t area;

    if (!own_event() || !in_session()) {
        return EIDOS_EVENT_CONTINUE;
    }
    area = box_area(&e->mon);
    if (area != 0u && shared_is(key("lost", area), 1) == 1) {
        eidos_message("This Pokemon's pair is gone: it cannot come back.");
        return EIDOS_EVENT_REFUSE;
    }
    if (area != 0u && setting("soul_link.together") == 1) {
        (void)eidos_shared_set(key("boxed", area), 0);
    }
    return EIDOS_EVENT_CONTINUE;
}

/* ------------------------------------------------------------------ */
/* In a battle: a Pokemon whose pair is gone                           */
/* ------------------------------------------------------------------ */

/*
 * When a pair is lost while this player is in a battle, the linked Pokemon
 * does not wait for the battle to end:
 * - on the bench, it faints at once (its HP goes to 0);
 * - on the field, it cannot be switched out, and it faints at its next
 *   attack (after the move) or at the next hit it takes (the hit is made
 *   lethal), whichever comes first.
 * Once it has fainted, the game's own faint follows, and it is released or
 * boxed after the battle like any lost Pokemon.
 */
static uint32_t g_on_field = 1;   /* the player's Pokemon on the field */

static int doomed_battle(const EidosMonRef *m)
{
    uint32_t pers = 0, hp = 0, area;

    if (m->side != 0u ||
        eidos_battle_get(REF_ARGS(*m), EIDOS_MON_FIELD_PERSONALITY, &pers) != 0 ||
        eidos_battle_get(REF_ARGS(*m), EIDOS_MON_FIELD_HP, &hp) != 0 || hp == 0u) {
        return 0;
    }
    area = linked_area(pers);
    return area != 0u && shared_is(key("lost", area), 1) == 1;
}

static void faint_now(const EidosMonRef *m)
{
    (void)eidos_battle_set(REF_ARGS(*m), EIDOS_MON_FIELD_HP, 0);
}

/* The bench: a doomed Pokemon that is not on the field faints at once.
 * The battle keeps the player's Pokemon on the field at the front of its
 * order, so a position past the field's count is the bench. */
static void battle_bench_sweep(void)
{
    uint32_t slot;
    EidosMonRef r;

    for (slot = g_on_field; slot < PARTY_MAX; slot++) {
        if (eidos_battle_ref(0, slot, &r) == 0 && doomed_battle(&r)) {
            faint_now(&r);
            eidos_message("Its pair is gone: a Pokemon on the bench fainted.");
        }
    }
}

static int32_t on_battle_start(void *p)
{
    EidosBattleStart *e = (EidosBattleStart *)p;

    if (!own_event()) {
        return EIDOS_EVENT_CONTINUE;
    }
    g_on_field = e->format == EIDOS_FORMAT_DOUBLE ? 2u
               : (e->format == EIDOS_FORMAT_TRIPLE || e->format == EIDOS_FORMAT_ROTATION) ? 3u
               : 1u;
    return EIDOS_EVENT_CONTINUE;
}

static int32_t on_battle_action(void *p)
{
    EidosBattleAction *e = (EidosBattleAction *)p;

    if (!own_event() || e->side != 0u || !in_session()) {
        return EIDOS_EVENT_CONTINUE;
    }
    battle_bench_sweep();
    if (e->action == EIDOS_ACTION_SWITCH && doomed_battle(&e->mon)) {
        eidos_message("Its pair is gone: this Pokemon cannot leave the battle.");
        return EIDOS_EVENT_REFUSE;
    }
    return EIDOS_EVENT_CONTINUE;
}

static int32_t on_battle_damage(void *p)
{
    EidosBattleDamage *e = (EidosBattleDamage *)p;

    if (!own_event() || !in_session()) {
        return EIDOS_EVENT_CONTINUE;
    }
    if (doomed_battle(&e->target) && e->amount < e->target_hp) {
        e->amount = e->target_hp;          /* the hit it takes is its last */
    }
    if (doomed_battle(&e->attacker)) {
        faint_now(&e->attacker);           /* faints after its own attack */
    }
    return EIDOS_EVENT_CONTINUE;
}

/* A move that does no damage is an attack too. */
static int32_t on_battle_move(void *p)
{
    EidosBattleMove *e = (EidosBattleMove *)p;

    if (own_event() && in_session() && doomed_battle(&e->user)) {
        faint_now(&e->user);
    }
    return EIDOS_EVENT_CONTINUE;
}

/* No revives: a fainted Pokemon stays fainted (the Nuzlocke rule). */
static int32_t on_mon_revive(void *p)
{
    (void)p;
    if (own_event() && setting("soul_link.no_revive") == 1) {
        return EIDOS_EVENT_REFUSE;
    }
    return EIDOS_EVENT_CONTINUE;
}

/* Losing every Pokemon ends the run for every player. */
static int g_told_over;

static int32_t on_whiteout(void *p)
{
    EidosWhiteout *e = (EidosWhiteout *)p;

    if (own_event() && e->in_battle != 0u && in_session()) {
        (void)eidos_shared_set("over", 1);
        g_told_over = 1;
        eidos_message("Every Pokemon has fainted. The Soul Link is over for every player.");
    }
    return EIDOS_EVENT_CONTINUE;
}

/* ------------------------------------------------------------------ */
/* The sweep: this game's Pokemon follow the shared table              */
/* ------------------------------------------------------------------ */

static void tell(const char *what, uint32_t species_unused)
{
    (void)species_unused;
    eidos_message(what);
}

static void sweep(void)
{
    uint32_t n = eidos_party_count(), i, b, s, boxes;
    int together = setting("soul_link.together") == 1;
    int to_box = setting("soul_link.dead") == 1;
    EidosMonRef r;

    if (!g_told_over && shared_is("over", 1) == 1) {
        g_told_over = 1;
        eidos_message("A player lost every Pokemon. The Soul Link is over for every player.");
    }

    /* The party: lost pairs go, boxed pairs go to the boxes. */
    for (i = 0; i < n; i++) {
        uint32_t area, pers;

        if (eidos_party_ref(i, &r) != 0 || party_field(&r, EIDOS_MON_FIELD_IS_EGG)) {
            continue;
        }
        area = party_area(&r);
        pers = party_field(&r, EIDOS_MON_FIELD_PERSONALITY);
        if (area == 0u || handled(pers)) {
            continue;
        }
        if (shared_is(key("lost", area), 1) == 1) {
            int rc = to_box ? eidos_mon_to_box_n(REF_ARGS(r), GRAVE_BOX)
                            : eidos_mon_release(REF_ARGS(r));
            if (rc == 0) {
                mark_handled(pers);
                tell(to_box ? "Its pair is gone: a Pokemon went to box 24."
                            : "Its pair is gone: a Pokemon was released.", 0);
            }
        } else if (together && shared_is(key("boxed", area), 1) == 1) {
            if (eidos_mon_to_box(REF_ARGS(r)) == 0) {
                mark_handled(pers);
                tell("Its pair was boxed: a Pokemon went to the boxes.", 0);
            }
        }
    }
    if (!together) {
        return;
    }
    /* The boxes: a pair taken back into a party comes back here too. */
    boxes = eidos_box_count();
    for (b = 0; b < boxes && eidos_party_count() < PARTY_MAX; b++) {
        if (b == GRAVE_BOX) {
            continue;
        }
        for (s = 0; s < BOX_SLOTS && eidos_party_count() < PARTY_MAX; s++) {
            uint32_t area, pers;

            if (eidos_box_ref(b, s, &r) != 0 || box_field(&r, EIDOS_MON_FIELD_SPECIES) == 0u ||
                box_field(&r, EIDOS_MON_FIELD_IS_EGG)) {
                continue;
            }
            area = box_area(&r);
            pers = box_field(&r, EIDOS_MON_FIELD_PERSONALITY);
            if (area == 0u || handled(pers) || shared_is(key("lost", area), 1) == 1) {
                continue;
            }
            if (shared_is(key("boxed", area), 0) == 1 &&
                eidos_mon_from_box(b, s) == 0) {
                mark_handled(pers);
                tell("Its pair joined the party: a Pokemon came out of the boxes.", 0);
            }
        }
    }
}

static uint32_t g_frames;

static int32_t on_player_step(void *p)
{
    (void)p;
    if (!own_event()) {
        return EIDOS_EVENT_CONTINUE;
    }
    if (++g_frames < SWEEP_EVERY) {
        return EIDOS_EVENT_CONTINUE;
    }
    g_frames = 0;
    if (in_session()) {
        sweep();
    }
    return EIDOS_EVENT_CONTINUE;
}

/* The handled list is about Pokemon the game is moving now; once the
 * moves are applied (a map change) it starts again. */
static int32_t on_route_change(void *p)
{
    (void)p;
    if (own_event()) {
        g_handled_count = 0;
    }
    return EIDOS_EVENT_CONTINUE;
}

/* ------------------------------------------------------------------ */
/* The start                                                           */
/* ------------------------------------------------------------------ */

#define BIND(kind, fn) (void)eidos_on((kind), (uint32_t)(uintptr_t)(fn))

int32_t eidos_mod_start(void)
{
    BIND(EIDOS_EVENT_ENCOUNTER_WILD, on_encounter_wild);
    BIND(EIDOS_EVENT_CATCH_ATTEMPT, on_catch_attempt);
    BIND(EIDOS_EVENT_CATCH_RESULT, on_catch_result);
    BIND(EIDOS_EVENT_BATTLE_END, on_battle_end);
    BIND(EIDOS_EVENT_BATTLE_FAINT, on_battle_faint);
    BIND(EIDOS_EVENT_MON_RELEASE, on_mon_release);
    BIND(EIDOS_EVENT_BOX_DEPOSIT, on_box_deposit);
    BIND(EIDOS_EVENT_BOX_WITHDRAW, on_box_withdraw);
    BIND(EIDOS_EVENT_PLAYER_STEP, on_player_step);
    BIND(EIDOS_EVENT_ROUTE_CHANGE, on_route_change);
    BIND(EIDOS_EVENT_MON_RECEIVE, on_mon_receive);
    BIND(EIDOS_EVENT_BATTLE_START, on_battle_start);
    BIND(EIDOS_EVENT_BATTLE_ACTION, on_battle_action);
    BIND(EIDOS_EVENT_BATTLE_DAMAGE, on_battle_damage);
    BIND(EIDOS_EVENT_BATTLE_MOVE, on_battle_move);
    BIND(EIDOS_EVENT_MON_REVIVE, on_mon_revive);
    BIND(EIDOS_EVENT_WHITEOUT, on_whiteout);
    BIND(EIDOS_EVENT_HEAL_HP, on_heal_hp);
    line_start("Soul Link started");
    log_line();
    return 0;
}

void eidos_mod_stop(void)
{
}
