#include "rat.h"

bool rat_overflow;

#ifdef RAT_COUNT
unsigned long rat_calls[8], gcd_calls, gcd_loops, big_mul, div32;
#define COUNT(i) rat_calls[i]++
#else
#define COUNT(i)
#endif

/* Binary GCD: shifts and subtracts, which the 6502 does far faster than a
 * divide. 16-bit when it can be. */
static uint16_t gcd16(uint16_t a, uint16_t b)
{
    if (a == 0)
        return b;
    if (b == 0)
        return a;
    if (a == 1 || b == 1)
        return 1;
#ifdef RAT_COUNT
    gcd_calls++;
#endif
    uint8_t shift = 0;
    while (((a | b) & 1) == 0) {
        a >>= 1;
        b >>= 1;
        shift++;
    }
    while ((a & 1) == 0)
        a >>= 1;
    do {
#ifdef RAT_COUNT
        gcd_loops++;
#endif
        while ((b & 1) == 0)
            b >>= 1;
        if (a > b) {
            uint16_t t = a;
            a = b;
            b = t;
        }
        b -= a;
    } while (b);
    return a << shift;
}

static uint32_t gcd32(uint32_t a, uint32_t b)
{
    if ((a | b) <= 0xFFFF)
        return gcd16((uint16_t)a, (uint16_t)b);
    /* Euclid until both fit in 16 bits */
    while (b) {
        uint32_t t = a % b;
        a = b;
        b = t;
        if ((a | b) <= 0xFFFF)
            return gcd16((uint16_t)a, (uint16_t)b);
    }
    return a;
}

void rat_set(rat_t *r, int32_t n, int32_t d)
{
    COUNT(0);
    if (d == 0) {
        rat_overflow = true;
        r->n = 0;
        r->d = 1;
        return;
    }
    if (d < 0) {
        n = -n;
        d = -d;
    }
    if (d == 1) {
        rat_set_int(r, n);
        return;
    }
    /* the usual case: everything in 16 bits */
    if (n >= -32767 && n <= 32767 && d <= 32767) {
        int16_t n16 = (int16_t)n, d16 = (int16_t)d;
        uint16_t g = gcd16(n16 < 0 ? (uint16_t)-n16 : (uint16_t)n16, (uint16_t)d16);
        if (g > 1) {
            n16 /= (int16_t)g;
            d16 /= (int16_t)g;
        }
        r->n = n16;
        r->d = d16;
        return;
    }
    {
        uint32_t g = gcd32(n < 0 ? (uint32_t)-n : (uint32_t)n, (uint32_t)d);
        if (g > 1) {
            n /= (int32_t)g;
            d /= (int32_t)g;
        }
    }
    if (n < INT16_MIN || n > INT16_MAX || d > INT16_MAX)
        rat_overflow = true;
    r->n = (int16_t)n;
    r->d = (int16_t)d;
}

void rat_set_int(rat_t *r, int32_t n)
{
    if (n < INT16_MIN || n > INT16_MAX)
        rat_overflow = true;
    r->n = (int16_t)n;
    r->d = 1;
}

void rat_add_int(rat_t *r, const rat_t *a, int32_t k)
{
    COUNT(5);
    int32_t n = a->n + k * a->d;
    if (n < INT16_MIN || n > INT16_MAX)
        rat_overflow = true;
    r->n = (int16_t)n;
    r->d = a->d;
}

void rat_add(rat_t *r, const rat_t *a, const rat_t *b)
{
    COUNT(1);
    int16_t ad = a->d, bd = b->d;
    if (ad == bd) {
        rat_set(r, (int32_t)a->n + b->n, ad);
        return;
    }
    if (bd == 1) {
        rat_add_int(r, a, b->n);
        return;
    }
    if (ad == 1) {
        rat_add_int(r, b, a->n);
        return;
    }
    rat_set(r, (int32_t)a->n * bd + (int32_t)b->n * ad, (int32_t)ad * bd);
}

void rat_sub(rat_t *r, const rat_t *a, const rat_t *b)
{
    COUNT(2);
    rat_t nb = {(int16_t)-b->n, b->d};
    rat_add(r, a, &nb);
}

void rat_mul(rat_t *r, const rat_t *a, const rat_t *b)
{
    COUNT(3);
    if (a->d == 1 && b->d == 1)
        rat_set_int(r, (int32_t)a->n * b->n);
    else
        rat_set(r, (int32_t)a->n * b->n, (int32_t)a->d * b->d);
}

void rat_div(rat_t *r, const rat_t *a, const rat_t *b)
{
    COUNT(4);
    rat_set(r, (int32_t)a->n * b->d, (int32_t)a->d * b->n);
}

void rat_min(rat_t *r, const rat_t *a, const rat_t *b)
{
    *r = rat_cmp(a, b) <= 0 ? *a : *b;
}

void rat_max(rat_t *r, const rat_t *a, const rat_t *b)
{
    *r = rat_cmp(a, b) >= 0 ? *a : *b;
}

int8_t rat_cmp(const rat_t *a, const rat_t *b)
{
    COUNT(6);
    if (a->d == b->d)
        return a->n < b->n ? -1 : a->n > b->n;
    int32_t l = (int32_t)a->n * b->d, r = (int32_t)b->n * a->d;
    return l < r ? -1 : l > r;
}

int16_t rat_floor(const rat_t *a)
{
    COUNT(7);
    if (a->d == 1)
        return a->n;
    if (a->n >= 0)
        return a->n / a->d;
    return -((-a->n + a->d - 1) / a->d);
}
