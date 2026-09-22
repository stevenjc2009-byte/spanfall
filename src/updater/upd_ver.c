#include "upd_ver.h"

#include <string.h>

/* ------------------------------------------------------------------ states */

int upd_decide(upd_state_t state, upd_event_t ev)
{
    switch (ev) {
    case UPD_EV_START_CHECK:
        if (state == UPD_IDLE || state == UPD_UP_TO_DATE || state == UPD_ERROR)
            return UPD_CHECKING;
        return -1;
    case UPD_EV_START_INSTALL:
        return state == UPD_AVAILABLE ? UPD_DOWNLOADING : -1;
    case UPD_EV_FOUND_NEWER:
        return state == UPD_CHECKING ? UPD_AVAILABLE : -1;
    case UPD_EV_NO_NEWER:
        return state == UPD_CHECKING ? UPD_UP_TO_DATE : -1;
    case UPD_EV_DOWNLOADED:
        return state == UPD_DOWNLOADING ? UPD_VERIFYING : -1;
    case UPD_EV_VERIFIED:
        return state == UPD_VERIFYING ? UPD_INSTALLING : -1;
    case UPD_EV_INSTALLED:
        return state == UPD_INSTALLING ? UPD_RESTART_READY : -1;
    case UPD_EV_FAILED:
        return upd_state_is_busy(state) ? UPD_ERROR : -1;
    case UPD_EV_CANCELLED:
        return upd_state_is_busy(state) ? UPD_IDLE : -1;
    }
    return -1;
}

int upd_state_is_busy(upd_state_t state)
{
    return state == UPD_CHECKING || state == UPD_DOWNLOADING || state == UPD_VERIFYING
        || state == UPD_INSTALLING;
}

/* ------------------------------------------------------------------ versions */

