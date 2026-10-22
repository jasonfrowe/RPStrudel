#include "prog.h"

#include "mini.h"
#include "pattern.h"
#include "text.h"
#include "overlay.h"

/* Part of the parser overlay (overlay.h). */
#ifdef __mos__
#pragma clang section text = ".ovl_parse"
#endif

prog_pattern_t prog_patterns[PROG_PATTERNS];
uint8_t prog_pattern_count;
int16_t prog_cpm10;
uint16_t prog_error_pos;

static uint16_t P, END;

/* A pattern being built: its node, and whether it is a control pattern
 * (made by s, note, n or gain) or a plain one (a string or number). */
typedef struct
{
    uint8_t node;
    bool ctl;
} val_t;

static const val_t NOTHING = {PAT_NONE, false};

/* The parameter of the lambda being parsed (x in x => x.fast(2)), and the
 * pattern it stands for. */
static uint16_t lam_at;
static uint8_t lam_len;
static val_t lam_val;

static val_t expr(void);

static val_t failv(const char *msg, uint16_t pos)
{
    if (!pat_error) {
        pat_error = msg;
        prog_error_pos = pos;
    }
    return NOTHING;
}

static bool fail(const char *msg, uint16_t pos)
{
    failv(msg, pos);
    return false;
}

static bool is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static char peek(void)
{
    return P < END ? text_at(P) : 0;
}

/* Skips spaces, newlines and comments. */
static void skip(void)
{
    while (P < END) {
        char c = text_at(P);
        char c2 = P + 1 < END ? text_at(P + 1) : 0;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ';') {
            P++;
        } else if (c == '/' && c2 == '/') {
            while (P < END && text_at(P) != '\n')
                P++;
        } else if (c == '/' && c2 == '*') {
            P += 2;
            while (P + 1 < END && !(text_at(P) == '*' && text_at(P + 1) == '/'))
                P++;
            P = P + 2 < END ? P + 2 : END;
        } else {
            break;
        }
    }
}

/* An identifier at P: returns its length, 0 if none. */
static uint8_t ident(void)
{
    uint16_t start = P;
    if (!is_alpha(peek()))
        return 0;
    while (is_alpha(peek()) || is_digit(peek()))
        P++;
    return (uint8_t)(P - start);
}

static bool word_is(uint16_t at, uint8_t len, const char *w)
{
    for (uint8_t i = 0; i < len; i++)
        if (text_at(at + i) != w[i])
            return false;
    return w[len] == 0;
}

static bool expect(char c)
{
    skip();
    if (peek() != c) {
        static char msg[] = "expected ?";
        msg[9] = c;
        return fail(msg, P);
    }
    P++;
    return true;
}

static bool next_is(char c)
{
    skip();
    if (peek() != c)
        return false;
    P++;
    return true;
}

/* ---- names ---- */

/* Every function, in the order of the F_ values below. */
static const char *const names[] = {
    "s",        "sound",     "note",   "n",           "gain",      "fast",      "slow",        "rev",
    "every",    "firstOf",   "lastOf", "add",         "sub",       "scale",     "ply",         "late",
    "early",    "off",       "echo",   "struct",      "mask",      "degrade",   "degradeBy",   "sometimesBy",
    "sometimes", "often",    "rarely", "almostNever", "almostAlways", "euclid", "euclidRot",   "stack",
    "cat",      "slowcat",   "seq",    "fastcat",     "silence",   "trans",     "transpose",
};
enum {
    F_S, F_SOUND, F_NOTE, F_N, F_GAIN, F_FAST, F_SLOW, F_REV,
    F_EVERY, F_FIRSTOF, F_LASTOF, F_ADD, F_SUB, F_SCALE, F_PLY, F_LATE,
    F_EARLY, F_OFF, F_ECHO, F_STRUCT, F_MASK, F_DEGRADE, F_DEGRADEBY, F_SOMETIMESBY,
    F_SOMETIMES, F_OFTEN, F_RARELY, F_ALMOSTNEVER, F_ALMOSTALWAYS, F_EUCLID, F_EUCLIDROT, F_STACK,
    F_CAT, F_SLOWCAT, F_SEQ, F_FASTCAT, F_SILENCE, F_TRANS, F_TRANSPOSE, F_COUNT
};

