#ifndef PROG_H
#define PROG_H

#include <stdbool.h>
#include <stdint.h>

/* The Strudel-style program: a small part of JavaScript, enough for
 *
 *   setcpm(30)
 *   $: s("bd*4, [~ sd]*2, hh*8")
 *   $: note("<c2 eb2>*4").s("gm_synth_bass_1").gain(.8)
 *   _$: ...                  (muted)
 *   // comments
 *
 * Functions: s/sound, note, n, gain (also chained), fast, slow, and
 * "mini".note() style calls on strings. */

#define PROG_PATTERNS 8

typedef struct
{
    uint8_t root;
    bool muted;
} prog_pattern_t;

extern prog_pattern_t prog_patterns[PROG_PATTERNS];
extern uint8_t prog_pattern_count;
extern int16_t prog_cpm10; /* tempo set by the program, or -1 */
extern uint16_t prog_error_pos;

/* Parses the program text (text.h, 0..len) into pattern nodes (after
 * pat_reset). Returns false with pat_error and prog_error_pos set. */
bool prog_parse(uint16_t len);

/* One expression, for the host tests. */
uint8_t prog_parse_expr(uint16_t from, uint16_t to);

#endif
