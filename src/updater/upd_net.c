#include "upd_net.h"
#include "upd_sys.h"
#include "upd_ver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#ifdef __vita__
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#define NET_POOL_SIZE (1 * 1024 * 1024)
#endif

/* CURLOPT_PROTOCOLS_STR / REDIR_PROTOCOLS_STR need 7.85.0; CAINFO_BLOB needs 7.77.0. */
#if LIBCURL_VERSION_NUM < 0x075500
#error "libcurl >= 7.85.0 required"
#endif

static int    s_ready;
static void  *s_ca_blob;
static size_t s_ca_len;
static char   s_ca_note[160];
#ifdef __vita__
static void *s_net_pool;
static int   s_net_inited, s_netctl_inited;
#endif
static int s_curl_inited;

/* ------------------------------------------------------------------ init */

/* Replays what curl's blob loader does (PEM_X509_INFO_read_bio + X509_STORE_add_cert) on a
 * throwaway store, so a bad bundle on hardware reports counts and an OpenSSL reason instead of
 * one opaque CURLcode 77. Observational only. */
static void ca_describe(void)
{
    BIO *bio = BIO_new_mem_buf(s_ca_blob, (int)s_ca_len);
    if (!bio) {
        snprintf(s_ca_note, sizeof(s_ca_note), "%uB BIO alloc failed", (unsigned)s_ca_len);
        return;
    }
    ERR_clear_error();
    STACK_OF(X509_INFO) *inf = PEM_X509_INFO_read_bio(bio, NULL, NULL, NULL);
    if (!inf) {
        unsigned long e = ERR_get_error();
        const char *r = ERR_reason_error_string(e);
        snprintf(s_ca_note, sizeof(s_ca_note), "%uB PEM unreadable 0x%08lX %s",
                 (unsigned)s_ca_len, e, r ? r : "?");
        BIO_free(bio);
        return;
    }
    X509_STORE *store = X509_STORE_new();
    int entries = sk_X509_INFO_num(inf), certs = 0, added = 0;
    unsigned long first_err = 0;
    for (int i = 0; store && i < entries; i++) {
        X509_INFO *it = sk_X509_INFO_value(inf, i);
        if (!it || !it->x509)
            continue;
        certs++;
        ERR_clear_error();
        if (X509_STORE_add_cert(store, it->x509))
            added++;
        else if (!first_err)
            first_err = ERR_get_error();
    }
    if (!store)
        snprintf(s_ca_note, sizeof(s_ca_note), "%uB store alloc failed", (unsigned)s_ca_len);
    else if (first_err) {
        const char *r = ERR_reason_error_string(first_err);
        snprintf(s_ca_note, sizeof(s_ca_note), "%uB %d/%d certs 0x%08lX %s", (unsigned)s_ca_len,
                 added, certs, first_err, r ? r : "?");
    } else
        snprintf(s_ca_note, sizeof(s_ca_note), "%uB %d/%d certs", (unsigned)s_ca_len, added,
                 certs);
    if (store)
        X509_STORE_free(store);
    sk_X509_INFO_pop_free(inf, X509_INFO_free);
    BIO_free(bio);
}

static int ca_load(const char *ca_path, char *err, size_t cap)
{
    FILE *f = upd_sys_fopen(ca_path, "rb");
    if (!f) {
        snprintf(err, cap, "CA bundle %s: cannot open", ca_path);
        return -1;
    }
    long n = -1;
    if (fseek(f, 0, SEEK_END) == 0)
        n = ftell(f);
    if (n <= 0 || n > 1024 * 1024) {
        fclose(f);
        snprintf(err, cap, "CA bundle %s: bad size %ld", ca_path, n);
        return -1;
    }
    rewind(f);
    void *buf = malloc((size_t)n);
    if (!buf) {
        fclose(f);
        snprintf(err, cap, "CA bundle: no memory for %ld B", n);
        return -1;
    }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n || !strstr((const char *)buf, "-----BEGIN CERTIFICATE-----")) {
        free(buf);
        snprintf(err, cap, "CA bundle %s: short read or no certificate", ca_path);
        return -1;
    }
    s_ca_blob = buf;
    s_ca_len = got;
    ca_describe();
    return 0;
}

