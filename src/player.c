#include "player.h"

#include <string.h>

#include "mini.h"
#include "opl.h"
#include "patches.h"
#include "pattern.h"
#include "prog.h"
#include "text.h"
#include "xram.h"
#include <rp6502.h>

const char *player_error;
uint16_t player_error_pos;
uint8_t player_voice_patch[OPL_VOICES];


/* Times are absolute: cycle << 16 | position in the cycle. */
typedef struct
{
    uint32_t free_at;
    uint8_t serial;
} voice_t;

static voice_t voices[OPL_VOICES];

typedef struct
{
    uint32_t t;
    uint8_t voice, serial;
} pending_off_t;

#define PENDING_MAX 24
static pending_off_t pending[PENDING_MAX];
static uint8_t pending_count;

typedef struct
{
    uint32_t t, end;
    uint8_t patch, note;
    int8_t att;
    uint8_t src[2];
} note_t;

#define NOTES_MAX 48
static note_t notes[NOTES_MAX];

/* A pattern that repeats gives the same notes at the same place in its
 * period, so each (pattern, cycle mod period) is worked out once and kept.
 * The notes are kept in XRAM (relative to their cycle's start); only the
 * index of slots is in RAM. */
typedef struct
{
    uint16_t pos; /* start in the cycle */
    uint32_t dur; /* length, may run past the cycle */
    uint8_t patch, note;
    int8_t att;
    uint8_t src[2];
} cached_note_t;

typedef struct
{
    uint8_t pattern;
    uint16_t residue;
    uint16_t first; /* into the XRAM notes */
    uint8_t count;
} cache_slot_t;

#define CACHE_NOTES (CACHE_XRAM_SIZE / sizeof(cached_note_t))
#define CACHE_SLOTS 40
static cache_slot_t cache_slots[CACHE_SLOTS];
static uint16_t cache_note_count;
static uint8_t cache_slot_count;

static void cache_clear(void)
{
    cache_note_count = 0;
    cache_slot_count = 0;
}

static const cache_slot_t *cache_find(uint8_t pattern, uint16_t residue)
{
    for (uint8_t i = 0; i < cache_slot_count; i++)
        if (cache_slots[i].pattern == pattern && cache_slots[i].residue == residue)
            return &cache_slots[i];
    return 0;
}

/* Keeps notes[from..to) for this pattern and residue; when full, starts
 * over (the patterns now playing fill it again). */
static void cache_store(uint8_t pattern, uint16_t residue, uint8_t from, uint8_t to, uint32_t start)
{
    uint8_t n = to - from;
    if (cache_slot_count >= CACHE_SLOTS || cache_note_count + n > CACHE_NOTES)
        cache_clear();
    cache_slot_t *slot = &cache_slots[cache_slot_count++];
    slot->pattern = pattern;
    slot->residue = residue;
    slot->first = cache_note_count;
    slot->count = n;
    for (uint8_t i = from; i < to; i++) {
        cached_note_t c;
        c.pos = (uint16_t)(notes[i].t - start);
        c.dur = notes[i].end - notes[i].t;
        c.patch = notes[i].patch;
        c.note = notes[i].note;
        c.att = notes[i].att;
        c.src[0] = notes[i].src[0];
        c.src[1] = notes[i].src[1];
        xram0_write(XRAM_CACHE + cache_note_count * sizeof c, &c, sizeof c);
        cache_note_count++;
    }
}

/* Half-step boundaries of 0.75 dB steps, for gain in 1/4096ths. */
static const uint16_t att_steps[] = {
    3923, 3598, 3301, 3028, 2777, 2547, 2337, 2143, 1966, 1803, 1654, 1517, 1392, 1277, 1171, 1074,
    985,  904,  829,  761,  698,  640,  587,  538,  494,  453,  416,  381,  350,  321,  294,  270,
    248,  227,  208,  191,  175,  161,  147,  135,  124,  114,  104,  96,   88,   81,   74,
};

/* While a new program is parsed, the playing one waits in XRAM. */
typedef struct
{
    uint8_t node_count, kid_count, atom_count, pattern_count;
    int16_t cpm10;
} counts_t;

