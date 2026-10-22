#include "mini.h"

#include "pattern.h"

/* A recursive descent parser that follows Strudel's krill.pegjs grammar
 * rule for rule, building pattern nodes the way mini.mjs does:
 *
 *   stack_or_choose = sequence ("," sequence)* | ("." sequence)*
 *   sequence        = "^"? slice_with_ops+
 *   slice_with_ops  = slice ("@" | "_" | "!" | "(p,s,r)" | "/" slice
 *                           | "*" slice | ":" slice)*
 *   slice           = step | "[" stack_or_choose "]"
 *                   | "{" polymeter_stack "}" ("%" slice)?
 *                   | "<" polymeter_stack ">"
 *
 * Every sequence is a stepcat (Strudel always takes that branch because
 * each element carries a weight), and "<...>" is each sequence slowed by
 * its own weight. Not yet: "?" and "|" (random), and "..". */

#define MAX_OPS 6
#define PSTACK 48

uint16_t mini_error_pos;

static const char *S;
static uint16_t P, END;

static const rat_t R0 = {0, 1};
static const rat_t R1 = {1, 1};

static char peek(void)
{
    return P < END ? S[P] : 0;
}

static char peek2(void)
{
    return P + 1 < END ? S[P + 1] : 0;
}

static void ws(void)
{
    while (P < END && (S[P] == ' ' || S[P] == '\t' || S[P] == '\n' || S[P] == '\r'))
        P++;
}

static uint8_t fail(const char *msg)
{
    if (!pat_error) {
        pat_error = msg;
        mini_error_pos = P;
    }
    return PAT_NONE;
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool is_step_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || is_digit(c) || c == '~' || c == '-' ||
           c == '#' || c == '.' || c == '^' || c == '_';
}

/* A decimal number from text[0..len), as JavaScript's Number() reads the
 * common cases: sign, digits, fraction, exponent. */
bool mini_decimal(const char *t, uint8_t len, rat_t *out)
{
    uint8_t i = 0;
    bool neg = false;
    if (i < len && (t[i] == '-' || t[i] == '+'))
        neg = t[i++] == '-';
    int32_t num = 0, den = 1;
    bool digits = false;
    while (i < len && is_digit(t[i])) {
        num = num * 10 + (t[i++] - '0');
        digits = true;
        if (num > 100000000)
            return false;
    }
    if (i < len && t[i] == '.') {
        i++;
        while (i < len && is_digit(t[i])) {
            num = num * 10 + (t[i++] - '0');
            den *= 10;
            digits = true;
            if (num > 100000000 || den > 100000000)
                return false;
        }
    }
    if (!digits)
        return false;
    if (i < len && (t[i] == 'e' || t[i] == 'E')) {
        i++;
        bool eneg = false;
        if (i < len && (t[i] == '-' || t[i] == '+'))
            eneg = t[i++] == '-';
        if (i >= len || !is_digit(t[i]))
            return false;
        uint8_t ex = 0;
        while (i < len && is_digit(t[i]))
            ex = ex * 10 + (t[i++] - '0');
        while (ex--) {
            if (eneg)
                den *= 10;
            else
                num *= 10;
            if (num > 100000000 || den > 100000000)
                return false;
        }
    }
    if (i != len)
        return false;
    bool was = rat_overflow;
    rat_overflow = false;
    rat_set(out, neg ? -num : num, den);
    bool bad = rat_overflow;
    rat_overflow = was;
    return !bad;
}

/* The grammar's number: minus? int frac? exp? */
static bool number(rat_t *out)
{
    uint16_t start = P;
    if (peek() == '-')
        P++;
    if (!is_digit(peek())) {
        P = start;
        return false;
    }
    while (is_digit(peek()))
        P++;
    if (peek() == '.' && is_digit(peek2())) {
        P++;
        while (is_digit(peek()))
            P++;
    }
    if ((peek() == 'e' || peek() == 'E') &&
        (is_digit(peek2()) || ((peek2() == '-' || peek2() == '+') && P + 2 < END && is_digit(S[P + 2])))) {
        P += 2;
        while (is_digit(peek()))
            P++;
    }
    if (!mini_decimal(S + start, (uint8_t)(P - start), out)) {
        P = start;
        return false;
    }
    return true;
}

static uint8_t unary(uint8_t type, uint8_t k)
{
    uint8_t n = pat_new_node(type);
    if (n != PAT_NONE)
        pat_add_kid(n, k);
    return n;
}

static uint8_t silence(void)
{
    return pat_new_node(N_SILENCE);
}

static uint8_t fastc(uint8_t k, const rat_t *f)
{
    if (rat_eq(f, &R1))
        return k;
    uint8_t n = unary(N_FASTC, k);
    if (n != PAT_NONE)
        pat_nodes[n].r1 = *f;
    return n;
}

