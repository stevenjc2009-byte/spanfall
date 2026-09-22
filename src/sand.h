#ifndef SAND_H
#define SAND_H

#include <stdint.h>

/* The well, measured in sand grains. One tetromino block is BLOCK x BLOCK
 * grains; the well is WELL_COLS x WELL_ROWS blocks. */
#define BLOCK       8
#define WELL_COLS   10
#define WELL_ROWS   20
#define GRID_W      (WELL_COLS * BLOCK)
#define GRID_H      (WELL_ROWS * BLOCK)
#define GRID_CELLS  (GRID_W * GRID_H)

#define COLOR_NONE  0
#define COLOR_COUNT 4 /* grain colours are 1..COLOR_COUNT */

typedef struct {
    uint8_t color[GRID_H][GRID_W];    /* COLOR_NONE = empty */
    uint8_t shade[GRID_H][GRID_W];    /* per-grain brightness, 0..3 */
    uint8_t marked[GRID_H][GRID_W];   /* 1 = in a region being cleared */
    uint32_t rng;
} Sand;

void     sand_init(Sand *s, uint32_t seed);
uint32_t sand_rand(Sand *s);

/* Put one grain at (x, y). Out-of-range positions are ignored. */
void sand_set(Sand *s, int x, int y, uint8_t color, uint8_t shade);

/* 1 if (x, y) is outside the well or holds a grain. */
int sand_blocked(const Sand *s, int x, int y);

/* One gravity pass: every grain moves at most one cell, straight down if it
 * can, otherwise diagonally down. Returns the number of grains that moved. */
int sand_step(Sand *s);

/* Finds every same-colour region (8-connected) touching both side walls and
 * marks it in s->marked. Returns the number of grains marked. */
int sand_mark_spans(Sand *s);

/* Removes every marked grain. Returns how many were removed. */
int sand_remove_marked(Sand *s);

/* Highest occupied row (0 = top), or GRID_H if the well is empty. */
int sand_top_row(const Sand *s);

#endif