#define BK_COUNTS XRAM_BACKUP
#define BK_PATTERNS (BK_COUNTS + sizeof(counts_t))
#define BK_NODES (BK_PATTERNS + sizeof(prog_patterns))
#define BK_KIDS (BK_NODES + sizeof(pat_nodes))
#define BK_ATOMS (BK_KIDS + sizeof(pat_kids))
_Static_assert(sizeof(counts_t) + sizeof(prog_patterns) + sizeof(pat_nodes) + sizeof(pat_kids) +
                       sizeof(pat_atoms) <=
                   4096,
               "the backup must fit its XRAM space");

static void backup(void)
{
    counts_t c = {pat_node_count, pat_kid_count, pat_atom_count, prog_pattern_count, prog_cpm10};
    xram0_write(BK_COUNTS, &c, sizeof c);
    xram0_write(BK_PATTERNS, prog_patterns, sizeof prog_patterns);
    xram0_write(BK_NODES, pat_nodes, sizeof pat_nodes);
    xram0_write(BK_KIDS, pat_kids, sizeof pat_kids);
    xram0_write(BK_ATOMS, pat_atoms, sizeof pat_atoms);
}

static void restore(void)
{
    counts_t c;
    xram0_read(&c, BK_COUNTS, sizeof c);
    xram0_read(prog_patterns, BK_PATTERNS, sizeof prog_patterns);
    xram0_read(pat_nodes, BK_NODES, sizeof pat_nodes);
    xram0_read(pat_kids, BK_KIDS, sizeof pat_kids);
    xram0_read(pat_atoms, BK_ATOMS, sizeof pat_atoms);
    pat_node_count = c.node_count;
    pat_kid_count = c.kid_count;
    pat_atom_count = c.atom_count;
    prog_pattern_count = c.pattern_count;
    prog_cpm10 = c.cpm10;
}

/* Works out what each value means as a sound and as a note, so playing
 * needs no text. */
static void resolve_atoms(void)
{
    for (uint8_t i = 0; i < pat_atom_count; i++) {
        pat_atom_t *at = &pat_atoms[i];
        char buf[32];
        uint8_t n = text_copy(at->pos, at->len, buf, sizeof buf);
        at->patch = n < at->len ? PATCH_UNKNOWN : patch_find(buf, n);
        int16_t note;
        if (at->is_num) {
            rat_t t, half = {1, 2};
            rat_add(&t, &at->num, &half);
            note = rat_floor(&t); /* MIDI number, rounded */
        } else {
            note = mini_note_number(buf, n);
        }
        at->note = note < 0 || note > 127 ? -1 : (int8_t)note;
    }
}

bool player_load(uint16_t len)
{
    static bool first = true;
    if (first) {
        memset(player_voice_patch, PATCH_UNKNOWN, sizeof player_voice_patch);
        first = false;
    }
    backup();
    if (!prog_parse(len)) {
        player_error = pat_error;
        player_error_pos = prog_error_pos;
        pat_error = 0; /* queries stop at any pat_error */
        restore();
        return false;
    }
    player_error = 0;
    resolve_atoms();
    cache_clear();
    if (prog_cpm10 > 0)
        clock_set_cpm10((uint16_t)prog_cpm10);
    return true;
}

static void problem(const char *msg, uint16_t pos)
{
    if (!player_error) {
        player_error = msg;
        player_error_pos = pos;
    }
}

/* Position of t inside the cycle that starts at base, in 1/65536ths (may
 * pass 65536 for a note that runs into later cycles). */
static uint32_t pos_of(const rat_t *t, int32_t base)
{
    rat_t f;
    rat_add_int(&f, t, -base);
    if (f.n <= 0)
        return 0;
    return (uint32_t)(((int32_t)f.n << 16) / f.d);
}

/* 0.75 dB steps below full for a level in 1/4096ths (up to 4096). */
static int8_t steps_below(int32_t g12)
{
    int8_t a = 0;
    while (a < (int8_t)(sizeof(att_steps) / 2) && g12 < (int32_t)att_steps[a])
        a++;
    return a == (int8_t)(sizeof(att_steps) / 2) ? 63 : a;
}

