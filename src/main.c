/* RPStrudel: Strudel-style live coding for the Picocomputer's OPL2.
 *
 * Edit the code, press Ctrl+Enter, and it plays from the next cycle. Each
 * token lights up as it sounds. */

#include <rp6502.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clock.h"
#include "editbuf.h"
#include "editor.h"
#include "files.h"
#include "hidkeys.h"
#include "opl.h"
#include "patches.h"
#include "pattern.h"
#include "player.h"
#include "prog.h"
#include "screen.h"
#include "text.h"
#include "xram.h"

static const char demo[] =
    "// RPStrudel: Strudel patterns on the OPL2\n"
    "// Ctrl+Enter plays your changes, Ctrl+. stops\n"
    "setcpm(30)\n"
    "\n"
    "$: s(\"bd(3,8,<0 2>), [~ sd]*2\").every(4, x => x.ply(2))\n"
    "$: s(\"hh*8?\").gain(\"[.5 1]*4\")\n"
    "$: n(\"<0 3 5 [7 10]>*2\").scale(\"C2:minor\").s(\"gm_synth_bass_1\")\n"
    "$: n(\"0 [2 4] <3 6> 7\").scale(\"C4:minor\").s(\"gm_epiano1\")\n"
    "  .off(0.25, x => x.add(note(12))).gain(.6)\n";

/* Screen layout */
#define TITLE_BG 24
#define EDIT_X 1
#define EDIT_Y 2
#define EDIT_COLS 78
#define EDIT_ROWS 43
#define STATUS_Y 46
#define ERROR_Y 47
#define BAR_Y 49
#define BAR_X 8
#define BAR_W 64
#define VOICE_Y 51
#define METER_W 16
#define STATS_Y 57
#define HILITE_FRAMES 6
#define MESSAGE_FRAMES 150
#define GLYPH_BLOCK 0xDB /* code page 437 full block */
#define GLYPH_SHADE 0xB0 /* light shade */
#define NO_POS 0xFFFF
#define RIA_READY_RX_BIT 0x40

static keyboard_t kb;
static uint8_t hilite[PAT_ATOMS]; /* frames left per atom */
static uint8_t meter[OPL_VOICES];
static uint8_t shown_patch[OPL_VOICES];
static const char *shown_error = (const char *)1;
static uint16_t error_pos = NO_POS;
static uint8_t message_frames;
static uint16_t ms_per_second;
static uint16_t fill_ms, fill_ms_max;

/* Stack check: the soft stack grows down from $FF00 toward the end of the
 * program's data. At boot the gap is filled with a marker; the marker left
 * at the bottom is the stack never used. */
extern char __heap_start[];
#define STACK_TOP 0xFF00
#define STACK_MARK 0xA5

static void stack_mark(void)
{
    /* the stack holds little this early: leave its top 256 bytes alone */
    for (char *p = __heap_start; p < (char *)(STACK_TOP - 256); p++)
        *p = (char)STACK_MARK;
}

static uint16_t stack_free(void)
{
    const char *p = __heap_start;
    while (p < (char *)STACK_TOP && *p == (char)STACK_MARK)
        p++;
    return (uint16_t)(p - __heap_start);
}

/* ---- numbers ---- */

static char *u16_str(char *end, uint16_t v)
{
    *--end = 0;
    do {
        *--end = '0' + v % 10;
        v /= 10;
    } while (v);
    return end;
}

static void draw_u16(uint8_t x, uint8_t y, uint16_t v, uint8_t width, uint8_t fg, uint8_t bg)
{
    char buf[8];
    char *s = u16_str(buf + sizeof(buf), v);
    uint8_t len = (uint8_t)(buf + sizeof(buf) - 1 - s);
    if (len < width)
        screen_fill(x, y, width - len, ' ', fg, bg);
    screen_text(x + (len < width ? width - len : 0), y, s, fg, bg);
}

/* A tenths value as "30.0". */
static void draw_tenths(uint8_t x, uint8_t y, uint16_t v, uint8_t fg, uint8_t bg)
{
    draw_u16(x, y, v / 10, 3, fg, bg);
    char frac[3] = {'.', (char)('0' + v % 10), 0};
    screen_text(x + 3, y, frac, fg, bg);
}

