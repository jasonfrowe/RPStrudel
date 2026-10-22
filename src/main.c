/* RPStrudel: Strudel-style live coding for the Picocomputer's OPL2.
 *
 * Milestone 2 plays a Strudel program through the real pattern engine,
 * shows the code, and lights each token as it plays. The editor comes in
 * milestone 3; for now the program is the one below. */

#include <rp6502.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clock.h"
#include "hidkeys.h"
#include "opl.h"
#include "patches.h"
#include "pattern.h"
#include "player.h"
#include "prog.h"
#include "screen.h"
#include "xram.h"

static const char program[] =
    "// RPStrudel: Strudel patterns on the OPL2\n"
    "setcpm(30)\n"
    "\n"
    "$: s(\"bd(3,8), [~ sd]*2\")\n"
    "$: s(\"hh*8\").gain(\"[.5 1]*4\")\n"
    "$: note(\"<[c2 c3]*4 [ab1 ab2]*4>\").s(\"gm_synth_bass_1\")\n"
    "$: note(\"<c4 eb4 g4 [bb4 c5]>(3,8)\").s(\"gm_epiano1\").gain(.7)\n";

#define CODE_X 2
#define CODE_Y 3
#define CODE_ROWS 28
#define CODE_COLS (TEXT_COLS - CODE_X - 1)
#define BAR_X 13
#define BAR_Y 33
#define BAR_W 64
#define TEMPO_Y 35
#define VOICE_Y 38
#define METER_X 24
#define METER_W 32
#define ERROR_Y 49
#define TIMER_Y 52
#define HILITE_FRAMES 6
#define GLYPH_BLOCK 0xDB /* code page 437 full block */
#define GLYPH_SHADE 0xB0 /* light shade */

static keyboard_t kb, kb_prev;
static uint8_t hilite[PAT_ATOMS]; /* frames left per atom */
static uint8_t meter[OPL_VOICES];
static uint8_t shown_patch[OPL_VOICES];
static uint16_t line_start[CODE_ROWS + 1];
static uint8_t line_count;
static bool loaded;
static const char *shown_error = (const char *)1;
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

static char *u16_str(char *end, uint16_t v)
{
    *--end = 0;
    do {
        *--end = '0' + v % 10;
        v /= 10;
    } while (v);
    return end;
}

static void draw_u16(uint8_t x, uint8_t y, uint16_t v, uint8_t width, uint8_t fg)
{
    char buf[8];
    char *s = u16_str(buf + sizeof(buf), v);
    uint8_t len = (uint8_t)(buf + sizeof(buf) - 1 - s);
    if (len < width)
        screen_fill(x, y, width - len, ' ', fg, COL_BLACK);
    screen_text(x + (len < width ? width - len : 0), y, s, fg, COL_BLACK);
}

/* A tenths value as "30.0". */
static void draw_tenths(uint8_t x, uint8_t y, uint16_t v, uint8_t fg)
{
    draw_u16(x, y, v / 10, 3, fg);
    char frac[3] = {'.', (char)('0' + v % 10), 0};
    screen_text(x + 3, y, frac, fg, COL_BLACK);
}

/* ---- code ---- */

static void split_lines(void)
{
    line_count = 0;
    uint16_t len = sizeof(program) - 1;
    uint16_t at = 0;
    while (at < len && line_count < CODE_ROWS) {
        line_start[line_count++] = at;
        while (at < len && program[at] != '\n')
            at++;
        at++;
    }
    /* one past each line's newline, so a line ends at line_start[i+1] - 1 */
    line_start[line_count] = at;
}

/* Where a position in the code is on the screen. */
static bool code_xy(uint16_t pos, uint8_t *x, uint8_t *y)
{
    for (uint8_t i = 0; i < line_count; i++) {
        if (pos < line_start[i + 1]) {
            uint16_t col = pos - line_start[i];
            if (col >= CODE_COLS)
                return false;
            *x = CODE_X + (uint8_t)col;
            *y = CODE_Y + i;
            return true;
        }
    }
    return false;
}

static void draw_atom(uint8_t a, bool lit)
{
    uint8_t x, y;
    const pat_atom_t *at = &pat_atoms[a];
    if (!code_xy(at->pos, &x, &y))
        return;
    uint8_t len = at->len;
    if (x + len > CODE_X + CODE_COLS)
        len = CODE_X + CODE_COLS - x;
    screen_color(x, y, len, lit ? COL_BLACK : COL_YELLOW, lit ? COL_HILITE : COL_BLACK);
}

