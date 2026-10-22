/* Pattern periods: how many cycles until a pattern repeats. Worked out once
 * after parsing (part of the parser overlay, overlay.h), and kept in each
 * node for the queries (pat_period_memo). */

#include "pattern.h"

#include "overlay.h"
#include "pattern_pool.h"

#ifdef __mos__
#pragma clang section text = ".ovl_parse"
#endif

#define RAND_CYCLES 16 /* as query.c's random numbers */

static const rat_t R1 = {1, 1};

static uint8_t kid(const pat_node_t *n, uint8_t i)
{
    return pool_peek(KID_AT(n->kids + i));
}

/* ---- periods ---- */

#define PERIOD_MAX 1000

static uint16_t gcd_u16(uint16_t a, uint16_t b)
{
    while (b) {
        uint16_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/* lcm, or 0 when either is unknown or it grows too big */
static uint16_t lcm8(uint16_t a, uint16_t b)
{
    if (!a || !b)
        return 0;
    uint32_t l = (uint32_t)(a / gcd_u16(a, b)) * b;
    return l > PERIOD_MAX ? 0 : (uint16_t)l;
}

static uint16_t times8(uint16_t a, uint16_t b)
{
    uint32_t l = (uint32_t)a * b;
    return !a || !b || l > PERIOD_MAX ? 0 : (uint16_t)l;
}

/* A kid with period p sped up by f = a/b repeats after the fewest whole
 * cycles T with f * T a multiple of p: T = p * b / gcd(a, p * b). */
static uint16_t fast_period(uint16_t p, const rat_t *f)
{
    if (!p || f->n <= 0)
        return 0;
    uint32_t pb = (uint32_t)p * (uint16_t)f->d;
    if (pb > 0xFFFF)
        return 0;
    uint16_t t = (uint16_t)pb / gcd_u16((uint16_t)f->n, (uint16_t)pb);
    return t > PERIOD_MAX ? 0 : t;
}

/* For a patterned factor: lcm over every number that can appear in it. */
static uint16_t fast_period_all(uint16_t p, uint8_t arg, const pat_node_t *n)
{
    pat_node_t a;
    pat_node_get(arg, &a);
    if (a.type == N_ATOM) {
        rat_t f;
        if (!pat_atom_num((uint8_t)a.kids, &f) || f.n == 0)
            return 1;
        if (n->r2.n)
            rat_div(&f, &R1, &f);
        rat_mul(&f, &f, &n->r1);
        return fast_period(p, &f);
    }
    uint16_t l = 1;
    for (uint8_t i = 0; i < a.nkids && l; i++)
        l = lcm8(l, fast_period_all(p, kid(&a, i), n));
    return l;
}

OVERLAY_ENTRY uint16_t pat_period(uint8_t node)
{
    if (node == PAT_NONE)
        return 0;
    pat_node_t nn;
    pat_node_get(node, &nn);
    const pat_node_t *n = &nn;
    if (n->period != 0xFFFF)
        return n->period;
    uint16_t p = 1;
    switch (n->type) {
    case N_SILENCE:
    case N_ATOM:
        break;
    case N_STACK:
    case N_SEQ:
    case N_EUCLID:
    case N_TAIL:
    case N_SET:
    case N_STRUCT:
    case N_MASK:
    case N_ARITH:
    case N_SCALE:
        for (uint8_t i = 0; i < n->nkids && p; i++)
            p = lcm8(p, pat_period(kid(n, i)));
        break;
    case N_DEGRADE:
    case N_RANDCAT:
        p = RAND_CYCLES;
        for (uint8_t i = 0; i < n->nkids && p; i++)
            p = lcm8(p, pat_period(kid(n, i)));
        break;
    case N_EVERY:
        p = (uint16_t)n->r1.n;
        for (uint8_t i = 0; i < n->nkids && p; i++)
            p = lcm8(p, pat_period(kid(n, i)));
        break;
    case N_SLOWCAT:
        for (uint8_t i = 0; i < n->nkids && p; i++)
            p = lcm8(p, pat_period(kid(n, i)));
        p = times8(n->nkids, p);
        break;
    case N_FASTC:
        p = fast_period(pat_period(kid(n, 0)), &n->r1);
        break;
    case N_FAST:
        p = lcm8(pat_period(kid(n, 1)), fast_period_all(pat_period(kid(n, 0)), kid(n, 1), n));
        break;
    case N_COMPRESS:
    case N_CTL:
    case N_REV:
    case N_LATE:
    case N_PLY:
    case N_GAIN:
        p = pat_period(kid(n, 0));
        break;
    case N_REPEAT:
        p = times8((uint16_t)n->r1.n, pat_period(kid(n, 0)));
        break;
    default:
        p = 0;
    }
    pool_write(NODE_AT(node) + offsetof(pat_node_t, period), &p, sizeof p);
    return p;
}

