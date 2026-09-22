#include "render.h"

#include <stdio.h>

/* The well is drawn as a GRID_W x GRID_H texture, one texel per grain,
 * scaled up with point filtering so every grain stays a crisp square. */
#define SCALE   3
#define WELL_X  ((SCREEN_W - GRID_W * SCALE) / 2)
#define WELL_Y  40
#define FRAME   6
#define LEFT_X  84
#define RIGHT_X 640

#define COL_BG       RGBA8(28, 24, 32, 255)
#define COL_WELL     RGBA8(14, 12, 18, 255)
#define COL_FRAME    RGBA8(120, 100, 70, 255)
#define COL_FLASH    RGBA8(255, 255, 240, 255)
#define COL_PANEL    RGBA8(40, 34, 44, 240)

static vita2d_pgf *font;
/* Two textures, written on alternate frames, so the CPU never rewrites the
 * one the GPU may still be reading for the previous frame. */
static vita2d_texture *well_tex[2];
static int tex_index;
static uint32_t palette[COLOR_COUNT + 1][4];

static const uint8_t BASE_RGB[COLOR_COUNT + 1][3] = {
    {0, 0, 0},
    {214, 66, 58},  /* red */
    {58, 118, 214}, /* blue */
    {74, 184, 88},  /* green */
    {232, 196, 64}, /* yellow */
};

static uint8_t shade_channel(int v, int shade)
{
    static const int PERCENT[4] = {80, 90, 100, 110};
    int s = v * PERCENT[shade] / 100;
    return (uint8_t)(s > 255 ? 255 : s);
}

int render_init(void)
{
    if (vita2d_init() < 0)
        return -1;
    vita2d_set_clear_color(COL_BG);
    vita2d_set_vblank_wait(1);
    font = vita2d_load_default_pgf();
    for (int i = 0; i < 2; i++) {
        well_tex[i] = vita2d_create_empty_texture_format(
            GRID_W, GRID_H, SCE_GXM_TEXTURE_FORMAT_A8B8G8R8);
        if (!well_tex[i] || !font) {
            render_shutdown();
            return -1;
        }
        vita2d_texture_set_filters(well_tex[i], SCE_GXM_TEXTURE_FILTER_POINT,
                                   SCE_GXM_TEXTURE_FILTER_POINT);
    }
    for (int c = 1; c <= COLOR_COUNT; c++)
        for (int s = 0; s < 4; s++)
            palette[c][s] = RGBA8(shade_channel(BASE_RGB[c][0], s),
                                  shade_channel(BASE_RGB[c][1], s),
                                  shade_channel(BASE_RGB[c][2], s), 255);
    return 0;
}

void render_shutdown(void)
{
    vita2d_wait_rendering_done();
    for (int i = 0; i < 2; i++) {
        if (well_tex[i])
            vita2d_free_texture(well_tex[i]);
        well_tex[i] = NULL;
    }
    if (font)
        vita2d_free_pgf(font);
    font = NULL;
    vita2d_fini();
}

void render_begin(void)
{
    vita2d_start_drawing();
    vita2d_clear_screen();
}

void render_end(void)
{
    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void render_text(int x, int y, float scale, uint32_t colour, const char *s)
{
    vita2d_pgf_draw_text(font, x, y, colour, scale, s);
}

void render_text_centered(int y, float scale, uint32_t colour, const char *s)
{
    int w = vita2d_pgf_text_width(font, scale, s);
    vita2d_pgf_draw_text(font, (SCREEN_W - w) / 2, y, colour, scale, s);
}

void render_dim(int alpha)
{
    vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, alpha));
}

void render_panel(int x, int y, int w, int h)
{
    vita2d_draw_rectangle(x - 3, y - 3, w + 6, h + 6, COL_FRAME);
    vita2d_draw_rectangle(x, y, w, h, COL_PANEL);
}

void render_bar(int x, int y, int w, int h, float fill, uint32_t colour)
{
    if (fill < 0.0f)
        fill = 0.0f;
    if (fill > 1.0f)
        fill = 1.0f;
    vita2d_draw_rectangle(x, y, w, h, RGBA8(70, 62, 76, 255));
    vita2d_draw_rectangle(x, y, w * fill, h, colour);
}

