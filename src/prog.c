#include "prog.h"

#include "mini.h"
#include "pattern.h"

prog_pattern_t prog_patterns[PROG_PATTERNS];
uint8_t prog_pattern_count;
int16_t prog_cpm10;
uint16_t prog_error_pos;

static const char *S;
static uint16_t P, END;

static bool fail(const char *msg, uint16_t pos)
{
    if (!pat_error) {
        pat_error = msg;
        prog_error_pos = pos;
    }
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
    return P < END ? S[P] : 0;
}

/* Skips spaces, newlines and comments. */
static void skip(void)
{
    while (P < END) {
        char c = S[P];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ';') {
            P++;
        } else if (c == '/' && P + 1 < END && S[P + 1] == '/') {
            while (P < END && S[P] != '\n')
                P++;
        } else if (c == '/' && P + 1 < END && S[P + 1] == '*') {
            P += 2;
            while (P + 1 < END && !(S[P] == '*' && S[P + 1] == '/'))
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
        if (S[at + i] != w[i])
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
    pat_atom_t *at = &pat_atoms[a];
    at->pos = start;
    at->len = (uint8_t)(P - start);
    if (!mini_decimal(S + start, at->len, &at->num)) {
        fail("bad number", start);
        return PAT_NONE;
    }
    at->is_num = true;
    pat_nodes[n].kids = a;
    return n;
}

/* An argument: a string (mini-notation) or a number. */
static uint8_t arg(void)
{
    skip();
    char c = peek();
    if (c == '"' || c == '\'' || c == '`') {
        uint16_t start = ++P;
        while (P < END && S[P] != c)
            P++;
        if (P >= END) {
            fail("unclosed string", start - 1);
            return PAT_NONE;
        }
        uint8_t node = mini_parse(S, start, P);
        if (node == PAT_NONE) {
            prog_error_pos = mini_error_pos;
            return PAT_NONE;
        }
        P++;
        return node;
    }
    if (is_digit(c) || c == '.' || c == '-')
        return number_atom();
    fail("expected a string or number", P);
    return PAT_NONE;
}

static int8_t control_of(uint16_t at, uint8_t len)
{
    if (word_is(at, len, "s") || word_is(at, len, "sound"))
        return CTL_S;
    if (word_is(at, len, "note"))
        return CTL_NOTE;
    if (word_is(at, len, "n"))
        return CTL_N;
    if (word_is(at, len, "gain"))
        return CTL_GAIN;
    return -1;
}

static uint8_t ctl_node(uint8_t k, int8_t c)
{
    uint8_t n = pat_new_node(N_CTL);
    if (n == PAT_NONE)
        return n;
    pat_add_kid(n, k);
    rat_set_int(&pat_nodes[n].r1, c);
    return n;
}

static uint8_t pair(uint8_t type, uint8_t a, uint8_t b)
{
    uint8_t n = pat_new_node(type);
    if (n == PAT_NONE)
        return n;
    pat_add_kid(n, a);
    pat_add_kid(n, b);
    return n;
}

/* (args): up to 1 here; returns PAT_NONE for none (without an error). */
static bool call_args(uint8_t *out)
{
    *out = PAT_NONE;
    if (!expect('('))
        return false;
    skip();
    if (peek() == ')') {
        P++;
        return true;
    }
    *out = arg();
    if (*out == PAT_NONE)
        return false;
    return expect(')');
}

static uint8_t method(uint8_t left, uint16_t at, uint8_t len)
{
    uint8_t a;
    if (!call_args(&a))
        return PAT_NONE;
    int8_t c = control_of(at, len);
    if (c >= 0) {
        if (a == PAT_NONE)
            return ctl_node(left, c); /* "c e g".note() */
        uint8_t friend = ctl_node(a, c);
        return friend == PAT_NONE ? PAT_NONE : pair(N_SET, left, friend);
    }
    if (word_is(at, len, "fast") || word_is(at, len, "slow")) {
        bool slow = S[at] == 's';
        if (a == PAT_NONE) {
            fail("needs an amount", at);
            return PAT_NONE;
        }
        const pat_node_t *an = &pat_nodes[a];
        if (an->type == N_ATOM && pat_atoms[an->kids].is_num) {
            rat_t f = pat_atoms[an->kids].num;
            if (f.n == 0)
                return pat_new_node(N_SILENCE);
            uint8_t n = pat_new_node(N_FASTC);
            if (n == PAT_NONE)
                return n;
            pat_add_kid(n, left);
            if (slow) {
                rat_t one = {1, 1};
                rat_div(&pat_nodes[n].r1, &one, &f);
            } else {
                pat_nodes[n].r1 = f;
            }
            return n;
        }
        uint8_t n = pair(N_FAST, left, a);
        if (n != PAT_NONE)
            pat_nodes[n].r2.n = slow;
        return n;
    }
    fail("unknown function", at);
    return PAT_NONE;
}

static uint8_t expr(void)
{
    skip();
    uint8_t node;
    char c = peek();
    if (c == '"' || c == '\'' || c == '`') {
        node = arg();
    } else {
        uint16_t at = P;
        uint8_t len = ident();
        if (!len) {
            fail("expected a pattern", P);
            return PAT_NONE;
        }
        int8_t ctl = control_of(at, len);
        if (ctl < 0) {
            fail("unknown function", at);
            return PAT_NONE;
        }
        uint8_t a;
        if (!call_args(&a))
            return PAT_NONE;
        if (a == PAT_NONE) {
            fail("needs a pattern", at);
            return PAT_NONE;
        }
        node = ctl_node(a, ctl);
    }
    for (;;) {
        if (node == PAT_NONE)
            return PAT_NONE;
        skip();
        if (peek() != '.')
            return node;
        P++;
        skip();
        uint16_t at = P;
        uint8_t len = ident();
        if (!len) {
            fail("expected a function name", P);
            return PAT_NONE;
        }
        node = method(node, at, len);
    }
}

static bool tempo(uint16_t at, uint8_t len)
{
    bool cps = word_is(at, len, "setcps");
    uint8_t a;
    if (!call_args(&a))
        return false;
    if (a == PAT_NONE || pat_nodes[a].type != N_ATOM || !pat_atoms[pat_nodes[a].kids].is_num)
        return fail("needs a number", at);
    rat_t v = pat_atoms[pat_nodes[a].kids].num;
    /* tenths of a cycle per minute, rounded */
    /* rounded */
    int32_t x = ((int32_t)v.n * (cps ? 1200 : 20) + v.d) / (2 * (int32_t)v.d);
    if (x < 1 || x > 6000)
        return fail("tempo out of range", at);
    prog_cpm10 = (int16_t)x;
    return true;
}

bool prog_parse(const char *src, uint16_t len)
{
    S = src;
    P = 0;
    END = len;
    pat_reset();
    prog_pattern_count = 0;
    prog_cpm10 = -1;
    for (;;) {
        skip();
        if (P >= END)
            return true;
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
        uint8_t root = expr();
        if (root == PAT_NONE)
            return false;
        if (prog_pattern_count >= PROG_PATTERNS)
            return fail("too many patterns", at);
        if (rat_overflow)
            return fail("numbers too big", at);
        prog_patterns[prog_pattern_count].root = root;
        prog_patterns[prog_pattern_count].muted = label && S[at] == '_';
        prog_pattern_count++;
    }
}

uint8_t prog_parse_expr(const char *src, uint16_t from, uint16_t to)
{
    S = src;
    P = from;
    END = to;
    uint8_t root = expr();
    skip();
    if (root != PAT_NONE && P != END) {
        fail("unexpected text", P);
        return PAT_NONE;
    }
    return root;
}
