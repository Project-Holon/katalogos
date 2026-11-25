/*
 * RetroAchievements for eidos, through RetroAchievements' own rcheevos
 * library (rc_client). Every call to the game goes through ra_host.c.
 *
 * Sign-in: the mod's tab (mod/settings.txt) has a user name, a password and
 * a Sign in button. The password is read once and cleared on the tab at
 * once; only the user name and the token the server gives back are kept,
 * in the mod's own file login.txt (per profile, never in the game save).
 * Sign out deletes the file. With a token, the next start signs in by
 * itself, then loads the game's achievements by the game's
 * RetroAchievements hash.
 *
 * Hardcore is read when the game's achievements load: on only when the
 * tab asks for it and the game allows it (the tab says why not).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eidos_wasm.h"
#include "ra_host.h"
#include "rc_client.h"

/* RetroAchievements' DS memory map (rcheevos consoleinfo.c): its addresses
 * 0 to 0x3FFFFF are main RAM at 0x02000000; 0x1000000 to 0x1003FFF are the
 * data TCM, which Black keeps at 0x02FE0000 (16 KB). */
#define RA_MAIN_RAM_SIZE 0x400000u
#define RA_DS_MAIN_RAM 0x02000000u
#define RA_DTCM 0x1000000u
#define RA_DTCM_SIZE 0x4000u
#define RA_DS_DTCM 0x02FE0000u

#define LOGIN_FILE "login.txt"
/* The keys of mod/settings.txt. */
#define KEY_USER "ra.user"
#define KEY_PASSWORD "ra.pass"
#define KEY_SIGN_IN "ra.signin"
#define KEY_SIGN_OUT "ra.signout"
#define KEY_WHO "ra.who"
#define KEY_HARDCORE "ra.hardcore"
#define KEY_WHY "ra.why"
#define KEY_QUEUE "ra.queue"

static rc_client_t *client;

/* rc_client's message, or the error's name when it gives none. */
static const char *reason(const char *message, int result)
{
    return message && message[0] ? message : rc_error_str(result);
}

static uint32_t read_memory(uint32_t address, uint8_t *buffer, uint32_t num_bytes, rc_client_t *c)
{
    uint32_t ds;
    (void)c;
    if (address < RA_MAIN_RAM_SIZE && num_bytes <= RA_MAIN_RAM_SIZE - address) {
        ds = RA_DS_MAIN_RAM + address;
    } else if (address >= RA_DTCM && address - RA_DTCM < RA_DTCM_SIZE &&
               num_bytes <= RA_DTCM_SIZE - (address - RA_DTCM)) {
        ds = RA_DS_DTCM + (address - RA_DTCM);
    } else {
        memset(buffer, 0, num_bytes);
        return 0;
    }
    if (eidos_read(ds, buffer, num_bytes) == 0) {
        return num_bytes;
    }
    /* the game keeps some variables apart from the rest of the memory, so a
     * read across the edge of one fails as a whole: read byte by byte */
    for (uint32_t i = 0; i < num_bytes; i++) {
        if (eidos_read(ds + i, buffer + i, 1) != 0) {
            memset(buffer, 0, num_bytes);
            return 0;
        }
    }
    return num_bytes;
}

/* One server call in flight: what rc_client wants called with the answer. */
typedef struct Call {
    rc_client_server_callback_t callback;
    void *callback_data;
} Call;

static void http_done(int32_t status, const char *body, uint32_t body_len, void *user)
{
    Call *call = user;
    rc_api_server_response_t response;
    response.body = body;
    response.body_length = body_len;
    response.http_status_code = status < 0 ? RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR : status;
    call->callback(&response, call->callback_data);
    free(call);
}

static void server_call(const rc_api_request_t *request, rc_client_server_callback_t callback,
                        void *callback_data, rc_client_t *c)
{
    Call *call = malloc(sizeof *call);
    (void)c;
    if (call) {
        call->callback = callback;
        call->callback_data = callback_data;
        if (ra_host_http(request->url, request->post_data, request->content_type, http_done, call) == 0) {
            return;
        }
        free(call);
    }
    {
        rc_api_server_response_t response;
        memset(&response, 0, sizeof response);
        response.http_status_code = RC_API_SERVER_RESPONSE_CLIENT_ERROR;
        callback(&response, callback_data);
    }
}

static void on_load(int result, const char *message, rc_client_t *c, void *data)
{
    char line[200];
    const rc_client_game_t *game;
    (void)data;
    if (result != RC_OK) {
        snprintf(line, sizeof line, "RetroAchievements: the game's achievements did not load (%s)",
                 reason(message, result));
        ra_host_log(line);
        return;
    }
    game = rc_client_get_game_info(c);
    snprintf(line, sizeof line, "%s", game && game->title ? game->title : "This game");
    ra_host_banner(RA_BANNER_INFO, "RetroAchievements", line);
}

/* Hardcore, decided when the achievements load: what the tab asks, if the
 * game allows it. A change on the tab counts from the next game start. */
static void set_hardcore(void)
{
    char why[128];
    int wanted = ra_host_setting_get(KEY_HARDCORE) == 1;
    int allowed = wanted && ra_host_hardcore_allowed(why, sizeof why) == 1;
    rc_client_set_hardcore_enabled(client, allowed);
    ra_host_setting_show(KEY_WHY, wanted && !allowed ? why : "");
}

static void load_game(void)
{
    char hash[33];
    if (ra_host_game_hash(hash) != 0) {
        ra_host_log("RetroAchievements: the game's hash is not known");
        return;
    }
    set_hardcore();
    rc_client_begin_load_game(client, hash, on_load, NULL);
}