/* ---- highlights ---- */

/* A token's cells, recoloured in one go (a token never spans lines). */
static void draw_atom(uint8_t a, bool lit)
{
    pat_atom_t atom;
    pat_atom_get(a, &atom);
    const pat_atom_t *at = &atom;
    uint8_t x, y;
    if (!at->len || !editor_xy(at->pos, &x, &y))
        return;
    uint8_t len = at->len;
    if (x + len > EDIT_X + EDIT_COLS)
        len = EDIT_X + EDIT_COLS - x;
    screen_color(x, y, len, lit ? COL_BLACK : EDITOR_VALUE, lit ? COL_HILITE : COL_BLACK);
    /* the cursor and an error mark stay as they were */
    uint16_t cur = edit_cursor();
    if (cur >= at->pos && cur < at->pos + len)
        screen_color(x + (uint8_t)(cur - at->pos), y, 1, COL_BLACK, COL_WHITE);
    if (error_pos >= at->pos && error_pos < at->pos + len)
        screen_color(x + (uint8_t)(error_pos - at->pos), y, 1, COL_WHITE, COL_RED);
}

static void update_hilites(void)
{
    uint8_t s;
    while ((s = clock_next_hit()) != 0xFF) {
        if (s < pat_atom_count) {
            if (!hilite[s])
                draw_atom(s, true);
            hilite[s] = HILITE_FRAMES;
        }
    }
    for (uint8_t i = 0; i < pat_atom_count; i++) {
        if (hilite[i] && --hilite[i] == 0)
            draw_atom(i, false);
    }
}

/* Keep positions on the same text as it is edited. */
void editor_on_edit(uint16_t pos, int8_t delta)
{
    files_modified = true;
    for (uint8_t i = 0; i < pat_atom_count; i++) {
        pat_atom_t at;
        pat_atom_get(i, &at);
        if (!at.len)
            continue;
        if (delta > 0) {
            if (at.pos >= pos)
                at.pos += (uint16_t)delta;
            else if (pos < at.pos + at.len)
                at.len = 0; /* typed inside it */
            else
                continue;
        } else {
            if (at.pos > pos)
                at.pos -= (uint16_t)-delta;
            else if (pos < at.pos + at.len)
                at.len = 0; /* part of it deleted */
            else
                continue;
        }
        if (!at.len)
            hilite[i] = 0;
        pat_atom_put(i, &at);
    }
    if (error_pos != NO_POS) {
        if (delta > 0 && error_pos >= pos)
            error_pos += (uint16_t)delta;
        else if (delta < 0 && error_pos > pos)
            error_pos -= (uint16_t)-delta;
        else if (delta < 0 && error_pos == pos)
            error_pos = NO_POS;
        editor_mark(error_pos);
    }
}

/* New text from a file: the playing program's positions no longer point
 * at anything, so nothing lights up until the next Ctrl+Enter. */
void ui_text_replaced(void)
{
    for (uint8_t i = 0; i < pat_atom_count; i++) {
        pat_atom_t at;
        pat_atom_get(i, &at);
        at.len = 0;
        pat_atom_put(i, &at);
    }
    memset(hilite, 0, sizeof hilite);
    error_pos = NO_POS;
    editor_mark(NO_POS);
}

/* ---- status ---- */

void ui_message(const char *msg, uint8_t fg)
{
    screen_fill(44, STATUS_Y, TEXT_COLS - 44, ' ', fg, COL_PANEL);
    screen_text(TEXT_COLS - 1 - (uint8_t)strlen(msg), STATUS_Y, msg, fg, COL_PANEL);
    message_frames = MESSAGE_FRAMES;
}