static int8_t lookup(uint16_t at, uint8_t len)
{
    for (uint8_t i = 0; i < F_COUNT; i++)
        if (word_is(at, len, names[i]))
            return (int8_t)i;
    return -1;
}

/* The chance sometimes, often and the rest stand for, in hundredths. */
static const uint8_t chances[] = {50, 75, 25, 10, 90};

/* ---- nodes ---- */

static uint8_t node1(uint8_t type, uint8_t k)
{
    uint8_t n = pat_new_node(type);
    if (n != PAT_NONE)
        pat_add_kid(n, k);
    return n;
}

static uint8_t node2(uint8_t type, uint8_t a, uint8_t b)
{
    uint8_t n = pat_new_node(type);
    if (n != PAT_NONE) {
        pat_add_kid(n, a);
        pat_add_kid(n, b);
    }
    return n;
}

/* Sets a node's numbers (r1, and r2 unless NULL); passes the node on. */
static uint8_t with(uint8_t n, const rat_t *r1, const rat_t *r2)
{
    if (n != PAT_NONE) {
        pat_set_r1(n, r1);
        if (r2)
            pat_set_r2(n, r2);
    }
    return n;
}

static uint8_t with_ints(uint8_t n, int16_t a, int16_t b)
{
    rat_t r1 = {a, 1}, r2 = {b, 1};
    return with(n, &r1, &r2);
}

static uint8_t ctl_node(uint8_t k, int8_t c)
{
    return with_ints(node1(N_CTL, k), c, 0);
}

static uint8_t number_atom(void)
{
    uint16_t start = P;
    if (peek() == '-')
        P++;
    while (is_digit(peek()) || peek() == '.')
        P++;
    uint8_t a = pat_new_atom();
    uint8_t n = pat_new_node(N_ATOM);
    if (a == PAT_NONE || n == PAT_NONE)
        return PAT_NONE;
    pat_atom_t at;
    pat_atom_get(a, &at);
    at.pos = start;
    at.len = (uint8_t)(P - start);
    char buf[24];
    uint8_t got = text_copy(start, at.len, buf, sizeof buf);
    if (at.len >= sizeof buf || !mini_decimal(buf, got, &at.num)) {
        fail("bad number", start);
        return PAT_NONE;
    }
    at.is_num = true;
    at.ival = mini_ival(&at.num, buf, got);
    pat_atom_put(a, &at);
    pat_set_atom(n, a);
    return n;
}

/* A string: mini-notation. */
static uint8_t string(void)
{
    char q = peek();
    uint16_t start = ++P;
    while (P < END && text_at(P) != q)
        P++;
    if (P >= END) {
        fail("unclosed string", start - 1);
        return PAT_NONE;
    }
    uint8_t node = mini_parse(start, P);
    if (node == PAT_NONE) {
        prog_error_pos = mini_error_pos;
        return PAT_NONE;
    }
    P++;
    return node;
}

/* A number argument (a constant). */
static bool number_arg(rat_t *out)
{
    skip();
    uint16_t at = P;
    val_t v = expr();
    if (v.node == PAT_NONE)
        return false;
    if (!pat_const_num(v.node, out))
        return fail("needs a number", at);
    return true;
}

static bool count_arg(int16_t *out, int16_t hi)
{
    skip();
    uint16_t at = P;
    rat_t r;
    if (!number_arg(&r))
        return false;
    if (r.d != 1 || r.n < 1 || r.n > hi)
        return fail("needs a whole number", at);
    *out = r.n;
    return true;
}

/* The values of a scale argument ("C:major", "<C:major A:minor>") that are
 * a tonic followed by :name get that scale. */
static bool mark_scales(uint8_t from)
{
    for (uint8_t i = from; i < pat_atom_count; i++) {
        pat_atom_t at;
        pat_atom_get(i, &at);
        uint16_t p = at.pos + at.len;
        if (at.pos && text_at(at.pos - 1) == ':')
            continue; /* a word of a name, as minor in C:minor */
        if (at.ival == NO_VAL || text_at(p) != ':')
            return fail("scale names are as \"g:minor\" (with a :)", at.pos);
        char buf[24];
        uint8_t n = 0;
        for (p++; n < sizeof buf; p++) {
            char c = text_at(p);
            if (!is_alpha(c) && c != ':')
                break;
            buf[n++] = c;
        }
        at.scale = pat_scale_find(buf, n);
        if (!at.scale)
            return fail("unknown scale", at.pos);
        pat_atom_put(i, &at);
    }
    return true;
}