static uint8_t fastc_int(uint8_t k, int32_t f)
{
    rat_t r;
    rat_set_int(&r, f);
    return fastc(k, &r);
}

static bool const_num(uint8_t node, rat_t *out)
{
    if (node == PAT_NONE || pat_nodes[node].type != N_ATOM)
        return false;
    pat_atom_t *a = &pat_atoms[pat_nodes[node].kids];
    if (!a->is_num)
        return false;
    *out = a->num;
    return true;
}

/* ---- grammar ---- */

typedef struct
{
    uint8_t node;
    rat_t weight;
} seq_t;

static uint8_t stack_or_choose(void);
static uint8_t slice(void);
static seq_t sequence(void);

/* One list shared by every level of the parse, for the steps and layers
 * being gathered (local arrays would fill the 6502's soft stack). Each
 * level pushes above what the outer levels hold and pops back. */
static seq_t pstack[PSTACK];
static uint8_t psp;

static bool push(seq_t s)
{
    if (psp >= PSTACK) {
        fail("pattern too deeply nested");
        return false;
    }
    pstack[psp++] = s;
    return true;
}

/* polymeter_stack = sequence ("," sequence)*, pushed onto pstack. */
static uint8_t polymeter_stack(void)
{
    uint8_t n = 0;
    for (;;) {
        seq_t q = sequence();
        if (q.node == PAT_NONE || !push(q))
            return 0;
        n++;
        ws();
        if (peek() != ',')
            return n;
        P++;
    }
}

static uint8_t stack_of(const seq_t *seqs, uint8_t n)
{
    if (n == 1)
        return seqs[0].node;
    uint8_t st = pat_new_node(N_STACK);
    for (uint8_t i = 0; i < n && st != PAT_NONE; i++)
        pat_add_kid(st, seqs[i].node);
    return st;
}

/* Returns PAT_NONE without an error when there is no slice here. */
static uint8_t slice(void)
{
    ws();
    char c = peek();
    if (c == '[') {
        P++;
        uint8_t n = stack_or_choose();
        ws();
        if (peek() != ']')
            return fail("expected ]");
        P++;
        ws();
        return n;
    }
    if (c == '<') {
        P++;
        uint8_t base = psp;
        uint8_t count = polymeter_stack();
        seq_t *seqs = &pstack[base];
        psp = base; /* still readable until the next push */
        if (!count)
            return PAT_NONE;
        ws();
        if (peek() != '>')
            return fail("expected >");
        P++;
        ws();
        for (uint8_t i = 0; i < count; i++) {
            if (seqs[i].weight.n == 0)
                return fail("empty < >");
            rat_t inv;
            rat_div(&inv, &R1, &seqs[i].weight);
            seqs[i].node = fastc(seqs[i].node, &inv);
        }
        return stack_of(seqs, count);
    }
    if (c == '{') {
        P++;
        uint8_t base = psp;
        uint8_t count = polymeter_stack();
        seq_t *seqs = &pstack[base];
        if (!count) {
            psp = base;
            return PAT_NONE;
        }
        ws();
        if (peek() != '}') {
            psp = base;
            return fail("expected }");
        }
        P++;
        uint8_t steps_node = PAT_NONE;
        rat_t steps = seqs[0].weight;
        if (peek() == '%') {
            P++;
            steps_node = slice();
            if (steps_node == PAT_NONE) {
                psp = base;
                return fail("expected steps after %");
            }
            if (const_num(steps_node, &steps))
                steps_node = PAT_NONE;
        }
        ws();
        for (uint8_t i = 0; i < count; i++) {
            if (seqs[i].weight.n == 0) {
                psp = base;
                return fail("empty { }");
            }
            rat_t inv;
            rat_div(&inv, &R1, &seqs[i].weight);
            if (steps_node == PAT_NONE) {
                rat_t f;
                rat_mul(&f, &steps, &inv);
                seqs[i].node = fastc(seqs[i].node, &f);
            } else {
                uint8_t f = pat_new_node(N_FAST);
                if (f == PAT_NONE)
                    return PAT_NONE;
                pat_add_kid(f, seqs[i].node);
                pat_add_kid(f, steps_node);
                pat_nodes[f].r1 = inv;
                seqs[i].node = f;
            }
        }
        psp = base;
        return stack_of(seqs, count);
    }

    /* step */
    uint16_t start = P;
    while (is_step_char(peek()))
        P++;
    uint8_t len = (uint8_t)(P - start);
    if (len == 0 || (len == 1 && (S[start] == '.' || S[start] == '_'))) {
        P = start;
        return PAT_NONE;
    }
    ws();
    if (len == 1 && (S[start] == '~' || S[start] == '-'))
        return silence();
    uint8_t a = pat_new_atom();
    uint8_t n = pat_new_node(N_ATOM);
    if (a == PAT_NONE || n == PAT_NONE)
        return PAT_NONE;
    pat_atom_t *at = &pat_atoms[a];
    at->pos = start;
    at->len = len;
    at->is_num = mini_decimal(S + start, len, &at->num);
    pat_nodes[n].kids = a;
    return n;
}

