#include "screen.h"

#include <rp6502.h>

#include "xram.h"

static unsigned cell_addr(uint8_t x, uint8_t y)
{
    return XRAM_TEXT + ((unsigned)y * TEXT_COLS + x) * sizeof(text_cell_t);
}

void screen_init(void)
{
    mode1_config_t cfg = {
        .x_wrap = false,
        .y_wrap = false,
        .x_pos_px = 0,
        .y_pos_px = 0,
        .width_chars = TEXT_COLS,
        .height_chars = TEXT_ROWS,
        .xram_data_ptr = XRAM_TEXT,
        .xram_palette_ptr = 0xFFFF,
        .xram_font_ptr = 0xFFFF,
    };
    xreg_vga_canvas(CANVAS_640X480);
    screen_clear(COL_WHITE, COL_BLACK);
    xram0_write(XRAM_TEXT_CONFIG, &cfg, sizeof(cfg));
    xreg_vga_mode1(MODE1_8BPP | MODE1_8X8, XRAM_TEXT_CONFIG, 0, 0, 0);
}

void screen_clear(uint8_t fg, uint8_t bg)
{
    for (uint8_t y = 0; y < TEXT_ROWS; y++)
        screen_fill(0, y, TEXT_COLS, ' ', fg, bg);
}

void screen_text(uint8_t x, uint8_t y, const char *s, uint8_t fg, uint8_t bg)
{
    RIA.addr0 = cell_addr(x, y);
    RIA.step0 = 1;
    while (*s && x++ < TEXT_COLS) {
        RIA.rw0 = *s++;
        RIA.rw0 = fg;
        RIA.rw0 = bg;
    }
}

void screen_fill(uint8_t x, uint8_t y, uint8_t len, char glyph, uint8_t fg, uint8_t bg)
{
    RIA.addr0 = cell_addr(x, y);
    RIA.step0 = 1;
    while (len--) {
        RIA.rw0 = glyph;
        RIA.rw0 = fg;
        RIA.rw0 = bg;
    }
}

void screen_color(uint8_t x, uint8_t y, uint8_t len, uint8_t fg, uint8_t bg)
{
    unsigned a = cell_addr(x, y) + 1;
    RIA.step0 = 3;
    RIA.addr0 = a;
    for (uint8_t i = 0; i < len; i++)
        RIA.rw0 = fg;
    RIA.addr0 = a + 1;
    for (uint8_t i = 0; i < len; i++)
        RIA.rw0 = bg;
}
