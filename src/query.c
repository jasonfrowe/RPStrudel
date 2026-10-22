/* Pattern queries: Strudel's primitives, run over the nodes in the pool.
 * An overlay (overlay.h): in RAM only while cycles are worked out, sharing
 * its space with the parser. */

#include "pattern.h"

#include <string.h>

#include "overlay.h"
#include "pattern_pool.h"

#ifdef __mos__
#pragma clang section text = ".ovl_query"
#endif

static const rat_t R1 = {1, 1};

static uint8_t kid(const pat_node_t *n, uint8_t i)
{
    return pool_peek(KID_AT(n->kids + i));
}

/* ---- haps ---- */

static pat_hap_t *push_hap(void)
{
    if (pat_hap_top >= PAT_HAPS) {
        pat_error = "too many events";
        return 0;
    }
    return &pat_haps[pat_hap_top++];
}

/* All four times of the haps from `from` up: t + k. */
static void shift_haps(uint8_t from, int32_t k)
{
    if (!k)
        return;
    for (uint8_t i = from; i < pat_hap_top; i++)
        for (uint8_t j = 0; j < 4; j++)
            rat_add_int(&pat_haps[i].t[j], &pat_haps[i].t[j], k);
}

/* t * f */
static void scale_haps(uint8_t from, const rat_t *f)
{
    for (uint8_t i = from; i < pat_hap_top; i++)
        for (uint8_t j = 0; j < 4; j++)
            rat_mul(&pat_haps[i].t[j], &pat_haps[i].t[j], f);
}

/* t + r */
static void offset_haps(uint8_t from, const rat_t *r)
{
    for (uint8_t i = from; i < pat_hap_top; i++)
        for (uint8_t j = 0; j < 4; j++)
            rat_add(&pat_haps[i].t[j], &pat_haps[i].t[j], r);
}

/* Strudel's TimeSpan.intersection of a..ae and b..be, into o[0..1]. */
static bool intersect(const rat_t *a, const rat_t *ae, const rat_t *b, const rat_t *be, rat_t *o)
{
    rat_max(&o[0], a, b);
    rat_min(&o[1], ae, be);
    int8_t c = rat_cmp(&o[0], &o[1]);
    if (c > 0)
        return false;
    if (c == 0) {
        if (rat_eq(&o[0], ae) && rat_cmp(a, ae) < 0)
            return false;
        if (rat_eq(&o[0], be) && rat_cmp(b, be) < 0)
            return false;
    }
    return true;
}

static uint8_t per_cycle(const pat_node_t *n, const rat_t *b, const rat_t *e);

/* Strudel's spanCycles: b..e cut at each whole cycle, each piece given to
 * per_cycle. (A switch there rather than a function pointer: the guide
 * says pointers make llvm-mos keep locals on the soft stack.) */
static uint8_t each_cycle(const pat_node_t *n, const rat_t *b, const rat_t *e)
{
    if (rat_eq(b, e))
        return per_cycle(n, b, e);
    uint8_t count = 0;
    rat_t sb = *b, next, se;
    while (rat_cmp(&sb, e) < 0 && !pat_error) {
        rat_set_int(&next, rat_floor(&sb) + 1);
        rat_min(&se, &next, e);
        count += per_cycle(n, &sb, &se);
        sb = next;
    }
    return count;
}

/* ---- primitives ---- */

static uint8_t q_pure(uint8_t atom, const rat_t *b, const rat_t *e)
{
    uint8_t count = 0;
    rat_t sb = *b;
    for (;;) {
        pat_hap_t *h = push_hap();
        if (!h)
            return count;
        int32_t k = rat_floor(&sb);
        rat_set_int(&h->t[WB], k);
        rat_set_int(&h->t[WE], k + 1);
        h->t[PB] = sb;
        rat_min(&h->t[PE], &h->t[WE], e);
        h->atom = atom;
        h->tail = PAT_NONE;
        memset(h->ctl, PAT_NONE, PAT_CTLS);
        h->val = h->note = h->n = NO_VAL;
        h->gain = 0xFF;
        count++;
        if (rat_cmp(&h->t[WE], e) >= 0)
            return count;
        sb = h->t[WE];
    }
}

