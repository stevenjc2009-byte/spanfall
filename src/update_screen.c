#include "update_screen.h"

#include <string.h>

/*                      heading             fallback body                                  prog tag */
static const UpdScreen SCREENS[] = {
    /* UPD_IDLE */
    { "Updates", "Check github.com for a newer Spanfall.", 0, 0,
      UPD_ACT_CHECK, "check now", UPD_ACT_LEAVE, "back" },
    /* UPD_CHECKING */
    { "Checking", "Checking for updates...", 0, 0,
      UPD_ACT_NONE, NULL, UPD_ACT_CANCEL, "cancel" },
    /* UPD_UP_TO_DATE */
    { "Up to date", "You have the latest version.", 0, 0,
      UPD_ACT_CHECK, "check again", UPD_ACT_LEAVE, "back" },
    /* UPD_AVAILABLE */
    { "Update available", "A newer version is available.", 0, 1,
      UPD_ACT_INSTALL, "download and install", UPD_ACT_LEAVE, "back" },
    /* UPD_DOWNLOADING */
    { "Downloading", "Downloading...", 1, 1,
      UPD_ACT_NONE, NULL, UPD_ACT_CANCEL, "cancel" },
    /* UPD_VERIFYING */
    { "Verifying", "Verifying the download...", 0, 1,
      UPD_ACT_NONE, NULL, UPD_ACT_CANCEL, "cancel" },
    /* UPD_INSTALLING */
    { "Installing", "Installing the update...", 0, 1,
      UPD_ACT_NONE, NULL, UPD_ACT_CANCEL, "cancel" },
    /* UPD_RESTART_READY */
    { "Update installed", "Update installed. Restart now?", 0, 1,
      UPD_ACT_RELAUNCH, "restart now", UPD_ACT_LEAVE, "later" },
    /* UPD_ERROR */
    { "Update failed", "The update failed.", 0, 0,
      UPD_ACT_CHECK, "try again", UPD_ACT_LEAVE, "back" }
};

#define SCREEN_COUNT ((int)(sizeof(SCREENS) / sizeof(SCREENS[0])))

UpdScreen update_screen_for(upd_state_t st)
{
    int i = (int)st;
    if (i < 0 || i >= SCREEN_COUNT)
        i = (int)UPD_IDLE;
    return SCREENS[i];
}

int update_screen_wrap(const char *s, char lines[UPD_WRAP_LINES][UPD_WRAP_CAP])
{
    int n = 0;

    if (!s)
        return 0;
    while (*s == ' ')
        s++;
    while (*s && n < UPD_WRAP_LINES) {
        size_t len = strlen(s);
        size_t take = len > UPD_WRAP_COLS ? UPD_WRAP_COLS : len;

        if (take == UPD_WRAP_COLS) {
            /* break on the last space that still fits; no space means an over-long word */
            size_t brk = take;
            while (brk > 0 && s[brk] != ' ')
                brk--;
            if (brk > 0)
                take = brk;
        }
        memcpy(lines[n], s, take);
        lines[n][take] = '\0';
        s += take;
        while (*s == ' ')
            s++;
        n++;
    }
    if (*s && n > 0) {
        /* more text than fits: mark the last line so nothing looks silently complete */
        size_t l = strlen(lines[n - 1]);
        size_t at = l + 3 <= UPD_WRAP_COLS ? l : UPD_WRAP_COLS - 3;
        memcpy(lines[n - 1] + at, "...", 4);
    }
    return n;
}
