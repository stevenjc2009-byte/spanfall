#ifndef PIECE_H
#define PIECE_H

#include "sand.h"

#define PIECE_TYPES 7 /* I O T S Z J L */

typedef struct {
    int type;  /* 0..PIECE_TYPES-1 */
    int rot;   /* 0..3, clockwise quarter turns */
    int color; /* 1..COLOR_COUNT */
    int x, y;  /* top-left of the piece's bounding box, in grains */
} Piece;

/* Side of the piece's rotation box in blocks: 4 for I, 2 for O, else 3. */
int piece_box(int type);

/* Block coordinates (within the rotation box) of the piece's four blocks. */
void piece_blocks(const Piece *p, int bx[4], int by[4]);

/* 1 if every grain of the piece is inside the well walls, above the floor
 * and on an empty cell. Grains above the top edge count as free. */
int piece_fits(const Piece *p, const Sand *s);

/* Writes the piece into the well as loose grains. */
void piece_stamp(const Piece *p, Sand *s);

/* Stable per-grain shade so a piece keeps its texture when it turns to sand.
 * gx, gy are grain offsets inside the piece's box. */
uint8_t piece_grain_shade(int gx, int gy);

#endif