/* Queries kid over b..e moved by -k, and moves its haps back by +k. */
static uint8_t query_shifted(uint8_t k_node, const rat_t *b, const rat_t *e, int32_t k)
{
    rat_t qb, qe;
    rat_add_int(&qb, b, -k);
    rat_add_int(&qe, e, -k);
    uint8_t from = pat_hap_top;
    uint8_t count = pat_query(k_node, &qb, &qe);
    shift_haps(from, k);
    return count;
}

static uint8_t q_slowcat_cycle(const pat_node_t *n, const rat_t *b, const rat_t *e)
{
    int32_t cyc = rat_floor(b);
    int32_t i = cyc % n->nkids;
    if (i < 0)
        i += n->nkids;
    /* Each kid keeps its own cycle count: the 4th cycle of 3 kids is the
     * 2nd cycle of the first. */
    rat_t t, cnt;
    rat_set_int(&cnt, n->nkids);
    rat_div(&t, b, &cnt);
    return query_shifted(kid(n, (uint8_t)i), b, e, cyc - rat_floor(&t));
}

static uint8_t q_fastc(uint8_t k, const rat_t *f, const rat_t *b, const rat_t *e)
{
    if (f->n <= 0)
        return 0; /* Strudel reverses for negative factors; not yet */
    rat_t qb, qe, inv;
    rat_mul(&qb, b, f);
    rat_mul(&qe, e, f);
    uint8_t from = pat_hap_top;
    uint8_t count = pat_query(k, &qb, &qe);
    rat_div(&inv, &R1, f);
    scale_haps(from, &inv);
    return count;
}

/* Moves the haps from `from` up down to `to`, dropping those between
 * (argument haps that were only needed while querying). */
static uint8_t drop_between(uint8_t to, uint8_t from)
{
    uint8_t made = pat_hap_top - from;
    memmove(&pat_haps[to], &pat_haps[from], made * sizeof(pat_hap_t));
    pat_hap_top = to + made;
    return made;
}


/* fast with a pattern for the factor: Strudel's innerJoin, so each piece
 * of the factor pattern queries the kid sped up by its own value. The
 * pieces stay on the hap stack while the kid is queried above them. */
static uint8_t q_fast(const pat_node_t *n, const rat_t *b, const rat_t *e)
{
    uint8_t p0 = pat_hap_top;
    uint8_t np = pat_query(kid(n, 1), b, e);
    uint8_t r0 = pat_hap_top;
    for (uint8_t i = 0; i < np; i++) {
        const pat_hap_t *p = &pat_haps[p0 + i];
        rat_t f;
        if (!pat_atom_num(p->atom, &f) || f.n == 0)
            continue;
        if (n->r2.n)
            rat_div(&f, &R1, &f);
        rat_mul(&f, &f, &n->r1);
        q_fastc(kid(n, 0), &f, &p->t[PB], &p->t[PE]);
    }
    return drop_between(p0, r0);
}

/* Strudel's compress(b0, e0) = fastGap(1 / (e0 - b0)).late(b0), with
 * fastGap's fiddly edges kept. len is e0 - b0, the slot's length. */
static uint8_t q_compress_cycle(const pat_node_t *n, const rat_t *b, const rat_t *e)
{
    rat_t len, bpos, epos, t;
    rat_sub(&len, &n->r2, &n->r1);
    int32_t cycle = rat_floor(b);
    /* bpos = min((b - cycle) / len, 1) */
    rat_add_int(&t, b, -cycle);
    rat_div(&t, &t, &len);
    rat_min(&bpos, &t, &R1);
    if (rat_cmp(&bpos, &R1) >= 0)
        return 0;
    rat_add_int(&t, e, -cycle);
    rat_div(&t, &t, &len);
    rat_min(&epos, &t, &R1);
    rat_add_int(&bpos, &bpos, cycle);
    rat_add_int(&epos, &epos, cycle);

    uint8_t from = pat_hap_top;
    uint8_t count = pat_query(kid(n, 0), &bpos, &epos);
    for (uint8_t i = from; i < pat_hap_top; i++) {
        rat_t *h = pat_haps[i].t;
        rat_t npb, npe;
        int32_t c2 = rat_floor(&h[PB]);
        /* new part = cycle + min((part - cycle) * len, 1) */
        rat_add_int(&t, &h[PB], -c2);
        rat_mul(&t, &t, &len);
        rat_min(&npb, &t, &R1);
        rat_add_int(&npb, &npb, c2);
        rat_add_int(&t, &h[PE], -c2);
        rat_mul(&t, &t, &len);
        rat_min(&npe, &t, &R1);
        rat_add_int(&npe, &npe, c2);
        /* whole keeps its distance from the part, scaled */
        rat_sub(&t, &h[PB], &h[WB]);
        rat_mul(&t, &t, &len);
        rat_sub(&h[WB], &npb, &t);
        rat_sub(&t, &h[WE], &h[PE]);
        rat_mul(&t, &t, &len);
        rat_add(&h[WE], &npe, &t);
        h[PB] = npb;
        h[PE] = npe;
    }
    return count;
}

