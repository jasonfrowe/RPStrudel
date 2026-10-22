#ifndef SOUNDS_H
#define SOUNDS_H

/* Which sound each value of a new program names (patch and drum note), so
 * playing needs no text. Part of the parser overlay (overlay.h): call right
 * after parsing. */
void sounds_resolve(void);

#endif
