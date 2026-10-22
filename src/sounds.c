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

static int8_t hex_digit(char c)
{
    c |= 0x20;
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

/* "#50": an RPTracker patch number. */
static uint8_t patch_number(const char *name, uint8_t len, uint8_t *drum_note)
{
    uint8_t v = 0;
    for (uint8_t i = 1; i < len; i++) {
        int8_t d = hex_digit(name[i]);
        if (d < 0)
            return PATCH_UNKNOWN;
        v = (uint8_t)(v * 16 + d);
    }
    if (len < 2 || len > 3)
        return PATCH_UNKNOWN;
    if (v < 0x80)
        return v;
    if (v >= 0xFD) {
        *drum_note = 48; /* C3, as RPTracker plays the kit */
        return v - 0xFD + 0x80;
    }
    return PATCH_UNKNOWN;
}

static uint8_t patch_find(const char *name, uint8_t len, uint8_t *drum_note)
{
    if (len && name[0] == '#')
        return patch_number(name, len, drum_note);
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

