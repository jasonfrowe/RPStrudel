#ifndef EDITOR_H
#define EDITOR_H

#include <stdbool.h>
#include <stdint.h>

/* The code editor: the text is in editbuf, drawn in a window of the Mode 1
 * screen. Keys arrive as the console types them (characters and xterm
 * sequences), so the keyboard layout and key repeat are the firmware's. */

void editor_init(uint8_t x, uint8_t y, uint8_t cols, uint8_t rows, const char *text);

/* One byte from the console. ctrl is whether Ctrl is held, so Ctrl+Enter
 * (which types a plain Enter) can be left to the caller. */
void editor_key(uint8_t byte, bool ctrl);

/* Call once a frame after the keys: an Escape with nothing after it was the
 * Escape key itself. */
void editor_end_keys(void);

/* Redraws what changed: usually a row or two, the whole window after a
 * scroll or a new line. True if it drew anything. */
bool editor_draw(void);

/* Forces a full redraw on the next editor_draw. */
void editor_dirty(void);

/* Frames left per atom (pattern.h): tokens with a count are drawn lit, so
 * a redraw keeps the highlights instead of flashing them off. */
void editor_set_lit(const uint8_t *frames);

/* Where a text position is on the screen, if it is in view. */
bool editor_xy(uint16_t pos, uint8_t *x, uint8_t *y);

/* A position to mark as an error (red), or 0xFFFF for none. */
void editor_mark(uint16_t pos);

/* The program provides this, to keep its positions pointing at the same
 * text: delta characters were inserted (delta > 0) or removed (< 0) at pos. */
void editor_on_edit(uint16_t pos, int8_t delta);

/* Colours the editor draws with. */
#define EDITOR_FG 15      /* white */
#define EDITOR_VALUE 11   /* strings and numbers: yellow */
#define EDITOR_COMMENT 244 /* grey */

/* For the status line. */
uint16_t editor_line(void);
uint16_t editor_col(void);

#endif
