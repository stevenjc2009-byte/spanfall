#include "upd_install.h"
#include "upd_fs.h"
#include "upd_sys.h"
#include "upd_ver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FILES 1024

typedef struct {
    char rel[256];
    unsigned char hash[UPD_SHA256_LEN];
} FileRec;

typedef struct {
    FileRec *v;
    size_t n, cap;
    int failed;
} FileList;

static int path_join(char *out, size_t cap, const char *a, const char *b, const char *suffix)
{
    return (size_t)snprintf(out, cap, "%s/%s%s", a, b, suffix ? suffix : "") < cap ? 0 : -1;
}

/* ------------------------------------------------------------------ stage verify */

int upd_stage_verify(const char *stage_dir, const char *title_id, char *err, size_t err_cap)
{
    char path[UPD_PATH_CAP];
    unsigned char buf[16 * 1024];

    path_join(path, sizeof(path), stage_dir, "sce_sys/param.sfo", NULL);
    FILE *f = upd_sys_fopen(path, "rb");
    if (!f) {
        snprintf(err, err_cap, "no sce_sys/param.sfo in update");
        return UPD_E_NO_SFO;
    }
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    char tid[16];
    if (upd_sfo_titleid(buf, n, tid, sizeof(tid)) != 0) {
        snprintf(err, err_cap, "param.sfo unreadable (%u B)", (unsigned)n);
        return UPD_E_NO_SFO;
    }
    if (strcmp(tid, title_id) != 0) {
        snprintf(err, err_cap, "update TITLE_ID %s, expected %s", tid, title_id);
        return UPD_E_TITLE;
    }

    path_join(path, sizeof(path), stage_dir, "eboot.bin", NULL);
    f = upd_sys_fopen(path, "rb");
    if (!f) {
        snprintf(err, err_cap, "no eboot.bin in update");
        return UPD_E_NO_EBOOT;
    }
    unsigned char magic[4] = { 1, 1, 1, 1 };
    n = fread(magic, 1, 4, f);
    fclose(f);
    if (n != 4 || memcmp(magic, "SCE\0", 4) != 0) {
        snprintf(err, err_cap, "eboot.bin is not a SELF (%02X%02X%02X%02X)", magic[0], magic[1],
                 magic[2], magic[3]);
        return UPD_E_BAD_EBOOT;
    }
    return 0;
}

/* ------------------------------------------------------------------ file list */

typedef struct {
    FileList *list;
    const char *root;
    char prefix[256];
} WalkCtx;

static int ends_with(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && strcmp(s + a - b, suf) == 0;
}

static int walk_cb(const char *name, int is_dir, void *user)
{
    WalkCtx *w = (WalkCtx *)user;
    char rel[256];
    if ((size_t)snprintf(rel, sizeof(rel), "%s%s", w->prefix, name) >= sizeof(rel)
        || !upd_zip_path_ok(rel) || ends_with(rel, ".new") || ends_with(rel, ".old")) {
        w->list->failed = 1;
        return 1;
    }
    if (is_dir) {
        WalkCtx sub = *w;
        char full[UPD_PATH_CAP];
        if ((size_t)snprintf(sub.prefix, sizeof(sub.prefix), "%s/", rel) >= sizeof(sub.prefix)
            || path_join(full, sizeof(full), w->root, rel, NULL) != 0
            || upd_sys_list_dir(full, walk_cb, &sub) != 0) {
            w->list->failed = 1;
            return 1;
        }
        return 0;
    }
    FileList *l = w->list;
    if (l->n >= MAX_FILES) {
        l->failed = 1;
        return 1;
    }
    if (l->n == l->cap) {
        size_t ncap = l->cap ? l->cap * 2 : 16;
        FileRec *nv = realloc(l->v, ncap * sizeof(*nv));
        if (!nv) {
            l->failed = 1;
            return 1;
        }
        l->v = nv;
        l->cap = ncap;
    }
    snprintf(l->v[l->n].rel, sizeof(l->v[l->n].rel), "%s", rel);
    l->n++;
    return 0;
}

