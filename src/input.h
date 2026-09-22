#ifndef SF_INPUT_H
#define SF_INPUT_H

/* Pad input as SCE_CTRL_* bits, read once per frame. Adapted from Foldwind's
 * input.c: the confirm button follows the system's enter-button setting. */

void input_init(void);
void input_poll(void);
unsigned input_held(void);
unsigned input_pressed(void); /* went down this frame */

unsigned input_confirm_button(void); /* SCE_CTRL_CROSS or SCE_CTRL_CIRCLE */
unsigned input_back_button(void);
const char *input_confirm_name(void); /* "X" or "O" */
const char *input_back_name(void);

#endif