enum { OP_FAST, OP_SLOW, OP_REPLICATE, OP_EUCLID, OP_TAIL };

typedef struct
{
    uint8_t type;
    uint8_t a, b, c;
    int16_t reps;
} op_t;

/* The pattern given to an op as an argument, as Strudel's enter(element):
 * the slice alone, without its own ops. */
static uint8_t arg_element(void)
{
    uint8_t s = slice();
    if (s == PAT_NONE)
        return pat_error ? PAT_NONE : fail("expected a value");
    return s;
}

static uint8_t apply_op(uint8_t node, const op_t *op)
{
    rat_t f;
    switch (op->type) {
    case OP_FAST:
    case OP_SLOW:
        if (const_num(op->a, &f)) {
            if (f.n == 0)
                return silence();
            if (op->type == OP_SLOW)
                rat_div(&f, &R1, &f);
            return fastc(node, &f);
        } else {
            uint8_t n = pat_new_node(N_FAST);
            if (n == PAT_NONE)
                return n;
            pat_add_kid(n, node);
            pat_add_kid(n, op->a);
            pat_nodes[n].r2.n = op->type == OP_SLOW;
            return n;
        }
    case OP_REPLICATE: {
        if (op->reps < 1)
            return silence();
        uint8_t n = unary(N_REPEAT, node);
        if (n == PAT_NONE)
            return n;
        rat_set_int(&pat_nodes[n].r1, op->reps);
        return fastc_int(n, op->reps);
    }
    case OP_EUCLID: {
        uint8_t n = pat_new_node(N_EUCLID);
        if (n == PAT_NONE)
            return n;
        pat_add_kid(n, node);
        pat_add_kid(n, op->a);
        pat_add_kid(n, op->b);
        if (op->c != PAT_NONE)
            pat_add_kid(n, op->c);
        return n;
    }
    case OP_TAIL: {
        uint8_t n = pat_new_node(N_TAIL);
        if (n == PAT_NONE)
            return n;
        pat_add_kid(n, node);
        pat_add_kid(n, op->a);
        return n;
    }
    }
    return node;
}

/* slice_with_ops. Returns PAT_NONE without an error when there is none. */
static seq_t element(void)
{
    seq_t r = {PAT_NONE, R1};
    uint8_t node = slice();
    if (node == PAT_NONE)
        return r;
    op_t ops[MAX_OPS];
    uint8_t nops = 0;
    int16_t reps = 1;
    for (;;) {
        if (pat_error)
            return r;
        if (nops >= MAX_OPS) {
            fail("too many operators");
            return r;
        }
        uint16_t save = P;
        ws();
        char c = peek();
        if (c == '@' || c == '_') {
            P++;
            rat_t a = {2, 1};
            number(&a);
            rat_add(&r.weight, &r.weight, &a);
            rat_add_int(&r.weight, &r.weight, -1);
            continue;
        }
        if (c == '!') {
            P++;
            rat_t a = {2, 1};
            number(&a);
            if (a.d != 1) {
                fail("! needs a whole number");
                return r;
            }
            reps = reps + (int16_t)a.n - 1;
            uint8_t k = 0;
            for (uint8_t i = 0; i < nops; i++)
                if (ops[i].type != OP_REPLICATE)
                    ops[k++] = ops[i];
            nops = k;
            ops[nops].type = OP_REPLICATE;
            ops[nops].reps = reps;
            nops++;
            rat_set_int(&r.weight, reps);
            continue;
        }
        P = save;
        if (c == '*' || c == '/') {
            P++;
            uint8_t amount = slice();
            if (amount == PAT_NONE) {
                fail("expected a number");
                return r;
            }
            ops[nops].type = c == '*' ? OP_FAST : OP_SLOW;
            ops[nops].a = amount;
            nops++;
            continue;
        }
        if (c == '(') {
            P++;
            ws();
            op_t *op = &ops[nops];
            op->type = OP_EUCLID;
            op->a = arg_element();
            ws();
            if (pat_error)
                return r;
            if (peek() != ',') {
                fail("expected ,");
                return r;
            }
            P++;
            ws();
            op->b = arg_element();
            ws();
            op->c = PAT_NONE;
            if (peek() == ',') {
                P++;
                ws();
                if (peek() != ')')
                    op->c = arg_element();
            }
            ws();
            if (pat_error)
                return r;
            if (peek() != ')') {
                fail("expected )");
                return r;
            }
            P++;
            nops++;
            continue;
        }
        if (c == ':') {
            P++;
            ops[nops].type = OP_TAIL;
            ops[nops].a = arg_element();
            if (pat_error)
                return r;
            nops++;
            continue;
        }
        if (c == '?') {
            fail("? (random) is not supported yet");
            return r;
        }
        if (c == '.' && peek2() == '.') {
            fail(".. is not supported yet");
            return r;
        }
        break;
    }
    for (uint8_t i = 0; i < nops && node != PAT_NONE; i++)
        node = apply_op(node, &ops[i]);
    r.node = node;
    return r;
}