static int8_t gain_att(uint8_t atom)
{
    if (atom == PAT_NONE || !pat_atoms[atom].is_num)
        return 0;
    rat_t g = pat_atoms[atom].num;
    if (g.n <= 0)
        return 63;
    int32_t g12 = ((int32_t)g.n << 12) / g.d;
    if (g12 <= 4096)
        return steps_below(g12);
    /* louder than the patch: the same steps the other way, up to 4x */
    int32_t inv = ((int32_t)g.d << 12) / g.n;
    return (int8_t)-steps_below(inv < 1024 ? 1024 : inv);
}

/* One hap becomes one note, or nothing. */
static bool make_note(const pat_hap_t *h, int32_t cycle, int32_t base, note_t *n)
{
    uint8_t s = h->ctl[CTL_S];
    uint8_t patch = PATCH_TRIANGLE; /* Strudel's default sound */
    if (s != PAT_NONE) {
        patch = pat_atoms[s].patch;
        if (patch == PATCH_UNKNOWN) {
            problem("unknown sound", pat_atoms[s].pos);
            return false;
        }
    }

    int16_t note;
    if (PATCH_IS_DRUM(patch)) {
        note = KIT_NOTE;
    } else if (h->ctl[CTL_NOTE] != PAT_NONE) {
        note = pat_atoms[h->ctl[CTL_NOTE]].note;
    } else if (h->ctl[CTL_N] != PAT_NONE && patch >= PATCH_GM_COUNT) {
        note = pat_atoms[h->ctl[CTL_N]].note; /* synths: n is the note */
    } else {
        note = patch >= PATCH_GM_COUNT ? 36 : 48;
    }
    if (note < 0 || note > 127) {
        problem("not a note", pat_atoms[h->ctl[CTL_NOTE] != PAT_NONE ? h->ctl[CTL_NOTE] : h->atom].pos);
        return false;
    }

    uint32_t start = (uint32_t)cycle << 16;
    n->t = start + pos_of(&h->t[WB], base);
    n->end = start + pos_of(&h->t[WE], base);
    n->patch = patch;
    n->note = (uint8_t)note;
    n->att = gain_att(h->ctl[CTL_GAIN]);
    n->src[0] = h->ctl[CTL_S];
    n->src[1] = h->ctl[CTL_NOTE] != PAT_NONE ? h->ctl[CTL_NOTE] : h->ctl[CTL_N];
    if (n->src[0] == PAT_NONE)
        n->src[0] = h->atom;
    return true;
}

/* A free voice that already holds the patch, else the one free longest,
 * else the one that frees soonest. */
static uint8_t pick_voice(uint32_t t, uint8_t patch)
{
    uint8_t best = 0xFF;
    for (uint8_t v = 0; v < OPL_VOICES; v++)
        if (voices[v].free_at <= t && player_voice_patch[v] == patch)
            return v;
    for (uint8_t v = 0; v < OPL_VOICES; v++)
        if (best == 0xFF || voices[v].free_at < voices[best].free_at)
            best = v;
    return best;
}

static void add_event(cycle_events_t *out, uint16_t time, uint8_t kind, const note_t *n, uint8_t serial)
{
    if (out->count >= EV_MAX) {
        problem("too many notes in one cycle", 0);
        return;
    }
    event_t *e = &out->ev[out->count++];
    e->time = time;
    e->kind = kind;
    e->serial = serial;
    if (n) {
        e->note = n->note;
        e->att = n->att;
        e->patch = n->patch;
        e->src[0] = n->src[0];
        e->src[1] = n->src[1];
    } else {
        e->src[0] = e->src[1] = 0xFF;
    }
}

/* Voices and note-offs as they were before the last fill. */
static struct
{
    voice_t voices[OPL_VOICES];
    pending_off_t pending[PENDING_MAX];
    uint8_t pending_count;
    uint8_t voice_patch[OPL_VOICES];
} saved;