static void draw_error(void)
{
    const char *msg = player_error;
    if (msg == shown_error)
        return;
    shown_error = msg;
    screen_fill(0, ERROR_Y, TEXT_COLS, ' ', COL_WHITE, COL_BLACK);
    if (!msg) {
        error_pos = NO_POS;
        editor_mark(NO_POS);
        return;
    }
    screen_text(2, ERROR_Y, "error:", COL_RED, COL_BLACK);
    screen_text(9, ERROR_Y, msg, COL_WHITE, COL_BLACK);
    error_pos = player_error_pos;
    editor_mark(error_pos);
    if (error_pos < edit_length()) {
        /* where, as line and column (also marked red in the code) */
        uint16_t line = 1, ls = 0;
        for (uint16_t i = 0; i < error_pos; i++)
            if (text_at(i) == '\n') {
                line++;
                ls = i + 1;
            }
        screen_text(TEXT_COLS - 22, ERROR_Y, "line", COL_DARKGREY, COL_BLACK);
        draw_u16(TEXT_COLS - 17, ERROR_Y, line, 1, COL_WHITE, COL_BLACK);
        screen_text(TEXT_COLS - 12, ERROR_Y, "col", COL_DARKGREY, COL_BLACK);
        draw_u16(TEXT_COLS - 8, ERROR_Y, error_pos - ls + 1, 1, COL_WHITE, COL_BLACK);
    }
}

static void draw_title(void)
{
    bool run = clock_running();
    screen_text(TEXT_COLS - 10, 0, run ? "  PLAYING" : "  STOPPED", run ? COL_GREEN : COL_RED, TITLE_BG);
    uint16_t cpm10 = clock_cpm10();
    screen_text(36, 0, "cpm", COL_GREY, TITLE_BG);
    draw_tenths(40, 0, cpm10, COL_CYAN, TITLE_BG);
    screen_text(48, 0, "bpm", COL_GREY, TITLE_BG);
    draw_tenths(52, 0, cpm10 * 4, COL_CYAN, TITLE_BG); /* 4 beats per cycle */
}

static void draw_status(void)
{
    screen_text(2, STATUS_Y, "Ln", COL_GREY, COL_PANEL);
    draw_u16(5, STATUS_Y, editor_line(), 4, COL_WHITE, COL_PANEL);
    screen_text(11, STATUS_Y, "Col", COL_GREY, COL_PANEL);
    draw_u16(15, STATUS_Y, editor_col(), 3, COL_WHITE, COL_PANEL);
    screen_fill(19, STATUS_Y, 24, ' ', COL_WHITE, COL_PANEL);
    screen_text(20, STATUS_Y, files_name(), COL_GREY, COL_PANEL);
    if (files_modified)
        screen_text(20 + (uint8_t)strlen(files_name()), STATUS_Y, "*", COL_YELLOW, COL_PANEL);
}

static void draw_static(void)
{
    screen_fill(0, 0, TEXT_COLS, ' ', COL_WHITE, TITLE_BG);
    screen_text(2, 0, "RPStrudel", COL_WHITE, TITLE_BG);
    draw_title();
    screen_fill(0, STATUS_Y, TEXT_COLS, ' ', COL_WHITE, COL_PANEL);

    screen_text(2, BAR_Y, "cycle", COL_GREY, COL_BLACK);
    for (uint8_t v = 0; v < OPL_VOICES; v++) {
        char label[2] = {(char)('1' + v), 0};
        screen_text(v < 5 ? 2 : 42, VOICE_Y + v % 5, label, COL_DARKGREY, COL_BLACK);
    }
    screen_text(2, STATS_Y, "timer", COL_GREY, COL_BLACK);
    screen_text(14, STATS_Y, "ms/s", COL_DARKGREY, COL_BLACK);
    screen_text(20, STATS_Y, "query", COL_GREY, COL_BLACK);
    screen_text(32, STATS_Y, "ms, max", COL_DARKGREY, COL_BLACK);
    screen_text(46, STATS_Y, "stack", COL_GREY, COL_BLACK);
    screen_text(58, STATS_Y, "free", COL_DARKGREY, COL_BLACK);
    screen_text(64, STATS_Y, "late", COL_GREY, COL_BLACK);

    screen_fill(0, TEXT_ROWS - 1, TEXT_COLS, ' ', COL_WHITE, COL_PANEL);
    screen_text(2, TEXT_ROWS - 1, "Ctrl+Enter play/update  Ctrl+. or Esc stop  Ctrl+S save  Ctrl+O open", COL_GREY,
                COL_PANEL);
}

