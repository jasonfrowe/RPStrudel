#include "pool.h"

#ifdef __mos__

#include <rp6502.h>

#include "xram.h"

_Static_assert(POOL_SIZE <= POOL_XRAM_SIZE, "the pools must fit their XRAM space");

void pool_read(uint16_t addr, void *dst, uint8_t n)
{
    uint8_t *d = dst;
    RIA.step0 = 1;
    RIA.addr0 = XRAM_POOL + addr;
    while (n--)
        *d++ = RIA.rw0;
}

void pool_write(uint16_t addr, const void *src, uint8_t n)
{
    const uint8_t *s = src;
    RIA.step0 = 1;
    RIA.addr0 = XRAM_POOL + addr;
    while (n--)
        RIA.rw0 = *s++;
}

uint8_t pool_peek(uint16_t addr)
{
    RIA.addr0 = XRAM_POOL + addr;
    return RIA.rw0;
}

void pool_poke(uint16_t addr, uint8_t v)
{
    RIA.addr0 = XRAM_POOL + addr;
    RIA.rw0 = v;
}

#else

#include <string.h>

static uint8_t mem[POOL_SIZE];

void pool_read(uint16_t addr, void *dst, uint8_t n)
{
    memcpy(dst, &mem[addr], n);
}

void pool_write(uint16_t addr, const void *src, uint8_t n)
{
    memcpy(&mem[addr], src, n);
}

uint8_t pool_peek(uint16_t addr)
{
    return mem[addr];
}

void pool_poke(uint16_t addr, uint8_t v)
{
    mem[addr] = v;
}

#endif