static uint8_t q_compress(const pat_node_t *n, const rat_t *b, const rat_t *e)
{
    /* late(b0): query earlier, then move the haps later */
    rat_t qb, qe;
    rat_sub(&qb, b, &n->r1);
    rat_sub(&qe, e, &n->r1);
    uint8_t from = pat_hap_top;
    uint8_t count = each_cycle(n, &qb, &qe);
    offset_haps(from, &n->r1);
    return count;
}

/* Each kid squeezed into its share of the cycle, as N_COMPRESS does. Kids
 * whose share cannot overlap b..e are skipped (compress would give
 * nothing for them). */
static uint8_t q_seq(const pat_node_t *n, const rat_t *b, const rat_t *e)
{
    pat_node_t c;
    c.type = N_COMPRESS;
    c.nkids = 1;
    int16_t cyc = rat_floor(b);
    rat_t fb, fe;
    rat_add_int(&fb, b, -cyc);
    rat_add_int(&fe, e, -cyc);
    rat_t one = {1, 1};
    bool within = !rat_eq(b, e) && rat_cmp(&fe, &one) <= 0; /* inside one cycle */
    uint8_t count = 0;
    for (uint8_t i = 0; i < n->nkids && !pat_error; i++) {
        rat_set(&c.r1, i, n->nkids);
        rat_set(&c.r2, i + 1, n->nkids);
        if (within && (rat_cmp(&c.r1, &fe) >= 0 || rat_cmp(&c.r2, &fb) <= 0))
            continue;
        c.kids = n->kids + i;
        count += q_compress(&c, b, e);
    }
    return count;
}

static uint8_t q_repeat_cycle(const pat_node_t *n, const rat_t *b, const rat_t *e)
{
    int32_t cycle = rat_floor(b);
    int32_t reps = n->r1.n;
    int32_t source = cycle >= 0 ? cycle / reps : -((-cycle + reps - 1) / reps);
    return query_shifted(kid(n, 0), b, e, cycle - source);
}

/* ---- euclid ---- */

/* Strudel's bjorklund (from Rohan Drape's Haskell): each group is a run of
 * bits, held as a bit string with its length. */
#define EUCLID_MAX 32

bool pat_euclid_bits(int8_t pulses, uint8_t steps, int8_t rotation, uint8_t *out)
{
    bool inverted = pulses < 0;
    uint8_t ons = (uint8_t)(inverted ? -pulses : pulses);
    if (steps == 0 || steps > EUCLID_MAX || ons > steps)
        return false;

    static uint32_t xs[EUCLID_MAX], ys[EUCLID_MAX];
    static uint8_t xl[EUCLID_MAX], yl[EUCLID_MAX];
    uint8_t nx = ons, ny = steps - ons;
    for (uint8_t i = 0; i < nx; i++) {
        xs[i] = 1;
        xl[i] = 1;
    }
    for (uint8_t i = 0; i < ny; i++) {
        ys[i] = 0;
        yl[i] = 1;
    }
    /* (nx, ny) is Strudel's n = [ons, offs] */
    while ((nx < ny ? nx : ny) > 1) {
        if (nx > ny) {
            /* left: the first ny xs take a y each; the other xs become ys */
            uint8_t k = ny, rest = nx - ny;
            for (uint8_t i = 0; i < k; i++) {
                uint32_t z = (xs[i] << yl[i]) | ys[i];
                uint8_t zl = xl[i] + yl[i];
                ys[i] = i < rest ? xs[k + i] : 0;
                yl[i] = i < rest ? xl[k + i] : 0;
                xs[i] = z;
                xl[i] = zl;
            }
            for (uint8_t i = k; i < rest; i++) {
                ys[i] = xs[k + i];
                yl[i] = xl[k + i];
            }
            nx = k;
            ny = rest;
        } else {
            /* right: each x takes one of the first nx ys; the rest stay */
            uint8_t k = nx, rest = ny - nx;
            for (uint8_t i = 0; i < k; i++) {
                xs[i] = (xs[i] << yl[i]) | ys[i];
                xl[i] += yl[i];
            }
            for (uint8_t i = 0; i < rest; i++) {
                ys[i] = ys[k + i];
                yl[i] = yl[k + i];
            }
            ny = rest;
        }
    }
    uint8_t bits[EUCLID_MAX];
    uint8_t pos = 0;
    for (uint8_t g = 0; g < nx; g++)
        for (int8_t j = (int8_t)xl[g] - 1; j >= 0; j--)
            bits[pos++] = (xs[g] >> j) & 1;
    for (uint8_t g = 0; g < ny; g++)
        for (int8_t j = (int8_t)yl[g] - 1; j >= 0; j--)
            bits[pos++] = (ys[g] >> j) & 1;

    /* rotate(bits, -rotation): arr.slice(n).concat(arr.slice(0, n)) */
    int16_t r = (int16_t)(-rotation) % steps;
    if (r < 0)
        r += steps;
    for (uint8_t i = 0; i < steps; i++) {
        uint8_t v = bits[(i + r) % steps];
        out[i] = inverted ? !v : v;
    }
    return true;
}

