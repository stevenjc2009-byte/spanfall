#include <string.h>

#include "../src/game.h"
#include "check.h"

static Game g;

static int count_colour(const Sand *s, int c)
{
    int n = 0;
    for (int y = 0; y < GRID_H; y++)
        for (int x = 0; x < GRID_W; x++)
            n += s->color[y][x] == c;
    return n;
}

static int count_all(const Sand *s)
{
    return GRID_CELLS - count_colour(s, COLOR_NONE);
}

static int piece_min_x(const Piece *p)
{
    int bx[4], by[4], m = 99;
    piece_blocks(p, bx, by);
    for (int i = 0; i < 4; i++)
        if (bx[i] < m)
            m = bx[i];
    return p->x + m * BLOCK;
}

/* Holds soft drop until the current piece turns to sand. Returns the frames
 * it took, or -1 if it never landed. */
static int soft_drop_until_landed(Game *gm)
{
    int before = count_all(&gm->sand);
    for (int i = 1; i <= 400; i++) {
        game_update(gm, IN_DOWN, 0);
        if (count_all(&gm->sand) != before || gm->over)
            return i;
    }
    return -1;
}

/* A full-width, one-grain row of colour c on the floor: it spans at once. */
static void lay_span(Game *gm, uint8_t c)
{
    for (int x = 0; x < GRID_W; x++)
        sand_set(&gm->sand, x, GRID_H - 1, c, 0);
    gm->sand_dirty = 1;
}

/* Runs frames until the pending clear has been scored. */
static void run_clear(Game *gm)
{
    int clears = gm->clears;
    for (int i = 0; i <= CLEAR_FRAMES + 1 && gm->clears == clears; i++)
        game_update(gm, 0, 0);
}

/* A settled 45-degree slope, highest at the left wall, starting `lift`
 * grains below the top. Two colours, so no region touches both walls. */
static void lay_slope(Game *gm, int lift)
{
    for (int x = 0; x < GRID_W; x++)
        for (int y = x + lift; y < GRID_H; y++)
            sand_set(&gm->sand, x, y, (uint8_t)(x < GRID_W / 2 ? 1 : 2), 0);
}

static void starts_clean(void)
{
    game_init(&g, 42);
    CHECK(!g.over);
    CHECK_EQ(g.level, 1);
    CHECK_EQ(g.score, 0);
    CHECK_EQ(count_all(&g.sand), 0);
    CHECK(piece_fits(&g.cur, &g.sand));
}

static void landed_piece_turns_to_sand(void)
{
    game_init(&g, 42);
    int colour = g.cur.color;
    int frames = soft_drop_until_landed(&g);
    CHECK(frames > 0 && frames < 60); /* soft drop, not the 576-frame fall */
    CHECK_EQ(count_colour(&g.sand, colour), 4 * BLOCK * BLOCK);
    CHECK_EQ(count_all(&g.sand), 4 * BLOCK * BLOCK);
    CHECK(sand_top_row(&g.sand) >= GRID_H - 2 * BLOCK); /* on the floor */
    CHECK(!g.over);
}

static void left_moves_a_block_then_repeats(void)
{
    game_init(&g, 42);
    g.cur.type = 1; /* O, so the shape is known */
    g.cur.rot = 0;
    g.cur.x = 4 * BLOCK;
    g.cur.y = 0;
    game_update(&g, IN_LEFT, IN_LEFT);
    CHECK_EQ(g.cur.x, 3 * BLOCK);
    for (int i = 0; i < DAS_DELAY - 1; i++)
        game_update(&g, IN_LEFT, 0);
    CHECK_EQ(g.cur.x, 3 * BLOCK); /* still inside the repeat delay */
    game_update(&g, IN_LEFT, 0);
    CHECK(g.cur.x < 3 * BLOCK);
    for (int i = 0; i < 100; i++)
        game_update(&g, IN_LEFT, 0);
    CHECK_EQ(piece_min_x(&g.cur), 0); /* stopped by the wall */
}

