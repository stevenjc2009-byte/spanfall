#ifndef UPD_INSTALL_H
#define UPD_INSTALL_H

/* Install method A (steve, 2026-09-17): replace files inside ux0:app/<TITLE_ID>/ from a staged,
 * verified extraction, then relaunch. No promoter, no head.bin.
 *
 * Crash safety. work_dir holds two files while an install is in flight:
 *   install.manifest  "<sha256hex> <relative path>\n" per staged file, written (tmp + rename)
 *                     before any file in app_dir is touched
 *   install.renaming  created only after every <path>.new copy in app_dir is hash-verified
 * upd_install_sweep() on boot: manifest without marker -> delete the .new copies (app untouched);
 * manifest with marker -> roll every file forward to the verified .new (or keep an already
 * renamed file whose hash matches); then remove both files. */

#include <stddef.h>
#include "upd_zip.h" /* UPD_RC_*, upd_cancel_fn, upd_count_fn */

/* File names inside work_dir (UPD_DATA_DIR). */
#define UPD_WORK_STAGE    "stage"
#define UPD_WORK_VPK      "update.vpk"
#define UPD_WORK_PART     "update.vpk.part"
#define UPD_WORK_MANIFEST "install.manifest"
#define UPD_WORK_MARKER   "install.renaming"
#define UPD_WORK_LASTERR  "last_error.txt"

#define UPD_E_NO_SFO      -10 /* stage has no readable sce_sys/param.sfo */
#define UPD_E_TITLE       -11 /* TITLE_ID differs from this game */
#define UPD_E_NO_EBOOT    -12 /* eboot.bin missing */
#define UPD_E_BAD_EBOOT   -13 /* eboot.bin does not start with "SCE\0" */
#define UPD_E_COPY        -20 /* copying into app_dir failed (app untouched) */
#define UPD_E_RENAME      -21 /* rename pass failed; marker kept so the boot sweep retries */
#define UPD_E_VERIFY      -22 /* files in app_dir do not hash to the staged files */
#define UPD_E_STAGE       -23 /* stage unreadable / unsafe name / manifest write failed */

/* Check a staged extraction belongs to this game and carries an eboot. 0 or UPD_E_*. */
int upd_stage_verify(const char *stage_dir, const char *title_id, char *err, size_t err_cap);

typedef struct {
    upd_cancel_fn cancel;   /* polled only before the rename pass */
    upd_count_fn  progress; /* files copied / total */
    void (*commit)(void *user); /* called once, right before the first rename */
    void *user;
} upd_install_cb_t;

/* Copy stage_dir over app_dir as described above. Returns UPD_RC_OK, UPD_RC_CANCELLED (app
 * untouched), or UPD_E_COPY / UPD_E_RENAME / UPD_E_VERIFY / UPD_E_STAGE with err filled. */
int upd_install_apply(const char *stage_dir, const char *app_dir, const char *work_dir,
                      const upd_install_cb_t *cb, char *err, size_t err_cap);

/* Boot-time recovery + leftover cleanup (stage/, update.vpk, update.vpk.part). Safe to call
 * every boot. Returns 0 if nothing was pending, 1 if it finished or undid an install, -1 if a
 * roll-forward could not complete (manifest and marker are kept for the next boot). */
int upd_install_sweep(const char *app_dir, const char *work_dir);

#endif
