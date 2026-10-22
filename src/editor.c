#include "editor.h"

#include <rp6502.h>

#include "editbuf.h"
#include "pattern.h"
#include "screen.h"
#include "text.h"
#include "xram.h"

#define MAX_COLS 80
#define MAX_ROWS 60
#define NO_POS 0xFFFF
#define INDENT 2
#define LINE_MAX 160 /* columns that can be shown, also when scrolled right */
#define ESC_FRAMES 3 /* an Escape with nothing after it this long is the Escape key */

static uint8_t wx, wy, wcols, wrows;
static uint16_t top;      /* where the first line in view starts */
static uint16_t top_line; /* its line number, from 1 */
static uint16_t left;     /* first column in view */
static uint16_t want_col; /* column up and down aim for */
static uint16_t mark = NO_POS;
static const uint8_t *lit; /* frames left per atom: lit when not 0 */

/* Where each line in view starts, as last drawn; row_start[wrows] is the
 * line after the window. NO_POS past the end of the text. Kept right
 * through edits that stay inside a line, so most keys redraw a row or two
 * instead of the whole window. */
static uint16_t row_start[MAX_ROWS + 1];
static bool need_all;
#define DIRTY_MAX 4
static uint8_t dirty_rows[DIRTY_MAX], dirty_count;
static uint16_t cur_line, cur_col;

static uint8_t esc_state; /* 0 none, 1 after ESC, 2 in ESC [, 3 after ESC O */
static uint8_t esc_param, esc_which, esc_age;

/* ---- lines ---- */

static uint16_t line_start(uint16_t pos)
{
    while (pos > 0 && text_at(pos - 1) != '\n')
        pos--;
    return pos;
}

static uint16_t line_end(uint16_t pos)
{
    uint16_t len = edit_length();
    while (pos < len && text_at(pos) != '\n')
        pos++;
    return pos;
}

/* The row in view a position is on, or 0xFF. Needs a good row_start. */
static uint8_t row_of(uint16_t pos)
{
    for (uint8_t r = 0; r < wrows && row_start[r] != NO_POS; r++) {
        uint16_t next = row_start[r + 1];
        if (pos >= row_start[r] && (next == NO_POS || pos < next))
            return r;
    }
    return 0xFF;
}

static void dirty_row(uint8_t r)
{
    if (need_all || r == 0xFF)
        return;
    for (uint8_t i = 0; i < dirty_count; i++)
        if (dirty_rows[i] == r)
            return;
    if (dirty_count < DIRTY_MAX)
        dirty_rows[dirty_count++] = r;
    else
        need_all = true;
}

/* Scrolls so the cursor is in view. */
static void follow_cursor(void)
{
    uint16_t cur = edit_cursor();
    uint16_t cl = line_start(cur);
    if (cl < top) {
        /* up: count the lines passed */
        for (uint16_t p = cl; p < top; p++)
            if (text_at(p) == '\n')
                top_line--;
        top = cl;
        need_all = true;
    } else {
        uint8_t n;
        if (!need_all && (n = row_of(cur)) != 0xFF) {
            /* in view */
        } else {
            n = 0;
            for (uint16_t p = top; p < cl && n < wrows; p++)
                if (text_at(p) == '\n')
                    n++;
        }
        while (n >= wrows) {
            top = line_end(top) + 1;
            top_line++;
            n--;
            need_all = true;
        }
    }
    uint16_t col = cur - cl;
    if (col < left) {
        left = col;
        need_all = true;
    } else if (col >= left + wcols) {
        left = col - wcols + 1;
        need_all = true;
    }
}

/* ---- editing ---- */

/* After an edit inside one line, the lines below it move. */
static void shift_rows(uint16_t pos, int8_t delta)
{
    for (uint8_t r = 0; r <= wrows; r++)
        if (row_start[r] != NO_POS && row_start[r] > pos)
            row_start[r] += (uint16_t)(int16_t)delta;
}

static void insert(char c)
{
    uint16_t pos = edit_cursor();
    if (!edit_insert(c))
        return;
    editor_on_edit(pos, 1);
    if (c == '\n')
        need_all = true;
    else
        shift_rows(pos, 1);
}

/* Removes the character at pos (the cursor is already right for it). */
static void removed(uint16_t pos, char c)
{
    editor_on_edit(pos, -1);
    if (c == '\n')
        need_all = true;
    else
        shift_rows(pos, -1);
}

static void move_to(uint16_t pos, bool keep_col)
{
    edit_set_cursor(pos);
    if (!keep_col)
        want_col = edit_cursor() - line_start(edit_cursor());
}

