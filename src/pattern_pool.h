#ifndef PATTERN_POOL_H
#define PATTERN_POOL_H

/* Inside the pattern code: where things are in the current bank of the
 * pool. Shared by pattern.c (always in RAM) and query.c (an overlay). */

#include <stddef.h>

#include "pattern.h"
#include "pool.h"

extern uint16_t pat_bank_base;

#define NODES_BYTES ((uint16_t)PAT_NODES * sizeof(pat_node_t))
#define NODE_AT(i) (pat_bank_base + (uint16_t)(i) * sizeof(pat_node_t))
#define KID_AT(i) (pat_bank_base + NODES_BYTES + (i))
#define ATOM_AT(i) (pat_bank_base + NODES_BYTES + PAT_KIDS + (uint16_t)(i) * sizeof(pat_atom_t))

#endif