static void rotate_turns_and_kicks_off_the_wall(void)
{
    game_init(&g, 42);
    g.cur = (Piece){2, 0, 1, 3 * BLOCK, 40}; /* T in open space */
    game_update(&g, 0, IN_ROTATE);
    CHECK_EQ(g.cur.rot, 1);
    CHECK_EQ(g.cur.x, 3 * BLOCK);

    /* Upright I against the right wall: lying flat needs a full block of
     * room, so the turn nudges it left by one block. */
    g.cur = (Piece){0, 1, 1, GRID_W - 3 * BLOCK, 40};
    game_update(&g, 0, IN_ROTATE);
    CHECK_EQ(g.cur.rot, 2);
    CHECK_EQ(g.cur.x, GRID_W - 4 * BLOCK);
}

static void full_well_ends_the_game(void)
{
    game_init(&g, 42);
    /* Vertical stripes: no colour can reach both walls. */
    for (int y = 0; y < GRID_H; y++)
        for (int x = 0; x < GRID_W; x++)
            sand_set(&g.sand, x, y, (uint8_t)(1 + (x & 1)), 0);
    for (int i = 0; i < 10 && !g.over; i++)
        game_update(&g, 0, 0);
    CHECK(g.over);
}

static void settled_sand_at_the_top_ends_the_game(void)
{
    game_init(&g, 42);
    lay_slope(&g, 1); /* one grain short of the top: play on */
    game_update(&g, 0, 0);
    CHECK(!g.over);

    game_init(&g, 42);
    lay_slope(&g, 0); /* reaches row 0 at the left wall */
    game_update(&g, 0, 0);
    CHECK(g.over);
}

static void landing_above_the_top_ends_the_game(void)
{
    game_init(&g, 42);
    /* Full rows from y=12 down, striped so nothing spans; an O piece
     * hanging 4 grains above the well lands on them at once. */
    for (int y = 12; y < GRID_H; y++)
        for (int x = 0; x < GRID_W; x++)
            sand_set(&g.sand, x, y, (uint8_t)(1 + (x & 1)), 0);
    g.cur = (Piece){1, 0, 3, 4 * BLOCK, -4};
    game_update(&g, IN_DOWN, 0); /* soft drop: tries to move down this frame */
    CHECK(g.over);
    CHECK_EQ(g.cur.y, -4); /* ended on landing: no next piece spawned */
}

static void blocked_spawn_ends_the_game(void)
{
    game_init(&g, 42);
    for (int y = 20; y < GRID_H; y++)
        for (int x = 0; x < GRID_W; x++)
            sand_set(&g.sand, x, y, (uint8_t)(1 + (x & 1)), 0);
    g.cur = (Piece){1, 0, 3, 4 * BLOCK, 4};  /* O resting on row 20 */
    g.next = (Piece){1, 0, 3, 4 * BLOCK, 0}; /* spawns into it */
    game_update(&g, IN_DOWN, 0);
    CHECK(g.over);
    CHECK_EQ(sand_top_row(&g.sand), 4); /* row 0 empty: the spawn ended it */
}

static void span_flashes_then_clears_and_scores(void)
{
    game_init(&g, 42);
    /* Bottom two block-rows of colour 1 with a 2-block gap on the left; an
     * O piece of colour 1 resting in the gap lands on the first frame. */
    for (int y = GRID_H - 2 * BLOCK; y < GRID_H; y++)
        for (int x = 2 * BLOCK; x < GRID_W; x++)
            sand_set(&g.sand, x, y, 1, 0);
    g.cur = (Piece){1, 0, 1, 0, GRID_H - 2 * BLOCK};
    g.next = (Piece){1, 0, 2, 4 * BLOCK, 0};

    game_update(&g, IN_DOWN, 0);
    CHECK_EQ(g.clear_timer, CLEAR_FRAMES);
    CHECK_EQ(count_colour(&g.sand, 1), 2 * BLOCK * GRID_W);

    for (int i = 0; i < CLEAR_FRAMES - 1; i++)
        game_update(&g, 0, 0);
    CHECK_EQ(g.clears, 0); /* still flashing */
    game_update(&g, 0, 0);
    CHECK_EQ(g.clears, 1);
    CHECK_EQ(count_colour(&g.sand, 1), 0);
    CHECK_EQ(g.combo, 1);
    CHECK_EQ(g.score, (uint32_t)(2 * BLOCK * GRID_W)); /* 1 point a grain */
    CHECK_EQ(g.grains_cleared, (uint32_t)(2 * BLOCK * GRID_W));
}

