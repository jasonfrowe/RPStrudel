#ifndef PLAYER_H
#define PLAYER_H

#include <stdbool.h>
#include <stdint.h>

#include "clock.h"

/* Turns a parsed program into OPL2 events, one cycle at a time: queries
 * each pattern's onsets, picks a patch and note for each, gives it a voice,
 * and schedules its note-off (in a later cycle when it lasts that long). */

/* Parses the program and keeps its tempo. Returns false with pat_error
 * and prog_error_pos set. */
bool player_load(const char *code, uint16_t len);

/* The clock's fill callback. */
void player_fill(cycle_events_t *out, uint16_t cycle);

/* A problem found while playing (an unknown sound, a pattern that grew too
 * big), with where it is in the code; NULL when there is none. */
extern const char *player_error;
extern uint16_t player_error_pos;

/* The patch each voice was last given, for the screen. */
extern uint8_t player_voice_patch[];

#endif
