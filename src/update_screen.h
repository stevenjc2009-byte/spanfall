#ifndef SF_UPDATE_SCREEN_H
#define SF_UPDATE_SCREEN_H

/* What the "Check for updates" screen shows, and what its two buttons do, for each state the
 * updater core can be in. Pure: no drawing, no sce* calls, no updater calls, so the whole
 * state -> text mapping can be host-tested.
 *
 * The body line comes from upd_message() at draw time; `fallback` covers the moment before the
 * worker has written one. app.c acts on `confirm` / `back` and draws the rest. */

#include "updater/upd_ver.h"

#define UPD_WRAP_LINES 3
#define UPD_WRAP_COLS  44 /* fits the update panel at text scale 1.0 */
#define UPD_WRAP_CAP   (UPD_WRAP_COLS + 1)

typedef enum {
    UPD_ACT_NONE = 0, /* the button does nothing in this state */
    UPD_ACT_CHECK,    /* upd_start_check() */
    UPD_ACT_INSTALL,  /* upd_start_install() */
    UPD_ACT_RELAUNCH, /* upd_relaunch(); does not return on success */
    UPD_ACT_CANCEL,   /* upd_cancel(), stay on the screen */
    UPD_ACT_LEAVE     /* back to the title menu */
} UpdAction;

typedef struct {
    const char *heading;
    const char *fallback;     /* body text until the worker has posted a message */
    int         show_progress;/* draw the download bar */
    int         show_tag;     /* draw upd_latest_tag() under the message */
    UpdAction   confirm;      /* IN_CONFIRM */
    const char *confirm_hint; /* NULL when confirm is UPD_ACT_NONE */
    UpdAction   back;         /* IN_BACK; never UPD_ACT_NONE */
    const char *back_hint;
} UpdScreen;

/* Never fails: an out-of-range state maps to the idle screen. */
UpdScreen update_screen_for(upd_state_t st);

/* Greedy word wrap of `s` into at most UPD_WRAP_LINES lines of UPD_WRAP_COLS characters.
 * Returns the number of lines written (0 for NULL/empty). A word longer than a line is split.
 * Text that does not fit is dropped, with the last line ending in "..." . */
int update_screen_wrap(const char *s, char lines[UPD_WRAP_LINES][UPD_WRAP_CAP]);

#endif
