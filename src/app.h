#ifndef SF_APP_H
#define SF_APP_H

/* The screens (title, play, pause, game over, updates) and their flow. */

void app_init(void);
void app_frame(void); /* read input, advance one frame, draw it */
int app_quit_requested(void);

#endif
