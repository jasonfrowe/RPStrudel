#ifndef POOL_H
#define POOL_H

#include <stdint.h>

/* Where the pattern pools live: XRAM on the Picocomputer (through portal
 * 0, main loop only), a plain array in the host tests. Addresses count from
 * the start of the pool space. */

#define POOL_SIZE 13312

void pool_read(uint16_t addr, void *dst, uint8_t n);
void pool_write(uint16_t addr, const void *src, uint8_t n);
uint8_t pool_peek(uint16_t addr);
void pool_poke(uint16_t addr, uint8_t v);

#endif
