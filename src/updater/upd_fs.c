#include "upd_fs.h"
#include "upd_sys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>

int upd_fs_mkdir_p(const char *path)
{
    char buf[UPD_PATH_CAP];
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof(buf))
        return -1;
    memcpy(buf, path, n + 1);
    while (n > 1 && buf[n - 1] == '/')
        buf[--n] = '\0';

    /* Skip the device prefix ("ux0:") so it is never passed to mkdir. */
    char *p = strchr(buf, ':');
    p = p ? p + 1 : buf;
    while (*p == '/')
        p++;
    for (; *p; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        upd_sys_mkdir(buf);
        *p = '/';
    }
    return upd_sys_mkdir(buf);
}

int upd_fs_mkdir_parent(const char *file_path)
{
    char buf[UPD_PATH_CAP];
    size_t n = strlen(file_path);
    if (n == 0 || n >= sizeof(buf))
        return -1;
    memcpy(buf, file_path, n + 1);
    char *slash = strrchr(buf, '/');
    if (!slash)
        return 0;
    *slash = '\0';
    return upd_fs_mkdir_p(buf);
}

static int remove_child(const char *name, int is_dir, void *user)
{
    char child[UPD_PATH_CAP];
    const char *parent = (const char *)user;
    if ((size_t)snprintf(child, sizeof(child), "%s/%s", parent, name) >= sizeof(child))
        return 0; /* cannot name it; skip rather than delete the wrong thing */
    if (is_dir)
        upd_fs_remove_tree(child);
    else
        upd_sys_remove(child);
    return 0;
}

void upd_fs_remove_tree(const char *path)
{
    if (!upd_sys_exists(path))
        return;
    if (!upd_sys_is_dir(path)) {
        upd_sys_remove(path);
        return;
    }
    upd_sys_list_dir(path, remove_child, (void *)path);
    upd_sys_rmdir(path);
}

int upd_fs_copy_file(const char *from, const char *to)
{
    FILE *in = upd_sys_fopen(from, "rb");
    if (!in)
        return -1;
    FILE *out = upd_sys_fopen(to, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }
    enum { CHUNK = 64 * 1024 };
    unsigned char *buf = malloc(CHUNK);
    int rc = buf ? 0 : -1;
    while (rc == 0) {
        size_t n = fread(buf, 1, CHUNK, in);
        if (n > 0 && fwrite(buf, 1, n, out) != n)
            rc = -1;
        if (n < CHUNK) {
            if (ferror(in))
                rc = -1;
            break;
        }
    }
    free(buf);
    fclose(in);
    if (fclose(out) != 0)
        rc = -1;
    return rc;
}

int upd_fs_sha256(const char *path, unsigned char out[UPD_SHA256_LEN])
{
    FILE *f = upd_sys_fopen(path, "rb");
    if (!f)
        return -1;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    unsigned char *buf = malloc(64 * 1024);
    int rc = (ctx && buf && EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) == 1) ? 0 : -1;
    while (rc == 0) {
        size_t n = fread(buf, 1, 64 * 1024, f);
        if (n > 0 && EVP_DigestUpdate(ctx, buf, n) != 1)
            rc = -1;
        if (n < 64 * 1024) {
            if (ferror(f))
                rc = -1;
            break;
        }
    }
    unsigned int len = 0;
    if (rc == 0 && (EVP_DigestFinal_ex(ctx, out, &len) != 1 || len != UPD_SHA256_LEN))
        rc = -1;
    free(buf);
    if (ctx)
        EVP_MD_CTX_free(ctx);
    fclose(f);
    return rc;
}

int upd_fs_write_text(const char *path, const char *text)
{
    FILE *f = upd_sys_fopen(path, "wb");
    if (!f)
        return -1;
    size_t n = strlen(text);
    int rc = fwrite(text, 1, n, f) == n ? 0 : -1;
    if (fclose(f) != 0)
        rc = -1;
    return rc;
}

void upd_fs_hex(const unsigned char *bin, size_t n, char *out)
{
    static const char hx[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = hx[bin[i] >> 4];
        out[2 * i + 1] = hx[bin[i] & 15];
    }
    out[2 * n] = '\0';
}