static void fill_well(const Game *g, int show_piece, vita2d_texture *t)
{
    uint32_t *px = vita2d_texture_get_datap(t);
    unsigned stride = vita2d_texture_get_stride(t) / 4; /* bytes -> texels */
    int flash = g->clear_timer > 0 && ((g->clear_timer / 4) & 1);

    for (int y = 0; y < GRID_H; y++) {
        uint32_t *row = px + y * stride;
        for (int x = 0; x < GRID_W; x++) {
            uint8_t c = g->sand.color[y][x];
            if (c == COLOR_NONE)
                row[x] = COL_WELL;
            else if (flash && g->sand.marked[y][x])
                row[x] = COL_FLASH;
            else
                row[x] = palette[c][g->sand.shade[y][x] & 3];
        }
    }
    if (!show_piece)
        return;

    int bx[4], by[4];
    const Piece *p = &g->cur;
    piece_blocks(p, bx, by);
    for (int i = 0; i < 4; i++) {
        for (int gy = 0; gy < BLOCK; gy++) {
            for (int gx = 0; gx < BLOCK; gx++) {
                int ox = bx[i] * BLOCK + gx, oy = by[i] * BLOCK + gy;
                int x = p->x + ox, y = p->y + oy;
                if (x < 0 || x >= GRID_W || y < 0 || y >= GRID_H)
                    continue;
                px[y * stride + x] = palette[p->color][piece_grain_shade(ox, oy)];
            }
        }
    }
}

static void draw_next(const Piece *p, int x0, int y0)
{
    const int size = 22;
    int bx[4], by[4];
    Piece shown = *p;
    shown.rot = 0;
    piece_blocks(&shown, bx, by);
    int n = piece_box(p->type);
    int ox = x0 + (4 - n) * size / 2, oy = y0 + (n == 4 ? -size / 2 : 0);
    for (int i = 0; i < 4; i++) {
        int x = ox + bx[i] * size, y = oy + by[i] * size;
        vita2d_draw_rectangle(x, y, size, size, palette[p->color][0]);
        vita2d_draw_rectangle(x + 2, y + 2, size - 4, size - 4, palette[p->color][2]);
    }
}

static void draw_stat(int y, const char *label, const char *value, uint32_t colour)
{
    render_text(LEFT_X, y, 0.9f, COL_HINT, label);
    render_text(LEFT_X, y + 34, 1.5f, colour, value);
}

void render_game(const Game *g, uint32_t best, int show_piece)
{
    char buf[48];
    vita2d_texture *t = well_tex[tex_index];
    tex_index ^= 1;

    fill_well(g, show_piece, t);
    vita2d_draw_rectangle(WELL_X - FRAME, WELL_Y - FRAME, GRID_W * SCALE + 2 * FRAME,
                          GRID_H * SCALE + 2 * FRAME, COL_FRAME);
    vita2d_draw_texture_scale(t, WELL_X, WELL_Y, SCALE, SCALE);

    render_text(LEFT_X, 80, 1.5f, COL_GOLD, "SPANFALL");
    snprintf(buf, sizeof buf, "%lu", (unsigned long)g->score);
    draw_stat(140, "SCORE", buf, COL_WHITE);
    snprintf(buf, sizeof buf, "%lu", (unsigned long)(g->score > best ? g->score : best));
    draw_stat(220, "BEST", buf, COL_TEXT);
    snprintf(buf, sizeof buf, "%d", g->level);
    draw_stat(300, "SPEED", buf, COL_TEXT);

    render_text(LEFT_X, 420, 0.8f, COL_HINT, "D-pad: move");
    render_text(LEFT_X, 444, 0.8f, COL_HINT, "Up / X: rotate");
    render_text(LEFT_X, 468, 0.8f, COL_HINT, "Down: drop faster");
    render_text(LEFT_X, 492, 0.8f, COL_HINT, "START: pause");

    render_text(RIGHT_X, 80, 0.9f, COL_HINT, "NEXT");
    draw_next(&g->next, RIGHT_X, 100);

    render_text(RIGHT_X, 230, 0.9f, COL_HINT, "COMBO");
    if (g->combo_timer > 0) {
        snprintf(buf, sizeof buf, "x%d", g->combo);
        render_text(RIGHT_X, 268, 1.5f, COL_GOLD, buf);
        render_bar(RIGHT_X, 282, 200, 10, (float)g->combo_timer / COMBO_FRAMES, COL_GOLD);
        if (g->combo_timer > COMBO_FRAMES - 60) {
            snprintf(buf, sizeof buf, "+%d", g->last_gain);
            render_text(RIGHT_X, 330, 1.2f, COL_WHITE, buf);
        }
    } else {
        render_text(RIGHT_X, 268, 1.5f, COL_HINT, "x1");
    }
    render_text(RIGHT_X, 420, 0.8f, COL_HINT, "Join one colour from");
    render_text(RIGHT_X, 444, 0.8f, COL_HINT, "wall to wall to clear it.");
}
