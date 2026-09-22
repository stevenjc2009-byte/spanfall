#ifndef UPD_ZIP_H
#define UPD_ZIP_H

#include <stddef.h>

#define UPD_ZIP_MAX_ENTRIES     1024
#define UPD_ZIP_MAX_TOTAL_BYTES (64UL * 1024UL * 1024UL) /* sum of uncompressed sizes */

#define UPD_RC_OK        0
#define UPD_RC_FAIL     -1
#define UPD_RC_CANCELLED -2

typedef int  (*upd_cancel_fn)(void *user);                         /* nonzero = stop */
typedef void (*upd_count_fn)(unsigned done, unsigned total, void *user);

/* Wipe dest_dir and extract the zip at zip_path into it. Stored and deflate entries only.
 * Rejects: unsafe names (upd_zip_path_ok), encrypted entries, ZIP64, data descriptors that
 * disagree with the central directory, entry data overlapping the central directory, sizes over
 * the caps, CRC or size mismatch. Returns UPD_RC_OK / UPD_RC_FAIL (err filled) /
 * UPD_RC_CANCELLED. On any non-OK return dest_dir is removed. */
int upd_zip_extract(const char *zip_path, const char *dest_dir, upd_cancel_fn cancel,
                    upd_count_fn count, void *user, char *err, size_t err_cap);

/* 1 if the file starts with "PK\3\4". */
int upd_zip_has_magic(const char *zip_path);

#endif
