#include "game.h"

#include <string.h>

#define SOFT_DROP_SPEED (3 * 256) /* grains per frame, fixed point */
#define DAS_STEP        2         /* grains per frame once a hold repeats */

static int next_type(Game *g)
{
    /* 7-bag: every piece once per bag, so droughts stay short. */
    if (g->bag_left == 0) {
        for (int i = 0; i < PIECE_TYPES; i++)
            g->bag[i] = i;
        for (int i = PIECE_TYPES - 1; i > 0; i--) {
            int j = (int)(sand_rand(&g->sand) % (uint32_t)(i + 1));
            int t = g->bag[i];
            g->bag[i] = g->bag[j];
            g->bag[j] = t;
        }
        g->bag_left = PIECE_TYPES;
    }
    return g->bag[--g->bag_left];
}

/* Row (in blocks, inside the rotation box) of the piece's highest block. */
static int top_block_row(const Piece *p)
{
    int bx[4], by[4], top = 4;
    piece_blocks(p, bx, by);
    for (int i = 0; i < 4; i++)
        if (by[i] < top)
            top = by[i];
    return top;
}

static void make_piece(Game *g, Piece *p)
{
    p->type = next_type(g);
    p->rot = 0;
    p->color = 1 + (int)(sand_rand(&g->sand) % COLOR_COUNT);
    p->x = ((WELL_COLS - piece_box(p->type)) / 2) * BLOCK;
    /* Put the piece's top row of blocks on the well's top edge. */
    p->y = -top_block_row(p) * BLOCK;
}

static void spawn(Game *g)
{
    g->cur = g->next;
    make_piece(g, &g->next);
    g->fall_accum = 0;
    if (!piece_fits(&g->cur, &g->sand))
        g->over = 1;
}

void game_init(Game *g, uint32_t seed)
{
    memset(g, 0, sizeof *g);
    sand_init(&g->sand, seed);
    g->level = 1;
    make_piece(g, &g->next);
    spawn(g);
}

int game_fall_speed(int level)
{
    if (level < 1)
        level = 1;
    if (level > MAX_LEVEL)
        level = MAX_LEVEL;
    return 64 + (level - 1) * 16;
}

static int try_move(Game *g, int dx, int dy)
{
    Piece t = g->cur;
    t.x += dx;
    t.y += dy;
    if (!piece_fits(&t, &g->sand))
        return 0;
    g->cur = t;
    return 1;
}

/* Moves up to max_grains sideways, stopping at the first obstacle. */
static void slide(Game *g, int dir, int max_grains)
{
    for (int i = 0; i < max_grains; i++)
        if (!try_move(g, dir, 0))
            break;
}

/* Turns clockwise, nudging the piece sideways (then upward) until it fits. */
static void rotate(Game *g)
{
    Piece t = g->cur;
    t.rot = (t.rot + 1) & 3;
    for (int dy = 0; dy <= BLOCK; dy++) {
        for (int k = 0; k <= 2 * BLOCK; k++) {
            int dx = (k & 1) ? -(k + 1) / 2 : k / 2;
            Piece u = t;
            u.x += dx;
            u.y -= dy;
            if (piece_fits(&u, &g->sand)) {
                g->cur = u;
                return;
            }
        }
    }
}

static void land(Game *g)
{
    piece_stamp(&g->cur, &g->sand);
    g->sand_dirty = 1;
    /* Part of the piece is still above the well: the sand has reached the top. */
    if (g->cur.y + top_block_row(&g->cur) * BLOCK < 0) {
        g->over = 1;
        return;
    }
    spawn(g);
}

static void finish_clear(Game *g)
{
    int n = sand_remove_marked(&g->sand);
    /* Sandtrix scoring: 1 point per grain. Clearing again before the combo
     * meter runs out raises the multiplier, up to COMBO_MAX. */
    g->combo = g->combo_timer > 0 ? g->combo + 1 : 1;
    if (g->combo > COMBO_MAX)
        g->combo = COMBO_MAX;
    g->combo_timer = COMBO_FRAMES;
    g->clears++;
    g->grains_cleared += (uint32_t)n;
    g->last_gain = n * g->combo;
    g->score += (uint32_t)g->last_gain;
    g->sand_dirty = 1;
}

static int row_has_sand(const Sand *s, int y)
{
    for (int x = 0; x < GRID_W; x++)
        if (s->color[y][x] != COLOR_NONE)
            return 1;
    return 0;
}

void game_update(Game *g, unsigned held, unsigned pressed)
{
    if (g->over)
        return;
    g->frame++;
    g->level = 1 + (int)(g->frame / LEVEL_FRAMES);
    if (g->level > MAX_LEVEL)
        g->level = MAX_LEVEL;

    /* A clearing region flashes with everything frozen, then vanishes. */
    if (g->clear_timer > 0) {
        if (--g->clear_timer == 0)
            finish_clear(g);
        return;
    }
    if (g->combo_timer > 0)
        g->combo_timer--;

    int dir = (held & IN_LEFT) ? -1 : (held & IN_RIGHT) ? 1 : 0;
    if (dir != 0) {
        if (pressed & (dir < 0 ? IN_LEFT : IN_RIGHT)) {
            slide(g, dir, BLOCK);
            g->das_frames = 0;
        } else if (++g->das_frames >= DAS_DELAY) {
            slide(g, dir, DAS_STEP);
        }
    } else {
        g->das_frames = 0;
    }

    if (pressed & IN_ROTATE)
        rotate(g);

    g->fall_accum += (held & IN_DOWN) ? SOFT_DROP_SPEED
                                      : game_fall_speed(g->level);
    while (g->fall_accum >= 256) {
        g->fall_accum -= 256;
        if (!try_move(g, 0, 1)) {
            land(g);
            break;
        }
    }
    if (g->over)
        return;

    int moved = 0;
    for (int i = 0; i < SAND_STEPS; i++)
        moved += sand_step(&g->sand);
    if (moved > 0)
        g->sand_dirty = 1;

    if (g->sand_dirty) {
        g->sand_dirty = 0;
        if (sand_mark_spans(&g->sand) > 0) {
            g->clear_timer = CLEAR_FRAMES;
            return;
        }
    }

    /* Sandtrix: the game ends when settled sand reaches the top. */
    if (moved == 0 && row_has_sand(&g->sand, 0))
        g->over = 1;
}
