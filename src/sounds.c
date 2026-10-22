#include "sounds.h"

#include <rp6502.h>

#include "overlay.h"
#include "patches.h"
#include "pattern.h"
#include "text.h"
#include "xram.h"

/* Part of the parser overlay (overlay.h). */
#ifdef __mos__
#pragma clang section text = ".ovl_parse"
#endif

#define NAMES_AT (XRAM_PATCHES + PATCH_COUNT * sizeof(opl_patch_t))

static uint8_t patch_find(const char *name, uint8_t len, uint8_t *drum_note)
{
    RIA.addr0 = NAMES_AT;
    RIA.step0 = 1;
    for (;;) {
        uint8_t l = RIA.rw0;
        if (!l)
            return PATCH_UNKNOWN;
        bool match = l == len;
        for (uint8_t i = 0; i < l; i++) {
            char c = (char)RIA.rw0;
            if (match && c != name[i])
                match = false;
        }
        uint8_t id = RIA.rw0;
        uint8_t note = RIA.rw0;
        if (match) {
            *drum_note = note;
            return id;
        }
    }
}

/* Works out which sound each value names, so playing needs no text. (The
 * parser has already worked out their numbers and notes.) */
OVERLAY_ENTRY void sounds_resolve(void)
{
    for (uint8_t i = 0; i < pat_atom_count; i++) {
        pat_atom_t at;
        pat_atom_get(i, &at);
        if (at.is_num)
            continue;
        char buf[32];
        uint8_t n = text_copy(at.pos, at.len, buf, sizeof buf);
        at.drum = 0;
        at.patch = n < at.len ? PATCH_UNKNOWN : patch_find(buf, n, &at.drum);
        pat_atom_put(i, &at);
    }
}

