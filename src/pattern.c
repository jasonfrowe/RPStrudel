/* Patterns: the parts that stay in RAM, holding the nodes and values in
 * the pool. The queries are in query.c, an overlay. */

#include "pattern.h"

#include <string.h>

#include "pattern_pool.h"
#include "overlay.h"
#include <stddef.h>

_Static_assert(2 * PAT_BANK_SIZE <= POOL_SIZE, "two banks must fit the pool");

uint8_t pat_node_count, pat_atom_count;
uint16_t pat_kid_count;
uint8_t pat_seed;

/* The bank in use, and the counts of the other while parsing into it. */
static uint8_t bank;
uint16_t pat_bank_base;
static uint8_t other_nodes, other_atoms;
static uint16_t other_kids;


pat_hap_t pat_haps[PAT_HAPS];
uint8_t pat_hap_top;

const char *pat_error;

static const rat_t R0 = {0, 1};
static const rat_t R1 = {1, 1};

static void switch_bank(void)
{
    uint8_t n = pat_node_count, a = pat_atom_count;
    uint16_t k = pat_kid_count;
    pat_node_count = other_nodes;
    pat_atom_count = other_atoms;
    pat_kid_count = other_kids;
    other_nodes = n;
    other_atoms = a;
    other_kids = k;
    bank ^= 1;
    pat_bank_base = bank ? PAT_BANK_SIZE : 0;
}

void pat_begin(void)
{
    switch_bank();
    pat_reset();
}

void pat_abort(void)
{
    switch_bank();
}

void pat_node_get(uint8_t i, pat_node_t *n)
{
    pool_read(NODE_AT(i), n, sizeof *n);
}

void pat_node_put(uint8_t i, const pat_node_t *n)
{
    pool_write(NODE_AT(i), n, sizeof *n);
}

uint8_t pat_node_type(uint8_t i)
{
    return pool_peek(NODE_AT(i) + offsetof(pat_node_t, type));
}

PARSE_ONLY void pat_set_r1(uint8_t node, const rat_t *r)
{
    pool_write(NODE_AT(node) + offsetof(pat_node_t, r1), r, sizeof *r);
}

PARSE_ONLY void pat_set_r2(uint8_t node, const rat_t *r)
{
    pool_write(NODE_AT(node) + offsetof(pat_node_t, r2), r, sizeof *r);
}

PARSE_ONLY void pat_set_atom(uint8_t node, uint8_t atom)
{
    uint16_t k = atom;
    pool_write(NODE_AT(node) + offsetof(pat_node_t, kids), &k, sizeof k);
}

void pat_atom_get(uint8_t i, pat_atom_t *a)
{
    pool_read(ATOM_AT(i), a, sizeof *a);
}

void pat_atom_put(uint8_t i, const pat_atom_t *a)
{
    pool_write(ATOM_AT(i), a, sizeof *a);
}

bool pat_atom_num(uint8_t i, rat_t *out)
{
    if (!pool_peek(ATOM_AT(i) + offsetof(pat_atom_t, is_num)))
        return false;
    pool_read(ATOM_AT(i) + offsetof(pat_atom_t, num), out, sizeof *out);
    return true;
}

uint16_t pat_period_memo(uint8_t node)
{
    uint16_t p;
    pool_read(NODE_AT(node) + offsetof(pat_node_t, period), &p, sizeof p);
    return p == 0xFFFF ? 0 : p;
}

PARSE_ONLY bool pat_const_num(uint8_t node, rat_t *out)
{
    if (node == PAT_NONE || pat_node_type(node) != N_ATOM)
        return false;
    uint16_t a;
    pool_read(NODE_AT(node) + offsetof(pat_node_t, kids), &a, sizeof a);
    return pat_atom_num((uint8_t)a, out);
}

void pat_reset(void)
{
    pat_seed = 0;
    pat_node_count = 0;
    pat_kid_count = 0;
    pat_atom_count = 0;
    pat_hap_top = 0;
    pat_error = 0;
    rat_overflow = false;
}

PARSE_ONLY uint8_t pat_new_node(uint8_t type)
{
    if (pat_node_count >= PAT_NODES) {
        if (!pat_error)
            pat_error = "pattern too big";
        return PAT_NONE;
    }
    uint8_t i = pat_node_count++;
    pat_node_t n;
    n.type = type;
    n.nkids = 0;
    n.kids = pat_kid_count;
    n.period = 0xFFFF;
    n.r1 = R1;
    n.r2 = R0;
    pat_node_put(i, &n);
    return i;
}

PARSE_ONLY uint8_t pat_new_atom(void)
{
    if (pat_atom_count >= PAT_ATOMS) {
        if (!pat_error)
            pat_error = "too many values";
        return PAT_NONE;
    }
    pat_atom_t a = {0};
    a.patch = 0xFF;
    a.ival = NO_VAL;
    pat_atom_put(pat_atom_count, &a);
    return pat_atom_count++;
}

PARSE_ONLY void pat_add_kid(uint8_t node, uint8_t kid)
{
    if (pat_kid_count >= PAT_KIDS) {
        if (!pat_error)
            pat_error = "pattern too big";
        return;
    }
    pool_poke(KID_AT(pat_kid_count++), kid);
    uint16_t at = NODE_AT(node) + offsetof(pat_node_t, nkids);
    pool_poke(at, pool_peek(at) + 1);
}