int upd_net_init(const char *ca_path, char *err, size_t err_cap)
{
    if (s_ready)
        return UPD_NET_OK;

#ifdef __vita__
    int rc = sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    if (rc < 0) {
        snprintf(err, err_cap, "SCE_SYSMODULE_NET load failed 0x%08X", (unsigned)rc);
        return UPD_NET_E_INIT;
    }
    if (!s_net_inited) {
        if (!s_net_pool)
            s_net_pool = malloc(NET_POOL_SIZE);
        if (!s_net_pool) {
            snprintf(err, err_cap, "no memory for net pool");
            return UPD_NET_E_INIT;
        }
        SceNetInitParam p;
        p.memory = s_net_pool;
        p.size = NET_POOL_SIZE;
        p.flags = 0;
        rc = sceNetInit(&p);
        /* 0x80410110 = already initialised by someone else in the process: fine. */
        if (rc < 0 && (unsigned)rc != 0x80410110U) {
            snprintf(err, err_cap, "sceNetInit failed 0x%08X", (unsigned)rc);
            return UPD_NET_E_INIT;
        }
        s_net_inited = 1;
    }
    if (!s_netctl_inited) {
        rc = sceNetCtlInit();
        if (rc < 0 && (unsigned)rc != 0x80412102U) { /* already initialised */
            snprintf(err, err_cap, "sceNetCtlInit failed 0x%08X", (unsigned)rc);
            return UPD_NET_E_INIT;
        }
        s_netctl_inited = 1;
    }
#endif

    if (!s_curl_inited) {
        CURLcode cc = curl_global_init(CURL_GLOBAL_ALL);
        if (cc != CURLE_OK) {
            snprintf(err, err_cap, "curl_global_init: %s", curl_easy_strerror(cc));
            return UPD_NET_E_INIT;
        }
        s_curl_inited = 1;
    }

    if (!s_ca_blob && ca_load(ca_path, err, err_cap) != 0)
        return UPD_NET_E_CA;

    s_ready = 1;
    return UPD_NET_OK;
}

void upd_net_term(void)
{
    if (s_curl_inited) {
        curl_global_cleanup();
        s_curl_inited = 0;
    }
#ifdef __vita__
    if (s_netctl_inited) {
        sceNetCtlTerm();
        s_netctl_inited = 0;
    }
    if (s_net_inited) {
        sceNetTerm();
        s_net_inited = 0;
    }
    free(s_net_pool);
    s_net_pool = NULL;
#endif
    free(s_ca_blob);
    s_ca_blob = NULL;
    s_ca_len = 0;
    s_ca_note[0] = '\0';
    s_ready = 0;
}

const char *upd_net_ca_note(void) { return s_ca_note[0] ? s_ca_note : "not loaded"; }

/* ------------------------------------------------------------------ helpers */

static void diag_reset(upd_net_diag_t *d)
{
    memset(d, 0, sizeof(*d));
}

/* Second opinion on the allowlist from curl's own URL parser: the host curl will connect to
 * must be the host our parser approved, with no userinfo and https scheme. */
static int url_ok_for_curl(const char *url, char *host_out, size_t cap)
{
    char mine[256];
    if (!upd_url_host_allowed(url) || upd_url_host(url, mine, sizeof(mine)) != 0)
        return 0;
    CURLU *u = curl_url();
    if (!u)
        return 0;
    int ok = 0;
    char *scheme = NULL, *host = NULL, *user = NULL;
    if (curl_url_set(u, CURLUPART_URL, url, 0) == CURLUE_OK
        && curl_url_get(u, CURLUPART_SCHEME, &scheme, 0) == CURLUE_OK
        && curl_url_get(u, CURLUPART_HOST, &host, 0) == CURLUE_OK
        && curl_url_get(u, CURLUPART_USER, &user, 0) != CURLUE_OK) {
        char low[256];
        size_t i, n = strlen(host);
        if (n < sizeof(low)) {
            for (i = 0; i <= n; i++)
                low[i] = (host[i] >= 'A' && host[i] <= 'Z') ? (char)(host[i] + 32) : host[i];
            ok = strcmp(scheme, "https") == 0 && strcmp(low, mine) == 0;
        }
    }
    curl_free(scheme);
    curl_free(host);
    curl_free(user);
    curl_url_cleanup(u);
    if (ok && host_out && cap) {
        /* Diagnostic copy of the host we approved; truncated to fit rather than formatted, so
         * the compiler can see it is bounded (host_out is a 128-byte diag field). */
        size_t n = strlen(mine);
        if (n >= cap)
            n = cap - 1;
        memcpy(host_out, mine, n);
        host_out[n] = '\0';
    }
    return ok;
}

