#ifndef PATTERN_H
#define PATTERN_H

#include <stdbool.h>
#include <stdint.h>

#include "rat.h"

/* Patterns as a tree of nodes that mirror Strudel's primitives, queried
 * the way Strudel queries them: give a time span, get back every event
 * ("hap") whose whole overlaps it, each with the part inside the span.
 *
 * The nodes, their kid lists and the values (atoms) live in the pool
 * (pool.h: XRAM on the Picocomputer), in two banks: a new program is
 * parsed into the bank not playing, so a mistake leaves the playing one
 * alone. The haps being worked on are in RAM.
 *
 * Plain C with no Picocomputer headers, so the host tests build it too. */

#define PAT_NODES 250
#define PAT_KIDS 512
#define PAT_ATOMS 200
#define PAT_HAPS 96
#define PAT_NONE 0xFF

enum {
    N_SILENCE,
    N_ATOM,     /* atom: Strudel pure(value) */
    N_STACK,    /* kids: all at once */
    N_SLOWCAT,  /* kids: one per cycle, each keeping its own cycle count */
    N_FASTC,    /* kid; r1 = constant factor */
    N_FAST,     /* kid, factor pattern; factor = value (or 1/value if r2.n) * r1 */
    N_COMPRESS, /* kid squeezed into r1..r2 of each cycle (fastGap + late) */
    N_REPEAT,   /* kid; each cycle repeated r1.n times */
    N_EUCLID,   /* kid, pulses, steps[, rotation]: struct of a Bjorklund rhythm */
    N_TAIL,     /* kid, friend: values become pairs, as "bd:2" */
    N_CTL,      /* kid; r1.n = control: each value becomes that control */
    N_SET,      /* kid, friend: friend's controls set into kid's (appLeft) */
    N_REV,      /* kid, each cycle reversed */
    N_EVERY,    /* kid, changed kid; changed in cycles where cycle % r1.n == r2.n */
    N_LATE,     /* kid moved later by r1 cycles */
    N_PLY,      /* kid, each event repeated r1.n times in its own time */
    N_STRUCT,   /* kid, bools: structure from the bools, values from kid */
    N_MASK,     /* kid, bools: kid's events where the bools are true */
    N_ARITH,    /* kid, friend: friend's values added (r1.n 1) or taken away (-1);
                   r2.n 0: to the plain value, 1: to the note and n controls,
                   2: to the note (transpose, by plain numbers) */
    N_SCALE,    /* kid, scale names: n (r2.n set) or the plain value as a degree */
    N_GAIN,     /* kid with gain r1 (echo's copies) */
    N_DEGRADE,  /* kid, events kept by a random draw: r1 the chance taken away,
                   r2 = {seed, keep the others instead} */
    N_RANDCAT,  /* kids: one picked at random each cycle (r1.n is the seed) */
    N_SEQ,      /* kids in equal shares of each cycle: a stepcat whose weights
                   are all the same (a stack of N_COMPRESS, in one node) */
};

/* A value an event carries that is not set (int8_t). */
#define NO_VAL (-128)

/* Controls an event can carry, as Strudel's s(), note(), n(), gain(). */
enum { CTL_S, CTL_NOTE, CTL_N, CTL_GAIN, PAT_CTLS };

typedef struct
{
    uint8_t type;
    uint8_t nkids;
    uint16_t kids;   /* first index into the kid list, or the atom for N_ATOM */
    uint16_t period; /* cycles until it repeats; 0 unknown, 0xFFFF not worked out */
    rat_t r1, r2;
} pat_node_t;

typedef struct
{
    uint16_t pos; /* where the text is in the source */
    uint8_t len;  /* 0 once edited away: no longer highlighted */
    bool is_num;
    rat_t num;
    uint8_t patch; /* as a sound name, filled in by the player */
    int8_t ival;   /* as a whole number or note ("c" is 48), or NO_VAL */
    uint8_t scale; /* as a scale ("C:major"): 1 + its index, or 0 */
    uint8_t drum;  /* as a drum sound: the note it plays, or 0 */
} pat_atom_t;

/* The four times of a hap, as indexes into t[]. */
enum { WB, WE, PB, PE };

