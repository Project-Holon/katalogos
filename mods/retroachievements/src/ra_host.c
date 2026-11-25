/*
 * The mod's calls to the game (see ra_host.h): the services of mod
 * interface 7 (web requests, the clock, the game's hash, the pop-up, the
 * mod's files, the log, the settings tab and its buttons, and the game's
 * answer on hardcore).
 */

#include <stdlib.h>
#include <string.h>

#include "eidos_wasm.h"
#include "ra_host.h"

/* Requests in flight; the game takes 4 at most from all mods together. */
#define MAX_PENDING 8

typedef struct Pending {
    uint32_t ticket;
    ra_http_done done;
    void *user;
} Pending;

static Pending pending[MAX_PENDING];
static int pending_count;

/* The game's answer to a request: read the body here, it is gone after. */
static int32_t on_web(EidosEvent *event)
{
    const EidosWeb *web = (const EidosWeb *)event;
    Pending p;
    char *body;
    int32_t got;
    int i;

    for (i = 0; i < pending_count && pending[i].ticket != web->ticket; i++) {
    }
    if (i == pending_count) {
        return EIDOS_EVENT_CONTINUE; /* another mod's request */
    }
    p = pending[i];
    pending[i] = pending[--pending_count];

    body = malloc(web->size + 1);
    got = body ? eidos_web_read(body, web->size) : -1;
    if (got < 0) {
        got = 0;
    }
    if (body) {
        body[got] = '\0';
    }
    /* a failed request gives a short reason, not a body; an answer too big
     * for the mod's memory counts as failed */
    p.done(web->status == EIDOS_WEB_FAILED || !body ? -1 : web->status, body ? body : "",
           (uint32_t)got, p.user);
    free(body);
    return EIDOS_EVENT_CONTINUE;
}

int ra_host_start(void)
{
    return eidos_on(EIDOS_EVENT_WEB, (uint32_t)(uintptr_t)on_web);
}

int ra_host_http(const char *url, const char *body, const char *content_type, ra_http_done done,
                 void *user)
{
    uint32_t ticket = 0;
    uint32_t size = body ? (uint32_t)strlen(body) : 0;

    if (pending_count == MAX_PENDING) {
        return -1;
    }
    /* a GET has no body and no type; a pointer 0 would reach the game as
     * the start of the module's memory, so the empty text stands in */
    if (eidos_web_request(url, size ? body : "", size, size && content_type ? content_type : "",
                          &ticket) != 0) {
        return -1;
    }
    pending[pending_count].ticket = ticket;
    pending[pending_count].done = done;
    pending[pending_count].user = user;
    pending_count++;
    return 0;
}

int ra_host_game_hash(char out[33])
{
    if (eidos_game_hash(EIDOS_GAME_HASH_RA, out, 33) != 32) {
        out[0] = '\0';
        return -1;
    }
    return 0;
}

/* Copies at most `max` bytes of UTF-8 text, never cutting a character. */
static void clip(char *out, const char *text, size_t max)
{
    size_t n = strlen(text);
    if (n > max) {
        n = max;
        while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) {
            n--;
        }
    }
    memcpy(out, text, n);
    out[n] = '\0';
}

void ra_host_banner(int kind, const char *title, const char *text)
{
    char t[64];
    char x[160];
    uint32_t icon = kind == RA_BANNER_TROPHY    ? EIDOS_POPUP_TROPHY
                    : kind == RA_BANNER_WARNING ? EIDOS_POPUP_WARNING
                                                : EIDOS_POPUP_INFO;
    clip(t, title, sizeof t - 1);
    clip(x, text, sizeof x - 1);
    /* the queue holds 8; when it is full the news goes to the log */
    if (eidos_popup(t, x, icon) != 0) {
        ra_host_log(title);
        ra_host_log(text);
    }
}

int ra_host_setting_text(const char *key, char *out, uint32_t out_size)
{
    int32_t n;

    if (!out_size) {
        return -1;
    }
    n = eidos_setting_text(key, out, out_size);
    /* -1: no such setting; a length that does not fit was cut */
    if (n < 0 || (uint32_t)n >= out_size) {
        out[0] = '\0';
        return -1;
    }
    out[n] = '\0';
    return 0;
}

static void (*button_pressed)(const char *key);

/* setting.button reaches every mod: the mod's own keys are checked by
 * `pressed` */
static int32_t on_button(EidosEvent *event)
{
    const EidosSettingButton *button = (const EidosSettingButton *)event;
    char key[sizeof button->key + 1];

    memcpy(key, button->key, sizeof button->key);
    key[sizeof button->key] = '\0';
    if (button_pressed) {
        button_pressed(key);
    }
    return EIDOS_EVENT_CONTINUE;
}

void ra_host_on_button(void (*pressed)(const char *key))
{
    button_pressed = pressed;
    eidos_on(EIDOS_EVENT_SETTING_BUTTON, (uint32_t)(uintptr_t)on_button);
}

int ra_host_setting_set_text(const char *key, const char *text)
{
    return eidos_setting_set_text(key, text);
}

int ra_host_setting_get(const char *key)
{
    return eidos_setting_get(key);
}

void ra_host_setting_show(const char *key, const char *text)
{
    char x[64];

    /* a show row takes 63 bytes, no line break */
    clip(x, text, sizeof x - 1);
    eidos_setting_show(key, x);
}

int ra_host_hardcore_allowed(char *why, uint32_t size)
{
    if (size) {
        why[0] = '\0';
    }
    return eidos_hardcore_allowed(why, size) == 1;
}

int ra_host_file_write(const char *name, const void *data, uint32_t size)
{
    return eidos_mod_file_write(name, data, size);
}

int ra_host_file_read(const char *name, void *out, uint32_t size)
{
    uint32_t got = 0;
    if (eidos_mod_file_read(name, out, size, &got) != 0) {
        return -1;
    }
    return (int)got;
}

int ra_host_file_delete(const char *name)
{
    return eidos_mod_file_delete(name);
}

void ra_host_log(const char *text)
{
    eidos_log(text, (uint32_t)strlen(text));
}

/* The C library's clock (time.h), from the game's. */
long long mod_libc_time_utc(void)
{
    return (long long)eidos_time_utc();
}

long long mod_libc_ticks_ms(void)
{
    return (long long)eidos_ticks_ms();
}
