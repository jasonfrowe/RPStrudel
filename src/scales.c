/* Scales for scale(): the notes stay in RAM for the queries; the names are
 * only for the parser, so they are in its overlay. */

#include "pattern.h"
#include "overlay.h"

/* Scales: names (words joined by ':') and the semitones in them, one bit
 * each. */
PARSE_ONLY const char pat_scale_names[] = "major|ionian|minor|aeolian|dorian|phrygian|lydian|mixolydian|locrian|"
                                  "harmonic:minor|melodic:minor|pentatonic|major:pentatonic|minor:pentatonic|"
                                  "blues|major:blues|chromatic|whole:tone";
static const uint16_t scale_masks[] = {0xAB5, 0xAB5, 0x5AD, 0x5AD, 0x6AD, 0x5AB, 0xAD5, 0x6B5, 0x56B,
                                       0x9AD, 0xAAD, 0x295, 0x295, 0x4A9, 0x4E9, 0x29D, 0xFFF, 0x555};

int8_t pat_scale_note(int8_t tonic, uint8_t scale, int8_t degree)
{
    if (!scale || tonic == NO_VAL || degree == NO_VAL)
        return NO_VAL;
    uint16_t mask = scale_masks[scale - 1];
    uint8_t len = 0;
    for (uint8_t i = 0; i < 12; i++)
        len += (mask >> i) & 1;
    int8_t oct = degree >= 0 ? degree / len : -((len - 1 - degree) / len);
    uint8_t idx = (uint8_t)(degree - oct * len);
    uint8_t semi = 0;
    for (;; semi++)
        if (((mask >> semi) & 1) && idx-- == 0)
            break;
    int16_t note = tonic + semi + 12 * oct;
    return note < -127 ? -127 : note > 127 ? 127 : (int8_t)note;
}

