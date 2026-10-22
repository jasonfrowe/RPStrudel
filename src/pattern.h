#ifndef PATTERN_H
#define PATTERN_H

#include <stdbool.h>
#include <stdint.h>

#include "rat.h"

/* Patterns as a tree of nodes that mirror Strudel's primitives, queried
 * the way Strudel queries them: give a time span, get back every event
 * ("hap") whose whole overlaps it, each with the part inside the span.
 *
 * Plain C with no Picocomputer headers, so the host tests build it too. */

#define PAT_NODES 128
#define PAT_KIDS 255
#define PAT_ATOMS 104
#define PAT_HAPS 104
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
};

/* Controls an event can carry, as Strudel's s(), note(), n(), gain(). */
enum { CTL_S, CTL_NOTE, CTL_N, CTL_GAIN, PAT_CTLS };

typedef struct
{
    uint8_t type;
    uint8_t nkids;
    uint8_t kids;   /* first index into pat_kids, or the atom for N_ATOM */
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
    int8_t note;   /* as a note (name or number), or -1 */
} pat_atom_t;

/* The four times of a hap, as indexes into t[]. */
enum { WB, WE, PB, PE };

typedef struct
{
    rat_t t[4]; /* whole begin, end; part (inside the queried span) begin, end */
    uint8_t atom;
    uint8_t tail;          /* atom after ':', or PAT_NONE */
    uint8_t ctl[PAT_CTLS]; /* atom of each control, or PAT_NONE */
} pat_hap_t;

extern pat_node_t pat_nodes[PAT_NODES];
extern uint8_t pat_kids[PAT_KIDS];
extern pat_atom_t pat_atoms[PAT_ATOMS];
extern uint8_t pat_node_count, pat_kid_count, pat_atom_count;

extern pat_hap_t pat_haps[PAT_HAPS];
extern uint8_t pat_hap_top; /* indexes under 256 suit the 6502 */

/* Why the last parse or query failed, or NULL. */
extern const char *pat_error;

void pat_reset(void);
uint8_t pat_new_node(uint8_t type);
uint8_t pat_new_atom(void);
/* Adds kids to the next free slots; call right after pat_new_node. */
void pat_add_kid(uint8_t node, uint8_t kid);

/* Haps of node whose whole overlaps b..e, appended at pat_hap_top.
 * Returns how many. */
uint8_t pat_query(uint8_t node, const rat_t *b, const rat_t *e);

/* Cycles after which the node repeats exactly, or 0 if unknown. */
uint16_t pat_period(uint8_t node);

/* Haps that start inside cycle c, at pat_haps[0..count). A node that
 * repeats is queried at c mod its period, which keeps the numbers small
 * enough for 16-bit fractions; *base says which cycle the haps' times are
 * in (c mod period, or c itself), so a time t is really t + c - *base. */
uint8_t pat_query_onsets(uint8_t node, int32_t c, int32_t *base);

/* The same, always at c itself (for checking the above). */
uint8_t pat_query_onsets_direct(uint8_t node, int32_t c);

bool pat_euclid_bits(int8_t pulses, uint8_t steps, int8_t rotation, uint8_t *out);

#endif
