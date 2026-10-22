#include "player.h"

#include <string.h>

#include "mini.h"
#include "opl.h"
#include "overlay.h"
#include "sounds.h"
#include "patches.h"
#include "pattern.h"
#include "prog.h"
#include "text.h"
#include "xram.h"
#include <rp6502.h>

const char *player_error;
uint16_t player_error_pos;
uint8_t player_voice_patch[OPL_VOICES];
bool player_voice_drum[OPL_VOICES];


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
    bool drum;             /* rings out by itself: no note-off */
    uint8_t voice, serial; /* given out in time order */
} note_t;

/* This cycle's note-offs, put in time order before they go out. */
typedef struct
{
    uint16_t time;
    uint8_t voice, serial;
} off_t;

#define OFFS_MAX 64
static off_t offs[OFFS_MAX];
static uint8_t off_count;

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
    bool drum;
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
        c.drum = notes[i].drum;
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

/* While a new program is parsed (into the other pattern bank), the playing
 * one's list of patterns waits here. */
static prog_pattern_t saved_patterns[PROG_PATTERNS];
static uint8_t saved_count;
static int16_t saved_cpm10;

bool player_load(uint16_t len)
{
    static bool first = true;
    if (first) {
        memset(player_voice_patch, PATCH_UNKNOWN, sizeof player_voice_patch);
        first = false;
    }
    memcpy(saved_patterns, prog_patterns, sizeof saved_patterns);
    saved_count = prog_pattern_count;
    saved_cpm10 = prog_cpm10;
    pat_begin();
    if (!overlay_load(OVERLAY_PARSE)) {
        player_error = "cannot read ROM:parse.ovl";
        player_error_pos = 0xFFFF;
        pat_abort();
        return false;
    }
    bool ok = prog_parse(len);
    if (ok)
        sounds_resolve();
    overlay_load(OVERLAY_QUERY);
    if (!ok) {
        player_error = pat_error;
        player_error_pos = prog_error_pos;
        pat_error = 0; /* queries stop at any pat_error */
        pat_abort();
        memcpy(prog_patterns, saved_patterns, sizeof saved_patterns);
        prog_pattern_count = saved_count;
        prog_cpm10 = saved_cpm10;
        return false;
    }
    player_error = 0;
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

/* Gain (in 64ths, 0xFF for none) as steps below the patch's level. */
static int8_t gain_att(uint8_t g64)
{
    if (g64 == 0xFF)
        return 0;
    if (g64 == 0)
        return 63;
    int16_t g12 = (int16_t)g64 << 6; /* 4096 is a gain of 1 */
    if (g12 <= 4096)
        return steps_below(g12);
    /* louder than the patch: the same steps the other way, up to 4x */
    int32_t inv = ((int32_t)4096 * 4096) / g12;
    return (int8_t)-steps_below(inv < 1024 ? 1024 : inv);
}

/* One hap becomes one note, or nothing. */
static bool make_note(const pat_hap_t *h, int32_t cycle, int32_t base, note_t *n)
{
    pat_atom_t at;
    uint8_t s = h->ctl[CTL_S];
    uint8_t patch = PATCH_DEFAULT; /* Strudel's default sound, triangle */
    uint8_t drum = SOUND_SYNTH;
    if (s != PAT_NONE) {
        pat_atom_get(s, &at);
        patch = at.patch;
        drum = at.drum;
        if (patch == PATCH_UNKNOWN) {
            problem("unknown sound", at.pos);
            return false;
        }
    }
    bool synth = drum == SOUND_SYNTH;
    if (synth)
        drum = 0;

    int16_t note;
    bool given = h->ctl[CTL_NOTE] != PAT_NONE || h->note != NO_VAL;
    if (drum) {
        note = drum;
    } else if (given) {
        note = h->note;
    } else if (h->n != NO_VAL && synth) {
        note = h->n; /* synths: n is the note */
    } else {
        note = synth ? 36 : 48;
    }
    if (note < 0 || note > 127) {
        pat_atom_get(h->ctl[CTL_NOTE] != PAT_NONE ? h->ctl[CTL_NOTE] : h->atom, &at);
        problem("not a note", at.pos);
        return false;
    }

    uint32_t start = (uint32_t)cycle << 16;
    n->t = start + pos_of(&h->t[WB], base);
    n->end = start + pos_of(&h->t[WE], base);
    n->patch = patch;
    n->note = (uint8_t)note;
    n->att = gain_att(h->gain);
    n->drum = drum != 0;
    n->src[0] = h->ctl[CTL_S];
    n->src[1] = h->ctl[CTL_NOTE] != PAT_NONE ? h->ctl[CTL_NOTE] : h->ctl[CTL_N];
    if (n->src[0] == PAT_NONE)
        n->src[0] = h->atom;
    return true;
}

/* The voice for a note at t.
 *
 * Drums retrigger their own voice (as RPTracker plays them). Other notes
 * avoid the voice whose note ends just as this one starts: keying it off
 * and on again at once cuts the old note and restarts the wave, which
 * clicks. So a note takes, in order: a voice of its patch that went quiet
 * earlier (two of them alternate, each fading out between its notes); a
 * voice never used; a free voice that was not a drum (drums may still
 * ring); then the voice just freed, any free voice, or the one that frees
 * soonest. The earliest freed wins each time. */
static uint8_t pick_voice(uint32_t t, uint8_t patch, bool drum)
{
    uint8_t best = 0xFF;
    for (uint8_t pass = drum ? 3 : 0; pass < 5 && best == 0xFF; pass++) {
        for (uint8_t v = 0; v < OPL_VOICES; v++) {
            uint32_t f = voices[v].free_at;
            uint8_t p = player_voice_patch[v];
            bool ok;
            switch (pass) {
            case 0: ok = p == patch && f < t; break;
            case 1: ok = p == PATCH_UNKNOWN; break;
            case 2: ok = f < t && !player_voice_drum[v]; break;
            case 3: ok = p == patch && f <= t; break;
            default: ok = f <= t; break;
            }
            if (ok && (best == 0xFF || f < voices[best].free_at))
                best = v;
        }
    }
    if (best != 0xFF)
        return best;
    for (uint8_t v = 0; v < OPL_VOICES; v++)
        if (best == 0xFF || voices[v].free_at < voices[best].free_at)
            best = v;
    return best;
}

static void add_off(uint16_t time, uint8_t voice, uint8_t serial)
{
    if (off_count >= OFFS_MAX)
        return;
    uint8_t j = off_count++;
    while (j > 0 && offs[j - 1].time > time) {
        offs[j] = offs[j - 1];
        j--;
    }
    offs[j].time = time;
    offs[j].voice = voice;
    offs[j].serial = serial;
}

static void emit(uint16_t time, uint8_t kind, const note_t *n, uint8_t serial)
{
    event_t e;
    e.time = time;
    e.kind = kind;
    e.serial = serial;
    if (n) {
        e.note = n->note;
        e.att = n->att;
        e.patch = n->patch;
        e.src[0] = n->src[0];
        e.src[1] = n->src[1];
    } else {
        e.src[0] = e.src[1] = 0xFF;
    }
    if (!clock_add_event(&e))
        problem("too many notes in one cycle", 0);
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

void player_fill(uint16_t cycle)
{
    uint32_t base = (uint32_t)cycle << 16;
    uint32_t next = base + 0x10000;

    overlay_load(OVERLAY_QUERY); /* (already there, unless just parsed) */
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
    off_count = 0;
    uint8_t keep = 0;
    for (uint8_t i = 0; i < pending_count; i++) {
        pending_off_t *p = &pending[i];
        if (p->t < next) {
            add_off(p->t > base ? (uint16_t)(p->t - base) : 0, p->voice, p->serial);
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
        uint16_t period = pat_period_memo(root);
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
                n->drum = c.drum;
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
        uint8_t v = pick_voice(n->t, n->patch, n->drum);
        voice_t *vo = &voices[v];
        uint8_t serial = ++vo->serial;
        vo->free_at = n->end;
        player_voice_patch[v] = n->patch;
        player_voice_drum[v] = n->drum;
        n->voice = v;
        n->serial = serial;

        /* Drums ring out by themselves; other notes end with the event. */
        if (n->drum)
            continue;
        if (n->end < next) {
            add_off((uint16_t)(n->end - base), v, serial);
        } else if (pending_count < PENDING_MAX) {
            pending[pending_count].t = n->end;
            pending[pending_count].voice = v;
            pending[pending_count].serial = serial;
            pending_count++;
        }
    }

    /* Out in time order: a note-off before a note-on at the same time. */
    uint8_t j = 0;
    for (uint8_t i = 0; i < count; i++) {
        uint16_t t = (uint16_t)(notes[i].t - base);
        for (; j < off_count && offs[j].time <= t; j++)
            emit(offs[j].time, offs[j].voice, 0, offs[j].serial);
        emit(t, EV_ON | notes[i].voice, &notes[i], notes[i].serial);
    }
    for (; j < off_count; j++)
        emit(offs[j].time, offs[j].voice, 0, offs[j].serial);
}
