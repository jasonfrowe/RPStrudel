#ifndef SCREEN_H
#define SCREEN_H

#include <stdint.h>

/* Mode 1 text, 80x60, 8-bit color (the built-in 256 color palette).
 * Uses XRAM portal 0. */

#define COL_BLACK 0
#define COL_RED 9
#define COL_GREEN 10
#define COL_YELLOW 11
#define COL_BLUE 12
#define COL_MAGENTA 13
#define COL_CYAN 14
#define COL_WHITE 15
#define COL_DARKGREY 8
#define COL_GREY 244
#define COL_ORANGE 214
#define COL_PANEL 235 /* very dark grey */
#define COL_HILITE 202 /* orange-red token highlight */

void screen_init(void);
void screen_clear(uint8_t fg, uint8_t bg);
void screen_text(uint8_t x, uint8_t y, const char *s, uint8_t fg, uint8_t bg);
void screen_fill(uint8_t x, uint8_t y, uint8_t len, char glyph, uint8_t fg, uint8_t bg);
void screen_color(uint8_t x, uint8_t y, uint8_t len, uint8_t fg, uint8_t bg);

#endif