/* struct of a boolean sequence over the kid (keepif.out): each true step
 * takes the whole, and the kid's value at that step. */
static uint8_t q_struct(uint8_t k, const uint8_t *bits, uint8_t steps, const rat_t *b, const rat_t *e)
{
    rat_t rs, eg, t, sb, se;
    rat_set_int(&rs, steps);
    rat_mul(&t, b, &rs);
    int32_t g = rat_floor(&t);
    rat_mul(&eg, e, &rs);
    uint8_t count = 0;
    bool point = rat_eq(b, e);
    for (;; g++) {
        rat_set_int(&t, g);
        if (!point && rat_cmp(&t, &eg) >= 0)
            break;
        int32_t m = g % steps;
        if (m < 0)
            m += steps;
        rat_t sp[2];
        if (bits[m]) {
            rat_div(&sb, &t, &rs);
            rat_set_int(&t, g + 1);
            rat_div(&se, &t, &rs);
        }
        if (bits[m] && intersect(&sb, &se, b, e, sp)) {
            uint8_t from = pat_hap_top;
            pat_query(k, &sb, &se);
            uint8_t out = from;
            for (uint8_t i = from; i < pat_hap_top; i++) {
                rat_t np[2];
                pat_hap_t *h = &pat_haps[i];
                if (!intersect(&sp[0], &sp[1], &h->t[PB], &h->t[PE], np))
                    continue;
                pat_hap_t *o = &pat_haps[out++];
                if (o != h)
                    *o = *h;
                o->t[WB] = sb;
                o->t[WE] = se;
                o->t[PB] = np[0];
                o->t[PE] = np[1];
            }
            pat_hap_top = out;
            count += out - from;
        }
        if (point || pat_error)
            break;
    }
    return count;
}

static bool small_int(uint8_t atom, int16_t lo, int16_t hi, int16_t *out)
{
    rat_t r;
    if (!pat_atom_num(atom, &r) || r.d != 1 || r.n < lo || r.n > hi)
        return false;
    *out = r.n;
    return true;
}

static uint8_t q_euclid_with(uint8_t k, uint8_t pa, uint8_t sa, uint8_t ra, const rat_t *b, const rat_t *e)
{
    int16_t p, s, r = 0;
    if (!small_int(pa, -EUCLID_MAX, EUCLID_MAX, &p) || !small_int(sa, 0, EUCLID_MAX, &s) ||
        (ra != PAT_NONE && !small_int(ra, -127, 127, &r))) {
        pat_error = "euclid needs whole numbers up to 32";
        return 0;
    }
    uint8_t bits[EUCLID_MAX];
    if (!pat_euclid_bits((int8_t)p, (uint8_t)s, (int8_t)r, bits))
        return 0;
    return q_struct(k, bits, (uint8_t)s, b, e);
}

/* Patterned arguments: pulses gives the structure, steps and rotation are
 * sampled into it (appLeft), and each piece queries its own rhythm
 * (innerJoin). Argument haps stay on the hap stack while in use. */
