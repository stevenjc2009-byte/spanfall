#ifndef GAME_H
#define GAME_H

#include <stdint.h>

#include "piece.h"
#include "sand.h"

/* Input bits handed to game_update(). "held" is the current state,
 * "pressed" is only the buttons that went down this frame. These are
 * Sandtrix's controls: move, rotate, soft drop. */
#define IN_LEFT   (1u << 0)
#define IN_RIGHT  (1u << 1)
#define IN_DOWN   (1u << 2) /* soft drop while held */
#define IN_ROTATE (1u << 3) /* clockwise quarter turn */

#define CLEAR_FRAMES 30   /* how long a clearing region flashes */
#define SAND_STEPS   2    /* gravity passes per frame */
#define DAS_DELAY    10   /* frames before a held direction repeats */
#define LEVEL_FRAMES 2400 /* the fall speed rises every 40 s of play */
#define MAX_LEVEL    15
#define COMBO_FRAMES 300  /* clear again within 5 s to raise the combo */
#define COMBO_MAX    10

typedef struct {
    Sand sand;
    Piece cur;
    Piece next;
    int bag[PIECE_TYPES];
    int bag_left;

    uint32_t score;   /* 1 point per cleared grain, times the combo */
    uint32_t grains_cleared;
    int clears;       /* clear events so far */
    int level;        /* 1..MAX_LEVEL, rises with play time */
    int combo;        /* multiplier of the most recent clear, 1..COMBO_MAX */
    int combo_timer;  /* frames left to clear again and raise the combo */
    int last_gain;    /* points from the most recent clear, for the HUD */

    int fall_accum;   /* piece gravity, 1/256 grain units */
    int das_frames;   /* frames the current direction has been held */
    int clear_timer;  /* >0 while a region flashes before it is removed */
    int sand_dirty;   /* sand changed since the last span check */
    int over;
    uint32_t frame;
} Game;

void game_init(Game *g, uint32_t seed);
void game_update(Game *g, unsigned held, unsigned pressed);

/* Piece gravity for a level, in 1/256 grain per frame. */
int game_fall_speed(int level);

#endif
