#ifndef EDITBUF_H
#define EDITBUF_H

#include <stdbool.h>
#include <stdint.h>

/* The program text: a gap buffer in XRAM, so it costs no 6502 RAM. The gap
 * sits at the cursor, which makes typing there cheap; moving the cursor
 * moves the gap. Positions are logical (as if there were no gap). Reads
 * through text_at (text.h). Uses XRAM portal 0. */

#define EDIT_CAP 8192

void edit_clear(void);
uint16_t edit_length(void);
uint16_t edit_cursor(void);
void edit_set_cursor(uint16_t pos);
bool edit_insert(char c);  /* at the cursor; false when full */
bool edit_backspace(void); /* the character before the cursor */
bool edit_delete(void);    /* the character at the cursor */

/* The line starting at pos: up to max characters into buf (no newline),
 * returns how many; *next is where the next line starts, or 0xFFFF if this
 * is the last. Fast: one sequential pass through XRAM. */
uint8_t edit_read_line(uint16_t pos, char *buf, uint8_t max, uint16_t *next);

#endif
