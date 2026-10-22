#ifndef TEXT_H
#define TEXT_H

#include <stdint.h>

/* The program text the parsers read, one character at a time: on the
 * Picocomputer the editor's buffer in XRAM (editbuf.c), in the host tests
 * a string. Positions are what highlights and errors point at. */

char text_at(uint16_t pos); /* 0 past the end */

/* Up to len characters from pos into buf (max bytes, with a NUL). Returns
 * how many it copied. */
static inline uint8_t text_copy(uint16_t pos, uint8_t len, char *buf, uint8_t max)
{
    uint8_t n = 0;
    while (n < len && n + 1 < max)
        buf[n++] = text_at(pos++);
    buf[n] = 0;
    return n;
}

#endif
