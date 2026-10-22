#ifndef PLAYER_H
#define PLAYER_H

#include <stdbool.h>
#include <stdint.h>

#include "clock.h"

/* Turns a parsed program into OPL2 events, one cycle at a time: queries
 * each pattern's onsets, picks a patch and note for each, gives it a voice,
 * and schedules its note-off (in a later cycle when it lasts that long). */

/* Parses the program text (text.h, 0..len) and keeps its tempo. If it
 * fails, the program that was playing is put back and false is returned
 * with player_error and player_error_pos saying why. */
bool player_load(uint16_t len);

/* Undoes the last fill's effect on voices and note-offs, before that cycle
 * is filled again (with a new program). */
void player_rewind(void);

/* Works out a cycle and gives its events to the clock (clock_add_event). */
void player_fill(uint16_t cycle);

/* A problem found while playing (an unknown sound, a pattern that grew too
 * big), with where it is in the code; NULL when there is none. */
extern const char *player_error;
extern uint16_t player_error_pos;

/* The patch each voice was last given, and whether it was a drum, for the
 * screen. */
extern uint8_t player_voice_patch[];
extern bool player_voice_drum[];

#endif