/* sequence: a stepcat of its elements, each squeezed into its share. */
static seq_t sequence(void)
{
    seq_t r = {PAT_NONE, R0};
    ws();
    if (peek() == '^')
        P++;
    uint8_t base = psp;
    for (;;) {
        seq_t e = element();
        if (pat_error) {
            psp = base;
            return r;
        }
        if (e.node == PAT_NONE)
            break;
        if (e.weight.n == 0)
            e.weight = R1; /* Strudel's "weight || 1" */
        if (!push(e)) {
            psp = base;
            return r;
        }
        rat_add(&r.weight, &r.weight, &e.weight);
    }
    uint8_t n = psp - base;
    seq_t *els = &pstack[base];
    psp = base;
    if (n == 0) {
        fail(peek() ? "unexpected character" : "expected a step");
        return r;
    }
    if (n == 1) {
        r.node = els[0].node;
        return r;
    }
    /* A node's kids must be next to each other in pat_kids, so make the
     * squeezed elements before the stack that holds them. */
    rat_t begin = R0;
    for (uint8_t i = 0; i < n; i++) {
        rat_t end;
        rat_add(&end, &begin, &els[i].weight);
        uint8_t c = unary(N_COMPRESS, els[i].node);
        if (c == PAT_NONE)
            return r;
        rat_div(&pat_nodes[c].r1, &begin, &r.weight);
        rat_div(&pat_nodes[c].r2, &end, &r.weight);
        els[i].node = c;
        begin = end;
    }
    uint8_t st = pat_new_node(N_STACK);
    for (uint8_t i = 0; i < n && st != PAT_NONE; i++)
        pat_add_kid(st, els[i].node);
    r.node = st;
    return r;
}

static uint8_t stack_or_choose(void)
{
    uint8_t base = psp;
    seq_t head = sequence();
    if (head.node == PAT_NONE || !push(head)) {
        psp = base;
        return PAT_NONE;
    }
    ws();
    char c = peek();
    if (c == '|') {
        psp = base;
        return fail("| (random choice) is not supported yet");
    }
    if (c != ',' && c != '.') {
        psp = base;
        return head.node;
    }
    while (peek() == c) {
        P++;
        seq_t q = sequence();
        if (q.node == PAT_NONE || !push(q)) {
            psp = base;
            return PAT_NONE;
        }
        ws();
    }
    uint8_t n = psp - base;
    seq_t *seqs = &pstack[base];
    psp = base;
    if (c == ',')
        return stack_of(seqs, n);
    /* feet: fastcat of the sequences */
    uint8_t cat = pat_new_node(N_SLOWCAT);
    for (uint8_t i = 0; i < n && cat != PAT_NONE; i++)
        pat_add_kid(cat, seqs[i].node);
    return fastc_int(cat, n);
}

uint8_t mini_parse(const char *src, uint16_t from, uint16_t to)
{
    psp = 0;
    S = src;
    P = from;
    END = to;
    ws();
    if (P == END)
        return fail("empty pattern");
    uint8_t root = stack_or_choose();
    if (root == PAT_NONE)
        return PAT_NONE;
    ws();
    if (P != END)
        return fail("unexpected character");
    return root;
}

int16_t mini_note_number(const char *s, uint8_t len)
{
    static const int8_t chroma[7] = {9, 11, 0, 2, 4, 5, 7}; /* a b c d e f g */
    if (len == 0)
        return -1;
    char c = s[0] | 0x20;
    if (c < 'a' || c > 'g')
        return -1;
    int16_t note = chroma[c - 'a'];
    uint8_t i = 1;
    while (i < len && (s[i] == '#' || s[i] == 'b' || s[i] == 's' || s[i] == 'f')) {
        note += (s[i] == '#' || s[i] == 's') ? 1 : -1;
        i++;
    }
    int16_t octave = 3;
    if (i < len) {
        bool neg = false;
        if (s[i] == '-') {
            neg = true;
            i++;
        }
        if (i >= len)
            return -1;
        octave = 0;
        while (i < len) {
            if (!is_digit(s[i]))
                return -1;
            octave = octave * 10 + (s[i++] - '0');
        }
        if (neg)
            octave = -octave;
    }
    return (octave + 1) * 12 + note;
}