static void draw_code(void)
{
    for (uint8_t i = 0; i < line_count; i++) {
        uint16_t from = line_start[i];
        uint16_t to = line_start[i + 1] - 1;
        uint8_t y = CODE_Y + i;
        screen_fill(CODE_X, y, CODE_COLS, ' ', COL_WHITE, COL_BLACK);
        bool comment = false;
        for (uint16_t p = from; p < to && p - from < CODE_COLS; p++) {
            char c = program[p];
            if (c == '/' && p + 1 < to && program[p + 1] == '/')
                comment = true;
            char s[2] = {c, 0};
            screen_text(CODE_X + (uint8_t)(p - from), y, s, comment ? COL_DARKGREY : COL_WHITE, COL_BLACK);
        }
    }
    if (loaded)
        for (uint8_t a = 0; a < pat_atom_count; a++)
            draw_atom(a, false);
}

/* ---- status ---- */

static void draw_tempo(void)
{
    uint16_t cpm10 = clock_cpm10();
    screen_text(2, TEMPO_Y, "cpm", COL_GREY, COL_BLACK);
    draw_tenths(6, TEMPO_Y, cpm10, COL_CYAN);
    screen_text(14, TEMPO_Y, "bpm", COL_GREY, COL_BLACK);
    draw_tenths(18, TEMPO_Y, cpm10 * 4, COL_CYAN); /* 4 beats per cycle */
    screen_text(26, TEMPO_Y, "(4 beats per cycle)", COL_DARKGREY, COL_BLACK);
}

/* An error with a caret under where it is in the code. */
static void draw_error(void)
{
    const char *msg = loaded ? player_error : pat_error;
    if (msg == shown_error)
        return;
    shown_error = msg;
    screen_fill(0, ERROR_Y, TEXT_COLS, ' ', COL_WHITE, COL_BLACK);
    screen_fill(0, ERROR_Y + 1, TEXT_COLS, ' ', COL_WHITE, COL_BLACK);
    if (!msg)
        return;
    screen_text(2, ERROR_Y, "error:", COL_RED, COL_BLACK);
    screen_text(9, ERROR_Y, msg, COL_WHITE, COL_BLACK);
    uint8_t x, y;
    uint16_t pos = loaded ? player_error_pos : prog_error_pos;
    if (code_xy(pos, &x, &y)) {
        screen_color(x, y, 1, COL_WHITE, COL_RED);
        screen_text(2, ERROR_Y + 1, "(marked in red in the code)", COL_DARKGREY, COL_BLACK);
    }
}

static void draw_static(void)
{
    screen_fill(0, 0, TEXT_COLS, ' ', COL_WHITE, 24);
    screen_text(2, 0, "RPStrudel", COL_WHITE, 24);
    screen_text(14, 0, "milestone 2: mini-notation", COL_CYAN, 24);

    screen_text(2, 2, "code", COL_GREY, COL_BLACK);
    draw_code();

    screen_text(2, BAR_Y, "cycle", COL_GREY, COL_BLACK);
    draw_tempo();

    screen_text(2, VOICE_Y - 1, "voices", COL_GREY, COL_BLACK);
    for (uint8_t v = 0; v < OPL_VOICES; v++) {
        char label[2] = {(char)('1' + v), 0};
        screen_text(2, VOICE_Y + v, label, COL_DARKGREY, COL_BLACK);
    }

    screen_text(2, TIMER_Y, "timer", COL_GREY, COL_BLACK);
    screen_text(14, TIMER_Y, "ms per second (should be 1000)", COL_DARKGREY, COL_BLACK);
    screen_text(2, TIMER_Y + 1, "query", COL_GREY, COL_BLACK);
    screen_text(14, TIMER_Y + 1, "ms per cycle, now and most", COL_DARKGREY, COL_BLACK);
    screen_text(2, TIMER_Y + 2, "stack", COL_GREY, COL_BLACK);
    screen_text(14, TIMER_Y + 2, "bytes never used", COL_DARKGREY, COL_BLACK);

    screen_fill(0, TEXT_ROWS - 1, TEXT_COLS, ' ', COL_WHITE, COL_PANEL);
    screen_text(2, TEXT_ROWS - 1, "SPACE play/stop   - = tempo   ESC hush", COL_GREY, COL_PANEL);
}

