#ifndef UPD_NET_H
#define UPD_NET_H

/* libcurl + OpenSSL transport for the updater (D2, steve 2026-09-17).
 * TLS rules, applied to every handle with no way to turn them off:
 *   CA store = the bundled PEM, loaded into memory once, passed as CURLOPT_CAINFO_BLOB (COPY)
 *   VERIFYPEER=1, VERIFYHOST=2, TLS >= 1.2, protocols and redirect protocols "https" only
 * Redirects are never followed by curl; upd_net_download walks them itself and every URL must
 * pass upd_url_host_allowed().
 * Vita-only: this file needs libcurl and is neither built nor tested on the host. The host
 * flow tests link tests/upd/fake_net.c in its place, so the two signatures must be kept in
 * step by hand -- nothing catches them drifting apart. */

#include <stddef.h>
#include <stdint.h>

#define UPD_NET_OK               0
#define UPD_NET_E_INIT          -1  /* sysmodule / sceNet / curl_global_init */
#define UPD_NET_E_CA            -2  /* certificate store missing or unreadable */
#define UPD_NET_E_TLS_VERIFY    -3  /* certificate chain / hostname did not verify */
#define UPD_NET_E_TLS           -4  /* other TLS handshake failure */
#define UPD_NET_E_DNS           -5
#define UPD_NET_E_CONNECT       -6
#define UPD_NET_E_TIMEOUT       -7
#define UPD_NET_E_LOST          -8  /* connection dropped mid-transfer */
#define UPD_NET_E_HTTP          -9  /* final status not what was required (see diag.http_code) */
#define UPD_NET_E_BAD_HOST      -10 /* URL or redirect outside the allowlist */
#define UPD_NET_E_REDIRECTS     -11 /* more than UPD_NET_MAX_HOPS requests */
#define UPD_NET_E_TOO_BIG       -12
#define UPD_NET_E_WRITE         -13 /* local file write failed */
#define UPD_NET_E_CANCELLED     -14
#define UPD_NET_E_OTHER         -15

#define UPD_NET_MAX_HOPS 5

typedef struct {
    int      curl_code;       /* last CURLcode */
    long     http_code;       /* last response code (0 if none) */
    long     verify_result;   /* CURLINFO_SSL_VERIFYRESULT of the last hop (0 = ok) */
    int      hops;            /* requests made */
    char     codes[64];       /* e.g. "302,200" */
    char     final_host[128]; /* host of the last request, never the full (signed) URL */
    uint64_t bytes;           /* body bytes written to the destination file */
    char     detail[400];     /* curl error buffer / reason, for last_error.txt */
} upd_net_diag_t;

/* Idempotent. Loads the network stack (Vita), curl_global_init, and reads the CA bundle at
 * ca_path into memory. No fallback of any kind if the bundle cannot be read. */
int  upd_net_init(const char *ca_path, char *err, size_t err_cap);
void upd_net_term(void);

/* A short "[CA ...]" description of the loaded bundle (size, certificates accepted by OpenSSL). */
const char *upd_net_ca_note(void);

/* Return nonzero to abort (cancel). total is 0 when unknown. */
typedef int (*upd_net_progress_fn)(uint64_t now, uint64_t total, void *user);

/* HEAD url without following redirects. UPD_NET_OK means a response arrived: *status holds it
 * and location holds the absolute redirect target ("" if none). fn is polled during the
 * request and nonzero aborts with UPD_NET_E_CANCELLED; fn may be NULL. */
int upd_net_head_ex(const char *url, const char *user_agent, long *status, char *location,
                    size_t loc_cap, upd_net_diag_t *diag, upd_net_progress_fn fn, void *user);


/* GET url into dest_path, walking at most UPD_NET_MAX_HOPS redirects by hand. Only a final 200
 * writes the file. Aborts once the body would exceed max_bytes. On failure dest_path is
 * removed. */
int upd_net_download(const char *url, const char *user_agent, const char *dest_path,
                     uint64_t max_bytes, upd_net_progress_fn fn, void *user,
                     upd_net_diag_t *diag);

#endif