static uint8_t q_euclid(const pat_node_t *n, const rat_t *b, const rat_t *e)
{
    uint8_t k = kid(n, 0);
    bool has_rot = n->nkids > 3;
    uint8_t a0 = pat_hap_top;
    uint8_t npp = pat_query(kid(n, 1), b, e);
    uint8_t a1 = pat_hap_top;
    for (uint8_t i = 0; i < npp; i++) {
        const pat_hap_t *pp = &pat_haps[a0 + i];
        uint8_t s0 = pat_hap_top;
        uint8_t nsp = pat_query(kid(n, 2), &pp->t[WB], &pp->t[WE]);
        uint8_t s1 = pat_hap_top;
        for (uint8_t j = 0; j < nsp; j++) {
            const pat_hap_t *sp = &pat_haps[s0 + j];
            rat_t x1[2];
            if (!intersect(&pp->t[PB], &pp->t[PE], &sp->t[PB], &sp->t[PE], x1))
                continue;
            if (!has_rot) {
                q_euclid_with(k, pp->atom, sp->atom, PAT_NONE, &x1[0], &x1[1]);
                continue;
            }
            uint8_t r0 = pat_hap_top;
            uint8_t nrp = pat_query(kid(n, 3), &pp->t[WB], &pp->t[WE]);
            uint8_t r1 = pat_hap_top;
            for (uint8_t m = 0; m < nrp; m++) {
                const pat_hap_t *rp = &pat_haps[r0 + m];
                rat_t x2[2];
                if (!intersect(&x1[0], &x1[1], &rp->t[PB], &rp->t[PE], x2))
                    continue;
                q_euclid_with(k, pp->atom, sp->atom, rp->atom, &x2[0], &x2[1]);
            }
            drop_between(r0, r1);
        }
        drop_between(s0, s1);
    }
    return drop_between(a0, a1);
}

/* ---- values ---- */

static int8_t atom_ival(uint8_t atom)
{
    return (int8_t)pool_peek(ATOM_AT(atom) + offsetof(pat_atom_t, ival));
}

int8_t pat_value(const pat_hap_t *h)
{
    return h->val != NO_VAL ? h->val : atom_ival(h->atom);
}

static int8_t clamp8(int16_t v)
{
    return v < -127 ? -127 : v > 127 ? 127 : (int8_t)v;
}

/* Truthy, as JavaScript: a non-zero number, or any word. */
static bool truthy(const pat_hap_t *h)
{
    rat_t r;
    if (h->val != NO_VAL)
        return h->val != 0;
    return !pat_atom_num(h->atom, &r) || r.n != 0;
}

/* ---- appLeft: "bd:2" tails, control sets, masks, arithmetic, scales ---- */

/* Strudel's appLeft: structure from the kid, values from the friend
 * wherever the two overlap. The friend's haps come first on the stack,
 * then the kid's, then the results, which move down at the end. What the
 * friend's value does depends on the node. */
static uint8_t q_app_left(const pat_node_t *n, const rat_t *b, const rat_t *e)
{
    uint8_t f0 = pat_hap_top;
    uint8_t nf = pat_query(kid(n, 1), b, e);
    uint8_t l0 = pat_hap_top;
    uint8_t nl = pat_query(kid(n, 0), b, e);
    uint8_t out = pat_hap_top;
    uint8_t ctls = (uint8_t)n->r2.n; /* N_ARITH: 1 controls, 2 transpose */
    for (uint8_t i = l0; i < l0 + nl && !pat_error; i++) {
        for (uint8_t j = f0; j < f0 + nf; j++) {
            rat_t np[2];
            pat_hap_t *l = &pat_haps[i], *f = &pat_haps[j];
            if (!intersect(&l->t[PB], &l->t[PE], &f->t[PB], &f->t[PE], np))
                continue;
            if (n->type == N_MASK && !truthy(f))
                continue;
            if (out >= PAT_HAPS) {
                pat_error = "too many events";
                break;
            }
            pat_hap_t *h = &pat_haps[out++];
            *h = *l;
            h->t[PB] = np[0];
            h->t[PE] = np[1];
            switch (n->type) {
            case N_TAIL:
                h->tail = f->atom;
                break;
            case N_SET:
                for (uint8_t c = 0; c < PAT_CTLS; c++)
                    if (f->ctl[c] != PAT_NONE)
                        h->ctl[c] = f->ctl[c];
                if (f->note != NO_VAL)
                    h->note = f->note;
                if (f->n != NO_VAL)
                    h->n = f->n;
                if (f->gain != 0xFF)
                    h->gain = f->gain;
                break;
            case N_ARITH: {
                int8_t sign = (int8_t)n->r1.n;
                if (!ctls) {
                    int8_t a = pat_value(l), v = pat_value(f);
                    h->val = a == NO_VAL || v == NO_VAL ? NO_VAL : clamp8(a + sign * v);
                } else if (ctls == 2) {
                    /* transpose: a plain number of semitones to the note */
                    int8_t v = pat_value(f);
                    if (h->note != NO_VAL && v != NO_VAL)
                        h->note = clamp8(h->note + v);
                } else {
                    if (h->note != NO_VAL && f->note != NO_VAL)
                        h->note = clamp8(h->note + sign * f->note);
                    if (h->n != NO_VAL && f->n != NO_VAL)
                        h->n = clamp8(h->n + sign * f->n);
                }
                break;
            }
            case N_SCALE: {
                uint8_t sc = pool_peek(ATOM_AT(f->atom) + offsetof(pat_atom_t, scale));
                int8_t tonic = atom_ival(f->atom);
                if (ctls) {
                    h->note = pat_scale_note(tonic, sc, h->n);
                    h->n = NO_VAL;
                } else {
                    h->val = pat_scale_note(tonic, sc, pat_value(l));
                }
                break;
            }
            }
        }
    }
    uint8_t made = out - (l0 + nl);
    memmove(&pat_haps[f0], &pat_haps[l0 + nl], made * sizeof(pat_hap_t));
    pat_hap_top = f0 + made;
    return made;
}

