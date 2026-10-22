/* RP6502 XRAM Mapping */

#ifndef XRAM_H
#define XRAM_H

#include <rp6502.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Snippets from the docs, copied unchanged. */
/* https://picocomputer.github.io/sdk.html#xram-memory-map */

/* RIA Keyboard */

#define KEYBOARD_NO_KEY 0
#define KEYBOARD_NUM_LOCK 1
#define KEYBOARD_CAPS_LOCK 2
#define KEYBOARD_SCROLL_LOCK 3

#define KEYBOARD_PRESSED(keys, code) ((keys)[(code) >> 3] & (1 << ((code) & 7)))

#define xreg_ria_keyboard(...) xreg(0, 0, 0, __VA_ARGS__)

typedef struct
{
    uint8_t keys[32];
} keyboard_t; /* layout */

/* RIA OPL2 */

#define xreg_ria_opl(...) xreg(0, 1, 1, __VA_ARGS__)

typedef struct
{
    uint8_t reg[256];
} opl_t; /* layout */

/* VGA Canvas */

#define xreg_vga_canvas(...) xreg(1, 0, 0, __VA_ARGS__)

#define CANVAS_CONSOLE 0
#define CANVAS_320X240 1
#define CANVAS_320X180 2
#define CANVAS_640X480 3
#define CANVAS_640X360 4

/* VGA Mode 1: Character */

#define xreg_vga_mode1(...) xreg(1, 0, 1, 1, __VA_ARGS__)

#define MODE1_1BPP 0x00
#define MODE1_4BPPR 0x01
#define MODE1_4BPP 0x02
#define MODE1_8BPP 0x03
#define MODE1_16BPP 0x04

#define MODE1_8X8 0x00
#define MODE1_8X16 0x08

typedef struct
{
    bool x_wrap;
    bool y_wrap;
    int16_t x_pos_px;
    int16_t y_pos_px;
    int16_t width_chars;
    int16_t height_chars;
    uint16_t xram_data_ptr;
    uint16_t xram_palette_ptr;
    uint16_t xram_font_ptr;
} mode1_config_t; /* layout */

/* RPStrudel */

/* 80x60 cells of 8x8 on the 640x480 canvas, 3 bytes per cell in 8-bit
 * color mode: glyph, foreground, background (as RPTracker). */
#define TEXT_COLS 80
#define TEXT_ROWS 60

typedef struct
{
    uint8_t glyph;
    uint8_t fg;
    uint8_t bg;
} text_cell_t;

typedef struct
{
    opl_t opl; /* first, so the registers start on a page boundary */
    keyboard_t keyboard;
    mode1_config_t text_config;
    text_cell_t text[TEXT_ROWS][TEXT_COLS];
    uint8_t patches[3584]; /* PATCH_XRAM_SIZE: patches and names (patches.h) */
    uint8_t edit[8192];    /* EDIT_CAP: the program text, a gap buffer (editbuf.h) */
    uint8_t backup[4096];  /* the playing program while a new one is parsed (player.c) */
    uint8_t cache[5632];   /* CACHE_XRAM_SIZE: notes worked out already (player.c) */
} xram_layout_t;

#define CACHE_XRAM_SIZE 5632

#define XRAM_OPL offsetof(xram_layout_t, opl)
#define XRAM_KEYBOARD offsetof(xram_layout_t, keyboard)
#define XRAM_TEXT_CONFIG offsetof(xram_layout_t, text_config)
#define XRAM_TEXT offsetof(xram_layout_t, text)
#define XRAM_PATCHES offsetof(xram_layout_t, patches)
#define XRAM_EDIT offsetof(xram_layout_t, edit)
#define XRAM_BACKUP offsetof(xram_layout_t, backup)
#define XRAM_CACHE offsetof(xram_layout_t, cache)

_Static_assert((XRAM_OPL & 0xFF) == 0, "The OPL2 registers must start on a page boundary.");

#endif
