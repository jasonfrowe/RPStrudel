#include "pattern.h"

#include <string.h>

pat_node_t pat_nodes[PAT_NODES];
uint8_t pat_kids[PAT_KIDS];
pat_atom_t pat_atoms[PAT_ATOMS];
uint8_t pat_node_count, pat_kid_count, pat_atom_count;

pat_hap_t pat_haps[PAT_HAPS];
uint8_t pat_hap_top;

const char *pat_error;

static const rat_t R0 = {0, 1};
static const rat_t R1 = {1, 1};

void pat_reset(void)
{
    pat_node_count = 0;
    pat_kid_count = 0;
    pat_atom_count = 0;
    pat_hap_top = 0;
    pat_error = 0;
    rat_overflow = false;
}

uint8_t pat_new_node(uint8_t type)
{
    if (pat_node_count >= PAT_NODES) {
        pat_error = "pattern too big";
        return PAT_NONE;
    }
    uint8_t i = pat_node_count++;
    pat_node_t *n = &pat_nodes[i];
    n->type = type;
    n->nkids = 0;
    n->kids = pat_kid_count;
    n->period = 0xFFFF;
    n->r1 = R1;
    n->r2 = R0;
    return i;
}

uint8_t pat_new_atom(void)
{
    if (pat_atom_count >= PAT_ATOMS) {
        pat_error = "too many values";
        return PAT_NONE;
    }
    return pat_atom_count++;
}

void pat_add_kid(uint8_t node, uint8_t kid)
{
    if (pat_kid_count >= PAT_KIDS) {
        pat_error = "pattern too big";
        return;
    }
    pat_kids[pat_kid_count++] = kid;
    pat_nodes[node].nkids++;
}

static uint8_t kid(const pat_node_t *n, uint8_t i)
{
    return pat_kids[n->kids + i];
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

static const rat_t *num_of(uint8_t atom)
{
    return pat_atoms[atom].is_num ? &pat_atoms[atom].num : 0;
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
        const rat_t *v = num_of(p->atom);
        if (!v || v->n == 0)
            continue;
        rat_t f;
        if (n->r2.n)
            rat_div(&f, &R1, v);
        else
            f = *v;
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
    const rat_t *r = num_of(atom);
    if (!r || r->d != 1 || r->n < lo || r->n > hi)
        return false;
    *out = (int16_t)r->n;
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

/* ---- appLeft: "bd:2" tails and control sets ---- */

/* Strudel's appLeft: structure from the kid, values from the friend
 * wherever the two overlap. The friend's haps come first on the stack,
 * then the kid's, then the results, which move down at the end. With tail
 * set, the friend's value becomes the second of a pair ("bd:2"); else its
 * controls are set into the kid's. */
static uint8_t q_app_left(const pat_node_t *n, const rat_t *b, const rat_t *e, bool tail)
{
    uint8_t f0 = pat_hap_top;
    uint8_t nf = pat_query(kid(n, 1), b, e);
    uint8_t l0 = pat_hap_top;
    uint8_t nl = pat_query(kid(n, 0), b, e);
    uint8_t out = pat_hap_top;
    for (uint8_t i = l0; i < l0 + nl && !pat_error; i++) {
        for (uint8_t j = f0; j < f0 + nf; j++) {
            rat_t np[2];
            pat_hap_t *l = &pat_haps[i], *f = &pat_haps[j];
            if (!intersect(&l->t[PB], &l->t[PE], &f->t[PB], &f->t[PE], np))
                continue;
            if (out >= PAT_HAPS) {
                pat_error = "too many events";
                break;
            }
            pat_hap_t *h = &pat_haps[out++];
            *h = *l;
            h->t[PB] = np[0];
            h->t[PE] = np[1];
            if (tail) {
                h->tail = f->atom;
            } else {
                for (uint8_t c = 0; c < PAT_CTLS; c++)
                    if (f->ctl[c] != PAT_NONE)
                        h->ctl[c] = f->ctl[c];
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
        /* s("bd:2") is s "bd" with n 2 */
        if (c == CTL_S && h->tail != PAT_NONE)
            h->ctl[CTL_N] = h->tail;
    }
    return count;
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
    const pat_node_t *a = &pat_nodes[arg];
    if (a->type == N_ATOM) {
        const rat_t *v = num_of(a->kids);
        if (!v || v->n == 0)
            return 1;
        rat_t f;
        if (n->r2.n)
            rat_div(&f, &R1, v);
        else
            f = *v;
        rat_mul(&f, &f, &n->r1);
        return fast_period(p, &f);
    }
    uint16_t l = 1;
    for (uint8_t i = 0; i < a->nkids && l; i++)
        l = lcm8(l, fast_period_all(p, kid(a, i), n));
    return l;
}

uint16_t pat_period(uint8_t node)
{
    if (node == PAT_NONE)
        return 0;
    pat_node_t *n = &pat_nodes[node];
    if (n->period != 0xFFFF)
        return n->period;
    uint16_t p = 1;
    switch (n->type) {
    case N_SILENCE:
    case N_ATOM:
        break;
    case N_STACK:
    case N_EUCLID:
    case N_TAIL:
    case N_SET:
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
        p = pat_period(kid(n, 0));
        break;
    case N_REPEAT:
        p = times8((uint16_t)n->r1.n, pat_period(kid(n, 0)));
        break;
    default:
        p = 0;
    }
    n->period = p;
    return p;
}

/* ---- query ---- */

static uint8_t per_cycle(const pat_node_t *n, const rat_t *b, const rat_t *e)
{
    switch (n->type) {
    case N_SLOWCAT:
        return q_slowcat_cycle(n, b, e);
    case N_COMPRESS:
        return q_compress_cycle(n, b, e);
    default:
        return q_repeat_cycle(n, b, e);
    }
}

uint8_t pat_query(uint8_t node, const rat_t *b, const rat_t *e)
{
    if (pat_error || node == PAT_NONE)
        return 0;
    const pat_node_t *n = &pat_nodes[node];
    switch (n->type) {
    case N_ATOM:
        return q_pure(n->kids, b, e);
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
        return q_app_left(n, b, e, true);
    case N_CTL:
        return q_ctl(n, b, e);
    case N_SET:
        return q_app_left(n, b, e, false);
    default:
        return 0;
    }
}

uint8_t pat_query_onsets(uint8_t node, int32_t c, int32_t *base)
{
    uint16_t p = pat_period(node);
    int32_t c0 = c;
    if (p) {
        c0 = c % p;
        if (c0 < 0)
            c0 += p;
    }
    *base = c0;
    return pat_query_onsets_direct(node, c0);
}

uint8_t pat_query_onsets_direct(uint8_t node, int32_t c)
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
