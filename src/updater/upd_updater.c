#include "upd_updater.h"
#include "upd_config.h"
#include "upd_fs.h"
#include "upd_install.h"
#include "upd_net.h"
#include "upd_sys.h"
#include "upd_zip.h"

#include <stdio.h>
#include <string.h>

#define MSG_CAP 160
#define TAG_CAP 64

#define STAGE_DIR UPD_DATA_DIR "/" UPD_WORK_STAGE
#define VPK_PATH  UPD_DATA_DIR "/" UPD_WORK_VPK
#define PART_PATH UPD_DATA_DIR "/" UPD_WORK_PART
#define LOG_PATH  UPD_DATA_DIR "/" UPD_WORK_LASTERR

typedef enum { JOB_CHECK, JOB_INSTALL } Job;

/* Shared with the worker; every access holds the lock. */
static int         s_inited;
static int         s_running;  /* worker thread alive */
static int         s_cancel;
static int         s_critical; /* rename pass in progress: shutdown must wait */
static upd_state_t s_state = UPD_IDLE;
static char        s_msg[MSG_CAP];
static char        s_tag[TAG_CAP];
static uint64_t    s_done, s_total;
static int         s_last_pct = -1;
static Job         s_job;

/* ------------------------------------------------------------------ shared-state helpers */

static void move(upd_event_t ev, const char *msg)
{
    upd_sys_lock();
    int next = upd_decide(s_state, ev);
    if (next < 0) {
        snprintf(s_msg, sizeof(s_msg), "Updater internal error (%d/%d)", (int)s_state, (int)ev);
        s_state = UPD_ERROR;
    } else {
        s_state = (upd_state_t)next;
        snprintf(s_msg, sizeof(s_msg), "%s", msg);
    }
    s_done = s_total = 0;
    s_last_pct = -1;
    upd_sys_unlock();
}

static int cancelled(void)
{
    upd_sys_lock();
    int c = s_cancel;
    upd_sys_unlock();
    return c;
}

static int cancel_cb(void *user)
{
    (void)user;
    return cancelled();
}

static void cleanup_download(void)
{
    upd_fs_remove_tree(STAGE_DIR);
    upd_sys_remove(PART_PATH);
    upd_sys_remove(VPK_PATH);
}

/* Short message on screen, full detail in last_error.txt, then ERROR. */
static void fail(const char *ui_msg, const char *step, const char *detail,
                 const upd_net_diag_t *d)
{
    char text[1024];
    char tag[TAG_CAP];
    upd_sys_lock();
    snprintf(tag, sizeof(tag), "%s", s_tag);
    upd_sys_unlock();
    int n = snprintf(text, sizeof(text),
                     "game: %s %s (updater believes %s)\nstep: %s\nmessage: %s\ndetail: %s\n"
                     "latest tag: %s\nca: %s\n",
                     UPD_GAME_NAME, UPD_GAME_VERSION, UPD_LOCAL_VERSION, step, ui_msg,
                     detail ? detail : "-", tag[0] ? tag : "-", upd_net_ca_note());
    if (d && n > 0 && (size_t)n < sizeof(text))
        snprintf(text + n, sizeof(text) - (size_t)n,
                 "curl: %d  http: %ld  codes: %s  host: %s  tls verify: %ld  bytes: %llu\n",
                 d->curl_code, d->http_code, d->codes[0] ? d->codes : "-",
                 d->final_host[0] ? d->final_host : "-", d->verify_result,
                 (unsigned long long)d->bytes);
    if (upd_fs_mkdir_p(UPD_DATA_DIR) == 0)
        upd_fs_write_text(LOG_PATH, text);
    move(UPD_EV_FAILED, ui_msg);
}

static const char *net_message(int rc, long http, char *buf, size_t cap)
{
    switch (rc) {
    case UPD_NET_E_INIT:       return "Network could not start";
    case UPD_NET_E_CA:         return "Certificate store missing";
    case UPD_NET_E_TLS_VERIFY: return "Secure connection failed (certificate). A newer build may be needed.";
    case UPD_NET_E_TLS:        return "Secure connection failed";
    case UPD_NET_E_DNS:        return "Could not reach GitHub (DNS)";
    case UPD_NET_E_CONNECT:    return "Could not connect to GitHub";
    case UPD_NET_E_TIMEOUT:    return "Connection timed out";
    case UPD_NET_E_LOST:       return "Connection lost";
    case UPD_NET_E_BAD_HOST:   return "Unexpected download host";
    case UPD_NET_E_REDIRECTS:  return "Too many redirects";
    case UPD_NET_E_TOO_BIG:    return "Update file is too large";
    case UPD_NET_E_WRITE:      return "Cannot write to memory card";
    case UPD_NET_E_HTTP:
        if (http == 404)
            return "Update file not found on GitHub";
        if (http == 403 || http == 429)
            return "GitHub is rate limiting, try later";
        snprintf(buf, cap, "Download failed (HTTP %ld)", http);
        return buf;
    default:
        snprintf(buf, cap, "Network error (%d)", rc);
        return buf;
    }
}

