#include <stdio.h>

#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>

#include "app.h"
#include "game.h"
#include "input.h"
#include "render.h"
#include "save.h"
#include "update_screen.h"
#include "updater/upd_updater.h"
#include "version.h"

#define OVER_INPUT_DELAY 45 /* frames before game over takes input: no accidental skip */

typedef enum { ST_TITLE, ST_PLAY, ST_PAUSE, ST_OVER, ST_UPDATE } State;

enum { TITLE_PLAY, TITLE_UPDATE, TITLE_QUIT, TITLE_COUNT };
enum { PAUSE_RESUME, PAUSE_QUIT, PAUSE_COUNT };
static const char *const TITLE_LABELS[TITLE_COUNT] = {"Play", "Check for updates", "Quit"};
static const char *const PAUSE_LABELS[PAUSE_COUNT] = {"Resume", "Quit to title"};

static Game game; /* ~40 KB of sand: file scope, never on the stack */
static State state;
static int sel, quit, over_timer;
static uint32_t best;
static SaveStatus save_status;
static int save_failed, new_best, update_init_failed;

void app_init(void)
{
    save_status = save_load(SAVE_PATH, &best);
    state = ST_TITLE;
}

int app_quit_requested(void)
{
    return quit;
}

/* ---------------- flow ---------------- */

static void enter_title(void)
{
    sel = TITLE_PLAY;
    state = ST_TITLE;
}

static void start_game(void)
{
    game_init(&game, (uint32_t)sceKernelGetProcessTimeWide());
    state = ST_PLAY;
}

/* Keeps the best score, whether the game ended or the player quit it. */
static void record_score(void)
{
    new_best = game.score > best;
    save_failed = 0;
    if (!new_best)
        return;
    best = game.score;
    sceIoMkdir(SAVE_DIR, 0777); /* fails harmlessly when it already exists */
    save_failed = save_write(SAVE_PATH, best) != 0;
}

static int menu_move(int s, int count, unsigned pressed)
{
    if (pressed & SCE_CTRL_UP)
        s = (s + count - 1) % count;
    if (pressed & SCE_CTRL_DOWN)
        s = (s + 1) % count;
    return s;
}

/* Sandtrix controls: D-pad moves, Down drops faster, Up or Cross turns. */
static unsigned game_bits(unsigned b)
{
    unsigned m = 0;
    if (b & SCE_CTRL_LEFT)
        m |= IN_LEFT;
    if (b & SCE_CTRL_RIGHT)
        m |= IN_RIGHT;
    if (b & SCE_CTRL_DOWN)
        m |= IN_DOWN;
    if (b & (SCE_CTRL_UP | SCE_CTRL_CROSS))
        m |= IN_ROTATE;
    return m;
}

static void update_title(unsigned pressed)
{
    sel = menu_move(sel, TITLE_COUNT, pressed);
    if (!(pressed & input_confirm_button()))
        return;
    switch (sel) {
    case TITLE_PLAY:
        start_game();
        break;
    case TITLE_UPDATE:
        /* upd_init() is cheap: no socket, no thread until a check starts. */
        update_init_failed = upd_init() != 0;
        state = ST_UPDATE;
        break;
    default:
        quit = 1;
        break;
    }
}

static void update_play(unsigned held, unsigned pressed)
{
    if (pressed & SCE_CTRL_START) {
        sel = PAUSE_RESUME;
        state = ST_PAUSE;
        return;
    }
    game_update(&game, game_bits(held), game_bits(pressed));
    if (game.over) {
        record_score();
        over_timer = OVER_INPUT_DELAY;
        state = ST_OVER;
    }
}

static void update_pause(unsigned pressed)
{
    if (pressed & (SCE_CTRL_START | input_back_button())) {
        state = ST_PLAY;
        return;
    }
    sel = menu_move(sel, PAUSE_COUNT, pressed);
    if (!(pressed & input_confirm_button()))
        return;
    if (sel == PAUSE_RESUME) {
        state = ST_PLAY;
    } else {
        record_score();
        enter_title();
    }
}

static void update_over(unsigned pressed)
{
    if (over_timer > 0) {
        over_timer--;
        return;
    }
    if (pressed & input_confirm_button())
        start_game();
    else if (pressed & input_back_button())
        enter_title();
}

/* Ported from Foldwind's update_updates(): the worker is polled, never waited on. */
static void update_updates(unsigned pressed)
{
    UpdScreen sc = update_screen_for(upd_state());

    if (pressed & input_confirm_button()) {
        switch (sc.confirm) {
        case UPD_ACT_CHECK:    (void)upd_start_check(); break;
        case UPD_ACT_INSTALL:  (void)upd_start_install(); break;
        case UPD_ACT_RELAUNCH: (void)upd_relaunch(); break; /* does not return on success */
        default: break;
        }
        return;
    }
    if (pressed & input_back_button()) {
        /* Cancel whether or not this state is busy: leaving must never strand a worker. */
        upd_cancel();
        if (sc.back == UPD_ACT_LEAVE)
            enter_title();
    }
}

/* ---------------- drawing ---------------- */

static void draw_menu(int y0, const char *const *labels, int count)
{
    char buf[64];
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof buf, i == sel ? "> %s <" : "%s", labels[i]);
        render_text_centered(y0 + i * 42, 1.2f, i == sel ? COL_GOLD : COL_TEXT, buf);
    }
}

