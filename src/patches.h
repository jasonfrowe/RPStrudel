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
 *   then names: length, characters, patch id, fixed note (drums) or 0;
 *   ... ; 0 */

#define PATCH_COUNT 135
#define PATCH_XRAM_SIZE 4096

#define PATCH_GM_COUNT 128
#define PATCH_BD 0x80
#define PATCH_SD 0x81
#define PATCH_HH 0x82
#define PATCH_SINE 0x83
#define PATCH_TRIANGLE 0x84
#define PATCH_UNKNOWN 0xFF

/* (Sound names are looked up in sounds.c.) */

/* A short name for the screen, without "gm_". Main loop only. */
const char *patch_name(uint8_t id);

/* Loads a patch into a voice. Interrupt only (XRAM portal 1). */
void patch_load(uint8_t voice, uint8_t id);

#endif
