#ifdef __vita__

#include "upd_sys.h"

#include <stdlib.h>
#include <string.h>

#include <psp2/types.h>
#include <psp2/appmgr.h>
#include <psp2/io/devctl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/netctl.h>

#define UPD_THREAD_STACK (512 * 1024)
#define UPD_THREAD_PRIO  0x10000100

FILE *upd_sys_fopen(const char *path, const char *mode) { return fopen(path, mode); }

int upd_sys_is_dir(const char *path)
{
    SceIoStat st;
    return sceIoGetstat(path, &st) >= 0 && SCE_S_ISDIR(st.st_mode);
}

int upd_sys_exists(const char *path)
{
    SceIoStat st;
    return sceIoGetstat(path, &st) >= 0;
}

int upd_sys_mkdir(const char *path)
{
    if (upd_sys_is_dir(path))
        return 0;
    sceIoMkdir(path, 0777);
    return upd_sys_is_dir(path) ? 0 : -1;
}

int upd_sys_remove(const char *path) { return sceIoRemove(path) < 0 ? -1 : 0; }
int upd_sys_rmdir(const char *path) { return sceIoRmdir(path) < 0 ? -1 : 0; }
int upd_sys_rename(const char *from, const char *to) { return sceIoRename(from, to) < 0 ? -1 : 0; }

typedef struct {
    char name[256];
    int is_dir;
} DirEntry;

int upd_sys_list_dir(const char *path, upd_dir_fn fn, void *user)
{
    SceUID d = sceIoDopen(path);
    if (d < 0)
        return -1;

    DirEntry *list = NULL;
    size_t n = 0, cap = 0;
    SceIoDirent ent;
    for (;;) {
        memset(&ent, 0, sizeof(ent));
        if (sceIoDread(d, &ent) <= 0)
            break;
        if (strcmp(ent.d_name, ".") == 0 || strcmp(ent.d_name, "..") == 0)
            continue;
        if (n == cap) {
            size_t ncap = cap ? cap * 2 : 16;
            DirEntry *nl = realloc(list, ncap * sizeof(*nl));
            if (!nl) {
                free(list);
                sceIoDclose(d);
                return -1;
            }
            list = nl;
            cap = ncap;
        }
        snprintf(list[n].name, sizeof(list[n].name), "%s", ent.d_name);
        list[n].is_dir = SCE_S_ISDIR(ent.d_stat.st_mode) ? 1 : 0;
        n++;
    }
    sceIoDclose(d);

    int rc = 0;
    for (size_t i = 0; i < n && rc == 0; i++)
        rc = fn(list[i].name, list[i].is_dir, user);
    free(list);
    return rc;
}

int upd_sys_free_space(const char *path, uint64_t *free_bytes)
{
    char dev[8];
    const char *colon = strchr(path, ':');
    size_t len = colon ? (size_t)(colon - path) + 1 : 0;
    if (len == 0 || len >= sizeof(dev))
        return -1;
    memcpy(dev, path, len);
    dev[len] = '\0';

    SceIoDevInfo info;
    memset(&info, 0, sizeof(info));
    if (sceIoDevctl(dev, 0x3001, NULL, 0, &info, sizeof(info)) < 0)
        return -1;
    *free_bytes = (uint64_t)info.free_size;
    return 0;
}

int upd_sys_net_connected(void)
{
    int state = 0;
    if (sceNetCtlInetGetState(&state) < 0)
        return -1;
    return state == SCE_NETCTL_STATE_CONNECTED ? 1 : 0;
}

int upd_sys_relaunch(void)
{
    int rc = sceAppMgrLoadExec("app0:eboot.bin", NULL, NULL);
    return rc < 0 ? rc : -1;
}

/* ------------------------------------------------------------------ threads */

static SceKernelLwMutexWork s_lock;
static SceUID s_thread = -1;

int upd_sys_lock_create(void)
{
    return sceKernelCreateLwMutex(&s_lock, "upd_lock", 0, 0, NULL) < 0 ? -1 : 0;
}
void upd_sys_lock(void) { sceKernelLockLwMutex(&s_lock, 1, NULL); }
void upd_sys_unlock(void) { sceKernelUnlockLwMutex(&s_lock, 1); }
void upd_sys_lock_destroy(void) { sceKernelDeleteLwMutex(&s_lock); }

typedef struct {
    int (*fn)(void *);
    void *arg;
} ThreadStart;

static int thread_entry(SceSize args, void *argp)
{
    ThreadStart ts;
    if (args != sizeof(ts) || !argp)
        return sceKernelExitThread(0);
    memcpy(&ts, argp, sizeof(ts));
    int rc = ts.fn(ts.arg);
    return sceKernelExitThread(rc);
}

int upd_sys_thread_start(int (*fn)(void *), void *arg)
{
    if (s_thread >= 0)
        return -1;
    SceUID th = sceKernelCreateThread("upd_worker", thread_entry, UPD_THREAD_PRIO,
                                      UPD_THREAD_STACK, 0, 0, NULL);
    if (th < 0)
        return th;
    ThreadStart ts = { fn, arg };
    int rc = sceKernelStartThread(th, sizeof(ts), &ts); /* the kernel copies ts */
    if (rc < 0) {
        sceKernelDeleteThread(th);
        return rc;
    }
    s_thread = th;
    return 0;
}

int upd_sys_thread_join(unsigned timeout_ms)
{
    if (s_thread < 0)
        return -1;
    SceUInt timeout = (SceUInt)timeout_ms * 1000U;
    if (sceKernelWaitThreadEnd(s_thread, NULL, &timeout) < 0)
        return 1;
    sceKernelDeleteThread(s_thread);
    s_thread = -1;
    return 0;
}

void upd_sys_sleep_ms(unsigned ms) { sceKernelDelayThread((SceUInt)ms * 1000U); }

#endif /* __vita__ */
