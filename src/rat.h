#ifndef RAT_H
#define RAT_H

#include <stdbool.h>
#include <stdint.h>

/* Exact fractions for pattern time, as Strudel uses: 16-bit numerator and
 * denominator, always reduced, denominator positive. Patterns are queried
 * at a cycle number below their period (pattern.h), which keeps times
 * small; anything that still won't fit sets rat_overflow rather than
 * giving a wrong answer quietly. 16-bit keeps the 6502's code small and
 * fast.
 *
 * Everything goes through pointers (by-value structs cost the 6502 a lot
 * of code). The result may be one of the inputs. */

typedef struct
{
    int16_t n;
    int16_t d;
} rat_t;

extern bool rat_overflow;

void rat_set(rat_t *r, int32_t n, int32_t d); /* reduces */
void rat_set_int(rat_t *r, int32_t n);
void rat_add(rat_t *r, const rat_t *a, const rat_t *b);
void rat_sub(rat_t *r, const rat_t *a, const rat_t *b);
void rat_mul(rat_t *r, const rat_t *a, const rat_t *b);
void rat_div(rat_t *r, const rat_t *a, const rat_t *b);
void rat_add_int(rat_t *r, const rat_t *a, int32_t k);
void rat_min(rat_t *r, const rat_t *a, const rat_t *b);
void rat_max(rat_t *r, const rat_t *a, const rat_t *b);
int8_t rat_cmp(const rat_t *a, const rat_t *b);
int16_t rat_floor(const rat_t *a); /* Strudel's sam() */

static inline bool rat_eq(const rat_t *a, const rat_t *b)
{
    return a->n == b->n && a->d == b->d;
}

#endif
