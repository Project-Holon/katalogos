/*
 * What the mod asks of the game, in one place. ra_host.c is the only file
 * that names the game's calls, so a call the game renames or reshapes
 * costs a change there and nowhere else.
 *
 * The sign-in settings (text, secret, button) are not in the game yet:
 * those calls answer that they are not there, and the sign-in comes from
 * the mod's login.txt.
 */
#ifndef RA_HOST_H
#define RA_HOST_H

#include <stdint.h>

/* Registers what the layer listens to (the answers to web requests).
 * 0, or non-zero when the game refuses. */
int ra_host_start(void);

/* A finished web request: the HTTP status (or -1 when the network
 * failed) and the body, valid only during the call. */
typedef void (*ra_http_done)(int32_t status, const char *body, uint32_t body_len, void *user);

/* Starts a request: a GET when `body` is NULL, else a POST of `body` with
 * `content_type`. 0 when started (`done` is called later, on the game's
 * thread); non-zero when it cannot start (then `done` is never called). */
int ra_host_http(const char *url, const char *body, const char *content_type, ra_http_done done,
                 void *user);

/* The RetroAchievements hash of the game in force: 32 hex characters and
 * a 0. 0, or non-zero when it is not known. */
int ra_host_game_hash(char out[33]);

/* An unlock or other news, drawn by the game in the chosen style. */
enum { RA_BANNER_INFO, RA_BANNER_TROPHY, RA_BANNER_WARNING };
void ra_host_banner(int kind, const char *title, const char *text);

/* The text of a text or secret setting of the mod's tab (`out` gets a 0 at
 * the end). 0, or non-zero when there is none. */
int ra_host_setting_text(const char *key, char *out, uint32_t out_size);

/* Sets a text or secret setting (the mod clears the password with ""). */
int ra_host_setting_set_text(const char *key, const char *text);

/* A bool, choice or int setting's value. */
int ra_host_setting_get(const char *key);

/* Sets the text of a show row; "" hides it. */
void ra_host_setting_show(const char *key, const char *text);

/* 1 when the game allows hardcore now; else 0, with a short reason. */
int ra_host_hardcore_allowed(char *why, uint32_t size);

/* Calls `pressed` with the key when a button of the mod's tab is pressed. */
void ra_host_on_button(void (*pressed)(const char *key));

/* The mod's own files, kept per profile and never in the game save.
 * ra_host_file_read gives the length read, or -1 when there is no file. */
int ra_host_file_write(const char *name, const void *data, uint32_t size);
int ra_host_file_read(const char *name, void *out, uint32_t size);
int ra_host_file_delete(const char *name);

/* A line in the mod log. */
void ra_host_log(const char *text);

#endif