static void draw_position(void)
{
    uint16_t cycle, pos;
    clock_position(&cycle, &pos);
    draw_u16(2, BAR_Y + 1, cycle, 5, COL_WHITE, COL_BLACK);
    uint8_t at = (uint8_t)(pos >> 10); /* 0-63 */
    RIA.addr0 = XRAM_TEXT + ((unsigned)BAR_Y * TEXT_COLS + BAR_X) * sizeof(text_cell_t);
    RIA.step0 = 1;
    for (uint8_t i = 0; i < BAR_W; i++) {
        bool beat = (i & 15) == 0;
        RIA.rw0 = i <= at ? GLYPH_BLOCK : GLYPH_SHADE;
        RIA.rw0 = i == at ? COL_WHITE : beat ? COL_CYAN : COL_BLUE;
        RIA.rw0 = COL_BLACK;
    }
}

static void draw_voices(void)
{
    uint16_t hits = clock_take_voice_hits();
    for (uint8_t v = 0; v < OPL_VOICES; v++) {
        uint8_t x = v < 5 ? 4 : 44;
        uint8_t y = VOICE_Y + v % 5;
        uint8_t p = player_voice_patch[v];
        if (p != shown_patch[v]) {
            shown_patch[v] = p;
            screen_fill(x, y, 18, ' ', COL_WHITE, COL_BLACK);
            screen_text(x, y, patch_name(p), COL_GREY, COL_BLACK);
        }
        if (hits & (1u << v))
            meter[v] = METER_W;
        else if (meter[v] > 0)
            meter[v]--;
        uint8_t n = meter[v];
        uint8_t color = player_voice_drum[v] ? COL_GREEN : COL_MAGENTA;
        screen_fill(x + 18, y, n, GLYPH_BLOCK, color, COL_BLACK);
        screen_fill(x + 18 + n, y, METER_W - n, ' ', COL_WHITE, COL_BLACK);
    }
}

/* ---- control ---- */

void clock_fill(uint16_t cycle)
{
    uint16_t t0 = clock_ms();
    player_fill(cycle);
    fill_ms = clock_ms() - t0;
    if (fill_ms > fill_ms_max)
        fill_ms_max = fill_ms;
}

static void stop(void)
{
    clock_stop();
    for (uint8_t i = 0; i < pat_atom_count; i++)
        if (hilite[i])
            hilite[i] = 1; /* unlit on the next frame */
    draw_title();
}

/* Ctrl+Enter: the new code plays from the next cycle, or the old code
 * carries on if the new code has an error. */
static void evaluate(void)
{
    uint16_t t0 = clock_ms();
    bool ok = player_load(edit_length());
    shown_error = (const char *)1; /* show it, or clear it */
    if (!ok) {
        ui_message("not changed: see the error", COL_RED);
        return;
    }
    memset(hilite, 0, sizeof hilite);
    editor_dirty();
    fill_ms_max = 0;
    if (clock_running()) {
        if (clock_refill())
            player_rewind();
    } else {
        clock_start();
    }
    char msg[32] = "updated in ";
    char num[8];
    strcat(msg, u16_str(num + sizeof num, clock_ms() - t0));
    strcat(msg, " ms");
    ui_message(msg, COL_GREEN);
    draw_title();
}

/* Ctrl+D: writes the screen's text to the console, for test scripts. */
static void console_out(char c)
{
    while (!(RIA.ready & 0x80))
        ;
    RIA.tx = (uint8_t)c;
}

static void dump_screen(void)
{
    for (uint8_t y = 0; y < TEXT_ROWS; y++) {
        uint8_t last = 0;
        for (uint8_t x = 0; x < TEXT_COLS; x++) {
            RIA.addr0 = XRAM_TEXT + ((unsigned)y * TEXT_COLS + x) * sizeof(text_cell_t);
            if (RIA.rw0 != ' ')
                last = x + 1;
        }
        for (uint8_t x = 0; x < last; x++) {
            RIA.addr0 = XRAM_TEXT + ((unsigned)y * TEXT_COLS + x) * sizeof(text_cell_t);
            uint8_t c = RIA.rw0;
            console_out(c >= 0x20 && c < 0x7F ? (char)c : '#');
        }
        console_out('\r');
        console_out('\n');
    }
}

/* The Enter a Ctrl+Enter types arrives just after it, if at all (a key
 * that only changed the key bitmap types nothing), so it is only looked
 * for for a moment. */
