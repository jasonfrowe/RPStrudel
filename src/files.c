#include "files.h"

#include <fcntl.h>
#include <rp6502.h>
#include <string.h>
#include <unistd.h>

#include "editbuf.h"
#include "editor.h"
#include "screen.h"
#include "xram.h"

#define NAME_MAX_LEN 40
#define LIST_TOP 2 /* rows of the list above the first name */

bool files_modified;

enum { NONE, SAVING, OPENING };
static uint8_t mode = NONE;
static char name[NAME_MAX_LEN + 1] = "song.str";
static char entry[NAME_MAX_LEN + 1]; /* the name being typed */
static uint8_t entry_len;

static uint8_t lx, ly, lcols, lrows, py;
static uint16_t selected, shown_top, file_count;
static uint8_t esc_state; /* arrows in the list: 1 after ESC, 2 in ESC [ */
static uint8_t esc_param;

static f_stat_t st; /* one directory entry at a time */

void files_init(uint8_t x, uint8_t y, uint8_t cols, uint8_t rows, uint8_t prompt_y)
{
    lx = x;
    ly = y;
    lcols = cols;
    lrows = rows;
    py = prompt_y;
}

bool files_active(void)
{
    return mode != NONE;
}

const char *files_name(void)
{
    return name;
}

/* ---- saving ---- */

static void draw_prompt(void)
{
    screen_fill(0, py, TEXT_COLS, ' ', COL_WHITE, COL_PANEL);
    screen_text(2, py, "save as:", COL_YELLOW, COL_PANEL);
    screen_text(11, py, entry, COL_WHITE, COL_PANEL);
    screen_fill(11 + entry_len, py, 1, ' ', COL_BLACK, COL_WHITE); /* cursor */
    screen_text(TEXT_COLS - 26, py, "Enter save, Esc cancel", COL_GREY, COL_PANEL);
}

void files_save_prompt(void)
{
    mode = SAVING;
    strcpy(entry, name);
    entry_len = (uint8_t)strlen(entry);
    draw_prompt();
}

static void save(void)
{
    if (!entry_len)
        return;
    int fd = open(entry, O_WRONLY | O_CREAT | O_TRUNC);
    bool ok = fd >= 0 && edit_save(fd);
    if (fd >= 0)
        close(fd);
    mode = NONE;
    if (ok) {
        strcpy(name, entry);
        files_modified = false;
        ui_message("saved", COL_GREEN);
    } else {
        ui_message("could not save", COL_RED);
    }
}

/* ---- opening ---- */

static bool wanted(const char *f)
{
    uint8_t n = (uint8_t)strlen(f);
    if (n < 5 || n > NAME_MAX_LEN || (st.fattrib & 0x10)) /* not folders */
        return false;
    const char *ext = f + n - 4;
    return !strcmp(ext, ".str") || !strcmp(ext, ".txt") || !strcmp(ext, ".STR") || !strcmp(ext, ".TXT");
}

/* Goes through the folder's wanted files: draws those from shown_top, and
 * copies the selected one's name into entry. Returns how many there are. */
static uint16_t walk(bool draw)
{
    uint16_t count = 0;
    uint8_t rows = lrows - LIST_TOP;
    int dir = f_opendir(".");
    if (dir < 0)
        return 0;
    while (f_readdir(&st, dir) >= 0 && st.fname[0]) {
        if (!wanted(st.fname))
            continue;
        if (count == selected) {
            strcpy(entry, st.fname);
            entry_len = (uint8_t)strlen(entry);
        }
        if (draw && count >= shown_top && count < shown_top + rows) {
            uint8_t y = ly + LIST_TOP + (uint8_t)(count - shown_top);
            bool sel = count == selected;
            uint8_t bg = sel ? COL_BLUE : COL_BLACK;
            screen_fill(lx, y, lcols, ' ', COL_WHITE, bg);
            screen_text(lx + 2, y, st.fname, sel ? COL_WHITE : COL_YELLOW, bg);
        }
        count++;
    }
    f_closedir(dir);
    return count;
}

static void draw_list(void)
{
    uint8_t rows = lrows - LIST_TOP;
    if (selected < shown_top)
        shown_top = selected;
    else if (selected >= shown_top + rows)
        shown_top = selected - rows + 1;
    for (uint8_t y = 0; y < lrows; y++)
        screen_fill(lx, ly + y, lcols, ' ', COL_WHITE, COL_BLACK);
    screen_text(lx, ly, "Open:", COL_CYAN, COL_BLACK);
    screen_text(lx + 6, ly, "arrows to choose, Enter to open, Esc to cancel", COL_GREY, COL_BLACK);
    if (files_modified)
        screen_text(lx, ly + 1, "(the changes since the last save will be lost)", COL_RED, COL_BLACK);
    file_count = walk(true);
    if (!file_count)
        screen_text(lx + 2, ly + LIST_TOP, "no .str or .txt files here", COL_GREY, COL_BLACK);
}

void files_open_list(void)
{
    mode = OPENING;
    selected = shown_top = 0;
    esc_state = 0;
    draw_list();
}

static void close_list(void)
{
    mode = NONE;
    editor_dirty();
}

static void open_selected(void)
{
    if (!file_count)
        return;
    walk(false); /* the selected name, into entry */
    int fd = open(entry, O_RDONLY);
    if (fd < 0) {
        ui_message("could not open", COL_RED);
        return;
    }
    bool ok = edit_load(fd);
    close(fd);
    close_list();
    editor_reset();
    ui_text_replaced();
    strcpy(name, entry);
    files_modified = false;
    ui_message(ok ? "opened: Ctrl+Enter to play it" : "too big: opened what fits", ok ? COL_GREEN : COL_RED);
}

/* ---- keys ---- */

void files_cancel(void)
{
    if (mode == OPENING)
        close_list();
    mode = NONE;
    ui_message("cancelled", COL_GREY);
}

static void list_key(uint8_t final, uint8_t param)
{
    uint8_t page = lrows - LIST_TOP - 1;
    if (final == 'A' && selected > 0)
        selected--;
    else if (final == 'B' && selected + 1 < file_count)
        selected++;
    else if (final == '~' && param == 5)
        selected = selected > page ? selected - page : 0;
    else if (final == '~' && param == 6)
        selected = selected + page < file_count ? selected + page : (file_count ? file_count - 1 : 0);
    else
        return;
    draw_list();
}

void files_key(uint8_t c)
{
    if (esc_state == 1) {
        esc_state = c == '[' ? 2 : 0;
        esc_param = 0;
        return;
    }
    if (esc_state == 2) {
        if (c >= '0' && c <= '9') {
            esc_param = (uint8_t)(esc_param * 10 + (c - '0'));
            return;
        }
        esc_state = 0;
        if (mode == OPENING)
            list_key(c, esc_param);
        return;
    }
    if (c == 0x1b) {
        esc_state = 1;
        return;
    }
    if (mode == SAVING) {
        if (c == '\r') {
            save();
        } else if ((c == 0x7f || c == 0x08) && entry_len) {
            entry[--entry_len] = 0;
            draw_prompt();
        } else if (c > 0x20 && c < 0x7f && entry_len < NAME_MAX_LEN) {
            entry[entry_len++] = (char)c;
            entry[entry_len] = 0;
            draw_prompt();
        }
    } else if (mode == OPENING && c == '\r') {
        open_selected();
    }
}

void files_end_keys(void)
{
    if (esc_state == 1)
        esc_state = 0;
}
