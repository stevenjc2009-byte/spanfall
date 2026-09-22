#ifndef UPD_FS_H
#define UPD_FS_H

/* Portable file helpers built on upd_sys.h. */

#include <stddef.h>
#include <stdint.h>

#define UPD_PATH_CAP 512
#define UPD_SHA256_LEN 32

int  upd_fs_mkdir_p(const char *path);            /* 0 if the directory exists afterwards */
int  upd_fs_mkdir_parent(const char *file_path);  /* mkdir_p of everything before the last '/' */
void upd_fs_remove_tree(const char *path);        /* file or tree; missing is fine */
int  upd_fs_copy_file(const char *from, const char *to);  /* 0 only if every byte + close ok */
int  upd_fs_sha256(const char *path, unsigned char out[UPD_SHA256_LEN]);
int  upd_fs_write_text(const char *path, const char *text); /* create/truncate; 0 on success */

void upd_fs_hex(const unsigned char *bin, size_t n, char *out); /* out gets 2n+1 bytes */

#endif