static int is_digit(char c) { return c >= '0' && c <= '9'; }
static int is_alnum(char c)
{
    return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int upd_version_parse(const char *s, upd_ver_t *out)
{
    upd_ver_t v;
    int i;

    if (!s || !out)
        return -1;
    memset(&v, 0, sizeof(v));
    if (*s == 'v' || *s == 'V')
        s++;
    for (i = 0; i < 3; i++) {
        unsigned long n = 0;
        if (!is_digit(*s))
            return -1;
        while (is_digit(*s)) {
            n = n * 10 + (unsigned long)(*s - '0');
            if (n > UPD_VER_PART_MAX)
                n = UPD_VER_PART_MAX; /* saturate; keeps n*10 far from overflow */
            s++;
        }
        v.part[i] = n;
        if (i < 2) {
            if (*s != '.')
                return -1;
            s++;
        }
    }
    if (*s == '-') {
        s++;
        if (*s == '\0')
            return -1;
        while (*s) {
            if (!is_alnum(*s) && *s != '.' && *s != '-')
                return -1;
            s++;
        }
        v.has_suffix = 1;
    } else if (*s != '\0') {
        return -1;
    }
    *out = v;
    return 0;
}

int upd_version_compare(const upd_ver_t *a, const upd_ver_t *b)
{
    int i;
    for (i = 0; i < 3; i++) {
        if (a->part[i] < b->part[i])
            return -1;
        if (a->part[i] > b->part[i])
            return 1;
    }
    if (a->has_suffix && !b->has_suffix)
        return -1;
    if (!a->has_suffix && b->has_suffix)
        return 1;
    return 0;
}

/* ------------------------------------------------------------------ URLs */

int upd_tag_from_location(const char *location, char *out, size_t cap)
{
    static const char seg[] = "/releases/tag/";
    const char *start, *end;
    size_t len;

    if (!location || !out || cap == 0)
        return -1;
    start = strstr(location, seg);
    if (!start)
        return -1;
    start += sizeof(seg) - 1;
    end = start;
    while (*end && *end != '?' && *end != '#' && *end != '/' && *end != '\r' && *end != '\n'
           && *end != ' ' && *end != '\t')
        end++;
    len = (size_t)(end - start);
    if (len == 0 || len + 1 > cap)
        return -1;
    memcpy(out, start, len);
    out[len] = '\0';
    return 0;
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

int upd_url_host(const char *url, char *out, size_t cap)
{
    static const char scheme[] = "https://";
    const char *p, *auth_end, *host_end;
    size_t i, len;

    if (!url || !out || cap == 0)
        return -1;
    for (i = 0; i < sizeof(scheme) - 1; i++)
        if (lower(url[i]) != scheme[i])
            return -1; /* also stops at a short string: '\0' never matches */
    p = url + sizeof(scheme) - 1;

    auth_end = p;
    while (*auth_end && *auth_end != '/' && *auth_end != '?' && *auth_end != '#')
        auth_end++;

    /* Userinfo ("user@host") is refused outright: it is how "github.com@evil.com" hides. */
    for (host_end = p; host_end < auth_end; host_end++)
        if (*host_end == '@')
            return -1;

    host_end = p;
    while (host_end < auth_end && *host_end != ':')
        host_end++;
    if (host_end < auth_end) {
        /* Only the default https port may be spelled out. */
        if ((size_t)(auth_end - host_end) != 4 || strncmp(host_end, ":443", 4) != 0)
            return -1;
    }

    len = (size_t)(host_end - p);
    if (len == 0 || len + 1 > cap)
        return -1;
    for (i = 0; i < len; i++) {
        char c = lower(p[i]);
        if (!((c >= 'a' && c <= 'z') || is_digit(c) || c == '.' || c == '-'))
            return -1;
        out[i] = c;
    }
    out[len] = '\0';
    if (out[0] == '.' || out[len - 1] == '.')
        return -1;
    return 0;
}

int upd_url_host_allowed(const char *url)
{
    static const char suffix[] = ".githubusercontent.com";
    char host[256];
    size_t n, sl = sizeof(suffix) - 1;

    if (upd_url_host(url, host, sizeof(host)) != 0)
        return 0;
    if (strcmp(host, "github.com") == 0)
        return 1;
    n = strlen(host);
    if (n > sl && strcmp(host + n - sl, suffix) == 0)
        return 1;
    return 0;
}

upd_check_result_t upd_classify_check(long status, const char *location, char *tag, size_t cap)
{
    if (tag && cap)
        tag[0] = '\0';
    if (status < 100 || status > 599)
        return UPD_CHK_BAD_RESPONSE;
    if (status == 404)
        return UPD_CHK_NOT_FOUND;
    if (status == 403 || status == 429)
        return UPD_CHK_RATE_LIMITED;
    if (status != 301 && status != 302 && status != 303 && status != 307 && status != 308)
        return UPD_CHK_BAD_RESPONSE;
    if (!location || location[0] == '\0')
        return UPD_CHK_BAD_RESPONSE;
    if (!upd_url_host_allowed(location))
        return UPD_CHK_BAD_HOST;
    if (upd_tag_from_location(location, tag, cap) == 0)
        return UPD_CHK_TAG;
    /* A repo with no releases redirects /releases/latest to /releases. Anything else (a login
     * page, a renamed repo's root) is not evidence of "no releases" and must not read as
     * "up to date". */
    {
        const char *r = strstr(location, "/releases");
        if (r) {
            r += 9;
            if (*r == '/')
                r++;
            if (*r == '\0' || *r == '?' || *r == '#')
                return UPD_CHK_NO_RELEASES;
        }
    }
    return UPD_CHK_BAD_RESPONSE;
}

/* ------------------------------------------------------------------ archives */

int upd_zip_path_ok(const char *name)
{
    const char *p, *comp;
    size_t n;

    if (!name)
        return 0;
    n = strlen(name);
    if (n == 0 || n >= 256 || name[0] == '/')
        return 0;
    for (p = name; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c == 0x7f || c == ':' || c == '\\')
            return 0;
    }
    comp = name;
    for (;;) {
        const char *slash = strchr(comp, '/');
        size_t len = slash ? (size_t)(slash - comp) : strlen(comp);
        if (!slash) {
            /* Last component; empty only when the name ended in '/' (a directory entry). */
            if (len == 0)
                return comp != name;
        }
        if (len == 0)
            return 0; /* "a//b" */
        if (len == 1 && comp[0] == '.')
            return 0;
        if (len == 2 && comp[0] == '.' && comp[1] == '.')
            return 0;
        if (!slash)
            return 1;
        comp = slash + 1;
    }
}

static unsigned rd16(const unsigned char *b) { return (unsigned)b[0] | ((unsigned)b[1] << 8); }
static unsigned long rd32(const unsigned char *b)
{
    return (unsigned long)b[0] | ((unsigned long)b[1] << 8) | ((unsigned long)b[2] << 16)
        | ((unsigned long)b[3] << 24);
}

int upd_sfo_titleid(const unsigned char *buf, size_t len, char *out, size_t cap)
{
    unsigned long key_tab, val_tab, count, i;

    if (!buf || !out || cap < 10)
        return -1;
    out[0] = '\0';
    if (len < 20 || rd32(buf) != 0x46535000UL) /* "\0PSF" */
        return -1;
    key_tab = rd32(buf + 8);
    val_tab = rd32(buf + 12);
    count = rd32(buf + 16);
    if (key_tab >= len || val_tab >= len || count > (len - 20) / 16)
        return -1;

    for (i = 0; i < count; i++) {
        const unsigned char *e = buf + 20 + i * 16;
        unsigned long k = key_tab + rd16(e);
        unsigned long vlen = rd32(e + 4);
        unsigned long v = val_tab + rd32(e + 12);
        const unsigned char *kend;
        size_t j;

        if (k >= len)
            return -1;
        kend = memchr(buf + k, '\0', len - k);
        if (!kend)
            return -1;
        if (strcmp((const char *)buf + k, "TITLE_ID") != 0)
            continue;
        if (v >= len || vlen > len - v)
            return -1;
        /* vlen counts the terminating NUL for utf8 strings. */
        for (j = 0; j < 9; j++) {
            unsigned char c;
            if (j >= vlen)
                return -1;
            c = buf[v + j];
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
                return -1;
            out[j] = (char)c;
        }
        if (vlen > 9 && buf[v + 9] != '\0')
            return -1;
        out[9] = '\0';
        return 0;
    }
    return -1;
}
