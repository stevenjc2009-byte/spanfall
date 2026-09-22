#include "upd_zip.h"
#include "upd_fs.h"
#include "upd_sys.h"
#include "upd_ver.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

static uint16_t rd16(const unsigned char *b) { return (uint16_t)(b[0] | (b[1] << 8)); }
static uint32_t rd32(const unsigned char *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16)
        | ((uint32_t)b[3] << 24);
}

int upd_zip_has_magic(const char *zip_path)
{
    unsigned char m[4] = { 0, 0, 0, 0 };
    FILE *f = upd_sys_fopen(zip_path, "rb");
    if (!f)
        return 0;
    size_t n = fread(m, 1, 4, f);
    fclose(f);
    return n == 4 && m[0] == 'P' && m[1] == 'K' && m[2] == 3 && m[3] == 4;
}

static int find_eocd(FILE *f, long *size_out, uint32_t *cd_off, uint32_t *cd_size,
                     uint16_t *count, char *err, size_t cap)
{
    if (fseek(f, 0, SEEK_END) != 0)
        goto bad;
    long size = ftell(f);
    if (size < 22)
        goto bad;
    long scan = size < (65535 + 22) ? size : (65535 + 22);
    unsigned char *tail = malloc((size_t)scan);
    if (!tail) {
        snprintf(err, cap, "out of memory reading archive");
        return -1;
    }
    if (fseek(f, size - scan, SEEK_SET) != 0 || fread(tail, 1, (size_t)scan, f) != (size_t)scan) {
        free(tail);
        goto bad;
    }
    for (long i = scan - 22; i >= 0; i--) {
        if (rd32(tail + i) != 0x06054b50UL)
            continue;
        uint16_t disk = rd16(tail + i + 4), cd_disk = rd16(tail + i + 6);
        uint16_t here = rd16(tail + i + 8);
        *count = rd16(tail + i + 10);
        *cd_size = rd32(tail + i + 12);
        *cd_off = rd32(tail + i + 16);
        free(tail);
        if (*count == 0xFFFF || *cd_off == 0xFFFFFFFFUL || *cd_size == 0xFFFFFFFFUL) {
            snprintf(err, cap, "archive uses ZIP64");
            return -1;
        }
        if (disk != 0 || cd_disk != 0 || here != *count) {
            snprintf(err, cap, "multi-disk archive");
            return -1;
        }
        if ((uint64_t)*cd_off + *cd_size > (uint64_t)size) {
            snprintf(err, cap, "central directory out of range");
            return -1;
        }
        *size_out = size;
        return 0;
    }
    free(tail);
bad:
    snprintf(err, cap, "not a valid zip");
    return -1;
}