/* ------------------------------------------------------------------ manifest */

static int manifest_write(const char *work_dir, const FileList *l)
{
    char tmp[UPD_PATH_CAP], fin[UPD_PATH_CAP];
    if (path_join(tmp, sizeof(tmp), work_dir, UPD_WORK_MANIFEST, ".tmp") != 0
        || path_join(fin, sizeof(fin), work_dir, UPD_WORK_MANIFEST, NULL) != 0)
        return -1;
    upd_sys_remove(tmp);
    FILE *f = upd_sys_fopen(tmp, "wb");
    if (!f)
        return -1;
    int rc = 0;
    for (size_t i = 0; i < l->n; i++) {
        char hex[2 * UPD_SHA256_LEN + 1];
        upd_fs_hex(l->v[i].hash, UPD_SHA256_LEN, hex);
        if (fprintf(f, "%s %s\n", hex, l->v[i].rel) < 0)
            rc = -1;
    }
    if (fclose(f) != 0)
        rc = -1;
    upd_sys_remove(fin);
    if (rc == 0 && upd_sys_rename(tmp, fin) != 0)
        rc = -1;
    if (rc != 0)
        upd_sys_remove(tmp);
    return rc;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

static int manifest_read(const char *work_dir, FileList *l)
{
    char path[UPD_PATH_CAP];
    char line[512];
    if (path_join(path, sizeof(path), work_dir, UPD_WORK_MANIFEST, NULL) != 0)
        return -1;
    FILE *f = upd_sys_fopen(path, "rb");
    if (!f)
        return -1;
    int rc = 0;
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        if (n == 0)
            continue;
        if (n < 2 * UPD_SHA256_LEN + 2 || line[2 * UPD_SHA256_LEN] != ' '
            || l->n >= MAX_FILES) {
            rc = -1;
            break;
        }
        const char *rel = line + 2 * UPD_SHA256_LEN + 1;
        size_t rel_len = strlen(rel);
        if (!upd_zip_path_ok(rel) || rel_len >= sizeof(l->v[0].rel)) {
            rc = -1;
            break;
        }
        if (l->n == l->cap) {
            size_t ncap = l->cap ? l->cap * 2 : 16;
            FileRec *nv = realloc(l->v, ncap * sizeof(*nv));
            if (!nv) {
                rc = -1;
                break;
            }
            l->v = nv;
            l->cap = ncap;
        }
        FileRec *r = &l->v[l->n];
        for (int i = 0; i < UPD_SHA256_LEN; i++) {
            int hi = hexval(line[2 * i]), lo = hexval(line[2 * i + 1]);
            if (hi < 0 || lo < 0) {
                rc = -1;
                break;
            }
            r->hash[i] = (unsigned char)(hi * 16 + lo);
        }
        if (rc != 0)
            break;
        memcpy(r->rel, rel, rel_len + 1); /* rel_len < sizeof(r->rel), checked above */
        l->n++;
    }
    fclose(f);
    return rc;
}

static int hash_matches(const char *path, const unsigned char *expect)
{
    unsigned char h[UPD_SHA256_LEN];
    return upd_fs_sha256(path, h) == 0 && memcmp(h, expect, UPD_SHA256_LEN) == 0;
}