static void apply_common(CURL *c, const char *url, const char *ua, char *errbuf)
{
    struct curl_blob ca;
    errbuf[0] = '\0';
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf);
    ca.data = s_ca_blob;
    ca.len = s_ca_len;
    ca.flags = CURL_BLOB_COPY;
    curl_easy_setopt(c, CURLOPT_CAINFO_BLOB, &ca);
    /* Clear any compiled-in default CA file/dir (a Linux libcurl has both) so the bundled blob
     * is the only trust source. The negative control in tests/ proves this. */
    curl_easy_setopt(c, CURLOPT_CAINFO, NULL);
    curl_easy_setopt(c, CURLOPT_CAPATH, NULL);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2);
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, ua);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 60L);
    curl_easy_setopt(c, CURLOPT_IPRESOLVE, (long)CURL_IPRESOLVE_V4);
}

static int map_curl(CURLcode cc)
{
    switch (cc) {
    case CURLE_OK:
        return UPD_NET_OK;
    case CURLE_PEER_FAILED_VERIFICATION:
    case CURLE_SSL_CACERT_BADFILE:
    case CURLE_SSL_ISSUER_ERROR:
    case CURLE_SSL_PINNEDPUBKEYNOTMATCH:
        return UPD_NET_E_TLS_VERIFY;
    case CURLE_SSL_CONNECT_ERROR:
    case CURLE_SSL_ENGINE_NOTFOUND:
    case CURLE_SSL_ENGINE_SETFAILED:
    case CURLE_SSL_CERTPROBLEM:
    case CURLE_SSL_CIPHER:
    case CURLE_USE_SSL_FAILED:
    case CURLE_SSL_ENGINE_INITFAILED:
    case CURLE_SSL_SHUTDOWN_FAILED:
    case CURLE_SSL_CRL_BADFILE:
        return UPD_NET_E_TLS;
    case CURLE_COULDNT_RESOLVE_HOST:
        return UPD_NET_E_DNS;
    case CURLE_COULDNT_CONNECT:
        return UPD_NET_E_CONNECT;
    case CURLE_OPERATION_TIMEDOUT:
        return UPD_NET_E_TIMEOUT;
    case CURLE_RECV_ERROR:
    case CURLE_SEND_ERROR:
    case CURLE_GOT_NOTHING:
    case CURLE_PARTIAL_FILE:
        return UPD_NET_E_LOST;
    case CURLE_FILESIZE_EXCEEDED:
        return UPD_NET_E_TOO_BIG;
    case CURLE_WRITE_ERROR:
        return UPD_NET_E_WRITE;
    case CURLE_ABORTED_BY_CALLBACK:
        return UPD_NET_E_CANCELLED;
    default:
        return UPD_NET_E_OTHER;
    }
}

static void record_hop(CURL *c, CURLcode cc, const char *errbuf, upd_net_diag_t *d)
{
    long code = 0, vr = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_getinfo(c, CURLINFO_SSL_VERIFYRESULT, &vr);
    d->curl_code = (int)cc;
    d->http_code = code;
    d->verify_result = vr;
    d->hops++;
    size_t used = strlen(d->codes);
    if (used + 8 < sizeof(d->codes))
        snprintf(d->codes + used, sizeof(d->codes) - used, "%s%ld", used ? "," : "", code);
    if (cc != CURLE_OK)
        snprintf(d->detail, sizeof(d->detail), "curl %d %s: %s (verify %ld) [CA %s]", (int)cc,
                 curl_easy_strerror(cc), errbuf[0] ? errbuf : "-", vr, upd_net_ca_note());
}