static void draw_title(void)
{
    char buf[64];
    render_panel(270, 70, 420, 400);
    render_text_centered(150, 2.6f, COL_GOLD, "SPANFALL");
    render_text_centered(190, 1.0f, COL_HINT, "join a colour wall to wall to clear it");
    draw_menu(260, TITLE_LABELS, TITLE_COUNT);
    snprintf(buf, sizeof buf, "Best: %lu", (unsigned long)best);
    render_text_centered(400, 1.0f, COL_TEXT, buf);
    if (save_status == SAVE_CORRUPT)
        render_text_centered(424, 0.8f, COL_ERROR, "Save file unreadable");
    snprintf(buf, sizeof buf, "%s: select", input_confirm_name());
    render_text_centered(452, 0.8f, COL_HINT, buf);
    render_text(12, SCREEN_H - 12, 0.7f, COL_HINT, "v" SF_VERSION);
}

static void draw_pause(void)
{
    char buf[64];
    render_game(&game, best, 1);
    render_dim(150);
    render_panel(330, 170, 300, 200);
    render_text_centered(220, 1.6f, COL_WHITE, "Paused");
    draw_menu(275, PAUSE_LABELS, PAUSE_COUNT);
    snprintf(buf, sizeof buf, "%s / START: resume", input_back_name());
    render_text_centered(355, 0.8f, COL_HINT, buf);
}

static void draw_over(void)
{
    char buf[80];
    render_game(&game, best, 1);
    render_dim(150);
    render_panel(300, 140, 360, 270);
    render_text_centered(190, 1.6f, COL_WHITE, "Game over");
    snprintf(buf, sizeof buf, "Score: %lu", (unsigned long)game.score);
    render_text_centered(240, 1.2f, COL_TEXT, buf);
    snprintf(buf, sizeof buf, "Best: %lu", (unsigned long)best);
    render_text_centered(276, 1.2f, COL_GOLD, buf);
    if (new_best)
        render_text_centered(314, 1.0f, RGBA8(140, 255, 160, 255), "NEW BEST!");
    if (save_failed)
        render_text_centered(340, 0.8f, COL_ERROR, "Could not save to " SAVE_DIR);
    if (over_timer == 0) {
        snprintf(buf, sizeof buf, "%s: play again   %s: title", input_confirm_name(),
                 input_back_name());
        render_text_centered(386, 0.8f, COL_HINT, buf);
    }
}

/* Ported from Foldwind's draw_update(). Nothing here blocks. */
static void draw_update(void)
{
    char msg[160], tag[48], buf[96];
    char lines[UPD_WRAP_LINES][UPD_WRAP_CAP];
    UpdScreen sc = update_screen_for(upd_state());
    uint64_t done = 0, total = 0;
    int i, n, pct;
    int y = 226;

    render_panel(170, 120, 620, 300);
    render_text_centered(170, 1.6f, COL_WHITE, sc.heading);

    upd_message(msg, sizeof(msg));
    n = update_screen_wrap(msg[0] ? msg : sc.fallback, lines);
    for (i = 0; i < n; i++, y += 26)
        render_text_centered(y, 1.0f, COL_TEXT, lines[i]);

    if (update_init_failed)
        render_text_centered(y + 4, 0.9f, COL_ERROR, "The updater could not start.");

    if (sc.show_tag) {
        upd_latest_tag(tag, sizeof(tag));
        if (tag[0]) {
            snprintf(buf, sizeof(buf), "Release %s", tag);
            render_text_centered(318, 0.9f, COL_GOLD, buf);
        }
    }
    if (sc.show_progress) {
        pct = upd_progress(&done, &total);
        if (pct < 0) {
            /* unknown length: show what has arrived rather than a bar that cannot move */
            snprintf(buf, sizeof(buf), "%u KB", (unsigned)(done / 1024u));
            render_text_centered(348, 0.9f, COL_TEXT, buf);
        } else {
            render_bar(330, 336, 300, 14, (float)pct / 100.0f, COL_GOLD);
            snprintf(buf, sizeof(buf), "%u / %u KB", (unsigned)(done / 1024u),
                     (unsigned)(total / 1024u));
            render_text_centered(372, 0.8f, COL_HINT, buf);
        }
    }

    if (sc.confirm_hint)
        snprintf(buf, sizeof(buf), "%s: %s   %s: %s", input_confirm_name(), sc.confirm_hint,
                 input_back_name(), sc.back_hint);
    else
        snprintf(buf, sizeof(buf), "%s: %s", input_back_name(), sc.back_hint);
    render_text_centered(402, 0.8f, COL_HINT, buf);
    render_text(12, SCREEN_H - 12, 0.7f, COL_HINT, "v" SF_VERSION);
}

void app_frame(void)
{
    input_poll();
    unsigned held = input_held(), pressed = input_pressed();

    switch (state) {
    case ST_TITLE:  update_title(pressed); break;
    case ST_PLAY:   update_play(held, pressed); break;
    case ST_PAUSE:  update_pause(pressed); break;
    case ST_OVER:   update_over(pressed); break;
    case ST_UPDATE: update_updates(pressed); break;
    }

    render_begin();
    switch (state) {
    case ST_TITLE:  draw_title(); break;
    case ST_PLAY:   render_game(&game, best, 1); break;
    case ST_PAUSE:  draw_pause(); break;
    case ST_OVER:   draw_over(); break;
    case ST_UPDATE: draw_update(); break;
    }
    render_end();
}
