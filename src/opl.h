#ifndef OPL_H
#define OPL_H

#include <stdint.h>

/* The built-in RIA OPL2 (emu8950 at 3.579545 MHz).
 *
 * Every register write goes through XRAM portal 1. Once the clock is
 * running, only the clock's interrupt calls these functions; the main
 * loop sends requests through clock.h instead. */

#define OPL_VOICES 9

typedef struct
{
    uint8_t m_ave, m_ksl, m_atdec, m_susrel, m_wave;
    uint8_t c_ave, c_ksl, c_atdec, c_susrel, c_wave;
    uint8_t feedback;
} opl_patch_t; /* same layout as RPTracker's OPL_Patch */

void opl_init(void);
void opl_patch(uint8_t voice, const opl_patch_t *p);

/* note is MIDI (60 = C4); att is 0.75 dB steps quieter (or louder, when
 * negative) than the patch's own carrier level. A note-on always keys off
 * first, so a repeated note on the same voice retriggers. */
void opl_note_on(uint8_t voice, uint8_t note, int8_t att);
void opl_note_off(uint8_t voice);
void opl_silence(void);

#endif
