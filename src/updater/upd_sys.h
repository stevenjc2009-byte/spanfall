#ifndef UPD_SYS_H
#define UPD_SYS_H

/* Platform layer for the updater. Two implementations, each self-guarded so a glob over src can
 * pick both files up safely:
 *   upd_sys_vita.c  (#ifdef __vita__)   - sceIo*, sceKernel threads, SceNetCtl, SceAppMgr
 *   upd_sys_host.c  (#ifndef __vita__)  - POSIX + pthreads, used by the host tests/live check.
 * On the host, "ux0:" and "app0:" path prefixes are mapped under $UPD_HOST_ROOT so the unchanged
 * game config headers can be exercised end to end. */

#include <stdint.h>
#include <stdio.h>

/* ------------------------------------------------------------------ files */

FILE *upd_sys_fopen(const char *path, const char *mode);
int   upd_sys_mkdir(const char *path);        /* 0 if created or already a directory */
int   upd_sys_is_dir(const char *path);       /* 1 / 0 */
int   upd_sys_exists(const char *path);       /* 1 / 0 (file or directory) */
int   upd_sys_remove(const char *path);       /* file; 0 on success */
int   upd_sys_rmdir(const char *path);        /* empty directory; 0 on success */
int   upd_sys_rename(const char *from, const char *to); /* target must not exist; 0 on success */

/* Calls fn for every entry except "." and "..". Names are collected and the directory handle is
 * closed before the first callback, so fn may delete entries. Returns 0, or -1 if the
 * directory cannot be opened (or out of memory). A nonzero fn return stops the walk and is
 * returned. */
typedef int (*upd_dir_fn)(const char *name, int is_dir, void *user);
int upd_sys_list_dir(const char *path, upd_dir_fn fn, void *user);

/* Free bytes on the device holding `path` ("ux0:"). 0 on success. */
int upd_sys_free_space(const char *path, uint64_t *free_bytes);

/* ------------------------------------------------------------------ system */

/* 1 connected, 0 not connected, <0 query failed. Requires the network layer to be up. */
int upd_sys_net_connected(void);

/* Replace the running process with app0:eboot.bin. Only returns on failure (<0). */
int upd_sys_relaunch(void);

/* ------------------------------------------------------------------ threads */

/* One global lock for the updater's shared state. */
int  upd_sys_lock_create(void);
void upd_sys_lock(void);
void upd_sys_unlock(void);
void upd_sys_lock_destroy(void);

/* One worker at a time. start: 0 on success, <0 on failure. join: 0 joined (thread reaped),
 * 1 still running after timeout_ms, -1 no thread. */
int  upd_sys_thread_start(int (*fn)(void *), void *arg);
int  upd_sys_thread_join(unsigned timeout_ms);
void upd_sys_sleep_ms(unsigned ms);

/* ------------------------------------------------------------------ test hooks */

#ifdef UPD_TESTING
/* Called at named points in the worker; tests use it to pause, cancel, or tamper. */
extern void (*upd_test_hook)(const char *point);
#define UPD_HOOK(p) do { if (upd_test_hook) upd_test_hook(p); } while (0)
#else
#define UPD_HOOK(p) ((void)0)
#endif

#endif