static uint8_t enters_to_swallow, swallow_left, swallow_vsync;
#define SWALLOW_FRAMES 30 /* real frames, counted by vsync */

static void handle_keys(void)
{
    xram0_read(&kb, XRAM_KEYBOARD, sizeof(kb));
    bool ctrl = KEYBOARD_PRESSED(kb.keys, HID_KEY_CONTROL_LEFT) || KEYBOARD_PRESSED(kb.keys, HID_KEY_CONTROL_RIGHT);
    uint8_t events = clock_take_keys();
    uint8_t ce = clock_take_ctrl_enters();
    uint8_t passed = (uint8_t)(RIA.vsync - swallow_vsync);
    swallow_vsync = RIA.vsync;
    if (ce) {
        enters_to_swallow += ce;
        swallow_left = SWALLOW_FRAMES;
    } else if (passed >= swallow_left) {
        swallow_left = 0;
        enters_to_swallow = 0;
    } else {
        swallow_left -= passed;
    }

    if (files_active()) {
        /* the save prompt or the open list has the keys */
        while (RIA.ready & RIA_READY_RX_BIT)
            files_key(RIA.rx);
        files_end_keys();
        if (events & CLOCK_KEY_ESC)
            files_cancel();
        if (!files_active())
            draw_status();
        return;
    }

    /* Text first (it comes from the console, which knows the keyboard
     * layout), so what was typed before a Ctrl+Enter is in what plays. */
    while (RIA.ready & RIA_READY_RX_BIT) {
        uint8_t c = RIA.rx;
        if (c == '\r') {
            if (enters_to_swallow) {
                enters_to_swallow--;
                continue; /* the Enter of a Ctrl+Enter */
            }
            if (ctrl)
                continue; /* its key repeat */
        }
        editor_key(c, ctrl);
    }
    editor_end_keys();

    if (events & CLOCK_KEY_EVAL)
        evaluate();
    if (events & (CLOCK_KEY_STOP | CLOCK_KEY_ESC))
        stop();
    if (events & CLOCK_KEY_DUMP)
        dump_screen();
    if (events & CLOCK_KEY_SAVE)
        files_save_prompt();
    if (events & CLOCK_KEY_OPEN)
        files_open_list();
}

int main(void)
{
    stack_mark();
    screen_init();
    xreg_ria_keyboard(XRAM_KEYBOARD);
    opl_init();
    clock_init();
    memset(shown_patch, 0xFE, sizeof(shown_patch));
    draw_static();
    editor_init(EDIT_X, EDIT_Y, EDIT_COLS, EDIT_ROWS, demo);
    editor_set_lit(hilite);
    files_init(EDIT_X, EDIT_Y, EDIT_COLS, EDIT_ROWS, STATUS_Y);
    evaluate();

    uint8_t vs = RIA.vsync;
    uint8_t frames = 0;
    uint16_t ms_mark = clock_ms();
    while (1) {
        while (RIA.vsync == vs)
            clock_service();
        /* count every frame, also ones missed while a cycle was filled */
        frames += (uint8_t)(RIA.vsync - vs);
        vs = RIA.vsync;

        handle_keys();
        clock_service(); /* also when the frame's work ran long */
        if (!files_active() && editor_draw())
            draw_status();
        update_hilites();
        draw_voices();
        draw_position();
        draw_error();
        if (message_frames && --message_frames == 0)
            screen_fill(44, STATUS_Y, TEXT_COLS - 44, ' ', COL_WHITE, COL_PANEL);

        if (frames >= 60) {
            uint16_t now = clock_ms();
            ms_per_second = (uint16_t)((uint32_t)(now - ms_mark) * 60 / frames);
            ms_mark = now;
            frames = 0;
            draw_u16(8, STATS_Y, ms_per_second, 5, COL_WHITE, COL_BLACK);
            draw_u16(26, STATS_Y, fill_ms, 5, COL_WHITE, COL_BLACK);
            draw_u16(39, STATS_Y, fill_ms_max, 5, COL_WHITE, COL_BLACK);
            draw_u16(52, STATS_Y, stack_free(), 5, COL_WHITE, COL_BLACK);
            draw_u16(69, STATS_Y, clock_late(), 5, COL_WHITE, COL_BLACK);
        }
    }
}
