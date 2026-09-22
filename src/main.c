#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>

#include "app.h"
#include "input.h"
#include "render.h"
#include "updater/upd_updater.h"

int main(void)
{
    /* Before anything draws: finish or undo an install that a crash or power loss interrupted,
     * and delete download leftovers. Pure file work, no network, and a failure must not stop
     * the game booting, so the result is not checked. */
    (void)upd_boot_sweep();

    if (render_init() != 0) {
        sceKernelExitProcess(0);
        return 0;
    }
    input_init();
    app_init();

    while (!app_quit_requested()) {
        /* A download has no button presses: keep the system from auto-suspending under it. */
        if (upd_busy())
            sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
        app_frame();
    }

    /* Cancel and join the updater worker (bounded): quit must not hang on a download. */
    (void)upd_shutdown();
    render_shutdown();
    sceKernelExitProcess(0);
    return 0;
}
