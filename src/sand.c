#include "sand.h"

#include <string.h>

void sand_init(Sand *s, uint32_t seed)
{
    memset(s, 0, sizeof *s);
    s->rng = seed ? seed : 0x9E3779B9u;
}

/* xorshift32: cheap, deterministic for a given seed, never returns 0. */
uint32_t sand_rand(Sand *s)
{
    uint32_t x = s->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s->rng = x;
    return x;
}

void sand_set(Sand *s, int x, int y, uint8_t color, uint8_t shade)
{
    if (x < 0 || x >= GRID_W || y < 0 || y >= GRID_H)
        return;
    s->color[y][x] = color;
    s->shade[y][x] = shade;
    s->marked[y][x] = 0;
}

int sand_blocked(const Sand *s, int x, int y)
{
    if (x < 0 || x >= GRID_W || y < 0 || y >= GRID_H)
        return 1;
    return s->color[y][x] != COLOR_NONE;
}

static void move_grain(Sand *s, int x, int y, int nx, int ny)
{
    s->color[ny][nx] = s->color[y][x];
    s->shade[ny][nx] = s->shade[y][x];
    s->color[y][x] = COLOR_NONE;
    s->shade[y][x] = 0;
}

int sand_step(Sand *s)
{
    int moved = 0;

    /* Bottom-up, so a grain that falls into a row already visited this pass
     * cannot move twice. The scan direction is random per row so piles do
     * not drift to one side. */
    for (int y = GRID_H - 2; y >= 0; y--) {
        int left_to_right = (int)(sand_rand(s) & 1u);
        for (int i = 0; i < GRID_W; i++) {
            int x = left_to_right ? i : GRID_W - 1 - i;
            if (s->color[y][x] == COLOR_NONE)
                continue;

            if (s->color[y + 1][x] == COLOR_NONE) {
                move_grain(s, x, y, x, y + 1);
                moved++;
                continue;
            }

            int d = (sand_rand(s) & 1u) ? 1 : -1;
            if (!sand_blocked(s, x + d, y + 1)) {
                move_grain(s, x, y, x + d, y + 1);
                moved++;
            } else if (!sand_blocked(s, x - d, y + 1)) {
                move_grain(s, x, y, x - d, y + 1);
                moved++;
            }
        }
    }
    return moved;
}

int sand_mark_spans(Sand *s)
{
    static uint16_t queue[GRID_CELLS];
    static uint8_t seen[GRID_CELLS];
    int marked = 0;

    memset(seen, 0, sizeof seen);

    /* Every region that touches the right wall and the left wall contains a
     * grain in column 0, so starting only from column 0 finds them all. */
    for (int y0 = 0; y0 < GRID_H; y0++) {
        uint8_t c = s->color[y0][0];
        int start = y0 * GRID_W;
        if (c == COLOR_NONE || seen[start])
            continue;

        int head = 0, tail = 0, touches_right = 0;
        queue[tail++] = (uint16_t)start;
        seen[start] = 1;

        while (head < tail) {
            int idx = queue[head++];
            int x = idx % GRID_W, y = idx / GRID_W;
            if (x == GRID_W - 1)
                touches_right = 1;

            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if ((dx == 0 && dy == 0) || nx < 0 || nx >= GRID_W ||
                        ny < 0 || ny >= GRID_H)
                        continue;
                    int n = ny * GRID_W + nx;
                    if (seen[n] || s->color[ny][nx] != c)
                        continue;
                    seen[n] = 1;
                    queue[tail++] = (uint16_t)n;
                }
            }
        }

        if (!touches_right)
            continue;
        for (int i = 0; i < tail; i++) {
            int x = queue[i] % GRID_W, y = queue[i] / GRID_W;
            if (!s->marked[y][x]) {
                s->marked[y][x] = 1;
                marked++;
            }
        }
    }
    return marked;
}

int sand_remove_marked(Sand *s)
{
    int removed = 0;
    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            if (!s->marked[y][x])
                continue;
            s->marked[y][x] = 0;
            s->color[y][x] = COLOR_NONE;
            s->shade[y][x] = 0;
            removed++;
        }
    }
    return removed;
}

int sand_top_row(const Sand *s)
{
    for (int y = 0; y < GRID_H; y++)
        for (int x = 0; x < GRID_W; x++)
            if (s->color[y][x] != COLOR_NONE)
                return y;
    return GRID_H;
}
