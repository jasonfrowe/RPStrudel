#include "editbuf.h"

#include <rp6502.h>
#include <unistd.h>

#include "text.h"
#include "xram.h"

char edit_scratch[EDIT_SCRATCH];

static uint16_t len; /* characters held */
static uint16_t gap; /* where the gap starts: the cursor */

#define GAP_LEN (EDIT_CAP - len)

static uint16_t physical(uint16_t pos)
{
    return XRAM_EDIT + (pos < gap ? pos : pos + GAP_LEN);
}

char text_at(uint16_t pos)
{
    if (pos >= len)
        return 0;
    RIA.addr0 = physical(pos);
    return (char)RIA.rw0;
}

void edit_clear(void)
{
    len = 0;
    gap = 0;
}

uint16_t edit_length(void)
{
    return len;
}

uint16_t edit_cursor(void)
{
    return gap;
}

void edit_set_cursor(uint16_t pos)
{
    if (pos > len)
        pos = len;
    /* Each character the cursor passes moves across the gap. */
    while (gap > pos) {
        gap--;
        RIA.addr0 = XRAM_EDIT + gap;
        uint8_t c = RIA.rw0;
        RIA.addr0 = XRAM_EDIT + gap + GAP_LEN;
        RIA.rw0 = c;
    }
    while (gap < pos) {
        RIA.addr0 = XRAM_EDIT + gap + GAP_LEN;
        uint8_t c = RIA.rw0;
        RIA.addr0 = XRAM_EDIT + gap;
        RIA.rw0 = c;
        gap++;
    }
}

bool edit_insert(char c)
{
    if (len >= EDIT_CAP)
        return false;
    RIA.addr0 = XRAM_EDIT + gap;
    RIA.rw0 = (uint8_t)c;
    gap++;
    len++;
    return true;
}

bool edit_backspace(void)
{
    if (gap == 0)
        return false;
    gap--;
    len--;
    return true;
}

bool edit_delete(void)
{
    if (gap >= len)
        return false;
    len--; /* the gap grows over it */
    return true;
}

uint8_t edit_read_line(uint16_t pos, char *buf, uint8_t max, uint16_t *next)
{
    /* Sequential reads with the portal stepping by one; one jump at the gap. */
    uint8_t n = 0;
    RIA.step0 = 1;
    RIA.addr0 = physical(pos);
    while (pos < len) {
        if (pos == gap)
            RIA.addr0 = physical(pos);
        char c = (char)RIA.rw0;
        pos++;
        if (c == '\n') {
            *next = pos;
            return n;
        }
        if (n < max)
            buf[n++] = c;
    }
    *next = 0xFFFF; /* the last line */
    return n;
}

bool edit_save(int fd)
{
    /* the text before the gap, then after it, straight from XRAM */
    uint16_t parts[2][2] = {{XRAM_EDIT, gap}, {XRAM_EDIT + gap + GAP_LEN, len - gap}};
    for (uint8_t i = 0; i < 2; i++) {
        uint16_t at = parts[i][0], left = parts[i][1];
        while (left) {
            uint16_t n = left > 0x1000 ? 0x1000 : left;
            if (write_xram(at, n, fd) != (int)n)
                return false;
            at += n;
            left -= n;
        }
    }
    return true;
}

bool edit_load(int fd)
{
    char *chunk = edit_scratch;
    edit_clear();
    int got;
    while ((got = read(fd, chunk, EDIT_SCRATCH)) > 0) {
        for (int i = 0; i < got; i++) {
            char c = chunk[i];
            if (c == '\r')
                continue; /* Windows line endings */
            if (c == '\t') {
                if (!edit_insert(' ') || !edit_insert(' '))
                    return false;
                continue;
            }
            if (!edit_insert(c))
                return false; /* too big: keeps what fit */
        }
    }
    edit_set_cursor(0);
    return got >= 0;
}
