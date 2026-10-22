/* Counts the fraction operations one cycle of a program needs, to see
 * where the 6502's time goes. */
#include <stdio.h>
#include <string.h>

#include "pattern.h"
#include "prog.h"
#include "text.h"

extern unsigned long rat_calls[8], gcd_calls, gcd_loops, big_mul, div32;

static const char program[] =
    "$: s(\"bd(3,8), [~ sd]*2\")\n"
    "$: s(\"hh*8\").gain(\"[.5 1]*4\")\n"
    "$: note(\"<[c2 c3]*4 [ab1 ab2]*4>\").s(\"gm_synth_bass_1\")\n"
    "$: note(\"<c4 eb4 g4 [bb4 c5]>(3,8)\").s(\"gm_epiano1\").gain(.7)\n";

char text_at(uint16_t pos)
{
    return pos < sizeof(program) - 1 ? program[pos] : 0;
}

int main(void)
{
    if (!prog_parse(sizeof(program) - 1)) {
        printf("parse error: %s\n", pat_error);
        return 1;
    }
    static const char *names[8] = {"set", "add", "sub", "mul", "div", "add_int", "cmp", "floor"};
    for (int p = 0; p < prog_pattern_count; p++) {
        memset(rat_calls, 0, sizeof rat_calls);
        gcd_calls = gcd_loops = big_mul = div32 = 0;
        int32_t base;
        unsigned n = pat_query_onsets(prog_patterns[p].root, 1005, &base);
        printf("period %u, ", pat_period(prog_patterns[p].root));
        printf("pattern %d: %u onsets, max haps %u |", p, n, PAT_HAPS);
        for (int i = 0; i < 8; i++)
            printf(" %s %lu", names[i], rat_calls[i]);
        printf(" | gcd %lu (loops %lu) mul32 %lu div %lu\n", gcd_calls, gcd_loops, big_mul, div32);
    }
    return 0;
}