typedef struct
{
    rat_t t[4]; /* whole begin, end; part (inside the queried span) begin, end */
    uint8_t atom;
    uint8_t tail;          /* atom after ':', or PAT_NONE */
    uint8_t ctl[PAT_CTLS]; /* atom each control came from, or PAT_NONE (for
                              sound names and highlighting) */
    int8_t val;            /* the plain value worked out ("0 2".add(1)), or NO_VAL
                              to take the atom's */
    int8_t note, n;        /* the note (MIDI) and n controls, or NO_VAL */
    uint8_t gain;          /* the gain control in 64ths, or 0xFF */
} pat_hap_t;

/* The plain value of a hap as a whole number (a note name as its MIDI
 * number), or NO_VAL. */
int8_t pat_value(const pat_hap_t *h);

/* A scale degree as a MIDI note: tonic from 0-127, scale from 1. */
int8_t pat_scale_note(int8_t tonic, uint8_t scale, int8_t degree);

/* The scale a name (as "minor:pentatonic") is: 1 + its index, or 0. */
uint8_t pat_scale_find(const char *name, uint8_t len);

#define PAT_BANK_SIZE (PAT_NODES * sizeof(pat_node_t) + PAT_KIDS + PAT_ATOMS * sizeof(pat_atom_t))

/* How much of the current bank is used. */
extern uint8_t pat_node_count, pat_atom_count;
extern uint16_t pat_kid_count;

extern pat_hap_t pat_haps[PAT_HAPS];
extern uint8_t pat_hap_top; /* indexes under 256 suit the 6502 */

/* Seeds the random functions in the order they are written, so the same
 * code makes the same random choices. Reset by pat_reset. */
extern uint8_t pat_seed;

/* Why the last parse or query failed, or NULL. */
extern const char *pat_error;

/* Empties the current bank. */
void pat_reset(void);

/* Parsing a new program: pat_begin moves to the other bank (empty);
 * pat_abort moves back to the one that was playing, untouched. */
void pat_begin(void);
void pat_abort(void);

uint8_t pat_new_node(uint8_t type);
uint8_t pat_new_atom(void);
/* Adds kids to the next free slots; call right after pat_new_node. */
void pat_add_kid(uint8_t node, uint8_t kid);

/* Nodes and atoms in the pool, copied in and out. */
void pat_node_get(uint8_t i, pat_node_t *n);
void pat_node_put(uint8_t i, const pat_node_t *n);
uint8_t pat_node_type(uint8_t i);
void pat_set_r1(uint8_t node, const rat_t *r);
void pat_set_r2(uint8_t node, const rat_t *r);
void pat_set_atom(uint8_t node, uint8_t atom); /* the atom of an N_ATOM */
void pat_atom_get(uint8_t i, pat_atom_t *a);
void pat_atom_put(uint8_t i, const pat_atom_t *a);
/* The number an atom holds, if it is one. */
bool pat_atom_num(uint8_t i, rat_t *out);
/* The number a node is, if it is a numeric atom. */
bool pat_const_num(uint8_t node, rat_t *out);

/* Haps of node whose whole overlaps b..e, appended at pat_hap_top.
 * Returns how many. */
uint8_t pat_query(uint8_t node, const rat_t *b, const rat_t *e);

/* Cycles after which the node repeats exactly, or 0 if unknown. Works it
 * out (and keeps it in the nodes below): call for each root after parsing,
 * as the queries only read what was kept, with pat_period_memo. */
uint16_t pat_period(uint8_t node);
uint16_t pat_period_memo(uint8_t node);

/* Haps that start inside cycle c, at pat_haps[0..count). A node that
 * repeats is queried at c mod its period, which keeps the numbers small
 * enough for 16-bit fractions; *base says which cycle the haps' times are
 * in (c mod period, or c itself), so a time t is really t + c - *base. */
uint8_t pat_query_onsets(uint8_t node, int32_t c, int32_t *base);

/* The same, always at c itself (for checking the above). */
uint8_t pat_query_onsets_direct(uint8_t node, int32_t c);

bool pat_euclid_bits(int8_t pulses, uint8_t steps, int8_t rotation, uint8_t *out);

#endif