static void newline(void)
{
    /* keep the current line's indent */
    uint16_t ls = line_start(edit_cursor());
    uint8_t indent = 0;
    while (text_at(ls + indent) == ' ' && ls + indent < edit_cursor() && indent < 40)
        indent++;
    insert('\n');
    while (indent--)
        insert(' ');
}

static void up_down(int8_t lines)
{
    uint16_t cur = edit_cursor();
    while (lines < 0) {
        uint16_t ls = line_start(cur);
        if (ls == 0) {
            cur = 0;
            break;
        }
        cur = ls - 1;
        lines++;
    }
    while (lines > 0) {
        uint16_t le = line_end(cur);
        if (le >= edit_length()) {
            cur = le;
            break;
        }
        cur = le + 1;
        lines--;
    }
    uint16_t ls = line_start(cur), le = line_end(cur);
    uint16_t pos = ls + want_col;
    move_to(pos < le ? pos : le, true);
}

static void sequence_key(uint8_t final, uint8_t param)
{
    uint16_t cur = edit_cursor();
    switch (final) {
    case 'A':
        up_down(-1);
        break;
    case 'B':
        up_down(1);
        break;
    case 'C':
        if (cur < edit_length())
            move_to(cur + 1, false);
        break;
    case 'D':
        if (cur > 0)
            move_to(cur - 1, false);
        break;
    case 'H':
        move_to(line_start(cur), false);
        break;
    case 'F':
        move_to(line_end(cur), false);
        break;
    case '~':
        if (param == 3) {
            char c = text_at(cur);
            if (edit_delete())
                removed(cur, c);
        } else if (param == 5) {
            up_down(-(int8_t)(wrows - 1));
        } else if (param == 6) {
            up_down((int8_t)(wrows - 1));
        } else if (param == 1) {
            move_to(line_start(cur), false);
        } else if (param == 4) {
            move_to(line_end(cur), false);
        }
        break;
    }
}

void editor_key(uint8_t c, bool ctrl)
{
    uint8_t old_row = need_all ? 0xFF : row_of(edit_cursor());

    if (esc_state == 1) {
        esc_state = c == '[' ? 2 : c == 'O' ? 3 : 0;
        esc_param = 0;
        esc_which = 0;
        return; /* Alt+key: nothing yet */
    }
    if (esc_state == 2) {
        if (c >= '0' && c <= '9') {
            if (esc_which == 0)
                esc_param = (uint8_t)(esc_param * 10 + (c - '0'));
            return;
        }
        if (c == ';') {
            esc_which++;
            return;
        }
        esc_state = 0;
        sequence_key(c, esc_param);
        goto moved;
    }
    if (esc_state == 3) {
        esc_state = 0; /* F1-F4 */
        return;
    }

    if (c == 0x1b) {
        esc_state = 1;
        esc_age = 0;
        return;
    }
    if (c == '\r') {
        if (ctrl)
            return; /* Ctrl+Enter is the caller's */
        newline();
    } else if (c == '\t') {
        for (uint8_t i = 0; i < INDENT; i++)
            insert(' ');
    } else if (c == 0x7f || c == 0x08) {
        uint16_t cur = edit_cursor();
        if (cur > 0) {
            char gone = text_at(cur - 1);
            edit_backspace();
            removed(cur - 1, gone);
        }
    } else if (c >= 0x20 && !(ctrl && c == '.')) {
        insert((char)c); /* (Ctrl+. is stop) */
    } else {
        return;
    }
    want_col = edit_cursor() - line_start(edit_cursor());
moved:
    follow_cursor();
    dirty_row(old_row);
    dirty_row(need_all ? 0xFF : row_of(edit_cursor()));
}

void editor_end_keys(void)
{
    /* An Escape alone for a few frames was the Escape key. The rest of a
     * sequence can arrive a frame later on real hardware. */
    if (esc_state == 1 && ++esc_age >= ESC_FRAMES)
        esc_state = 0;
}

/* ---- drawing ---- */

void editor_init(uint8_t x, uint8_t y, uint8_t cols, uint8_t rows, const char *text)
{
    wx = x;
    wy = y;
    wcols = cols > MAX_COLS ? MAX_COLS : cols;
    wrows = rows > MAX_ROWS ? MAX_ROWS : rows;
    edit_clear();
    while (*text)
        edit_insert(*text++);
    edit_set_cursor(0);
    top = left = want_col = 0;
    top_line = 1;
    need_all = true;
}

void editor_reset(void)
{
    edit_set_cursor(0);
    top = left = want_col = 0;
    top_line = 1;
    mark = NO_POS;
    need_all = true;
}

void editor_set_lit(const uint8_t *frames)
{
    lit = frames;
}

void editor_dirty(void)
{
    need_all = true;
}