/* ------------------------------------------------------------------ common pre-flight */

static int preflight(const char *step)
{
    char err[256];
    int rc = upd_net_init(UPD_CA_PATH, err, sizeof(err));
    if (rc != UPD_NET_OK) {
        fail(rc == UPD_NET_E_CA ? "Certificate store missing" : "Network could not start", step,
             err, NULL);
        return -1;
    }
    if (upd_sys_net_connected() != 1) {
        fail("Not connected to Wi-Fi", step, "sceNetCtlInetGetState != CONNECTED", NULL);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ check */

static int head_progress(uint64_t now, uint64_t total, void *user)
{
    (void)now;
    (void)total;
    (void)user;
    return cancelled();
}

static void do_check(void)
{
    char loc[1024], tag[TAG_CAP], buf[MSG_CAP], detail[512];
    long status = 0;
    upd_net_diag_t d;

    if (preflight("check") != 0)
        return;
    if (cancelled()) {
        move(UPD_EV_CANCELLED, "Check cancelled");
        return;
    }

    int rc = upd_net_head_ex("https://github.com/" UPD_OWNER "/" UPD_REPO "/releases/latest",
                             UPD_USER_AGENT, &status, loc, sizeof(loc), &d, head_progress, NULL);
    UPD_HOOK("check_answered");
    if (rc == UPD_NET_E_CANCELLED || cancelled()) {
        move(UPD_EV_CANCELLED, "Check cancelled");
        return;
    }
    if (rc != UPD_NET_OK) {
        fail(net_message(rc, d.http_code, buf, sizeof(buf)), "check", d.detail, &d);
        return;
    }

    switch (upd_classify_check(status, loc, tag, sizeof(tag))) {
    case UPD_CHK_NOT_FOUND:
        fail("Update source not found", "check", "HTTP 404 on releases/latest", &d);
        return;
    case UPD_CHK_RATE_LIMITED:
        fail("GitHub is rate limiting, try later", "check", "HTTP 403/429", &d);
        return;
    case UPD_CHK_BAD_HOST:
        fail("Unexpected update host", "check", "releases/latest redirected off github.com", &d);
        return;
    case UPD_CHK_BAD_RESPONSE:
        snprintf(buf, sizeof(buf), "Update check failed (HTTP %ld)", status);
        snprintf(detail, sizeof(detail), "status %ld location \"%.300s\"", status, loc);
        fail(buf, "check", detail, &d);
        return;
    case UPD_CHK_NO_RELEASES:
        move(UPD_EV_NO_NEWER, "No releases published yet");
        return;
    case UPD_CHK_TAG:
        break;
    }

    upd_ver_t remote, local;
    if (upd_version_parse(tag, &remote) != 0) {
        snprintf(detail, sizeof(detail), "tag \"%s\"", tag);
        fail("Release tag not understood", "check", detail, &d);
        return;
    }
    if (upd_version_parse(UPD_LOCAL_VERSION, &local) != 0) {
        fail("Local version not understood", "check", UPD_LOCAL_VERSION, &d);
        return;
    }
    if (upd_version_compare(&remote, &local) > 0) {
        upd_sys_lock();
        snprintf(s_tag, sizeof(s_tag), "%s", tag);
        upd_sys_unlock();
        snprintf(buf, sizeof(buf), "Version %s available. Press X to download and install.", tag);
        move(UPD_EV_FOUND_NEWER, buf);
    } else {
        snprintf(buf, sizeof(buf), "You have the latest version (v%s)", UPD_LOCAL_VERSION);
        move(UPD_EV_NO_NEWER, buf);
    }
}

/* ------------------------------------------------------------------ install */

static int dl_progress(uint64_t now, uint64_t total, void *user)
{
    (void)user;
    upd_sys_lock();
    s_done = now;
    s_total = total;
    if (total > 0) {
        int pct = (int)(now * 100 / total);
        if (pct > 100)
            pct = 100;
        if (pct != s_last_pct && s_state == UPD_DOWNLOADING) {
            s_last_pct = pct;
            snprintf(s_msg, sizeof(s_msg), "Downloading... %d%%", pct);
        }
    }
    int c = s_cancel;
    upd_sys_unlock();
    return c;
}

static void count_progress(unsigned done, unsigned total, void *user)
{
    (void)user;
    upd_sys_lock();
    s_done = done;
    s_total = total;
    upd_sys_unlock();
}

static void on_commit(void *user)
{
    (void)user;
    upd_sys_lock();
    s_critical = 1;
    upd_sys_unlock();
}

static void do_install(void)
{
    char tag[TAG_CAP], url[512], err[512], buf[MSG_CAP];
    upd_net_diag_t d;

    upd_sys_lock();
    snprintf(tag, sizeof(tag), "%s", s_tag);
    upd_sys_unlock();

    if (preflight("download") != 0)
        return;
    if (upd_fs_mkdir_p(UPD_DATA_DIR) != 0) {
        fail("Cannot write to memory card", "download", "mkdir " UPD_DATA_DIR, NULL);
        return;
    }
    uint64_t free_bytes = 0;
    if (upd_sys_free_space(UPD_DATA_DIR, &free_bytes) != 0) {
        fail("Cannot read free space on ux0", "download", "devctl 0x3001 failed", NULL);
        return;
    }
    if (free_bytes < UPD_MIN_FREE_BYTES) {
        snprintf(err, sizeof(err), "free %llu B, need %llu B", (unsigned long long)free_bytes,
                 (unsigned long long)UPD_MIN_FREE_BYTES);
        fail("Not enough free space on ux0", "download", err, NULL);
        return;
    }
    cleanup_download();

    /* The tag passed upd_version_parse, so it only holds [vV0-9A-Za-z.-]. */
    snprintf(url, sizeof(url),
             "https://github.com/" UPD_OWNER "/" UPD_REPO "/releases/download/%s/" UPD_ASSET, tag);
    int rc = upd_net_download(url, UPD_USER_AGENT, PART_PATH, UPD_MAX_DOWNLOAD, dl_progress, NULL,
                              &d);
    if (rc == UPD_NET_E_CANCELLED || (rc != UPD_NET_OK && cancelled())) {
        cleanup_download();
        move(UPD_EV_CANCELLED, "Update cancelled");
        return;
    }
    if (rc != UPD_NET_OK) {
        cleanup_download();
        fail(net_message(rc, d.http_code, buf, sizeof(buf)), "download", d.detail, &d);
        return;
    }
    if (upd_sys_rename(PART_PATH, VPK_PATH) != 0) {
        cleanup_download();
        fail("Cannot write to memory card", "download", "rename .part", &d);
        return;
    }
    UPD_HOOK("downloaded");

    move(UPD_EV_DOWNLOADED, "Checking download...");
    UPD_HOOK("verifying");
    if (!upd_zip_has_magic(VPK_PATH)) {
        cleanup_download();
        fail("Downloaded file is not a valid update", "verify", "no PK header", &d);
        return;
    }
    rc = upd_zip_extract(VPK_PATH, STAGE_DIR, cancel_cb, count_progress, NULL, err, sizeof(err));
    if (rc == UPD_RC_CANCELLED) {
        cleanup_download();
        move(UPD_EV_CANCELLED, "Update cancelled");
        return;
    }
    if (rc != UPD_RC_OK) {
        cleanup_download();
        fail("Downloaded file is not a valid update", "verify", err, &d);
        return;
    }
    upd_sys_remove(VPK_PATH); /* free card space before copying */

    rc = upd_stage_verify(STAGE_DIR, UPD_TITLE_ID, err, sizeof(err));
    if (rc != 0) {
        cleanup_download();
        fail(rc == UPD_E_TITLE ? "Update is for a different game"
                               : "Downloaded file is not a valid update",
             "verify", err, &d);
        return;
    }
    UPD_HOOK("verified");
    if (cancelled()) {
        cleanup_download();
        move(UPD_EV_CANCELLED, "Update cancelled");
        return;
    }

    move(UPD_EV_VERIFIED, "Installing... do not turn off the system");
    upd_install_cb_t cb = { cancel_cb, count_progress, on_commit, NULL };
    rc = upd_install_apply(STAGE_DIR, UPD_APP_DIR, UPD_DATA_DIR, &cb, err, sizeof(err));
    upd_fs_remove_tree(STAGE_DIR);
    switch (rc) {
    case UPD_RC_OK:
        move(UPD_EV_INSTALLED, "Update installed. Press X to restart " UPD_GAME_NAME ".");
        return;
    case UPD_RC_CANCELLED:
        move(UPD_EV_CANCELLED, "Update cancelled");
        return;
    case UPD_E_RENAME:
        fail("Install interrupted. Restart the game to finish it.", "install", err, NULL);
        return;
    case UPD_E_VERIFY:
        fail("Install could not be verified", "install", err, NULL);
        return;
    default:
        fail("Install failed. The game was not changed.", "install", err, NULL);
        return;
    }
}

/* ------------------------------------------------------------------ worker */

static int worker(void *arg)
{
    (void)arg;
    upd_sys_lock();
    Job job = s_job;
    upd_sys_unlock();

    if (job == JOB_CHECK)
        do_check();
    else
        do_install();

    upd_sys_lock();
    s_running = 0;
    s_critical = 0;
    upd_sys_unlock();
    return 0;
}

static int start_job(Job job, upd_event_t ev, const char *msg)
{
    if (!s_inited)
        return -1;
    upd_sys_lock();
    int next = upd_decide(s_state, ev);
    if (next < 0 || s_running) {
        upd_sys_unlock();
        return -1;
    }
    upd_sys_unlock();

    /* Reap the previous (finished) worker so its thread can be replaced. */
    if (upd_sys_thread_join(2000) == 1)
        return -1;

    upd_sys_lock();
    s_state = (upd_state_t)next;
    snprintf(s_msg, sizeof(s_msg), "%s", msg);
    if (job == JOB_CHECK)
        s_tag[0] = '\0';
    s_done = s_total = 0;
    s_last_pct = -1;
    s_cancel = 0;
    s_critical = 0;
    s_running = 1;
    s_job = job;
    upd_sys_unlock();

    if (upd_sys_thread_start(worker, NULL) != 0) {
        upd_sys_lock();
        s_running = 0;
        s_state = UPD_ERROR;
        snprintf(s_msg, sizeof(s_msg), "Cannot start updater thread");
        upd_sys_unlock();
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ public API */

int upd_init(void)
{
    if (s_inited)
        return 0;
    if (upd_sys_lock_create() != 0)
        return -1;
    s_state = UPD_IDLE;
    s_msg[0] = '\0';
    s_tag[0] = '\0';
    s_done = s_total = 0;
    s_cancel = s_critical = s_running = 0;
    s_inited = 1;
    return 0;
}

int upd_start_check(void) { return start_job(JOB_CHECK, UPD_EV_START_CHECK, "Checking for updates..."); }
int upd_start_install(void) { return start_job(JOB_INSTALL, UPD_EV_START_INSTALL, "Downloading... 0%"); }

upd_state_t upd_state(void)
{
    if (!s_inited)
        return UPD_IDLE;
    upd_sys_lock();
    upd_state_t st = s_state;
    upd_sys_unlock();
    return st;
}

void upd_message(char *out, size_t n)
{
    if (!out || !n)
        return;
    out[0] = '\0';
    if (!s_inited)
        return;
    upd_sys_lock();
    snprintf(out, n, "%s", s_msg);
    upd_sys_unlock();
}

void upd_latest_tag(char *out, size_t n)
{
    if (!out || !n)
        return;
    out[0] = '\0';
    if (!s_inited)
        return;
    upd_sys_lock();
    snprintf(out, n, "%s", s_tag);
    upd_sys_unlock();
}

int upd_progress(uint64_t *done, uint64_t *total)
{
    uint64_t dn = 0, tt = 0;
    if (s_inited) {
        upd_sys_lock();
        dn = s_done;
        tt = s_total;
        upd_sys_unlock();
    }
    if (done)
        *done = dn;
    if (total)
        *total = tt;
    if (tt == 0)
        return -1;
    return dn >= tt ? 100 : (int)(dn * 100 / tt);
}

int upd_busy(void)
{
    if (!s_inited)
        return 0;
    upd_sys_lock();
    int b = s_running || upd_state_is_busy(s_state);
    upd_sys_unlock();
    return b;
}

void upd_cancel(void)
{
    if (!s_inited)
        return;
    upd_sys_lock();
    s_cancel = 1;
    upd_sys_unlock();
}

int upd_shutdown(void)
{
    if (!s_inited)
        return 0;
    upd_cancel();
    int j = upd_sys_thread_join(3000);
    while (j == 1) {
        upd_sys_lock();
        int crit = s_critical;
        upd_sys_unlock();
        if (!crit)
            break; /* stuck in DNS/connect: let it die with the process */
        j = upd_sys_thread_join(1000); /* never leave mid-rename */
    }
    if (j == 1)
        return 1;
    upd_net_term();
    upd_sys_lock_destroy();
    s_inited = 0;
    return 0;
}

int upd_relaunch(void)
{
    if (upd_state() != UPD_RESTART_READY)
        return -1;
    if (upd_shutdown() != 0)
        return -1;
    return upd_sys_relaunch();
}

int upd_boot_sweep(void)
{
    return upd_install_sweep(UPD_APP_DIR, UPD_DATA_DIR);
}