static uint8_t q_ctl(const pat_node_t *n, const rat_t *b, const rat_t *e)
{
    uint8_t from = pat_hap_top;
    uint8_t count = pat_query(kid(n, 0), b, e);
    uint8_t c = (uint8_t)n->r1.n;
    for (uint8_t i = from; i < pat_hap_top; i++) {
        pat_hap_t *h = &pat_haps[i];
        h->ctl[c] = h->atom;
        switch (c) {
        case CTL_S:
            /* s("bd:2") is s "bd" with n 2 */
            if (h->tail != PAT_NONE)
                h->n = atom_ival(h->tail);
            break;
        case CTL_NOTE:
            h->note = pat_value(h);
            break;
        case CTL_N:
            h->n = pat_value(h);
            break;
        case CTL_GAIN: {
            rat_t g;
            int16_t v = -1;
            if (h->val != NO_VAL)
                v = h->val * 64;
            else if (pat_atom_num(h->atom, &g))
                v = (int16_t)(((int32_t)g.n * 64) / g.d);
            h->gain = v < 0 ? 0 : v > 254 ? 254 : (uint8_t)v;
            break;
        }
        }
    }
    return count;
}

/* ---- rev, late, ply, gain, struct ---- */

/* t reflected in its cycle c: 2c + 1 - t */
static void reflect(rat_t *t, int16_t c)
{
    t->n = -t->n;
    rat_add_int(t, t, 2 * c + 1);
}

static uint8_t q_rev_cycle(const pat_node_t *n, const rat_t *b, const rat_t *e)
{
    int16_t c = rat_floor(b);
    rat_t qb = *e, qe = *b;
    reflect(&qb, c);
    reflect(&qe, c);
    uint8_t from = pat_hap_top;
    uint8_t count = pat_query(kid(n, 0), &qb, &qe);
    for (uint8_t i = from; i < pat_hap_top; i++) {
        rat_t *h = pat_haps[i].t;
        for (uint8_t j = 0; j < 4; j++)
            reflect(&h[j], c);
        rat_t t = h[WB];
        h[WB] = h[WE];
        h[WE] = t;
        t = h[PB];
        h[PB] = h[PE];
        h[PE] = t;
    }
    return count;
}

static uint8_t q_late(const pat_node_t *n, const rat_t *b, const rat_t *e)
{
    rat_t qb, qe;
    rat_sub(&qb, b, &n->r1);
    rat_sub(&qe, e, &n->r1);
    uint8_t from = pat_hap_top;
    uint8_t count = pat_query(kid(n, 0), &qb, &qe);
    offset_haps(from, &n->r1);
    return count;
}

/* Each event split into r1.n events filling its whole (squeezeJoin of
 * pure(value).fast(r1)); the parts keep within the queried span. */
