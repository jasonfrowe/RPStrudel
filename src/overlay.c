#include "overlay.h"

#include <fcntl.h>
#include <unistd.h>

/* The start of the shared region (rpstrudel.ld). */
extern char __overlay_start[];
extern char __overlay_end[];

static uint8_t loaded = OVERLAY_NONE;

bool overlay_load(uint8_t which)
{
    if (loaded == which)
        return true;
    loaded = OVERLAY_NONE;
    int fd = open(which == OVERLAY_PARSE ? "ROM:parse.ovl" : "ROM:query.ovl", O_RDONLY);
    if (fd < 0)
        return false;
    char *p = __overlay_start;
    int got;
    while (p < __overlay_end && (got = read(fd, p, 512)) > 0)
        p += got;
    close(fd);
    loaded = which;
    return true;
}
