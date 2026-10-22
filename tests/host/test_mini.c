/* Host test: runs each case through RPStrudel's parser and pattern engine,
 * and prints the onsets of cycles 0-3 in the format that tests/ref/gen.mjs
 * makes from Strudel itself.
 *
 *   test_mini mini tests/mini_cases.txt   mini-notation, raw values
 *   test_mini prog tests/prog_cases.txt   program expressions, controls */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mini.h"
#include "pattern.h"
#include "prog.h"
#include "text.h"

/* r + k, printed as Strudel prints fractions */
static void print_rat(FILE *f, const rat_t *r, long k)
{
    long n = r->n + k * r->d;
    if (r->d == 1)
        fprintf(f, "%ld", n);
    else
        fprintf(f, "%ld/%ld", n, (long)r->d);
}

static void print_atom(FILE *f, const char *src, uint8_t a)
{
    const pat_atom_t *at = &pat_atoms[a];
    if (at->is_num)
        fprintf(f, "%.15g", (double)at->num.n / (double)at->num.d);
    else
        fprintf(f, "%.*s", at->len, src + at->pos);
}

typedef struct
{
    rat_t b;
    char line[96];
} row_t;

static int cmp_row(const void *pa, const void *pb)
{
    const row_t *a = pa, *b = pb;
    int c = rat_cmp(&a->b, &b->b);
    return c ? c : strcmp(a->line, b->line);
}

static const char *const ctl_names[PAT_CTLS] = {"s", "note", "n", "gain"};

/* The parsers read the current case's line through text_at. */
static const char *text;
static uint16_t text_len;

char text_at(uint16_t pos)
{
    return pos < text_len ? text[pos] : 0;
}

/* The haps at pat_haps[0..n) as sorted lines, joined, for comparing. */
static void snapshot(uint16_t n, long shift, char *out, size_t size)
{
    row_t rows[PAT_HAPS];
    for (uint16_t i = 0; i < n; i++) {
        const pat_hap_t *h = &pat_haps[i];
        rows[i].b = h->t[WB];
        snprintf(rows[i].line, sizeof rows[i].line, "%ld/%ld %ld/%ld %u %u %u %u %u %u",
                 (long)h->t[WB].n + shift * h->t[WB].d, (long)h->t[WB].d, (long)h->t[WE].n + shift * h->t[WE].d,
                 (long)h->t[WE].d, h->atom, h->tail, h->ctl[0], h->ctl[1], h->ctl[2], h->ctl[3]);
    }
    qsort(rows, n, sizeof rows[0], cmp_row);
    out[0] = 0;
    for (uint16_t i = 0; i < n; i++) {
        strncat(out, rows[i].line, size - strlen(out) - 2);
        strcat(out, ";");
    }
}

/* Querying at cycle mod period must give what querying directly gives. */
static int check_period(uint8_t root)
{
    static char a[8192], b[8192];
    for (int32_t c = 60; c < 68; c++) {
        int32_t base;
        uint16_t n = pat_query_onsets(root, c, &base);
        snapshot(n, c - base, a, sizeof a);
        n = pat_query_onsets_direct(root, c);
        snapshot(n, 0, b, sizeof b);
        if (strcmp(a, b)) {
            printf("! period %u differs from direct query at cycle %ld\n", pat_period(root), (long)c);
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: test_mini mini|prog cases.txt\n");
        return 2;
    }
    int prog = strcmp(argv[1], "prog") == 0;
    const char *path = argv[2];
    FILE *in = fopen(path, "r");
    if (!in) {
        perror(path);
        return 2;
    }
    char src[256];
    int failures = 0;
    while (fgets(src, sizeof src, in)) {
        size_t len = strcspn(src, "\r\n");
        src[len] = 0;
        char *s = src;
        while (*s == ' ')
            s++;
        if (!*s || *s == '#')
            continue;
        uint16_t from = (uint16_t)(s - src);
        uint16_t to = (uint16_t)len;
        while (to > from && src[to - 1] == ' ')
            to--;
        printf("= %.*s\n", to - from, s);

        pat_reset();
        text = src;
        text_len = (uint16_t)len;
        uint8_t root = prog ? prog_parse_expr(from, to) : mini_parse(from, to);
        if (root == PAT_NONE) {
            printf("! parse error at %u: %s\n", (prog ? prog_error_pos : mini_error_pos) - from, pat_error);
            failures++;
            continue;
        }
        failures += check_period(root);
        pat_error = 0;
        for (int32_t c = 0; c < 4; c++) {
            int32_t base;
            uint16_t n = pat_query_onsets(root, c, &base);
            long shift = c - base;
            if (pat_error) {
                printf("! query error in cycle %ld: %s\n", (long)c, pat_error);
                failures++;
                pat_error = 0;
                continue;
            }
            row_t rows[PAT_HAPS];
            for (uint16_t i = 0; i < n; i++) {
                const pat_hap_t *h = &pat_haps[i];
                char *buf;
                size_t sz;
                FILE *m = open_memstream(&buf, &sz);
                fprintf(m, "%ld ", (long)c);
                print_rat(m, &h->t[WB], shift);
                fputc(' ', m);
                print_rat(m, &h->t[WE], shift);
                if (prog) {
                    for (int k = 0; k < PAT_CTLS; k++) {
                        if (h->ctl[k] == PAT_NONE)
                            continue;
                        fprintf(m, " %s=", ctl_names[k]);
                        print_atom(m, src, h->ctl[k]);
                    }
                } else {
                    fputc(' ', m);
                    print_atom(m, src, h->atom);
                    if (h->tail != PAT_NONE) {
                        fputc(',', m);
                        print_atom(m, src, h->tail);
                    }
                }
                fclose(m);
                rows[i].b = h->t[WB];
                rows[i].b.n += shift * h->t[WB].d;
                snprintf(rows[i].line, sizeof rows[i].line, "%s", buf);
                free(buf);
            }
            qsort(rows, n, sizeof rows[0], cmp_row);
            for (uint16_t i = 0; i < n; i++)
                puts(rows[i].line);
        }
    }
    fclose(in);
    return failures ? 1 : 0;
}
