#ifndef MINI_H
#define MINI_H

#include <stdbool.h>
#include <stdint.h>

#include "rat.h"

/* Parses Strudel mini-notation (the text inside the quotes) into pattern
 * nodes: the program text (text.h) from..to. Atoms keep their position in
 * the text for highlighting. Returns the root node, or PAT_NONE with
 * pat_error and mini_error_pos set. */
uint8_t mini_parse(uint16_t from, uint16_t to);

extern uint16_t mini_error_pos;

/* A number as JavaScript's Number() reads the common cases. */
bool mini_decimal(const char *t, uint8_t len, rat_t *out);

/* An atom's whole-number value: a number rounded, or a note name as its
 * MIDI number, or NO_VAL. num is NULL for a word. */
int8_t mini_ival(const rat_t *num, const char *text, uint8_t len);

/* Strudel's note names: "c", "eb4", "f#2" (octave 3 when left out). */
int16_t mini_note_number(const char *s, uint8_t len);

#endif
