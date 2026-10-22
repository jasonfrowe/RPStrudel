#ifndef CLOCK_H
#define CLOCK_H

#include <stdbool.h>
#include <stdint.h>

/* The music clock: a 1 kHz VIA Timer 1 interrupt walks a 32-bit cycle
 * phase (the full 32 bits are one cycle) and plays each event of the
 * current cycle when the phase reaches it.
 *
 * Events come in two buffers. The interrupt plays one while the main loop
 * fills the other with the next cycle; the interrupt swaps them when the
 * cycle wraps. If the next cycle is not ready in time, the current one
 * plays again. */

#define CLOCK_HZ 1000u
#define EV_MAX 96

/* Event kinds: the high bit is note-on, the low nibble the voice. */
#define EV_ON 0x80
#define EV_VOICE 0x0F

typedef struct
{
    uint16_t time;  /* position in the cycle, 0-65535 */
    uint8_t kind;   /* EV_ON | voice, or just voice for a note-off */
    uint8_t note;   /* MIDI note */
    int8_t att;     /* 0.75 dB steps below the patch's level */
    uint8_t patch;  /* patch id (patches.h), loaded when the voice differs */
    uint8_t serial; /* a note-off only ends the note-on with its serial */
    uint8_t src[2]; /* source tokens to highlight, 0xFF for none */
} event_t;

/* Fill side: the main loop builds the next cycle here. */
typedef struct
{
    event_t ev[EV_MAX];
    uint8_t count;
} cycle_events_t;

void clock_init(void);

/* Tempo in tenths of a cycle per minute (300 = 30 cpm = Strudel's
 * default 0.5 cps). */
void clock_set_cpm10(uint16_t cpm10);
uint16_t clock_cpm10(void);

/* The program provides this: it fills out with the events of a cycle. */
void clock_fill(cycle_events_t *out, uint16_t cycle);

/* Start from cycle 0 (filling it before the first tick), or stop and
 * silence every voice. */
void clock_start(void);
void clock_stop(void);
bool clock_running(void);

/* Call from the main loop: fills the free buffer when it needs it. */
void clock_service(void);

/* Where the music is now. */
void clock_position(uint16_t *cycle, uint16_t *pos);

/* Milliseconds since clock_init, for checking the timer rate. */
uint16_t clock_ms(void);

/* Spans of fired events, oldest first; 0xFF when there are none left. */
uint8_t clock_next_hit(void);

/* Voices keyed on since the last call, one bit each. */
uint16_t clock_take_voice_hits(void);

#endif
