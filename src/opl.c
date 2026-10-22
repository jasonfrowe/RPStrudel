#include "opl.h"

#include <rp6502.h>

#include "xram.h"

/* Operator offsets of each voice's modulator; the carrier is 3 above. */
static const uint8_t op_mod[OPL_VOICES] = {0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12};

/* F-numbers of C to B at block 4 for a 3.579545 MHz chip (from RPTracker). */
static const uint16_t fnum_table[12] = {345, 365, 387, 410, 435, 460, 488, 517, 547, 580, 615, 651};

static uint8_t b0_shadow[OPL_VOICES]; /* block and F-number high, no key-on bit */
static uint8_t ksl_car[OPL_VOICES];   /* carrier KSL bits, kept when setting volume */
static uint8_t tl_car[OPL_VOICES];    /* the patch's own carrier level */

static inline void opl_write(uint8_t reg, uint8_t val)
{
    xram1_poke8(XRAM_OPL + reg, val);
}

void opl_init(void)
{
    xreg_ria_opl(XRAM_OPL);
    for (uint8_t v = 0; v < OPL_VOICES; v++) {
        opl_write(0xB0 + v, 0);
        b0_shadow[v] = 0;
        ksl_car[v] = 0;
        tl_car[v] = 0;
    }
    xram1_set(XRAM_OPL + 0x01, 0x00, 0xF5);
    opl_write(0x01, 0x20); /* waveform select on */
    opl_write(0xBD, 0x00); /* melodic mode */
}

void opl_patch(uint8_t voice, const opl_patch_t *p)
{
    uint8_t m = op_mod[voice];
    uint8_t c = m + 3;
    opl_write(0x20 + m, p->m_ave);
    opl_write(0x20 + c, p->c_ave);
    opl_write(0x40 + m, p->m_ksl);
    opl_write(0x40 + c, p->c_ksl);
    opl_write(0x60 + m, p->m_atdec);
    opl_write(0x60 + c, p->c_atdec);
    opl_write(0x80 + m, p->m_susrel);
    opl_write(0x80 + c, p->c_susrel);
    opl_write(0xE0 + m, p->m_wave);
    opl_write(0xE0 + c, p->c_wave);
    opl_write(0xC0 + voice, p->feedback);
    ksl_car[voice] = p->c_ksl & 0xC0;
    tl_car[voice] = p->c_ksl & 0x3F;
}

void opl_note_on(uint8_t voice, uint8_t note, int8_t att)
{
    /* Split the note into block and semitone without a divide. */
    uint8_t n = note < 12 ? 0 : note - 12;
    uint8_t block = 0;
    while (n >= 12) {
        n -= 12;
        block++;
    }
    if (block > 7)
        block = 7;
    uint16_t f = fnum_table[n];
    uint8_t b0 = (uint8_t)((block << 2) | (f >> 8));

    opl_write(0xB0 + voice, b0_shadow[voice]); /* key off, to retrigger */
    int16_t tl = (int16_t)tl_car[voice] + att;
    if (tl < 0)
        tl = 0;
    if (tl > 63)
        tl = 63;
    opl_write(0x40 + op_mod[voice] + 3, ksl_car[voice] | (uint8_t)tl);
    opl_write(0xA0 + voice, (uint8_t)f);
    opl_write(0xB0 + voice, b0 | 0x20);
    b0_shadow[voice] = b0;
}

void opl_note_off(uint8_t voice)
{
    opl_write(0xB0 + voice, b0_shadow[voice]);
}

void opl_silence(void)
{
    for (uint8_t v = 0; v < OPL_VOICES; v++)
        opl_note_off(v);
}