void editor_mark(uint16_t pos)
{
    if (pos == mark)
        return;
    if (!need_all) {
        dirty_row(row_of(mark));
        dirty_row(row_of(pos));
    }
    mark = pos;
}

static bool is_word(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$';
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

/* Draws the line starting at p on row r (blank when p is NO_POS), with its
 * colours, lit tokens, cursor and mark. Returns where the next starts. */
static uint16_t draw_row(uint8_t r, uint16_t p)
{
    static char line[LINE_MAX];
    static uint8_t fg[MAX_COLS], bg[MAX_COLS];
    uint16_t next = NO_POS;
    uint8_t n = 0;
    if (p != NO_POS)
        n = edit_read_line(p, line, LINE_MAX, &next);

    /* colour the whole line (strings and comments start before the window) */
    char quote = 0;
    bool comment = false, word = false;
    for (uint8_t i = 0; i < wcols; i++) {
        fg[i] = EDITOR_FG;
        bg[i] = COL_BLACK;
    }
    for (uint16_t col = 0; col < n; col++) {
        char c = line[col];
        char c2 = col + 1 < n ? line[col + 1] : 0;
        uint8_t color = EDITOR_FG;
        if (comment) {
            color = EDITOR_COMMENT;
        } else if (quote) {
            color = EDITOR_VALUE;
            if (c == quote)
                quote = 0;
        } else if (c == '"' || c == '\'' || c == '`') {
            quote = c;
            color = EDITOR_VALUE;
        } else if (c == '/' && c2 == '/') {
            comment = true;
            color = EDITOR_COMMENT;
        } else if (!word && (is_digit(c) || (c == '.' && is_digit(c2)))) {
            color = EDITOR_VALUE;
        }
        word = is_word(c);
        if (col >= left && col < left + wcols)
            fg[col - left] = color;
    }

    if (p != NO_POS) {
        uint16_t end = p + n;
        /* tokens playing now */
        if (lit) {
            for (uint8_t a = 0; a < pat_atom_count; a++) {
                if (!lit[a])
                    continue;
                pat_atom_t atom;
                pat_atom_get(a, &atom);
                const pat_atom_t *at = &atom;
                if (!at->len || at->pos + at->len <= p || at->pos >= end)
                    continue;
                for (uint16_t q = at->pos; q < at->pos + at->len; q++) {
                    uint16_t col = q - p;
                    if (col >= left && col < left + wcols) {
                        fg[col - left] = COL_BLACK;
                        bg[col - left] = COL_HILITE;
                    }
                }
            }
        }
        if (mark >= p && mark < end && mark - p >= left && mark - p < left + wcols) {
            fg[mark - p - left] = COL_WHITE;
            bg[mark - p - left] = COL_RED;
        }
        uint16_t cur = edit_cursor();
        if (cur >= p && cur <= end && cur - p >= left && cur - p < left + wcols) {
            fg[cur - p - left] = COL_BLACK;
            bg[cur - p - left] = COL_WHITE;
        }
    }

    RIA.addr0 = XRAM_TEXT + ((unsigned)(wy + r) * TEXT_COLS + wx) * sizeof(text_cell_t);
    RIA.step0 = 1;
    for (uint8_t i = 0; i < wcols; i++) {
        uint16_t col = left + i;
        RIA.rw0 = col < n ? (uint8_t)line[col] : ' ';
        RIA.rw0 = fg[i];
        RIA.rw0 = bg[i];
    }
    return next;
}

bool editor_draw(void)
{
    if (need_all) {
        uint16_t p = top;
        for (uint8_t r = 0; r < wrows; r++) {
            row_start[r] = p;
            p = draw_row(r, p);
        }
        row_start[wrows] = p;
        need_all = false;
        dirty_count = 0;
    } else if (dirty_count) {
        for (uint8_t i = 0; i < dirty_count; i++)
            draw_row(dirty_rows[i], row_start[dirty_rows[i]]);
        dirty_count = 0;
    } else {
        return false;
    }
    uint8_t r = row_of(edit_cursor());
    if (r != 0xFF) {
        cur_line = top_line + r;
        cur_col = edit_cursor() - row_start[r] + 1;
    }
    return true;
}

bool editor_xy(uint16_t pos, uint8_t *x, uint8_t *y)
{
    uint8_t r = row_of(pos);
    if (r == 0xFF)
        return false;
    uint16_t col = pos - row_start[r];
    if (col < left || col >= left + wcols)
        return false;
    *x = (uint8_t)(wx + col - left);
    *y = (uint8_t)(wy + r);
    return true;
}

uint16_t editor_line(void)
{
    return cur_line;
}

uint16_t editor_col(void)
{
    return cur_col;
}