typedef struct {
    upd_net_progress_fn fn;
    void *user;
    int cancelled;
} ProgressCtx;

/* ------------------------------------------------------------------ HEAD */

static int head_xferinfo(void *user, curl_off_t dt, curl_off_t dn, curl_off_t ut, curl_off_t un)
{
    (void)dt;
    (void)dn;
    (void)ut;
    (void)un;
    ProgressCtx *p = (ProgressCtx *)user;
    if (p->fn && p->fn(0, 0, p->user)) {
        p->cancelled = 1;
        return 1;
    }
    return 0;
}

int upd_net_head_ex(const char *url, const char *user_agent, long *status, char *location,
                    size_t loc_cap, upd_net_diag_t *d, upd_net_progress_fn fn, void *user)
{
    char errbuf[CURL_ERROR_SIZE];
    diag_reset(d);
    *status = 0;
    if (location && loc_cap)
        location[0] = '\0';
    if (!s_ready) {
        snprintf(d->detail, sizeof(d->detail), "network not initialised");
        return UPD_NET_E_INIT;
    }
    if (!url_ok_for_curl(url, d->final_host, sizeof(d->final_host))) {
        snprintf(d->detail, sizeof(d->detail), "URL host not allowed");
        return UPD_NET_E_BAD_HOST;
    }
    CURL *c = curl_easy_init();
    if (!c) {
        snprintf(d->detail, sizeof(d->detail), "curl_easy_init failed");
        return UPD_NET_E_OTHER;
    }
    ProgressCtx pc = { fn, user, 0 };
    apply_common(c, url, user_agent, errbuf);
    curl_easy_setopt(c, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 45L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, head_xferinfo);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &pc);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);

    CURLcode cc = curl_easy_perform(c);
    record_hop(c, cc, errbuf, d);
    int rc = map_curl(cc);
    if (rc == UPD_NET_OK) {
        *status = d->http_code;
        char *redir = NULL;
        if (curl_easy_getinfo(c, CURLINFO_REDIRECT_URL, &redir) == CURLE_OK && redir && location
            && loc_cap)
            snprintf(location, loc_cap, "%s", redir);
    }
    curl_easy_cleanup(c);
    return rc;
}

/* ------------------------------------------------------------------ download */

typedef struct {
    CURL *c;
    FILE *fp;
    uint64_t written;
    uint64_t discarded;
    uint64_t max_bytes;
    int too_big;
    int write_failed;
    ProgressCtx *pc;
} Sink;

static size_t dl_write(char *ptr, size_t size, size_t nmemb, void *user)
{
    Sink *s = (Sink *)user;
    size_t n = size * nmemb;
    long code = 0;
    curl_easy_getinfo(s->c, CURLINFO_RESPONSE_CODE, &code);
    if (code != 200) {
        /* Redirect / error bodies are thrown away, but not without limit. */
        s->discarded += n;
        return s->discarded > 64 * 1024 ? 0 : n;
    }
    if (s->written + n > s->max_bytes) {
        s->too_big = 1;
        return 0;
    }
    if (fwrite(ptr, 1, n, s->fp) != n) {
        s->write_failed = 1;
        return 0;
    }
    s->written += n;
    return n;
}

static int dl_xferinfo(void *user, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ut,
                       curl_off_t un)
{
    (void)ut;
    (void)un;
    Sink *s = (Sink *)user;
    long code = 0;
    curl_easy_getinfo(s->c, CURLINFO_RESPONSE_CODE, &code);
    if (code == 200 && dltotal > 0 && (uint64_t)dltotal > s->max_bytes) {
        s->too_big = 1;
        return 1;
    }
    if (s->pc->fn) {
        uint64_t now = code == 200 && dlnow > 0 ? (uint64_t)dlnow : 0;
        uint64_t tot = code == 200 && dltotal > 0 ? (uint64_t)dltotal : 0;
        if (s->pc->fn(now, tot, s->pc->user)) {
            s->pc->cancelled = 1;
            return 1;
        }
    }
    return 0;
}