void player_rewind(void)
{
    memcpy(voices, saved.voices, sizeof voices);
    memcpy(pending, saved.pending, sizeof pending);
    pending_count = saved.pending_count;
    memcpy(player_voice_patch, saved.voice_patch, sizeof player_voice_patch);
}

void player_fill(cycle_events_t *out, uint16_t cycle)
{
    uint32_t base = (uint32_t)cycle << 16;
    uint32_t next = base + 0x10000;

    memcpy(saved.voices, voices, sizeof voices);
    memcpy(saved.pending, pending, sizeof pending);
    saved.pending_count = pending_count;
    memcpy(saved.voice_patch, player_voice_patch, sizeof player_voice_patch);

    if (cycle == 0) {
        /* (Re)starting: forget the old voices and note-offs. */
        memset(voices, 0, sizeof(voices));
        memset(player_voice_patch, 0xFF, sizeof(player_voice_patch));
        pending_count = 0;
    }

    /* Note-offs left over from earlier cycles that end in this one. */
    uint8_t keep = 0;
    for (uint8_t i = 0; i < pending_count; i++) {
        pending_off_t *p = &pending[i];
        if (p->t < next) {
            uint16_t at = p->t > base ? (uint16_t)(p->t - base) : 0;
            add_event(out, at, p->voice, 0, p->serial);
        } else {
            pending[keep++] = *p;
        }
    }
    pending_count = keep;

    /* Every pattern's onsets, as notes: from the cache when this pattern
     * has played this part of its period before. */
    uint8_t count = 0;
    for (uint8_t i = 0; i < prog_pattern_count; i++) {
        if (prog_patterns[i].muted)
            continue;
        uint8_t root = prog_patterns[i].root;
        uint16_t period = pat_period(root);
        uint16_t residue = period ? (uint16_t)(cycle % period) : 0;
        const cache_slot_t *slot = period ? cache_find(i, residue) : 0;
        if (slot) {
            for (uint8_t j = 0; j < slot->count && count < NOTES_MAX; j++) {
                cached_note_t c;
                xram0_read(&c, XRAM_CACHE + (slot->first + j) * sizeof c, sizeof c);
                note_t *n = &notes[count++];
                n->t = base + c.pos;
                n->end = n->t + c.dur;
                n->patch = c.patch;
                n->note = c.note;
                n->att = c.att;
                n->src[0] = c.src[0];
                n->src[1] = c.src[1];
            }
            continue;
        }
        int32_t qbase;
        uint8_t nh = pat_query_onsets(root, cycle, &qbase);
        if (pat_error) {
            problem(pat_error, 0);
            pat_error = 0;
            continue;
        }
        uint8_t from = count;
        for (uint8_t j = 0; j < nh && count < NOTES_MAX; j++)
            if (make_note(&pat_haps[j], cycle, qbase, &notes[count]))
                count++;
        if (period)
            cache_store(i, residue, from, count, base);
    }

    /* In time order, so voices are given out as they will sound. */
    for (uint8_t i = 1; i < count; i++) {
        note_t n = notes[i];
        uint8_t j = i;
        while (j > 0 && notes[j - 1].t > n.t) {
            notes[j] = notes[j - 1];
            j--;
        }
        notes[j] = n;
    }

    for (uint8_t i = 0; i < count; i++) {
        note_t *n = &notes[i];
        uint8_t v = pick_voice(n->t, n->patch);
        voice_t *vo = &voices[v];
        uint8_t serial = ++vo->serial;
        vo->free_at = n->end;
        player_voice_patch[v] = n->patch;
        add_event(out, (uint16_t)(n->t - base), EV_ON | v, n, serial);

        /* Drums ring out by themselves; other notes end with the event. */
        if (PATCH_IS_DRUM(n->patch))
            continue;
        if (n->end < next) {
            add_event(out, (uint16_t)(n->end - base), v, 0, serial);
        } else if (pending_count < PENDING_MAX) {
            pending[pending_count].t = n->end;
            pending[pending_count].voice = v;
            pending[pending_count].serial = serial;
            pending_count++;
        }
    }
}