/* Semitones in an interval name: "3M", "M3", "5P", "-2m", "4A", "5d"; or
 * NOT_INTERVAL. */
#define NOT_INTERVAL 0x7FFF

static int16_t interval(const char *t, uint8_t len)
{
    static const uint8_t major[7] = {0, 2, 4, 5, 7, 9, 11};
    uint8_t i = 0;
    bool neg = false;
    char q = 0;
    if (i < len && t[i] == '-') {
        neg = true;
        i++;
    }
    if (i < len && !is_digit(t[i]))
        q = t[i++];
    uint8_t d = 0;
    while (i < len && is_digit(t[i]))
        d = d * 10 + (t[i++] - '0');
    if (!q && i < len)
        q = t[i++];
    if (i != len || d == 0 || d > 22)
        return NOT_INTERVAL;
    /* (d - 1) % 7 and / 7, by subtraction: dividing a byte by 7 crashes
     * this llvm-mos's backend */
    uint8_t oct = 0, simple = d - 1;
    while (simple >= 7) {
        simple -= 7;
        oct++;
    }
    bool perfect = simple == 0 || simple == 3 || simple == 4;
    int16_t semis = major[simple] + 12 * oct;
    if (q == 'A')
        semis++;
    else if (q == 'm' && !perfect)
        semis--;
    else if (q == 'd')
        semis -= perfect ? 1 : 2;
    else if (q != 'M' && q != 'P')
        return NOT_INTERVAL;
    return neg ? -semis : semis;
}

/* The values of a transpose argument that are interval names get their
 * semitones. */
static bool mark_intervals(uint8_t from)
{
    for (uint8_t i = from; i < pat_atom_count; i++) {
        pat_atom_t at;
        pat_atom_get(i, &at);
        if (at.is_num)
            continue;
        char buf[8];
        uint8_t n = text_copy(at.pos, at.len, buf, sizeof buf);
        int16_t semis = n < at.len ? NOT_INTERVAL : interval(buf, n);
        if (semis == NOT_INTERVAL)
            return fail("not an interval (as 3M or 5P)", at.pos);
        at.ival = (int8_t)semis;
        pat_atom_put(i, &at);
    }
    return true;
}

/* Scale names, for scale(): the names are in scales.c. */
extern const char pat_scale_names[];

uint8_t pat_scale_find(const char *name, uint8_t len)
{
    const char *p = pat_scale_names;
    for (uint8_t i = 0; *p; i++) {
        uint8_t j = 0;
        while (j < len && p[j] && p[j] != '|' && (p[j] == (name[j] | 0x20) || (p[j] == ':' && name[j] == ':')))
            j++;
        if (j == len && (p[j] == '|' || !p[j]))
            return i + 1;
        while (*p && *p != '|')
            p++;
        if (*p)
            p++;
    }
    return 0;
}

/* ---- functions ---- */

static val_t method(val_t in, uint16_t at, uint8_t len);

/* A function given as an argument (to every, off, sometimes...), applied to
 * in: a lambda (x => x.fast(2)), a call (fast(2)) or a name (rev). */
static val_t func_arg(val_t in)
{
    skip();
    uint16_t at = P;
    uint8_t len = ident();
    if (!len)
        return failv("expected a function", P);
    skip();
    if (peek() == '=' && P + 1 < END && text_at(P + 1) == '>') {
        P += 2;
        uint16_t old_at = lam_at;
        uint8_t old_len = lam_len;
        val_t old_val = lam_val;
        lam_at = at;
        lam_len = len;
        lam_val = in;
        val_t r = expr();
        lam_at = old_at;
        lam_len = old_len;
        lam_val = old_val;
        return r;
    }
    return method(in, at, len);
}

/* Patterns separated by commas, up to ')', into kids of a new node. */
static val_t list(uint8_t type)
{
    uint8_t kids[PROG_PATTERNS];
    uint8_t n = 0;
    bool ctl = false;
    skip();
    if (peek() != ')') {
        do {
            if (n >= PROG_PATTERNS)
                return failv("too many patterns", P);
            val_t v = expr();
            if (v.node == PAT_NONE)
                return NOTHING;
            kids[n++] = v.node;
            ctl |= v.ctl;
        } while (next_is(','));
    }
    if (!expect(')'))
        return NOTHING;
    uint8_t node = pat_new_node(n ? type : N_SILENCE);
    for (uint8_t i = 0; i < n && node != PAT_NONE; i++)
        pat_add_kid(node, kids[i]);
    val_t r = {node, ctl};
    return r;
}

