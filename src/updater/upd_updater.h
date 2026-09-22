#ifndef UPD_UPDATER_H
#define UPD_UPDATER_H

/* In-game "Check for updates". Polled API: every call returns immediately; one worker thread does
 * all network and file I/O. Call everything from the UI thread.
 *
 *   boot:        upd_boot_sweep()              (before the menu; finishes/undoes an interrupted
 *                                               install and deletes download leftovers)
 *   menu opened: upd_init()                    (cheap; no thread, no network yet)
 *   "Check":     upd_start_check()             IDLE / UP_TO_DATE / ERROR only
 *   AVAILABLE+X: upd_start_install()
 *   Back while busy: upd_cancel()              (flag only; worker ends in IDLE)
 *   RESTART_READY+X: upd_relaunch()            (does not return on success)
 *   quitting:    upd_shutdown()                (cancel + bounded join)
 */

#include <stddef.h>
#include <stdint.h>
#include "upd_ver.h" /* upd_state_t */

int         upd_init(void);          /* 0 ok; idempotent */
int         upd_start_check(void);   /* 0 started, -1 not allowed now */
int         upd_start_install(void); /* 0 started, -1 not allowed now */
upd_state_t upd_state(void);
void        upd_message(char *out, size_t n);    /* short, UI-ready; <= 3 lines at ~40 cols */
void        upd_latest_tag(char *out, size_t n); /* "" until a newer release was found */
int         upd_progress(uint64_t *done, uint64_t *total); /* percent 0..100, -1 if unknown */
int         upd_busy(void);
void        upd_cancel(void);
int         upd_relaunch(void);      /* RESTART_READY only; returns <0 on failure */
int         upd_shutdown(void);      /* 0 worker joined and net torn down; 1 worker left running */
int         upd_boot_sweep(void);    /* see upd_install_sweep() */

#endif