static int extract_entry(FILE *f, uint32_t local_off, uint32_t cd_off, uint16_t method,
                         uint32_t csize, uint32_t usize, uint32_t crc_expect,
                         const char *name, const char *out_path, char *err, size_t cap)
{
    unsigned char lh[30];
    if (fseek(f, (long)local_off, SEEK_SET) != 0 || fread(lh, 1, 30, f) != 30
        || rd32(lh) != 0x04034b50UL) {
        snprintf(err, cap, "bad local header: %s", name);
        return -1;
    }
    uint16_t lname = rd16(lh + 26), lextra = rd16(lh + 28);
    uint64_t data_off = (uint64_t)local_off + 30 + lname + lextra;
    if (data_off + csize > cd_off) {
        snprintf(err, cap, "entry data overlaps directory: %s", name);
        return -1;
    }
    if (lname != strlen(name) || rd16(lh + 8) != method) {
        snprintf(err, cap, "local header disagrees: %s", name);
        return -1;
    }
    {
        char lbuf[256];
        if (fread(lbuf, 1, lname, f) != lname || memcmp(lbuf, name, lname) != 0) {
            snprintf(err, cap, "local name disagrees: %s", name);
            return -1;
        }
    }
    if (fseek(f, (long)data_off, SEEK_SET) != 0) {
        snprintf(err, cap, "bad data offset: %s", name);
        return -1;
    }

    FILE *out = upd_sys_fopen(out_path, "wb");
    if (!out) {
        snprintf(err, cap, "cannot create %s", out_path);
        return -1;
    }

    enum { CHUNK = 64 * 1024 };
    unsigned char *inbuf = malloc(CHUNK);
    unsigned char *outbuf = malloc(CHUNK);
    int rc = -1;
    uLong crc = crc32(0L, Z_NULL, 0);
    uint32_t written = 0;
    uint32_t remaining = csize;

    if (!inbuf || !outbuf) {
        snprintf(err, cap, "out of memory extracting");
        goto done;
    }

    if (method == 0) {
        if (csize != usize) {
            snprintf(err, cap, "stored size mismatch: %s", name);
            goto done;
        }
        while (remaining > 0) {
            size_t want = remaining < CHUNK ? remaining : CHUNK;
            if (fread(inbuf, 1, want, f) != want) {
                snprintf(err, cap, "truncated entry: %s", name);
                goto done;
            }
            if (fwrite(inbuf, 1, want, out) != want) {
                snprintf(err, cap, "write failed (card full?): %s", out_path);
                goto done;
            }
            crc = crc32(crc, inbuf, (uInt)want);
            written += (uint32_t)want;
            remaining -= (uint32_t)want;
        }
    } else if (method == 8) {
        z_stream zs;
        memset(&zs, 0, sizeof(zs));
        if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
            snprintf(err, cap, "inflateInit2 failed");
            goto done;
        }
        int zr = Z_OK;
        while (zr != Z_STREAM_END) {
            if (zs.avail_in == 0) {
                if (remaining == 0) {
                    snprintf(err, cap, "truncated deflate stream: %s", name);
                    inflateEnd(&zs);
                    goto done;
                }
                size_t want = remaining < CHUNK ? remaining : CHUNK;
                if (fread(inbuf, 1, want, f) != want) {
                    snprintf(err, cap, "truncated entry: %s", name);
                    inflateEnd(&zs);
                    goto done;
                }
                remaining -= (uint32_t)want;
                zs.next_in = inbuf;
                zs.avail_in = (uInt)want;
            }
            zs.next_out = outbuf;
            zs.avail_out = CHUNK;
            zr = inflate(&zs, Z_NO_FLUSH);
            if (zr != Z_OK && zr != Z_STREAM_END) {
                snprintf(err, cap, "inflate error %d: %s", zr, name);
                inflateEnd(&zs);
                goto done;
            }
            size_t have = CHUNK - zs.avail_out;
            if ((uint64_t)written + have > usize) {
                snprintf(err, cap, "entry larger than declared: %s", name);
                inflateEnd(&zs);
                goto done;
            }
            if (have && fwrite(outbuf, 1, have, out) != have) {
                snprintf(err, cap, "write failed (card full?): %s", out_path);
                inflateEnd(&zs);
                goto done;
            }
            crc = crc32(crc, outbuf, (uInt)have);
            written += (uint32_t)have;
        }
        inflateEnd(&zs);
    } else {
        snprintf(err, cap, "unsupported compression %u: %s", (unsigned)method, name);
        goto done;
    }

    if (written != usize || (uint32_t)crc != crc_expect) {
        snprintf(err, cap, "CRC/size mismatch: %s", name);
        goto done;
    }
    rc = 0;

done:
    free(inbuf);
    free(outbuf);
    if (fclose(out) != 0 && rc == 0) {
        snprintf(err, cap, "write failed on close: %s", out_path);
        rc = -1;
    }
    return rc;
}