/* Two copies of in: the events a random draw keeps (chance x taken away),
 * and the rest, changed by the function argument. */
static val_t sometimes(val_t in, const rat_t *x, bool comma)
{
    rat_t keep = {0, 1}, rest = {0, 2};
    keep.n = rest.n = (int16_t)pat_seed++;
    uint8_t kept = with(node1(N_DEGRADE, in.node), x, &keep);
    val_t others = {with(node1(N_DEGRADE, in.node), x, &rest), in.ctl};
    if (comma && !next_is(','))
        return failv("expected ,", P);
    val_t changed = func_arg(others);
    if (changed.node == PAT_NONE)
        return NOTHING;
    val_t r = {node2(N_STACK, kept, changed.node), in.ctl || changed.ctl};
    return r;
}

/* .name(args) applied to in. The parentheses can be left out of a function
 * with no arguments (as an argument: every(3, rev)). */
static val_t method(val_t in, uint16_t at, uint8_t len)
{
    int8_t f = lookup(at, len);
    if (f < 0)
        return failv("unknown function", at);
    bool paren = next_is('(');
    val_t r = {PAT_NONE, in.ctl};
    rat_t x;
    int16_t k;
    if (!paren && f != F_REV && f != F_DEGRADE)
        return failv("expected (", P);

    switch (f) {
    case F_S:
    case F_SOUND:
    case F_NOTE:
    case F_N:
    case F_GAIN: {
        int8_t c = f <= F_SOUND ? CTL_S : (int8_t)(f - F_NOTE + CTL_NOTE);
        r.ctl = true;
        skip();
        if (peek() == ')') {
            r.node = ctl_node(in.node, c); /* "c e g".note() */
        } else {
            val_t a = expr();
            if (a.node == PAT_NONE)
                return NOTHING;
            uint8_t friend = a.ctl ? a.node : ctl_node(a.node, c);
            r.node = node2(N_SET, in.node, friend);
        }
        break;
    }
    case F_FAST:
    case F_SLOW: {
        skip();
        val_t a = expr();
        if (a.node == PAT_NONE)
            return NOTHING;
        if (pat_const_num(a.node, &x)) {
            if (x.n <= 0)
                r.node = pat_new_node(N_SILENCE);
            else {
                if (f == F_SLOW) {
                    rat_t one = {1, 1};
                    rat_div(&x, &one, &x);
                }
                r.node = with(node1(N_FASTC, in.node), &x, 0);
            }
        } else {
            r.node = with_ints(node2(N_FAST, in.node, a.node), 1, f == F_SLOW);
        }
        break;
    }
    case F_REV:
        r.node = node1(N_REV, in.node);
        break;
    case F_EVERY:
    case F_FIRSTOF:
    case F_LASTOF: {
        if (!count_arg(&k, 64) || !expect(','))
            return NOTHING;
        val_t changed = func_arg(in);
        if (changed.node == PAT_NONE)
            return NOTHING;
        r.node = with_ints(node2(N_EVERY, in.node, changed.node), k, f == F_LASTOF ? k - 1 : 0);
        r.ctl = in.ctl || changed.ctl;
        break;
    }
    case F_ADD:
    case F_SUB: {
        skip();
        uint16_t aat = P;
        val_t a = expr();
        if (a.node == PAT_NONE)
            return NOTHING;
        if (in.ctl != a.ctl)
            return failv(in.ctl ? "add note(...) or n(...) to this" : "add a plain number or string here", aat);
        r.node = with_ints(node2(N_ARITH, in.node, a.node), f == F_ADD ? 1 : -1, in.ctl);
        break;
    }
    case F_TRANS:
    case F_TRANSPOSE: {
        /* semitones (or intervals) added to the notes */
        skip();
        uint16_t aat = P;
        uint8_t from = pat_atom_count;
        val_t a = expr();
        if (a.node == PAT_NONE)
            return NOTHING;
        if (a.ctl)
            return failv("transpose by a number or interval", aat);
        if (!mark_intervals(from))
            return NOTHING;
        r.node = with_ints(node2(N_ARITH, in.node, a.node), 1, in.ctl ? 2 : 0);
        break;
    }
    case F_SCALE: {
        skip();
        uint8_t from = pat_atom_count;
        val_t a = expr();
        if (a.node == PAT_NONE || !mark_scales(from))
            return NOTHING;
        r.node = with_ints(node2(N_SCALE, in.node, a.node), 0, in.ctl);
        break;
    }
    case F_PLY:
        if (!count_arg(&k, 16))
            return NOTHING;
        r.node = with_ints(node1(N_PLY, in.node), k, 0);
        break;
    case F_LATE:
    case F_EARLY:
        if (!number_arg(&x))
            return NOTHING;
        if (f == F_EARLY)
            x.n = -x.n;
        r.node = with(node1(N_LATE, in.node), &x, 0);
        break;
    case F_OFF: {
        if (!number_arg(&x) || !expect(','))
            return NOTHING;
        val_t moved = {with(node1(N_LATE, in.node), &x, 0), in.ctl};
        val_t changed = func_arg(moved);
        if (changed.node == PAT_NONE)
            return NOTHING;
        r.node = node2(N_STACK, in.node, changed.node);
        r.ctl = in.ctl || changed.ctl;
        break;
    }
    case F_ECHO: {
        rat_t time, fb;
        if (!count_arg(&k, PROG_PATTERNS) || !expect(',') || !number_arg(&time) || !expect(',') ||
            !number_arg(&fb))
            return NOTHING;
        uint8_t copies[PROG_PATTERNS];
        int32_t g64 = 64; /* feedback^i, in 64ths */
        rat_t when = {0, 1};
        for (int16_t i = 0; i < k; i++) {
            rat_t gain = {(int16_t)g64, 64};
            copies[i] = with(node1(N_GAIN, with(node1(N_LATE, in.node), &when, 0)), &gain, 0);
            rat_add(&when, &when, &time);
            g64 = g64 * fb.n / fb.d;
        }
        r.node = pat_new_node(N_STACK);
        for (int16_t i = 0; i < k && r.node != PAT_NONE; i++)
            pat_add_kid(r.node, copies[i]);
        break;
    }
    case F_STRUCT:
    case F_MASK: {
        skip();
        val_t a = expr();
        if (a.node == PAT_NONE)
            return NOTHING;
        r.node = node2(f == F_STRUCT ? N_STRUCT : N_MASK, in.node, a.node);
        break;
    }
    case F_DEGRADE:
    case F_DEGRADEBY: {
        rat_t half = {1, 2}, seed = {(int16_t)pat_seed++, 1};
        x = half;
        if (f == F_DEGRADEBY && !number_arg(&x))
            return NOTHING;
        r.node = with(node1(N_DEGRADE, in.node), &x, &seed);
        break;
    }
    case F_SOMETIMESBY:
        if (!number_arg(&x))
            return NOTHING;
        r = sometimes(in, &x, true);
        break;
    case F_SOMETIMES:
    case F_OFTEN:
    case F_RARELY:
    case F_ALMOSTNEVER:
    case F_ALMOSTALWAYS: {
        rat_set(&x, chances[f - F_SOMETIMES], 100);
        r = sometimes(in, &x, false);
        break;
    }
    case F_EUCLID:
    case F_EUCLIDROT: {
        val_t a[3];
        uint8_t na = f == F_EUCLID ? 2 : 3;
        for (uint8_t i = 0; i < na; i++) {
            if (i && !expect(','))
                return NOTHING;
            a[i] = expr();
            if (a[i].node == PAT_NONE)
                return NOTHING;
        }
        r.node = pat_new_node(N_EUCLID);
        if (r.node != PAT_NONE) {
            pat_add_kid(r.node, in.node);
            for (uint8_t i = 0; i < na; i++)
                pat_add_kid(r.node, a[i].node);
        }
        break;
    }
    default:
        return failv("not a method", at);
    }
    if (r.node == PAT_NONE || (paren && !expect(')')))
        return NOTHING;
    return r;
}

