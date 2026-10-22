#ifndef OVERLAY_H
#define OVERLAY_H

#include <stdbool.h>
#include <stdint.h>

/* Overlays: the parser (mini.c, prog.c) and the queries (query.c) are
 * never needed at the same time, so on the Picocomputer they share one
 * region of RAM (see rpstrudel.ld). Each is a file in the ROM, read in when
 * it is needed: the parser for a Ctrl+Enter, then the queries again.
 *
 * Code in an overlay file goes in its section with a #pragma clang section
 * at the top of the file. Functions called from outside the overlay are
 * marked OVERLAY_ENTRY, so the linker's whole-program optimizer cannot
 * copy them into code that stays in RAM. */

#ifdef __mos__
#define OVERLAY_ENTRY __attribute__((noinline))
/* A function in a file that stays in RAM, used only by the parser. */
#define PARSE_ONLY __attribute__((section(".ovl_parse")))
#else
#define OVERLAY_ENTRY
#define PARSE_ONLY
#endif

enum { OVERLAY_NONE, OVERLAY_PARSE, OVERLAY_QUERY };

/* Makes sure that overlay is in RAM. False if its file could not be read. */
bool overlay_load(uint8_t which);

#endif