/* Bring one file to its verified new content. 0 when <path> holds the expected bytes. */
static int roll_forward_one(const char *app_dir, const FileRec *r)
{
    char p[UPD_PATH_CAP], pn[UPD_PATH_CAP], po[UPD_PATH_CAP];
    if (path_join(p, sizeof(p), app_dir, r->rel, NULL) != 0
        || path_join(pn, sizeof(pn), app_dir, r->rel, ".new") != 0
        || path_join(po, sizeof(po), app_dir, r->rel, ".old") != 0)
        return -1;

    if (upd_sys_exists(pn)) {
        if (!hash_matches(pn, r->hash)) {
            upd_sys_remove(pn);
            if (!upd_sys_exists(p) && upd_sys_exists(po))
                upd_sys_rename(po, p);
            return -1;
        }
        if (upd_sys_exists(p)) {
            if (upd_sys_exists(po))
                upd_sys_remove(po);
            if (upd_sys_rename(p, po) != 0)
                return -1;
        }
        if (upd_sys_rename(pn, p) != 0) {
            if (!upd_sys_exists(p) && upd_sys_exists(po))
                upd_sys_rename(po, p);
            return -1;
        }
        if (upd_sys_exists(po))
            upd_sys_remove(po);
        return 0;
    }
    if (upd_sys_exists(p) && hash_matches(p, r->hash)) {
        if (upd_sys_exists(po))
            upd_sys_remove(po);
        return 0;
    }
    if (!upd_sys_exists(p) && upd_sys_exists(po))
        upd_sys_rename(po, p);
    return -1;
}

static void remove_new_copies(const char *app_dir, const FileList *l)
{
    for (size_t i = 0; i < l->n; i++) {
        char pn[UPD_PATH_CAP];
        if (path_join(pn, sizeof(pn), app_dir, l->v[i].rel, ".new") == 0)
            upd_sys_remove(pn);
    }
}

static void remove_work_file(const char *work_dir, const char *name)
{
    char path[UPD_PATH_CAP];
    if (path_join(path, sizeof(path), work_dir, name, NULL) == 0)
        upd_fs_remove_tree(path);
}

/* ------------------------------------------------------------------ apply */

int upd_install_apply(const char *stage_dir, const char *app_dir, const char *work_dir,
                      const upd_install_cb_t *cb, char *err, size_t err_cap)
{
    FileList l = { NULL, 0, 0, 0 };
    WalkCtx w;
    int rc = UPD_RC_OK;
    void *user = cb ? cb->user : NULL;

    memset(&w, 0, sizeof(w));
    w.list = &l;
    w.root = stage_dir;
    if (upd_sys_list_dir(stage_dir, walk_cb, &w) != 0 || l.failed || l.n == 0) {
        snprintf(err, err_cap, "cannot list staged files (%u found)", (unsigned)l.n);
        free(l.v);
        return UPD_E_STAGE;
    }
    for (size_t i = 0; i < l.n; i++) {
        char sp[UPD_PATH_CAP];
        if (path_join(sp, sizeof(sp), stage_dir, l.v[i].rel, NULL) != 0
            || upd_fs_sha256(sp, l.v[i].hash) != 0) {
            snprintf(err, err_cap, "cannot hash staged %s", l.v[i].rel);
            free(l.v);
            return UPD_E_STAGE;
        }
    }
    if (upd_fs_mkdir_p(work_dir) != 0 || manifest_write(work_dir, &l) != 0) {
        snprintf(err, err_cap, "cannot write install manifest");
        free(l.v);
        return UPD_E_STAGE;
    }

    /* Pass 1: <app>/<rel>.new copies, each verified. Cancellable; the app is untouched. */
    for (size_t i = 0; i < l.n && rc == UPD_RC_OK; i++) {
        char sp[UPD_PATH_CAP], pn[UPD_PATH_CAP];
        if (cb && cb->cancel && cb->cancel(user)) {
            rc = UPD_RC_CANCELLED;
            break;
        }
        if (path_join(sp, sizeof(sp), stage_dir, l.v[i].rel, NULL) != 0
            || path_join(pn, sizeof(pn), app_dir, l.v[i].rel, ".new") != 0
            || upd_fs_mkdir_parent(pn) != 0) {
            snprintf(err, err_cap, "cannot prepare %s", l.v[i].rel);
            rc = UPD_E_COPY;
            break;
        }
        upd_sys_remove(pn);
        if (upd_fs_copy_file(sp, pn) != 0 || !hash_matches(pn, l.v[i].hash)) {
            snprintf(err, err_cap, "copy to app failed: %s", l.v[i].rel);
            rc = UPD_E_COPY;
            break;
        }
        if (cb && cb->progress)
            cb->progress((unsigned)i + 1, (unsigned)l.n, user);
    }
    UPD_HOOK("install_copied");
    /* Last chance to back out: the app directory is still untouched. */
    if (rc == UPD_RC_OK && cb && cb->cancel && cb->cancel(user))
        rc = UPD_RC_CANCELLED;
    if (rc != UPD_RC_OK) {
        remove_new_copies(app_dir, &l);
        remove_work_file(work_dir, UPD_WORK_MANIFEST);
        free(l.v);
        return rc;
    }

    /* Pass 2: commit. No cancellation from here on. */
    if (cb && cb->commit)
        cb->commit(user);
    {
        char marker[UPD_PATH_CAP];
        if (path_join(marker, sizeof(marker), work_dir, UPD_WORK_MARKER, NULL) != 0
            || upd_fs_write_text(marker, "renaming\n") != 0) {
            snprintf(err, err_cap, "cannot write install marker");
            remove_new_copies(app_dir, &l);
            remove_work_file(work_dir, UPD_WORK_MANIFEST);
            free(l.v);
            return UPD_E_COPY;
        }
    }
    UPD_HOOK("install_rename_begin");
    for (size_t i = 0; i < l.n; i++) {
        if (roll_forward_one(app_dir, &l.v[i]) != 0) {
            snprintf(err, err_cap, "rename failed: %s", l.v[i].rel);
            free(l.v);
            return UPD_E_RENAME; /* marker + manifest stay: the boot sweep retries */
        }
    }
    UPD_HOOK("install_renamed");

    /* Pass 3: prove the bytes on the card are the staged bytes. This is the check that goes red
     * if writes or renames silently did nothing. */
    for (size_t i = 0; i < l.n; i++) {
        char p[UPD_PATH_CAP];
        if (path_join(p, sizeof(p), app_dir, l.v[i].rel, NULL) != 0
            || !hash_matches(p, l.v[i].hash)) {
            snprintf(err, err_cap, "installed %s does not match the update", l.v[i].rel);
            rc = UPD_E_VERIFY;
            break;
        }
    }
    remove_work_file(work_dir, UPD_WORK_MARKER);
    remove_work_file(work_dir, UPD_WORK_MANIFEST);
    free(l.v);
    return rc;
}

