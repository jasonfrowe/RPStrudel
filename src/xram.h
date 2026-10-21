/* RP6502 XRAM Mapping */

#ifndef XRAM_H
#define XRAM_H

#include <rp6502.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Insert xram.h snippets from docs to build your system. */
/* https://picocomputer.github.io/sdk.html#xram-memory-map */

typedef struct
{
    int foo;
} xram_feature_t; /* layout */

/* Insert XRAM structs from those snippets here along with */
/* your own custom usage to define your XRAM memory map. */
typedef struct
{
    xram_feature_t foo;
} xram_layout_t;

/* Define constants to be your XRAM addresses. */
#define XRAM_FOO offsetof(xram_layout_t, foo)

#endif
