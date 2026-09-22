#include <string.h>

#include <psp2/apputil.h>
#include <psp2/ctrl.h>
#include <psp2/system_param.h>

#include "input.h"

static unsigned held_prev, held_now, pressed_now;
static int circle_confirms;

/* Returns 1 and the held buttons, or 0 if the pad could not be read. */
static int read_held(unsigned *held)
{
    SceCtrlData pad;
    if (sceCtrlPeekBufferPositive(0, &pad, 1) <= 0)
        return 0;
    *held = pad.buttons;
    return 1;
}

void input_init(void)
{
    SceAppUtilInitParam init;
    SceAppUtilBootParam boot;
    int enter;

    memset(&init, 0, sizeof(init));
    memset(&boot, 0, sizeof(boot));
    /* On failure the default (Cross confirms) stays. */
    if (sceAppUtilInit(&init, &boot) >= 0 &&
        sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON, &enter) >= 0)
        circle_confirms = (enter == SCE_SYSTEM_PARAM_ENTER_BUTTON_CIRCLE);

    /* A button already down at boot is not a press. */
    if (read_held(&held_prev))
        held_now = held_prev;
}

void input_poll(void)
{
    unsigned held;
    if (read_held(&held)) {
        pressed_now = held & ~held_prev;
        held_prev = held_now = held;
    } else {
        pressed_now = 0; /* keep held_prev: a held button must not re-edge after a bad read */
    }
}

unsigned input_held(void)
{
    return held_now;
}

unsigned input_pressed(void)
{
    return pressed_now;
}

unsigned input_confirm_button(void)
{
    return circle_confirms ? SCE_CTRL_CIRCLE : SCE_CTRL_CROSS;
}

unsigned input_back_button(void)
{
    return circle_confirms ? SCE_CTRL_CROSS : SCE_CTRL_CIRCLE;
}

const char *input_confirm_name(void)
{
    return circle_confirms ? "O" : "X";
}

const char *input_back_name(void)
{
    return circle_confirms ? "X" : "O";
}