static uint8_t q_ply(const pat_node_t *n, const rat_t *b, const rat_t *e)
{
    uint8_t k0 = pat_hap_top;
    uint8_t nk = pat_query(kid(n, 0), b, e);
    uint8_t out = pat_hap_top;
    rat_t parts;
    rat_set_int(&parts, n->r1.n);
    for (uint8_t i = k0; i < k0 + nk && !pat_error; i++) {
        rat_t len;
        rat_sub(&len, &pat_haps[i].t[WE], &pat_haps[i].t[WB]);
        rat_div(&len, &len, &parts);
        rat_t sb = pat_haps[i].t[WB];
        for (int16_t j = 0; j < n->r1.n; j++) {
            rat_t se, np[2];
            rat_add(&se, &sb, &len);
            if (intersect(&sb, &se, &pat_haps[i].t[PB], &pat_haps[i].t[PE], np)) {
                if (out >= PAT_HAPS) {
                    pat_error = "too many events";
                    break;
                }
                pat_hap_t *h = &pat_haps[out++];
                *h = pat_haps[i];
                h->t[WB] = sb;
                h->t[WE] = se;
                h->t[PB] = np[0];
                h->t[PE] = np[1];
            }
            sb = se;
        }
    }
    pat_hap_top = out;
    return drop_between(k0, k0 + nk);
}

static uint8_t q_gain(const pat_node_t *n, const rat_t *b, const rat_t *e)
{
    uint8_t from = pat_hap_top;
    uint8_t count = pat_query(kid(n, 0), b, e);
    int16_t g = (int16_t)(((int32_t)n->r1.n * 64) / n->r1.d);
    for (uint8_t i = from; i < pat_hap_top; i++)
        pat_haps[i].gain = g > 254 ? 254 : (uint8_t)g;
    return count;
}

/* struct with a pattern: each true event of the bools takes the kid's
 * value at that time (keepif.out). */
static uint8_t q_struct_pat(const pat_node_t *n, const rat_t *b, const rat_t *e)
{
    uint8_t s0 = pat_hap_top;
    uint8_t ns = pat_query(kid(n, 1), b, e);
    uint8_t s1 = pat_hap_top;
    for (uint8_t i = s0; i < s0 + ns && !pat_error; i++) {
        const pat_hap_t *sh = &pat_haps[i];
        if (!truthy(sh))
            continue;
        uint8_t from = pat_hap_top;
        pat_query(kid(n, 0), &sh->t[WB], &sh->t[WE]);
        uint8_t out = from;
        for (uint8_t j = from; j < pat_hap_top; j++) {
            rat_t np[2];
            pat_hap_t *h = &pat_haps[j];
            if (!intersect(&sh->t[PB], &sh->t[PE], &h->t[PB], &h->t[PE], np))
                continue;
            pat_hap_t *o = &pat_haps[out++];
            if (o != h)
                *o = *h;
            o->t[WB] = sh->t[WB];
            o->t[WE] = sh->t[WE];
            o->t[PB] = np[0];
            o->t[PE] = np[1];
        }
        pat_hap_top = out;
    }
    return drop_between(s0, s1);
}

/* ---- randomness ---- */

/* RPStrudel's own random numbers (not Strudel's): a hash of the time within
 * a 16 cycle window and a seed, so a random pattern repeats every 16
 * cycles, which keeps its numbers small like any other pattern. */
#define RAND_CYCLES 16

static uint16_t rand16(const rat_t *t, uint8_t seed)
{
    int16_t c = rat_floor(t);
    rat_t f;
    rat_add_int(&f, t, -c);
    uint16_t x = (uint16_t)((c & (RAND_CYCLES - 1)) * 0x9E37u) ^ (uint16_t)(f.n * 0x85EBu) ^
                 (uint16_t)(f.d * 0x2C1Bu) ^ (uint16_t)(seed * 0x6F4Bu) ^ 0x5A5Au;
    for (uint8_t i = 0; i < 3; i++) {
        x ^= x << 7;
        x ^= x >> 9;
        x ^= x << 8;
    }
    return x;
}

static uint8_t q_degrade(const pat_node_t *n, const rat_t *b, const rat_t *e)
{
    uint8_t from = pat_hap_top;
    pat_query(kid(n, 0), b, e);
    uint16_t limit = (uint16_t)(((uint32_t)n->r1.n << 16) / n->r1.d);
    uint8_t out = from;
    for (uint8_t i = from; i < pat_hap_top; i++) {
        bool low = rand16(&pat_haps[i].t[WB], (uint8_t)n->r2.n) < limit;
        if (low == (n->r2.d > 1)) /* r2.d 2: keep the others instead */
            pat_haps[out++] = pat_haps[i];
    }
    pat_hap_top = out;
    return out - from;
}