/* A string, number, (expr), the lambda's parameter, or a function. */
static val_t primary(void)
{
    skip();
    char c = peek();
    val_t r = {PAT_NONE, false};
    if (c == '"' || c == '\'' || c == '`') {
        r.node = string();
        return r;
    }
    if (is_digit(c) || c == '.' || c == '-') {
        r.node = number_atom();
        return r;
    }
    if (c == '(') {
        P++;
        r = expr();
        return expect(')') ? r : NOTHING;
    }
    uint16_t at = P;
    uint8_t len = ident();
    if (!len)
        return failv("expected a pattern", P);
    if (lam_len == len) {
        uint8_t i = 0;
        while (i < len && text_at(lam_at + i) == text_at(at + i))
            i++;
        if (i == len)
            return lam_val;
    }
    int8_t f = lookup(at, len);
    if (f == F_SILENCE) {
        r.node = pat_new_node(N_SILENCE);
        return r;
    }
    if (f >= F_STACK && f <= F_FASTCAT) {
        if (!expect('('))
            return NOTHING;
        if (f == F_STACK)
            return list(N_STACK);
        r = list(N_SLOWCAT);
        if (r.node != PAT_NONE && (f == F_SEQ || f == F_FASTCAT)) {
            /* fastcat: the slowcat sped up by how many there are */
            pat_node_t nn;
            pat_node_get(r.node, &nn);
            if (nn.type == N_SLOWCAT && nn.nkids > 1)
                r.node = with_ints(node1(N_FASTC, r.node), nn.nkids, 0);
        }
        return r;
    }
    if (f >= F_S && f <= F_GAIN) {
        int8_t ctl = f <= F_SOUND ? CTL_S : (int8_t)(f - F_NOTE + CTL_NOTE);
        if (!expect('('))
            return NOTHING;
        val_t a = expr();
        if (a.node == PAT_NONE || !expect(')'))
            return NOTHING;
        r.node = a.ctl ? a.node : ctl_node(a.node, ctl);
        r.ctl = true;
        return r;
    }
    return failv(f < 0 ? "unknown function" : "needs a pattern first, as \"...\".name()", at);
}