static void combo_rises_within_the_window_and_resets(void)
{
    game_init(&g, 42);
    g.cur.y = 0;
    lay_span(&g, 1);
    run_clear(&g);
    CHECK_EQ(g.combo, 1);
    CHECK_EQ(g.last_gain, GRID_W);

    lay_span(&g, 2); /* again before the meter runs out */
    run_clear(&g);
    CHECK_EQ(g.combo, 2);
    CHECK_EQ(g.last_gain, 2 * GRID_W);
    CHECK_EQ(g.score, (uint32_t)(3 * GRID_W));

    for (int i = 0; i < COMBO_FRAMES; i++)
        game_update(&g, 0, 0);
    CHECK_EQ(g.combo_timer, 0);
    lay_span(&g, 3); /* the meter ran out: back to x1 */
    run_clear(&g);
    CHECK_EQ(g.combo, 1);
    CHECK_EQ(g.last_gain, GRID_W);

    g.combo = COMBO_MAX; /* never above the cap */
    lay_span(&g, 4);
    run_clear(&g);
    CHECK_EQ(g.combo, COMBO_MAX);
    CHECK_EQ(g.last_gain, COMBO_MAX * GRID_W);
    CHECK(!g.over);
}

static void speed_rises_over_time(void)
{
    game_init(&g, 42);
    g.frame = LEVEL_FRAMES - 2;
    game_update(&g, 0, 0);
    CHECK_EQ(g.level, 1);
    game_update(&g, 0, 0);
    CHECK_EQ(g.level, 2);
    g.frame = 1000u * LEVEL_FRAMES;
    game_update(&g, 0, 0);
    CHECK_EQ(g.level, MAX_LEVEL);
    CHECK(game_fall_speed(2) > game_fall_speed(1));
    CHECK_EQ(game_fall_speed(99), game_fall_speed(MAX_LEVEL));
}

static void same_seed_same_game(void)
{
    static Game a, b;
    game_init(&a, 777);
    game_init(&b, 777);
    for (int i = 0; i < 900; i++) {
        unsigned held = (i % 90 < 50) ? IN_DOWN : 0;
        if (i % 40 < 6)
            held |= (i / 40) % 2 ? IN_LEFT : IN_RIGHT;
        unsigned pressed = (i % 7 == 0) ? IN_ROTATE : 0;
        game_update(&a, held, pressed | (held & (IN_LEFT | IN_RIGHT)));
        game_update(&b, held, pressed | (held & (IN_LEFT | IN_RIGHT)));
    }
    CHECK(count_all(&a.sand) > 0); /* pieces really landed */
    CHECK(memcmp(&a, &b, sizeof a) == 0);
}

void test_game(void)
{
    starts_clean();
    landed_piece_turns_to_sand();
    left_moves_a_block_then_repeats();
    rotate_turns_and_kicks_off_the_wall();
    full_well_ends_the_game();
    settled_sand_at_the_top_ends_the_game();
    landing_above_the_top_ends_the_game();
    blocked_spawn_ends_the_game();
    span_flashes_then_clears_and_scores();
    combo_rises_within_the_window_and_resets();
    speed_rises_over_time();
    same_seed_same_game();
}