static void draw_state(void)
{
    bool run = clock_running();
    screen_text(TEXT_COLS - 10, 0, run ? "  PLAYING" : "  STOPPED", run ? COL_GREEN : COL_RED, 24);
}

static void draw_position(void)
{
    uint16_t cycle, pos;
    clock_position(&cycle, &pos);
    draw_u16(8, BAR_Y, cycle, 4, COL_WHITE);
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
        uint8_t y = VOICE_Y + v;
        uint8_t p = player_voice_patch[v];
        if (p != shown_patch[v]) {
            shown_patch[v] = p;
            screen_fill(4, y, METER_X - 5, ' ', COL_WHITE, COL_BLACK);
            screen_text(4, y, patch_name(p), COL_GREY, COL_BLACK);
        }
        if (hits & (1u << v))
            meter[v] = METER_W;
        else if (meter[v] > 0)
            meter[v] -= meter[v] > 3 ? 3 : meter[v];
        uint8_t n = meter[v];
        uint8_t color = PATCH_IS_DRUM(p) ? COL_GREEN : COL_MAGENTA;
        screen_fill(METER_X, y, n, GLYPH_BLOCK, color, COL_BLACK);
        screen_fill(METER_X + n, y, METER_W - n, ' ', COL_WHITE, COL_BLACK);
    }
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

/* ---- control ---- */

void clock_fill(cycle_events_t *out, uint16_t cycle)
{
    uint16_t t0 = clock_ms();
    player_fill(out, cycle);
    fill_ms = clock_ms() - t0;
    if (fill_ms > fill_ms_max)
        fill_ms_max = fill_ms;
}

static bool key_pressed(uint8_t code)
{
    return KEYBOARD_PRESSED(kb.keys, code) && !KEYBOARD_PRESSED(kb_prev.keys, code);
}

static void stop(void)
{
    clock_stop();
    for (uint8_t i = 0; i < pat_atom_count; i++)
        if (hilite[i])
            hilite[i] = 1; /* unlit on the next frame */
}

static void start(void)
{
    if (loaded)
        clock_start();
}

static void handle_keys(void)
{
    kb_prev = kb;
    xram0_read(&kb, XRAM_KEYBOARD, sizeof(kb));
    if (KEYBOARD_PRESSED(kb.keys, KEYBOARD_NO_KEY))
        return;

    if (key_pressed(HID_KEY_SPACE)) {
        if (clock_running())
            stop();
        else
            start();
        draw_state();
    }
    if (key_pressed(HID_KEY_ESCAPE)) {
        stop();
        draw_state();
    }
    if (key_pressed(HID_KEY_MINUS) || key_pressed(HID_KEY_EQUAL)) {
        uint16_t c = clock_cpm10();
        if (key_pressed(HID_KEY_MINUS))
            c = c > 50 ? c - 10 : 40;
        else
            c = c < 990 ? c + 10 : 1000;
        clock_set_cpm10(c);
        draw_tempo();
    }
}

int main(void)
{
    stack_mark();
    screen_init();
    xreg_ria_keyboard(XRAM_KEYBOARD);
    opl_init();
    clock_init();
    split_lines();
    loaded = player_load(program, sizeof(program) - 1);
    memset(shown_patch, 0xFE, sizeof(shown_patch));
    draw_static();
    draw_error();
    start();
    draw_state();

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
        update_hilites();
        draw_voices();
        draw_position();
        draw_error();

        if (frames >= 60) {
            uint16_t now = clock_ms();
            ms_per_second = now - ms_mark;
            ms_mark = now;
            ms_per_second = (uint16_t)((uint32_t)ms_per_second * 60 / frames);
            frames = 0;
            draw_u16(8, TIMER_Y, ms_per_second, 5, COL_WHITE);
            draw_u16(8, TIMER_Y + 1, fill_ms, 5, COL_WHITE);
            draw_u16(46, TIMER_Y + 1, fill_ms_max, 5, COL_WHITE);
            draw_u16(8, TIMER_Y + 2, stack_free(), 5, COL_WHITE);
        }
    }
}