static val_t expr(void)
{
    val_t v = primary();
    for (;;) {
        if (v.node == PAT_NONE)
            return NOTHING;
        skip();
        if (peek() != '.' || (P + 1 < END && is_digit(text_at(P + 1))))
            return v;
        P++;
        skip();
        uint16_t at = P;
        uint8_t len = ident();
        if (!len)
            return failv("expected a function name", P);
        v = method(v, at, len);
    }
}

static bool tempo(uint16_t at, uint8_t len)
{
    bool cps = word_is(at, len, "setcps");
    rat_t v;
    if (!expect('(') || !number_arg(&v) || !expect(')'))
        return false;
    /* tenths of a cycle per minute, rounded */
    int32_t x = ((int32_t)v.n * (cps ? 1200 : 20) + v.d) / (2 * (int32_t)v.d);
    if (x < 1 || x > 6000)
        return fail("tempo out of range", at);
    prog_cpm10 = (int16_t)x;
    return true;
}

OVERLAY_ENTRY bool prog_parse(uint16_t len)
{
    P = 0;
    END = len;
    pat_reset();
    lam_len = 0;
    prog_pattern_count = 0;
    prog_cpm10 = -1;
    for (;;) {
        skip();
        if (P >= END) {
            for (uint8_t i = 0; i < prog_pattern_count; i++)
                pat_period(prog_patterns[i].root);
            return true;
        }
        uint16_t at = P;
        uint8_t n = ident();
        skip();
        bool label = n && peek() == ':';
        if (label) {
            P++;
        } else if (n && (word_is(at, n, "setcpm") || word_is(at, n, "setcps"))) {
            if (!tempo(at, n))
                return false;
            continue;
        } else {
            P = at; /* a bare expression */
        }
        val_t root = expr();
        if (root.node == PAT_NONE)
            return false;
        if (prog_pattern_count >= PROG_PATTERNS)
            return fail("too many patterns", at);
        if (rat_overflow)
            return fail("numbers too big", at);
        prog_patterns[prog_pattern_count].root = root.node;
        prog_patterns[prog_pattern_count].muted = label && text_at(at) == '_';
        prog_pattern_count++;
    }
}

OVERLAY_ENTRY uint8_t prog_parse_expr(uint16_t from, uint16_t to)
{
    P = from;
    END = to;
    lam_len = 0;
    val_t root = expr();
    skip();
    if (root.node != PAT_NONE && P != END) {
        fail("unexpected text", P);
        return PAT_NONE;
    }
    if (root.node != PAT_NONE)
        pat_period(root.node);
    return root.node;
}
