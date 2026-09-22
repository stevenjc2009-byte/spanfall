#ifndef UPD_VER_H
#define UPD_VER_H

/* Host-pure updater logic: no Vita headers, no I/O. Compiled into the eboot AND the host tests,
 * so every decision the updater makes about versions, URLs, archive paths, param.sfo and state
 * transitions is unit tested exactly as it runs on the console. */

#include <stddef.h>

/* ------------------------------------------------------------------ states */

typedef enum {
    UPD_IDLE = 0,
    UPD_CHECKING,
    UPD_UP_TO_DATE,
    UPD_AVAILABLE,
    UPD_DOWNLOADING,
    UPD_VERIFYING,
    UPD_INSTALLING,
    UPD_RESTART_READY, /* files in place, waiting for the user to relaunch */
    UPD_ERROR
} upd_state_t;

typedef enum {
    UPD_EV_START_CHECK = 0,
    UPD_EV_START_INSTALL,
    UPD_EV_FOUND_NEWER,
    UPD_EV_NO_NEWER,
    UPD_EV_DOWNLOADED,
    UPD_EV_VERIFIED,
    UPD_EV_INSTALLED,
    UPD_EV_FAILED,
    UPD_EV_CANCELLED
} upd_event_t;

/* Transition table. Returns the next state, or -1 if the event is not allowed in `state`. */
int upd_decide(upd_state_t state, upd_event_t ev);

/* 1 for CHECKING / DOWNLOADING / VERIFYING / INSTALLING. */
int upd_state_is_busy(upd_state_t state);

/* ------------------------------------------------------------------ versions */

#define UPD_VER_PART_MAX 99999999UL

typedef struct {
    unsigned long part[3];
    int has_suffix; /* "1.2.3-beta" */
} upd_ver_t;

/* Accepts v?MAJOR.MINOR.PATCH with an optional -suffix ([A-Za-z0-9.-]+). Each numeric part
 * saturates at UPD_VER_PART_MAX. Returns 0 on success, -1 for anything else (no partial
 * parses: "1.2", "", "vx", "1.2.3 " are all errors). */
int upd_version_parse(const char *s, upd_ver_t *out);

/* <0, 0, >0. Equal numbers: a suffixed version is older than an unsuffixed one; two suffixed
 * versions compare equal. */
int upd_version_compare(const upd_ver_t *a, const upd_ver_t *b);

/* ------------------------------------------------------------------ URLs */

/* "https://github.com/o/r/releases/tag/v1.0.1?x#y" -> "v1.0.1". 0 on success, -1 if there is
 * no non-empty "/releases/tag/<tag>" segment or `out` is too small. Does not check the host. */
int upd_tag_from_location(const char *location, char *out, size_t cap);

/* 1 only for https:// URLs whose host is exactly github.com or ends in .githubusercontent.com
 * (case-insensitive), with no userinfo and no port other than 443. Everything else 0. */
int upd_url_host_allowed(const char *url);

/* Copies the lower-cased host of an https:// URL into out. 0 on success, -1 if unparseable. */
int upd_url_host(const char *url, char *out, size_t cap);

typedef enum {
    UPD_CHK_TAG = 0,       /* tag copied out */
    UPD_CHK_NO_RELEASES,   /* redirect to .../releases with no tag */
    UPD_CHK_NOT_FOUND,     /* 404 */
    UPD_CHK_RATE_LIMITED,  /* 403 / 429 */
    UPD_CHK_BAD_HOST,      /* redirect to a host outside the allowlist */
    UPD_CHK_BAD_RESPONSE   /* anything else, including a status outside 100..599 */
} upd_check_result_t;

/* Classify the answer to HEAD https://github.com/<owner>/<repo>/releases/latest. */
upd_check_result_t upd_classify_check(long status, const char *location, char *tag, size_t cap);

/* ------------------------------------------------------------------ archives */

/* 1 if a zip entry name is safe to extract under a staging dir: relative, no "..", "." or
 * empty components, no ':' or '\\', no control bytes, shorter than 256. A trailing '/' (a
 * directory entry) is allowed. */
int upd_zip_path_ok(const char *name);

/* Read TITLE_ID from an in-memory param.sfo. It must be 9 chars of [A-Z0-9].
 * 0 on success, -1 on any structural problem (bad magic, truncated, out-of-range offsets). */
int upd_sfo_titleid(const unsigned char *buf, size_t len, char *out, size_t cap);

#endif