static uint8_t q_randcat_cycle(const pat_node_t *n, const rat_t *b, const rat_t *e)
{
    rat_t c;
    rat_set_int(&c, rat_floor(b));
    uint8_t k = (uint8_t)(rand16(&c, (uint8_t)n->r1.n) % n->nkids);
    return pat_query(kid(n, k), b, e);
}

static uint8_t q_every_cycle(const pat_node_t *n, const rat_t *b, const rat_t *e)
{
    int16_t c = rat_floor(b);
    int16_t m = c % n->r1.n;
    if (m < 0)
        m += n->r1.n;
    return pat_query(kid(n, m == n->r2.n ? 1 : 0), b, e);
}

/* ---- query ---- */

static uint8_t per_cycle(const pat_node_t *n, const rat_t *b, const rat_t *e)
{
    switch (n->type) {
    case N_SLOWCAT:
        return q_slowcat_cycle(n, b, e);
    case N_COMPRESS:
        return q_compress_cycle(n, b, e);
    case N_REV:
        return q_rev_cycle(n, b, e);
    case N_EVERY:
        return q_every_cycle(n, b, e);
    case N_RANDCAT:
        return q_randcat_cycle(n, b, e);
    default:
        return q_repeat_cycle(n, b, e);
    }
}

uint8_t pat_query(uint8_t node, const rat_t *b, const rat_t *e)
{
    if (pat_error || node == PAT_NONE)
        return 0;
    pat_node_t nn;
    pat_node_get(node, &nn); /* a copy: the node is in the pool */
    const pat_node_t *n = &nn;
    switch (n->type) {
    case N_ATOM:
        return q_pure((uint8_t)n->kids, b, e);
    case N_STACK: {
        uint8_t count = 0;
        for (uint8_t i = 0; i < n->nkids; i++)
            count += pat_query(kid(n, i), b, e);
        return count;
    }
    case N_SLOWCAT:
        return each_cycle(n, b, e);
    case N_FASTC:
        return q_fastc(kid(n, 0), &n->r1, b, e);
    case N_FAST:
        return q_fast(n, b, e);
    case N_COMPRESS:
        return q_compress(n, b, e);
    case N_REPEAT:
        return each_cycle(n, b, e);
    case N_EUCLID:
        return q_euclid(n, b, e);
    case N_TAIL:
    case N_SET:
    case N_MASK:
    case N_ARITH:
    case N_SCALE:
        return q_app_left(n, b, e);
    case N_CTL:
        return q_ctl(n, b, e);
    case N_REV:
    case N_EVERY:
    case N_RANDCAT:
        return each_cycle(n, b, e);
    case N_LATE:
        return q_late(n, b, e);
    case N_PLY:
        return q_ply(n, b, e);
    case N_STRUCT:
        return q_struct_pat(n, b, e);
    case N_GAIN:
        return q_gain(n, b, e);
    case N_DEGRADE:
        return q_degrade(n, b, e);
    case N_SEQ:
        return q_seq(n, b, e);
    default:
        return 0;
    }
}

OVERLAY_ENTRY uint8_t pat_query_onsets(uint8_t node, int32_t c, int32_t *base)
{
    uint16_t p = pat_period_memo(node);
    int32_t c0 = c;
    if (p) {
        c0 = c % p;
        if (c0 < 0)
            c0 += p;
    }
    *base = c0;
    return pat_query_onsets_direct(node, c0);
}

OVERLAY_ENTRY uint8_t pat_query_onsets_direct(uint8_t node, int32_t c)
{
    rat_t b, e;
    rat_set_int(&b, c);
    rat_set_int(&e, c + 1);
    pat_hap_top = 0;
    rat_overflow = false;
    pat_query(node, &b, &e);
    if (rat_overflow && !pat_error)
        pat_error = "numbers too big";
    uint8_t out = 0;
    for (uint8_t i = 0; i < pat_hap_top; i++) {
        if (rat_eq(&pat_haps[i].t[WB], &pat_haps[i].t[PB]))
            pat_haps[out++] = pat_haps[i];
    }
    pat_hap_top = out;
    return out;
}
