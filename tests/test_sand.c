#include "../src/sand.h"
#include "check.h"

static Sand s;

static int count_grains(const Sand *sd)
{
    int n = 0;
    for (int y = 0; y < GRID_H; y++)
        for (int x = 0; x < GRID_W; x++)
            n += sd->color[y][x] != COLOR_NONE;
    return n;
}

static void settle(Sand *sd)
{
    for (int i = 0; i < GRID_H * 4 && sand_step(sd) > 0; i++) {
    }
}

static void falls_to_floor(void)
{
    sand_init(&s, 1);
    sand_set(&s, 20, 0, 1, 0);
    settle(&s);
    CHECK_EQ(s.color[GRID_H - 1][20], 1);
    CHECK_EQ(count_grains(&s), 1);
    CHECK_EQ(sand_step(&s), 0); /* settled grains stay put */
}

static void slides_off_a_grain(void)
{
    sand_init(&s, 7);
    sand_set(&s, 10, GRID_H - 1, 1, 0);
    sand_set(&s, 10, GRID_H - 2, 2, 0);
    CHECK_EQ(sand_step(&s), 1);
    CHECK_EQ(s.color[GRID_H - 2][10], COLOR_NONE);
    CHECK(s.color[GRID_H - 1][9] == 2 || s.color[GRID_H - 1][11] == 2);
}

static void walls_hold_grains(void)
{
    sand_init(&s, 3);
    /* Floor grain under x=0 and x=1: the grain above x=0 cannot go left
     * (wall) or right-down (occupied), so it stays. */
    sand_set(&s, 0, GRID_H - 1, 1, 0);
    sand_set(&s, 1, GRID_H - 1, 1, 0);
    sand_set(&s, 0, GRID_H - 2, 2, 0);
    CHECK_EQ(sand_step(&s), 0);
    CHECK_EQ(s.color[GRID_H - 2][0], 2);
}

static void conserves_grains(void)
{
    sand_init(&s, 12345);
    for (int i = 0; i < 3000; i++) {
        uint32_t r = sand_rand(&s);
        sand_set(&s, (int)(r % GRID_W), (int)((r >> 8) % GRID_H),
                 (uint8_t)(1 + (r >> 20) % COLOR_COUNT), 0);
    }
    int before = count_grains(&s);
    for (int i = 0; i < 500; i++)
        sand_step(&s);
    CHECK_EQ(count_grains(&s), before);
    settle(&s);
    CHECK_EQ(sand_step(&s), 0);
}

static void full_row_spans(void)
{
    sand_init(&s, 1);
    for (int x = 0; x < GRID_W; x++)
        sand_set(&s, x, GRID_H - 1, 3, 0);
    CHECK_EQ(sand_mark_spans(&s), GRID_W);
    CHECK_EQ(sand_remove_marked(&s), GRID_W);
    CHECK_EQ(count_grains(&s), 0);
}

static void broken_row_does_not_span(void)
{
    sand_init(&s, 1);
    for (int x = 0; x < GRID_W; x++)
        sand_set(&s, x, GRID_H - 1, x == GRID_W - 1 ? 2 : 1, 0);
    CHECK_EQ(sand_mark_spans(&s), 0);
    CHECK_EQ(count_grains(&s), GRID_W);
}

static void diagonal_steps_connect(void)
{
    /* A zig-zag touching only corner to corner still counts as one region. */
    sand_init(&s, 1);
    for (int x = 0; x < GRID_W; x++)
        sand_set(&s, x, GRID_H - 1 - (x & 1), 4, 0);
    CHECK_EQ(sand_mark_spans(&s), GRID_W);
}

static void only_the_spanning_colour_clears(void)
{
    sand_init(&s, 1);
    for (int x = 0; x < GRID_W; x++) {
        sand_set(&s, x, GRID_H - 1, 1, 0);          /* spans */
        if (x < GRID_W / 2)
            sand_set(&s, x, GRID_H - 2, 2, 0);      /* half width only */
    }
    CHECK_EQ(sand_mark_spans(&s), GRID_W);
    CHECK_EQ(sand_remove_marked(&s), GRID_W);
    CHECK_EQ(count_grains(&s), GRID_W / 2);
    CHECK_EQ(s.color[GRID_H - 2][0], 2);
}

static void top_row(void)
{
    sand_init(&s, 1);
    CHECK_EQ(sand_top_row(&s), GRID_H);
    sand_set(&s, 5, 40, 1, 0);
    CHECK_EQ(sand_top_row(&s), 40);
}

void test_sand(void)
{
    falls_to_floor();
    slides_off_a_grain();
    walls_hold_grains();
    conserves_grains();
    full_row_spans();
    broken_row_does_not_span();
    diagonal_steps_connect();
    only_the_spanning_colour_clears();
    top_row();
}