int upd_zip_extract(const char *zip_path, const char *dest_dir, upd_cancel_fn cancel,
                    upd_count_fn count_fn, void *user, char *err, size_t err_cap)
{
    upd_fs_remove_tree(dest_dir);
    if (upd_fs_mkdir_p(dest_dir) != 0) {
        snprintf(err, err_cap, "cannot create %s", dest_dir);
        return UPD_RC_FAIL;
    }

    FILE *f = upd_sys_fopen(zip_path, "rb");
    if (!f) {
        snprintf(err, err_cap, "cannot open %s", zip_path);
        upd_fs_remove_tree(dest_dir);
        return UPD_RC_FAIL;
    }

    long size = 0;
    uint32_t cd_off = 0, cd_size = 0;
    uint16_t count = 0;
    int rc = UPD_RC_OK;
    if (find_eocd(f, &size, &cd_off, &cd_size, &count, err, err_cap) != 0) {
        rc = UPD_RC_FAIL;
        goto out;
    }
    if (count == 0 || count > UPD_ZIP_MAX_ENTRIES) {
        snprintf(err, err_cap, "archive has %u entries", (unsigned)count);
        rc = UPD_RC_FAIL;
        goto out;
    }

    uint64_t total = 0;
    uint64_t cursor = cd_off;
    for (uint16_t i = 0; i < count && rc == UPD_RC_OK; i++) {
        if (cancel && cancel(user)) {
            rc = UPD_RC_CANCELLED;
            break;
        }
        unsigned char ch[46];
        if (cursor + 46 > (uint64_t)cd_off + cd_size || fseek(f, (long)cursor, SEEK_SET) != 0
            || fread(ch, 1, 46, f) != 46 || rd32(ch) != 0x02014b50UL) {
            snprintf(err, err_cap, "corrupt central directory");
            rc = UPD_RC_FAIL;
            break;
        }
        uint16_t flags  = rd16(ch + 8);
        uint16_t method = rd16(ch + 10);
        uint32_t crc    = rd32(ch + 16);
        uint32_t csize  = rd32(ch + 20);
        uint32_t usize  = rd32(ch + 24);
        uint16_t nlen   = rd16(ch + 28);
        uint16_t xlen   = rd16(ch + 30);
        uint16_t clen   = rd16(ch + 32);
        uint32_t loff   = rd32(ch + 42);

        char name[256];
        if (nlen == 0 || nlen >= sizeof(name) || fread(name, 1, nlen, f) != nlen) {
            snprintf(err, err_cap, "bad entry name");
            rc = UPD_RC_FAIL;
            break;
        }
        name[nlen] = '\0';
        if (memchr(name, '\0', nlen)) {
            snprintf(err, err_cap, "entry name contains NUL");
            rc = UPD_RC_FAIL;
            break;
        }
        cursor += 46 + (uint64_t)nlen + xlen + clen;

        if (!upd_zip_path_ok(name)) {
            snprintf(err, err_cap, "unsafe path in archive: %.200s", name);
            rc = UPD_RC_FAIL;
            break;
        }
        if (flags & 1) {
            snprintf(err, err_cap, "encrypted entry: %s", name);
            rc = UPD_RC_FAIL;
            break;
        }
        if (csize == 0xFFFFFFFFUL || usize == 0xFFFFFFFFUL || loff == 0xFFFFFFFFUL) {
            snprintf(err, err_cap, "ZIP64 entry: %s", name);
            rc = UPD_RC_FAIL;
            break;
        }
        total += usize;
        if (total > UPD_ZIP_MAX_TOTAL_BYTES) {
            snprintf(err, err_cap, "archive expands past %lu bytes",
                     (unsigned long)UPD_ZIP_MAX_TOTAL_BYTES);
            rc = UPD_RC_FAIL;
            break;
        }

        char out_path[UPD_PATH_CAP];
        if ((size_t)snprintf(out_path, sizeof(out_path), "%s/%s", dest_dir, name)
            >= sizeof(out_path)) {
            snprintf(err, err_cap, "path too long: %s", name);
            rc = UPD_RC_FAIL;
            break;
        }

        if (name[nlen - 1] == '/') {
            out_path[strlen(out_path) - 1] = '\0';
            if (upd_fs_mkdir_p(out_path) != 0) {
                snprintf(err, err_cap, "cannot create %s", out_path);
                rc = UPD_RC_FAIL;
            }
        } else {
            if (upd_fs_mkdir_parent(out_path) != 0) {
                snprintf(err, err_cap, "cannot create parent of %s", out_path);
                rc = UPD_RC_FAIL;
            } else if (extract_entry(f, loff, cd_off, method, csize, usize, crc, name, out_path,
                                     err, err_cap) != 0) {
                rc = UPD_RC_FAIL;
            }
        }
        if (count_fn)
            count_fn((unsigned)i + 1, count, user);
    }

out:
    fclose(f);
    if (rc != UPD_RC_OK)
        upd_fs_remove_tree(dest_dir);
    return rc;
}