/* ------------------------------------------------------------------ sweep */

int upd_install_sweep(const char *app_dir, const char *work_dir)
{
    char manifest[UPD_PATH_CAP], marker[UPD_PATH_CAP];
    int result = 0;

    if (path_join(manifest, sizeof(manifest), work_dir, UPD_WORK_MANIFEST, NULL) != 0
        || path_join(marker, sizeof(marker), work_dir, UPD_WORK_MARKER, NULL) != 0)
        return -1;

    if (upd_sys_exists(manifest)) {
        FileList l = { NULL, 0, 0, 0 };
        int readable = manifest_read(work_dir, &l) == 0;
        if (upd_sys_exists(marker)) {
            if (!readable) {
                free(l.v);
                return -1; /* cannot know what to finish; keep evidence */
            }
            for (size_t i = 0; i < l.n; i++)
                if (roll_forward_one(app_dir, &l.v[i]) != 0)
                    result = -1;
            if (result == 0) {
                remove_work_file(work_dir, UPD_WORK_MARKER);
                remove_work_file(work_dir, UPD_WORK_MANIFEST);
                result = 1;
            }
        } else {
            /* Interrupted during pass 1: the app itself was never touched. */
            remove_new_copies(app_dir, &l);
            remove_work_file(work_dir, UPD_WORK_MANIFEST);
            result = 1;
        }
        free(l.v);
    } else if (upd_sys_exists(marker)) {
        remove_work_file(work_dir, UPD_WORK_MARKER); /* marker without manifest: nothing to do */
        result = 1;
    }

    remove_work_file(work_dir, UPD_WORK_MANIFEST ".tmp");
    remove_work_file(work_dir, UPD_WORK_STAGE);
    remove_work_file(work_dir, UPD_WORK_PART);
    remove_work_file(work_dir, UPD_WORK_VPK);
    return result;
}
