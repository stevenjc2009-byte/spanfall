#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>
#include <vita2d.h> /* RGBA8 */

#include "game.h"

#define SCREEN_W 960
#define SCREEN_H 544

#define COL_WHITE RGBA8(255, 255, 255, 255)
#define COL_TEXT  RGBA8(226, 220, 204, 255)
#define COL_HINT  RGBA8(150, 144, 132, 255)
#define COL_GOLD  RGBA8(244, 200, 90, 255)
#define COL_ERROR RGBA8(240, 110, 100, 255)

int  render_init(void);   /* 0 ok */
void render_shutdown(void);
void render_begin(void);  /* start a frame on a cleared screen */
void render_end(void);    /* finish the frame and show it */

/* The well with its sand, the falling piece (if show_piece) and the HUD. */
void render_game(const Game *g, uint32_t best, int show_piece);

void render_dim(int alpha);
void render_panel(int x, int y, int w, int h);
void render_bar(int x, int y, int w, int h, float fill, uint32_t colour);
void render_text(int x, int y, float scale, uint32_t colour, const char *s);
void render_text_centered(int y, float scale, uint32_t colour, const char *s);

#endif
