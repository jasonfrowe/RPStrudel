#ifndef PATCHES_H
#define PATCHES_H

#include <stdbool.h>
#include <stdint.h>

#include "opl.h"

/* Patch ids: 0x00-0x7F are General MIDI programs, then the drum kit, then
 * the basic synths. The patches and sound names live in XRAM (made by
 * tools/patches.py, loaded by the ROM) to leave the 6502's RAM for
 * patterns:
 *
 *   PATCH_COUNT patches of 11 bytes (opl_patch_t), in id order
 *   then names: length, characters, patch id; ... ; 0 */

#define PATCH_COUNT 133
#define PATCH_XRAM_SIZE 3584

#define PATCH_GM_COUNT 128
#define PATCH_BD 0x80
#define PATCH_SD 0x81
#define PATCH_HH 0x82
#define PATCH_SINE 0x83
#define PATCH_TRIANGLE 0x84
#define PATCH_UNKNOWN 0xFF

#define PATCH_IS_DRUM(id) ((id) >= PATCH_BD && (id) <= PATCH_HH)

/* Drum patches sound best at this pitch (RPTracker plays them at middle C). */
#define KIT_NOTE 60

/* A Strudel sound name ("bd", "sine", "gm_synth_bass_1"), or PATCH_UNKNOWN.
 * Main loop only (XRAM portal 0). */
uint8_t patch_find(const char *name, uint8_t len);

/* A short name for the screen, without "gm_". Main loop only. */
const char *patch_name(uint8_t id);

/* Loads a patch into a voice. Interrupt only (XRAM portal 1). */
void patch_load(uint8_t voice, uint8_t id);

#endif