/* Keeps the user name and token (never the password). */
static void keep_login(void)
{
    const rc_client_user_t *user = rc_client_get_user_info(client);
    char text[256];
    int n;
    if (!user || !user->username || !user->token) {
        return;
    }
    n = snprintf(text, sizeof text, "%s\n%s\n", user->username, user->token);
    if (n > 0 && n < (int)sizeof text) {
        ra_host_file_write(LOGIN_FILE, text, (uint32_t)n);
    }
}

static void on_login(int result, const char *message, rc_client_t *c, void *data)
{
    char line[200];
    const rc_client_user_t *user;
    (void)data;
    if (result != RC_OK) {
        /* a token the server no longer takes is dropped */
        if (result == RC_INVALID_CREDENTIALS || result == RC_EXPIRED_TOKEN) {
            ra_host_file_delete(LOGIN_FILE);
        }
        snprintf(line, sizeof line, "Not signed in: %s", reason(message, result));
        ra_host_banner(RA_BANNER_WARNING, "RetroAchievements", line);
        ra_host_setting_show(KEY_WHO, "");
        return;
    }
    keep_login();
    user = rc_client_get_user_info(c);
    snprintf(line, sizeof line, "Signed in as %s", user && user->display_name ? user->display_name : "");
    ra_host_banner(RA_BANNER_INFO, "RetroAchievements", line);
    ra_host_setting_show(KEY_WHO, user && user->display_name ? user->display_name : "");
    load_game();
}

/* Signs in with the kept token, if there is one. */
static void sign_in_with_token(void)
{
    char text[256];
    char *token;
    int n = ra_host_file_read(LOGIN_FILE, text, sizeof text - 1);
    if (n <= 0) {
        return;
    }
    text[n] = '\0';
    token = strchr(text, '\n');
    if (!token) {
        return;
    }
    *token++ = '\0';
    token[strcspn(token, "\n")] = '\0';
    if (text[0] && token[0]) {
        rc_client_begin_login_with_token(client, text, token, on_login, NULL);
    }
}

static void button_pressed(const char *key)
{
    if (strcmp(key, KEY_SIGN_IN) == 0) {
        char user[128];
        char password[256];
        int have = ra_host_setting_text(KEY_USER, user, sizeof user) == 0 && user[0] &&
                   ra_host_setting_text(KEY_PASSWORD, password, sizeof password) == 0 && password[0];
        /* the password leaves the tab at once, sign-in or not */
        ra_host_setting_set_text(KEY_PASSWORD, "");
        if (!have) {
            memset(password, 0, sizeof password);
            ra_host_banner(RA_BANNER_WARNING, "RetroAchievements", "Enter your user name and password first");
            return;
        }
        rc_client_logout(client);
        rc_client_begin_login_with_password(client, user, password, on_login, NULL);
        /* rc_client copies what it sends; the password goes now */
        memset(password, 0, sizeof password);
        __asm__ volatile("" ::"r"(password) : "memory");
    } else if (strcmp(key, KEY_SIGN_OUT) == 0) {
        rc_client_logout(client);
        ra_host_file_delete(LOGIN_FILE);
        ra_host_setting_show(KEY_WHO, "");
        ra_host_setting_show(KEY_WHY, "");
        ra_host_setting_show(KEY_QUEUE, "");
        ra_host_banner(RA_BANNER_INFO, "RetroAchievements", "Signed out");
    }
}

static void on_event(const rc_client_event_t *event, rc_client_t *c)
{
    char line[256];
    (void)c;
    switch (event->type) {
    case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
        snprintf(line, sizeof line, "%s (%u points)", event->achievement->title,
                 (unsigned)event->achievement->points);
        ra_host_banner(RA_BANNER_TROPHY, "Achievement unlocked", line);
        break;
    case RC_CLIENT_EVENT_GAME_COMPLETED:
        ra_host_banner(RA_BANNER_TROPHY, "RetroAchievements", "Every achievement of this game is yours");
        break;
    case RC_CLIENT_EVENT_DISCONNECTED:
        ra_host_banner(RA_BANNER_WARNING, "RetroAchievements", "Offline: unlocks wait to be sent");
        ra_host_setting_show(KEY_QUEUE, "Waiting for the network");
        break;
    case RC_CLIENT_EVENT_RECONNECTED:
        ra_host_banner(RA_BANNER_INFO, "RetroAchievements", "Back online: waiting unlocks are sent");
        ra_host_setting_show(KEY_QUEUE, "");
        break;
    case RC_CLIENT_EVENT_SERVER_ERROR:
        if (event->server_error && event->server_error->error_message) {
            snprintf(line, sizeof line, "RetroAchievements: %s", event->server_error->error_message);
            ra_host_log(line);
        }
        break;
    default:
        break;
    }
}

static void log_message(const char *message, const rc_client_t *c)
{
    (void)c;
    ra_host_log(message);
}

static void on_frame(void)
{
    rc_client_do_frame(client);
}

EIDOS_EXPORT("eidos_mod_start")
int32_t eidos_mod_start(void)
{
    client = rc_client_create(read_memory, server_call);
    if (!client) {
        ra_host_log("RetroAchievements: no memory for the client");
        return 1;
    }
    rc_client_enable_logging(client, RC_CLIENT_LOG_LEVEL_WARN, log_message);
    rc_client_set_event_handler(client, on_event);
    if (ra_host_start() != 0) {
        ra_host_log("RetroAchievements: the game gives this mod no web requests");
        return 1;
    }
    ra_host_on_button(button_pressed);
    eidos_on_frame((uint32_t)(uintptr_t)on_frame);
    sign_in_with_token();
    return 0;
}

EIDOS_EXPORT("eidos_mod_stop")
void eidos_mod_stop(void)
{
    if (client) {
        rc_client_destroy(client);
        client = NULL;
    }
}