int upd_net_download(const char *url, const char *user_agent, const char *dest_path,
                     uint64_t max_bytes, upd_net_progress_fn fn, void *user,
                     upd_net_diag_t *d)
{
    char errbuf[CURL_ERROR_SIZE];
    char cur[2048];
    diag_reset(d);
    if (!s_ready) {
        snprintf(d->detail, sizeof(d->detail), "network not initialised");
        return UPD_NET_E_INIT;
    }
    if (strlen(url) >= sizeof(cur)) {
        snprintf(d->detail, sizeof(d->detail), "URL too long");
        return UPD_NET_E_OTHER;
    }
    snprintf(cur, sizeof(cur), "%s", url);

    upd_sys_remove(dest_path);
    FILE *fp = upd_sys_fopen(dest_path, "wb");
    if (!fp) {
        snprintf(d->detail, sizeof(d->detail), "cannot create %s", dest_path);
        return UPD_NET_E_WRITE;
    }
    CURL *c = curl_easy_init();
    if (!c) {
        fclose(fp);
        upd_sys_remove(dest_path);
        snprintf(d->detail, sizeof(d->detail), "curl_easy_init failed");
        return UPD_NET_E_OTHER;
    }

    ProgressCtx pc = { fn, user, 0 };
    Sink sink;
    memset(&sink, 0, sizeof(sink));
    sink.c = c;
    sink.fp = fp;
    sink.max_bytes = max_bytes;
    sink.pc = &pc;

    int rc = UPD_NET_E_REDIRECTS;
    for (int hop = 0; hop < UPD_NET_MAX_HOPS; hop++) {
        if (!url_ok_for_curl(cur, d->final_host, sizeof(d->final_host))) {
            char h[128];
            if (upd_url_host(cur, h, sizeof(h)) != 0)
                snprintf(h, sizeof(h), "(unparseable)");
            snprintf(d->detail, sizeof(d->detail), "redirect %d to disallowed host %s", hop, h);
            rc = UPD_NET_E_BAD_HOST;
            break;
        }
        apply_common(c, cur, user_agent, errbuf);
        curl_easy_setopt(c, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(c, CURLOPT_MAXFILESIZE_LARGE, (curl_off_t)max_bytes);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, dl_write);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, dl_xferinfo);
        curl_easy_setopt(c, CURLOPT_XFERINFODATA, &sink);
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        sink.discarded = 0;

        CURLcode cc = curl_easy_perform(c);
        record_hop(c, cc, errbuf, d);
        if (cc != CURLE_OK) {
            if (pc.cancelled)
                rc = UPD_NET_E_CANCELLED;
            else if (sink.too_big)
                rc = UPD_NET_E_TOO_BIG;
            else if (sink.write_failed)
                rc = UPD_NET_E_WRITE;
            else
                rc = map_curl(cc);
            break;
        }
        long code = d->http_code;
        if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
            char *redir = NULL;
            if (curl_easy_getinfo(c, CURLINFO_REDIRECT_URL, &redir) != CURLE_OK || !redir
                || strlen(redir) >= sizeof(cur)) {
                snprintf(d->detail, sizeof(d->detail), "HTTP %ld without usable Location", code);
                rc = UPD_NET_E_HTTP;
                break;
            }
            snprintf(cur, sizeof(cur), "%s", redir); /* signed URL: used now, never stored */
            continue;
        }
        if (code == 200) {
            rc = UPD_NET_OK;
        } else {
            snprintf(d->detail, sizeof(d->detail), "HTTP %ld from %s", code, d->final_host);
            rc = UPD_NET_E_HTTP;
        }
        break;
    }
    if (rc == UPD_NET_E_REDIRECTS)
        snprintf(d->detail, sizeof(d->detail), "more than %d requests", UPD_NET_MAX_HOPS);
    curl_easy_cleanup(c);

    d->bytes = sink.written;
    if (fclose(fp) != 0 && rc == UPD_NET_OK) {
        snprintf(d->detail, sizeof(d->detail), "close failed on %s", dest_path);
        rc = UPD_NET_E_WRITE;
    }
    if (rc != UPD_NET_OK)
        upd_sys_remove(dest_path);
    return rc;
}
