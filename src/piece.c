#include "piece.h"

/* Spawn orientation of each tetromino inside its rotation box. */
static const int BASE_X[PIECE_TYPES][4] = {
    {0, 1, 2, 3}, /* I */
    {0, 1, 0, 1}, /* O */
    {1, 0, 1, 2}, /* T */
    {1, 2, 0, 1}, /* S */
    {0, 1, 1, 2}, /* Z */
    {0, 0, 1, 2}, /* J */
    {2, 0, 1, 2}, /* L */
};
static const int BASE_Y[PIECE_TYPES][4] = {
    {1, 1, 1, 1}, /* I */
    {0, 0, 1, 1}, /* O */
    {0, 1, 1, 1}, /* T */
    {0, 0, 1, 1}, /* S */
    {0, 0, 1, 1}, /* Z */
    {0, 1, 1, 1}, /* J */
    {0, 1, 1, 1}, /* L */
};

int piece_box(int type)
{
    if (type == 0)
        return 4;
    if (type == 1)
        return 2;
    return 3;
}

void piece_blocks(const Piece *p, int bx[4], int by[4])
{
    int n = piece_box(p->type);
    for (int i = 0; i < 4; i++) {
        int x = BASE_X[p->type][i], y = BASE_Y[p->type][i];
        /* Clockwise quarter turn inside an n x n box: (x, y) -> (n-1-y, x). */
        for (int r = 0; r < (p->rot & 3); r++) {
            int t = x;
            x = n - 1 - y;
            y = t;
        }
        bx[i] = x;
        by[i] = y;
    }
}

int piece_fits(const Piece *p, const Sand *s)
{
    int bx[4], by[4];
    piece_blocks(p, bx, by);
    for (int i = 0; i < 4; i++) {
        int x0 = p->x + bx[i] * BLOCK, y0 = p->y + by[i] * BLOCK;
        if (x0 < 0 || x0 + BLOCK > GRID_W || y0 + BLOCK > GRID_H)
            return 0;
        for (int gy = 0; gy < BLOCK; gy++) {
            int y = y0 + gy;
            if (y < 0)
                continue;
            for (int gx = 0; gx < BLOCK; gx++)
                if (s->color[y][x0 + gx] != COLOR_NONE)
                    return 0;
        }
    }
    return 1;
}

uint8_t piece_grain_shade(int gx, int gy)
{
    uint32_t h = (uint32_t)gx * 73856093u ^ (uint32_t)gy * 19349663u;
    h ^= h >> 13;
    h *= 2654435761u;
    return (uint8_t)(h >> 30);
}

void piece_stamp(const Piece *p, Sand *s)
{
    int bx[4], by[4];
    piece_blocks(p, bx, by);
    for (int i = 0; i < 4; i++) {
        for (int gy = 0; gy < BLOCK; gy++) {
            for (int gx = 0; gx < BLOCK; gx++) {
                int ox = bx[i] * BLOCK + gx, oy = by[i] * BLOCK + gy;
                sand_set(s, p->x + ox, p->y + oy, (uint8_t)p->color,
                         piece_grain_shade(ox, oy));
            }
        }
    }
}
