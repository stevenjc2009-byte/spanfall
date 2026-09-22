#include "../src/piece.h"
#include "check.h"

static Sand s;

static void blocks_are_distinct_and_in_box(void)
{
    for (int t = 0; t < PIECE_TYPES; t++) {
        for (int r = 0; r < 4; r++) {
            Piece p = {t, r, 1, 0, 0};
            int bx[4], by[4], n = piece_box(t);
            piece_blocks(&p, bx, by);
            for (int i = 0; i < 4; i++) {
                CHECK(bx[i] >= 0 && bx[i] < n && by[i] >= 0 && by[i] < n);
                for (int j = i + 1; j < 4; j++)
                    CHECK(bx[i] != bx[j] || by[i] != by[j]);
            }
        }
    }
}

/* 1 if the two orientations cover the same set of blocks. */
static int same_shape(int type, int r1, int r2)
{
    Piece a = {type, r1, 1, 0, 0}, b = {type, r2, 1, 0, 0};
    int ax[4], ay[4], bx[4], by[4];
    piece_blocks(&a, ax, ay);
    piece_blocks(&b, bx, by);
    for (int i = 0; i < 4; i++) {
        int hit = 0;
        for (int j = 0; j < 4; j++)
            hit |= ax[i] == bx[j] && ay[i] == by[j];
        if (!hit)
            return 0;
    }
    return 1;
}

static void turns_change_every_piece_but_o(void)
{
    for (int t = 0; t < PIECE_TYPES; t++) {
        if (t == 1) {
            for (int r = 1; r < 4; r++)
                CHECK(same_shape(t, 0, r)); /* O never wobbles */
            continue;
        }
        CHECK(!same_shape(t, 0, 1));
        CHECK(!same_shape(t, 1, 2));
    }
}

static void t_piece_turns_clockwise(void)
{
    /* T spawns pointing up; one clockwise turn points it right: the stem
     * block moves to (2,1). */
    Piece p = {2, 1, 1, 0, 0};
    int bx[4], by[4], found = 0;
    piece_blocks(&p, bx, by);
    for (int i = 0; i < 4; i++)
        found += (bx[i] == 2 && by[i] == 1);
    CHECK_EQ(found, 1);
}

static void fits_respects_walls_and_floor(void)
{
    sand_init(&s, 1);
    Piece p = {1, 0, 1, 0, 0}; /* O: 2x2 blocks */
    CHECK(piece_fits(&p, &s));
    p.x = GRID_W - 2 * BLOCK;
    CHECK(piece_fits(&p, &s));
    p.x = GRID_W - 2 * BLOCK + 1;
    CHECK(!piece_fits(&p, &s));
    p.x = -1;
    CHECK(!piece_fits(&p, &s));
    p.x = 0;
    p.y = GRID_H - 2 * BLOCK;
    CHECK(piece_fits(&p, &s));
    p.y++;
    CHECK(!piece_fits(&p, &s));
    p.y = -BLOCK; /* partly above the top edge is allowed */
    CHECK(piece_fits(&p, &s));
    sand_set(&s, 3, GRID_H - 1, 2, 0);
    p.y = GRID_H - 2 * BLOCK;
    CHECK(!piece_fits(&p, &s));
}

static void stamp_writes_every_grain(void)
{
    sand_init(&s, 1);
    Piece p = {0, 0, 3, 8, 40}; /* I */
    piece_stamp(&p, &s);
    int n = 0;
    for (int y = 0; y < GRID_H; y++)
        for (int x = 0; x < GRID_W; x++)
            n += s.color[y][x] == 3;
    CHECK_EQ(n, 4 * BLOCK * BLOCK);
}

void test_piece(void)
{
    blocks_are_distinct_and_in_box();
    turns_change_every_piece_but_o();
    t_piece_turns_clockwise();
    fits_respects_walls_and_floor();
    stamp_writes_every_grain();
}
