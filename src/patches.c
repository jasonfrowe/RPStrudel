#include "patches.h"

#include <rp6502.h>

#include "xram.h"

#define PATCH_BYTES sizeof(opl_patch_t)
#define NAMES_AT (XRAM_PATCHES + PATCH_COUNT * PATCH_BYTES)

_Static_assert(sizeof(opl_patch_t) == 11, "patches are 11 bytes in XRAM");

const char *patch_name(uint8_t id)
{
    static char buf[24];
    buf[0] = 0;
    if (id == PATCH_UNKNOWN)
        return buf;
    RIA.addr0 = NAMES_AT;
    RIA.step0 = 1;
    for (;;) {
        uint8_t l = RIA.rw0;
        if (!l)
            break;
        uint8_t n = 0;
        for (uint8_t i = 0; i < l; i++) {
            char c = (char)RIA.rw0;
            if (n < sizeof(buf) - 1)
                buf[n++] = c;
        }
        buf[n] = 0;
        uint8_t got = RIA.rw0;
        uint8_t note = RIA.rw0;
        if (got == id && note != SOUND_SYNTH) /* (sine, triangle... are aliases) */
            return buf[0] == 'g' && buf[1] == 'm' && buf[2] == '_' ? buf + 3 : buf;
    }
    /* General MIDI 1-3 have no name of their own: Strudel's gm_piano */
    return id < 4 ? "piano" : "";
}

void patch_load(uint8_t voice, uint8_t id)
{
    static opl_patch_t p;
    uint8_t *b = (uint8_t *)&p;
    RIA.addr1 = XRAM_PATCHES + (unsigned)id * PATCH_BYTES;
    RIA.step1 = 1;
    for (uint8_t i = 0; i < PATCH_BYTES; i++)
        b[i] = RIA.rw1;
    opl_patch(voice, &p);
}
