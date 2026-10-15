/* SPDX-License-Identifier: GPL-3.0-only */
/* MEL: a tape keyboard for the FM-1, after the Mellotron M400, with a love of old tape. Every key has its own tape:
 * press it and the tape runs past the head; let go and a spring pulls it back (pressed again before it is back, it
 * starts where it is). Built on Dinghy (its platform.c: updates, the USB rescue, USB-MIDI, USB audio, the panel,
 * the screen, settings).
 *
 * The panel: a button is a section; pressed again it turns its pages; SELECT turns them too. On every page the
 * four knobs do what the screen's aluminium plate says (on FX pages the last one is always MIX).
 *   HOME   PLAY    the M400: TONE, PITCH (semitones), ATTACK, RELEASE (each sound its own: SETUP's SOUNDS);
 *                  HOME again: the next sound
 *   SEL    LAYER   a second sound on the same tape's other track: SOUND (OFF A B C D E), MIX (between the two), OCTAVE,
 *                  DETUNE; on its page SEL again turns the SOUND (lit: a layer sounds)
 *   LFO    LOFI    TAPE: WOW, FLUTTER, DRIVE (into the tape: saturation), HISS  -  WEAR: AGE (a copy of a copy:
 *                  the band narrows, and the copying deck adds its hiss, flutter, drive), DROPOUT, DRIFT, CRUSH
 *   FX     FX      VIBRATO: RATE DEPTH TONE MIX - ECHO: TIME REPEATS TONE MIX - REVERB: SIZE TONE PRE MIX
 *          held    LFO held 0.6 s: LOFI out / in; FX held: FX out / in (each on its own; the EQ stays; lit: in, dark: out)
 *   EDIT   EQ      LOW, MID, FREQ (the mid's), HIGH; held 0.6 s: back to flat
 *   GLO    SETUP   TEMPO (ARP, SEQ), TUNE (fine: to play along with others), LENGTH (a note: as long as held / 8 s,
 *                  as on the M400), SCREEN (the old-tape look / clean); the screen says what each one does.
 *          SOUNDS  GLO again: PLAY OWN (each sound its own PLAY) / SHARED (one for all); every sound's PLAY, a table
 *   ARP    ARP     on / off and its page: MODE, RATE, OCTAVES, GATE
 *   SEQ    SEQ     16 steps of up to 4 notes: LENGTH, GATE, STEP, SWING; PLAY start / stop, REC record
 *                  (step recording: SEL is a rest, SEL held 1 s clears the pattern)
 *   PRESETS the sound: A FLUTE, B CHOIR, C CELLO, D VIBES, E PIANO (a track selector: it stops at A and E; HOME goes
 *          round)   ALGORITHM the tempo, anywhere
 *   ENV    HOLD    the notes stay after you let go; the next chord replaces them
 *          held    ENV held 0.6 s: HALF SPEED on / off (as the M4000D's switch): the motor at half speed, the tapes an
 *                  octave down and twice as long, its wow and flutter slower; it slows down and speeds up in ~0.4 s
 *   OCT- OCT+   SAVE (it also saves itself)
 *   USB-MIDI notes in (any channel), sustain pedal, mod wheel = vibrato, pitch bend = the motor (+-2 semitones)
 *
 * The sound: mel_fx.c (the tape, then EQ, VIBRATO, ECHO, REVERB). The screen: mel_ui.c. ARP and SEQ:
 * mel_seq.c. The tapes: firmware/gen/mel_tapes.h (tools/gen_tapes.py): IMA ADPCM at 11025 Hz, a root every 3-4 semitones
 * (the piano: at 22050 Hz, MEL_RATE, a root every half octave).
 * Integers only (no FPU, no libgcc): docs/RULES.md. */
#define APP_NAME "MEL"
#define CV_MAX (240u * 124u)                    /* the stage is the biggest canvas */
#include "platform.c"

typedef struct {
    uint32_t off, n, ls, ck;                    /* byte offset in MEL_DATA, samples, loop start, first checkpoint */
    uint8_t root, lo, hi;                       /* MIDI notes */
    uint8_t sh;                                 /* its rate: MEL_RATE >> sh (tape time stays in MEL_RATE samples) */
    uint8_t env;                                /* 1..: its loudness as recorded, MEL_ENV[env - 1] (the piano) */
    uint16_t dk, d1k, pk;                       /* a sound that dies away (dk 0: it does not): its decay per tape
                                                 * sample (Q24) and per 1024 (Q15), from its peak (sample pk) */
} mel_zone_t;
#include "mel_tapes.h"
#include "mel_sprites.h"
#include "mel_fonts.h"

/* ---- the sound (the audio ISR: no flash writes, no screen) */
#define NV 12u                                  /* voices */
#define SUB 32u                                 /* frames between envelope / pitch updates */
#define ONE (1 << 24)                           /* the envelope's full level */
#define TAPE_LEN (8u * MEL_RATE)                /* 8 s of tape, in tape samples */
#define REWIND (8u * HALF_FRAMES * MEL_RATE / FS)   /* back at 8 x the speed: an 8 s tape in 1 s */
#define K_END 19000                             /* the end of a tape (8 SEC): gone in ~12 ms */
#define G_TAPE 21500                            /* a tape's level, Q15 (the sets are all as loud: gen_tapes.py) */

static const int16_t IMA_STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
    4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767};
static const int8_t IMA_IDX[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

typedef struct {                                /* a head on a tape: the ADPCM decoder and the interpolator */
    const mel_zone_t *z;
    uint32_t pos, frac;                         /* next sample to decode; where we are between s[1] and s[2], Q16 */
    int32_t pred, idx, gain, s[4];              /* the last four samples (4-point Hermite between the middle two) */
    int32_t dg;                                 /* a sound that dies away: how far it has, 0..ONE */
    int32_t lp, kf;                             /* (env) its brightness: a low-pass closing as it dies away, Q14,
                                                 * down to kf (4 x the note's pitch) */
} head_t;

static struct {
    volatile uint32_t trig;                     /* main: + 1 = (re)start on note, from where its tape is */
    volatile uint8_t note, gate;                /* main: the note; held (key, MIDI, HOLD or pedal) */
    volatile uint8_t busy;                      /* ISR: the voice has its note (sounding, or a held key at the end of its tape) */
    volatile int32_t amp;                       /* ISR: the level now, 0..ONE */
    uint32_t seen, age;                         /* ISR: the last trig; main: when it started (stealing) */
    uint8_t ended, set, layer;                  /* ISR: the tape ran out; the sound its heads read (layer 0: none) */
    int8_t loct;                                /* ISR: the layer's octave */
    int32_t det, drift;                         /* ISR: this key's own pitch; the voice's slow wander (DRIFT) */
    head_t h, h2;                               /* the sound (PRESETS) and the layer (SEL) */
} voice[NV];
static volatile uint32_t tpos[128];             /* ISR: where each key's tape is, tape samples from its start */

static volatile uint8_t p_sound, p_tape8, p_layer;   /* main -> ISR (one writer each); layer 0 off, else sound + 1 */
static volatile int32_t p_tune16, p_bend16, p_drift, p_mod, p_atk, p_relk, p_loct, p_ldet16, p_ga, p_gb;
static volatile uint8_t p_half;                 /* main -> ISR: HALF SPEED */
static int32_t motor16;                         /* ISR: the motor's speed, 0 (full) .. -19200 (half: an octave, 1/16 cents) */

static int32_t sine(uint32_t ph)                /* a full cycle over 2^32 -> -32767..32767 (Dinghy's parabola) */
{
    int32_t x = (int32_t)ph >> 16, ax = x < 0 ? -x : x, y, ay;
    y = (x * (32768 - ax)) >> 13;
    ay = y < 0 ? -y : y;
    y += ((((y * ay) >> 15) - y) * 7373) >> 15;
    return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}
static uint32_t rnd_state = 0x2545F491u;
static uint32_t rnd(void)                       /* xorshift32 (ISR only) */
{
    uint32_t x = rnd_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return rnd_state = x;
}

/* 1/16 cents -> tape samples per output frame, Q16 */
static uint32_t step_of(int32_t c16)
{
    uint32_t u, c, f, oct, r, p;
    if (c16 < -4799 * 16) c16 = -4799 * 16;
    if (c16 > 4799 * 16) c16 = 4799 * 16;
    u = (uint32_t)(c16 + 4800 * 16);
    c = u >> 4;
    f = u & 15u;
    oct = c / 1200u;
    r = c % 1200u;
    p = MEL_POW2[r] + (((MEL_POW2[r + 1u] - MEL_POW2[r]) * f) >> 4);    /* 2^(r/1200), Q30 */
    p = ((p >> 14) * MEL_RATE_Q16) >> 16;      /* x MEL_RATE / 44100, Q16 (32-bit: no 64-bit shift, no libgcc) */
    return oct >= 4u ? p << (oct - 4u) : p >> (4u - oct);
}

static const mel_zone_t *zone_of(uint32_t set, uint32_t note)
{
    uint32_t i, z0 = MEL_SET_Z0[set], nz = MEL_SET_NZ[set];
    for (i = 0; i + 1u < nz; i++)
        if (note <= MEL_ZONE[z0 + i].hi)
            break;
    return &MEL_ZONE[z0 + i];
}

static int32_t adpcm_next(head_t *h)
{
    const mel_zone_t *z = h->z;
    uint32_t b = MEL_DATA[z->off + (h->pos >> 1)], code = h->pos & 1u ? b >> 4 : b & 15u;
    int32_t step = IMA_STEP[h->idx], vd = step >> 3, p;
    if (code & 4u) vd += step;
    if (code & 2u) vd += step >> 1;
    if (code & 1u) vd += step >> 2;
    p = h->pred + (code & 8u ? -vd : vd);
    h->pred = p > 32767 ? 32767 : p < -32768 ? -32768 : p;
    h->idx += IMA_IDX[code & 7u];
    h->idx = h->idx < 0 ? 0 : h->idx > 88 ? 88 : h->idx;
    if (++h->pos >= z->n) {                     /* the loop: back to its start, with the decoder's state there */
        uint32_t ck = MEL_CKS[z->ck + z->ls / MEL_CK];
        h->pos = z->ls;
        h->pred = (int16_t)(ck & 0xFFFFu);
        h->idx = (int32_t)(ck >> 16);
    }
    return h->pred;
}

static int32_t env_at(const mel_zone_t *z, uint32_t t)   /* a zone's loudness t of its samples in, Q15 */
{
    const uint16_t *e = MEL_ENV[z->env - 1u];
    uint32_t i = t / MEL_ENV_STEP, f = (t % MEL_ENV_STEP) * 256u / MEL_ENV_STEP;
    if (i + 1u >= MEL_ENV_N) return 0;
    return (int32_t)e[i] + ((((int32_t)e[i + 1u] - (int32_t)e[i]) * (int32_t)f) >> 8);
}
static int32_t sqrt_u(uint32_t x)               /* floor(sqrt(x)) */
{
    uint32_t r = 0, b = 1u << 30;
    while (b > x) b >>= 2;
    for (; b; b >>= 2) {
        if (x >= r + b) { x -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
    }
    return (int32_t)r;
}

/* put a head on tape t samples in (MEL_RATE's; past the end of the recording: round its loop), for the note n */
static void head_start(head_t *h, const mel_zone_t *z, uint32_t t, int32_t gain, uint32_t n)
{
    uint32_t p, k, ck;
    t >>= z->sh;                                /* (in the zone's own samples from here) */
    p = t < z->n ? t : z->ls + (t - z->ls) % (z->n - z->ls);
    k = p / MEL_CK;
    ck = MEL_CKS[z->ck + k];
    h->z = z;
    h->pos = k * MEL_CK;
    h->pred = (int16_t)(ck & 0xFFFFu);
    h->idx = (int32_t)(ck >> 16);
    h->frac = 0;
    h->gain = gain;
    h->dg = ONE;
    if (z->dk) {                                /* restarted mid-tape: as quiet as the tape is there */
        uint32_t rem = t > z->pk ? t - z->pk : 0;
        int32_t dg = ONE;
        for (; rem >= 1024u && dg > 4096; rem -= 1024u)
            dg = (dg >> 15) * z->d1k;
        h->dg = dg;
    }
    if (z->env) {                               /* 76 x 2^(n/12): 4 x its pitch, as a one-pole's coefficient, Q14 */
        int32_t kf = (int32_t)((((MEL_POW2[n % 12u * 100u] >> 14) * 76u) >> 16) << (n / 12u));
        h->kf = kf > 16384 ? 16384 : kf;
        h->dg = env_at(z, t) << 9;
    }
    h->s[1] = h->s[0] = adpcm_next(h);
    h->s[2] = adpcm_next(h);
    h->s[3] = adpcm_next(h);
    h->lp = h->s[1];
}

static uint32_t layer_note(uint32_t note, int32_t oct)   /* the layer's note, OCTAVE away */
{
    int32_t n = (int32_t)note + 12 * oct;
    return (uint32_t)(n < 0 ? 0 : n > 127 ? 127 : n);
}
static void voice_head(uint32_t vi)             /* the heads on the current sound's and layer's tracks, where the tape is */
{
    uint32_t st = p_sound % MEL_NSETS, ly = p_layer, note = voice[vi].note, hsh = note * 2654435761u;
    int32_t g = (G_TAPE * (32768 + ((int32_t)((hsh >> 8) & 0xFFu) - 128) * 30)) >> 15;   /* every tape its own level */
    voice[vi].set = (uint8_t)st;
    voice[vi].layer = (uint8_t)ly;
    voice[vi].loct = (int8_t)p_loct;
    head_start(&voice[vi].h, zone_of(st, note), tpos[note], g, note);
    if (ly) {                                   /* (the same tape, another track: as the M400's A B C) */
        uint32_t ln = layer_note(note, p_loct);
        head_start(&voice[vi].h2, zone_of((ly - 1u) % MEL_NSETS, ln), tpos[note], g, ln);
    }
}

static int struck(void)                        /* the piano is on either track: a key strikes anew */
{
    uint32_t ly = p_layer;
    return MEL_SET_STRUCK[p_sound % MEL_NSETS] || (ly && MEL_SET_STRUCK[(ly - 1u) % MEL_NSETS]);
}
static void voice_start(uint32_t vi)
{
    uint32_t note = voice[vi].note, hsh = note * 2654435761u;
    voice[vi].det = (int32_t)(hsh >> 24) - 128;   /* this key's tape: its own tuning (DRIFT) */
    if (struck())
        tpos[note] = 0;                         /* the piano (on either track): a new strike, its tape from the start */
    voice_head(vi);
    voice[vi].amp = 0;
    voice[vi].ended = p_tape8 && tpos[note] >= TAPE_LEN;
    voice[vi].busy = 1;
}

/* the head's samples into acc (n frames), 4-point Hermite (smooth: none of linear's fizz), at gm (Q15) of its level
 * (and, a sound that dies away, as far as it has); step and t, the tape samples it read: MEL_RATE's */
static uint32_t head_run(head_t *h, uint32_t step, int32_t *acc, uint32_t n, int32_t gm, uint32_t t)
{
    uint32_t i, ran = 0, frac = h->frac, sh = h->z->sh, env = h->z->env;
    int32_t a = h->s[0], b = h->s[1], c = h->s[2], d = h->s[3], g = (h->gain * gm) >> 15, k = 16384, lp = h->lp;
    step >>= sh;                                /* (the zone's own samples from here) */
    t >>= sh;
    if (env) {                                  /* the piano: as loud as its recording was here, and as bright: the
                                                 * low-pass closes with the square root of it (-20 dB: a third open) */
        int32_t ge = env_at(h->z, t);
        h->dg = ge << 9;
        k = h->kf + (((16384 - h->kf) * sqrt_u((uint32_t)ge << 15)) >> 15);
    }
    if (h->z->dk || env)
        g = (g * (h->dg >> 9)) >> 15;
    for (i = 0; i < n; i++) {
        int32_t t, c1, c2, c3, y;
        frac += step;
        while (frac >= 65536u) {
            frac -= 65536u;
            a = b;
            b = c;
            c = d;
            d = adpcm_next(h);
            ran++;
        }
        t = (int32_t)(frac >> 4);               /* Q12 */
        c1 = (c - a) >> 1;
        c2 = a - ((5 * b) >> 1) + 2 * c - (d >> 1);
        c3 = ((d - a) >> 1) + ((3 * (b - c)) >> 1);
        y = ((((((c3 * t) >> 12) + c2) * t >> 12) + c1) * t >> 12) + b;
        if (env) {
            lp += ((y - lp) * k) >> 14;
            y = lp;
        }
        acc[i] += (y * g) >> 15;
    }
    h->lp = lp;
    h->frac = frac;
    h->s[0] = a;
    h->s[1] = b;
    h->s[2] = c;
    h->s[3] = d;
    if (h->z->dk && t > h->z->pk)               /* the vibraphone, the piano: they die away as the tape runs */
        h->dg -= ((h->dg >> 12) * (int32_t)(ran * h->z->dk)) >> 12;
    return ran << sh;
}

static void voice_sub(uint32_t vi, int32_t *mix, int32_t g16, int32_t drift, int32_t atk, int32_t relk)
{
    int32_t acc[SUB], a0, a1, a, da, c16;
    uint32_t i, ran, note;
    if (voice[vi].trig != voice[vi].seen || (!voice[vi].busy && voice[vi].gate)) {
        voice[vi].seen = voice[vi].trig;
        voice_start(vi);
    }
    if (!voice[vi].busy)
        return;
    note = voice[vi].note;
    a0 = voice[vi].amp;
    if (voice[vi].gate && !voice[vi].ended)
        a1 = a0 + atk > ONE ? ONE : a0 + atk;  /* the head meets the tape (ATTACK) */
    else {
        int32_t rk = voice[vi].ended ? K_END : relk;
        if (MEL_SET_STRUCK[voice[vi].set] && note >= 89u && !voice[vi].ended)
            rk = MEL_REL[95];                   /* a piano's keys from F6 up have no dampers: they ring on as they
                                                 * would held (a slow fade, ~4.5 s to -60 dB, frees the voice) */
        a1 = a0 - (((a0 >> 8) * rk) >> 8) - 64;
        if (a1 < 0) a1 = 0;
    }
    if (!a0 && !a1) {
        if (!voice[vi].gate)
            voice[vi].busy = 0;                 /* gone (a held key at the end of its tape keeps its voice) */
        return;
    }
    if (voice[vi].set != p_sound % MEL_NSETS || voice[vi].layer != p_layer || voice[vi].loct != p_loct)
        voice_head(vi);                         /* a selector moved: the heads follow at once */
    voice[vi].drift += (int32_t)(rnd() >> 29) - 4;   /* a slow wander (~0.7 s) */
    voice[vi].drift -= voice[vi].drift >> 10;
    c16 = g16 + voice[vi].det * drift / 32 + ((voice[vi].drift * drift) >> 6);   /* DRIFT: keys +-25 ct, wander +-9 ct */
    for (i = 0; i < SUB; i++)
        acc[i] = 0;
    if (voice[vi].layer) {                      /* the sound and the layer, MIX between them */
        uint32_t ln = layer_note(note, voice[vi].loct);
        ran = head_run(&voice[vi].h, step_of(((int32_t)note - voice[vi].h.z->root) * 1600 + c16), acc, SUB, p_ga,
                       tpos[note]);
        head_run(&voice[vi].h2, step_of(((int32_t)ln - voice[vi].h2.z->root) * 1600 + c16 + p_ldet16), acc, SUB, p_gb,
                 tpos[note]);
    } else
        ran = head_run(&voice[vi].h, step_of(((int32_t)note - voice[vi].h.z->root) * 1600 + c16), acc, SUB, 32767,
                       tpos[note]);
    da = (a1 - a0) / (int32_t)SUB;
    for (a = a0, i = 0; i < SUB; i++, a += da)
        mix[i] += (acc[i] * (a >> 9)) >> 15;
    voice[vi].amp = a1;
    if (tpos[note] + ran >= TAPE_LEN) {
        tpos[note] = TAPE_LEN;                  /* (without 8 SEC the tape loops; it rewinds 8 s at most) */
        if (p_tape8)
            voice[vi].ended = 1;
    } else
        tpos[note] += ran;
}

/* the reverb: four combs and two allpasses per side (Freeverb's lengths), in the pool */
static const uint16_t RV_CL[4] = {1116, 1188, 1277, 1356}, RV_AL[4] = {556, 441, 579, 464};
static int32_t rv_c[4][1356] __attribute__((section(".pool")));
static int32_t rv_a[4][580] __attribute__((section(".pool")));
static uint16_t rv_ci[4], rv_ai[4];
static int32_t rv_f[4];

static int32_t sat(int32_t x)                   /* the tape's saturation, 2 x full scale -> full scale */
{
    uint32_t a = (uint32_t)(x < 0 ? -x : x), i, f;
    int32_t y;
    if (a >= 65536u)
        y = 32767;
    else {
        i = a >> 8;
        f = a & 255u;
        y = (int32_t)MEL_SAT[i] + ((((int32_t)MEL_SAT[i + 1u] - (int32_t)MEL_SAT[i]) * (int32_t)f) >> 8);
    }
    return x < 0 ? -y : y;
}

#include "mel_fx.c"

static void app_audio(int32_t *out, uint32_t n)
{
    static int32_t mix[HALF_FRAMES], act;
    static uint8_t held[128];
    int32_t drift = (p_drift * (32767 - lofi_bp)) >> 15, atk = p_atk, relk = p_relk, sum = 0;   /* (no LOFI: in tune) */
    uint32_t v, i, b;
    if (n > HALF_FRAMES)
        n = HALF_FRAMES;
    for (i = 0; i < n; i++)
        mix[i] = 0;
    fx_block();
    for (b = 0; b < n; b += SUB) {              /* the motor (the same for every tape), then every voice */
        int32_t g16;
        motor16 += p_half ? (motor16 > -19200 + 35 ? -35 : -19200 - motor16)   /* HALF SPEED: ~0.4 s to get there */
                          : (motor16 < -35 ? 35 : -motor16);
        g16 = p_tune16 + p_bend16 + fx_pitch16() + motor16;
        for (v = 0; v < NV; v++)
            voice_sub(v, mix + b, g16, drift, atk, relk);
    }
    for (i = 0; i < 128u; i++)                  /* the tapes not held run back */
        held[i] = 0;
    for (v = 0; v < NV; v++) {
        if (voice[v].busy) {
            held[voice[v].note] = 1;
            sum += voice[v].amp >> 9;
        }
    }
    for (i = 0; i < 128u; i++)
        if (!held[i] && tpos[i])
            tpos[i] = tpos[i] > REWIND ? tpos[i] - REWIND : 0;
    act += ((sum > 32767 ? 32767 : sum) - act) >> 2;
    fx_run(mix, out, n, act);
}

/* ---- notes (main loop): a note sounds while anything holds it */
enum { S_KEY = 1, S_MIDI = 2, S_LATCH = 4, S_PED = 8 };   /* (+ S_ARP, S_SEQ: mel_seq.c) */
static uint8_t src[128];
static uint32_t ages;

static void note_on(uint32_t note)
{
    uint32_t v, pick = 0, best = 0xFFFFFFFFu;
    int st = struck();
    for (v = 0; v < NV && !st; v++)         /* its own voice still sounding: the tape just goes on (the piano: a
                                                 * new strike in a voice of its own, the last one dying away) */
        if ((voice[v].busy || voice[v].gate) && voice[v].note == note) {
            voice[v].gate = 1;
            voice[v].age = ++ages;
            return;
        }
    for (v = 0; v < NV; v++) {                  /* else a free voice, else the oldest released, else the oldest */
        uint32_t score = voice[v].gate ? 0x80000000u + voice[v].age      /* (gate: taken, maybe not started yet) */
                       : voice[v].busy ? 0x40000000u + voice[v].age : 0u;
        if (score < best) { best = score; pick = v; }
    }
    voice[pick].note = (uint8_t)note;
    voice[pick].age = ++ages;
    voice[pick].trig++;
    voice[pick].gate = 1;
}
static void note_off(uint32_t note)
{
    uint32_t v;
    for (v = 0; v < NV; v++)
        if (voice[v].note == note)
            voice[v].gate = 0;
}
static void src_set(uint32_t note, uint32_t set_b, uint32_t clr_b)
{
    uint32_t was, now;
    if (note > 127u) return;
    was = src[note];
    now = (was | set_b) & ~clr_b;
    src[note] = (uint8_t)now;
    if (!was && now) note_on(note);
    else if (was && !now) note_off(note);
    else if (now & ~was & ~(uint32_t)(S_LATCH | S_PED) && struck())
        note_on(note);                          /* the piano: a key played again under the pedal or HOLD strikes it */
}
static int quiet(void)
{
    uint32_t v;
    for (v = 0; v < NV; v++)
        if (voice[v].busy) return 0;
    return 1;
}

/* ---- the parameters: every knob of every page, with its range, its default and its step */
enum {
    P_SOUND, P_OCT,
    P_TONE, P_PITCH, P_ATK, P_REL,              /* PLAY */
    P_BSND, P_BMIX, P_BOCT, P_BDET,             /*       LAYER */
    P_WOW, P_FLUT, P_SAT, P_HISS,               /* LOFI: TAPE */
    P_AGE, P_DROP, P_DRIFT, P_CRUSH,            /*       WEAR */
    P_VRATE, P_VDEPTH, P_VTONE, P_VMIX,         /* FX: VIBRATO */
    P_ETIME, P_EFB, P_ETONE, P_EMIX,            /*     ECHO */
    P_RSIZE, P_RTONE, P_RPRE, P_RMIX,           /*     REVERB */
    P_EQLO, P_EQMID, P_EQFREQ, P_EQHI,          /* EQ */
    P_BPM, P_TUNE, P_LEN, P_LOOK,               /* SETUP */
    P_AMODE, P_ARATE, P_AOCT, P_AGATE,          /* ARP */
    P_SLEN, P_SGATE, P_STEP, P_SWING,           /* SEQ */
    P_PMODE,                                    /* SETUP: SOUNDS (PLAY: each sound its own, or one for all) */
    NP
};
static const int16_t PINFO[NP][4] = {           /* lo, hi, default, step */
    {0, MEL_NSETS - 1, 0, 1}, {-2, 2, 0, 1},
    {0, 100, 72, 1}, {-12, 12, 0, 1}, {0, 100, 4, 1}, {0, 100, 18, 1},
    {0, MEL_NSETS, 0, 1}, {0, 100, 50, 1}, {-1, 1, 0, 1}, {0, 30, 0, 1},
    {0, 100, 28, 1}, {0, 100, 22, 1}, {0, 100, 22, 1}, {0, 100, 12, 1},
    {0, 100, 18, 1}, {0, 100, 0, 1}, {0, 100, 30, 1}, {0, 100, 0, 1},
    {0, 100, 75, 1}, {0, 100, 35, 1}, {0, 100, 70, 1}, {0, 100, 0, 1},
    {0, 100, 38, 1}, {0, 100, 35, 1}, {0, 100, 45, 1}, {0, 100, 0, 1},
    {0, 100, 55, 1}, {0, 100, 45, 1}, {0, 100, 15, 1}, {0, 100, 18, 1},
    {-12, 12, 0, 1}, {-12, 12, 0, 1}, {0, 15, 8, 1}, {-12, 12, 0, 1},
    {40, 240, 90, 1}, {-50, 50, 0, 1}, {0, 1, 0, 1}, {0, 1, 1, 1},
    {0, 4, 0, 1}, {0, 5, 3, 1}, {1, 3, 1, 1}, {10, 100, 60, 5},
    {1, 16, 16, 1}, {10, 100, 80, 5}, {0, 15, 0, 1}, {0, 100, 0, 5},
    {0, 1, 1, 1},
};

#define SET_VER 8u                              /* (7: the same, but the piano's PLAY was the tapes': it starts
                                                 * again from its own) */
#define NPLAY 4u                                /* PLAY's TONE PITCH ATTACK RELEASE: each sound its own */
typedef struct {
    uint8_t ver, np, half, rsv;                 /* half: HALF SPEED (a reserved byte before: older settings load, off) */
    int16_t p[NP];
    uint8_t steps[16][4];                       /* the pattern: up to 4 notes a step, 0 = none */
    int16_t play[MEL_NSETS][NPLAY];             /* every sound's PLAY (the one playing: in p, here when it changes) */
} mel_set_t;
typedef struct {                                /* version 6, before SOUNDS: it loads, its PLAY every sound's */
    uint8_t ver, np, half, rsv;
    int16_t p[NP - 1u];
    uint8_t steps[16][4];
} mel_set6_t;
static mel_set_t set;
#define PV(i) ((int32_t)set.p[i])

static const int16_t EQ_G[25] = {1029, 1154, 1295, 1453, 1631, 1830, 2053, 2303, 2584, 2900, 3254, 3651, 4096,   /* dB -> Q12 */
                                  4596, 5157, 5786, 6492, 7284, 8173, 9170, 10289, 11544, 12953, 14533, 16306};
static uint8_t by_lofi, by_fx;                  /* LFO / FX held: LOFI / FX out (not saved: a power-on has them in) */
static int32_t echo_ms(int32_t v) { return 60 + v * v * 940 / 10000; }   /* TIME: finer where it is short */
static void apply(void)                         /* the parameters -> the sound */
{
    int32_t d = PV(P_VDEPTH);
    p_sound = (uint8_t)PV(P_SOUND);
    p_loct = PV(P_BOCT);                        /* (the octave before the layer: a voice reads them together) */
    p_ldet16 = PV(P_BDET) * 16;
    p_ga = sine(0x40000000u - (uint32_t)PV(P_BMIX) * (0x40000000u / 100u));   /* MIX: the same power all the way */
    p_gb = sine((uint32_t)PV(P_BMIX) * (0x40000000u / 100u));
    p_layer = (uint8_t)PV(P_BSND);
    p_tape8 = (uint8_t)PV(P_LEN);
    p_half = set.half;
    p_tune16 = PV(P_TUNE) * 16;
    p_drift = PV(P_DRIFT);
    p_atk = (int32_t)MEL_ATK[PV(P_ATK)];
    p_relk = MEL_REL[PV(P_REL)];
    f_tone = MEL_TONE[PV(P_TONE)];
    f_wow = PV(P_WOW);
    f_flut = PV(P_FLUT);
    f_sat = PV(P_SAT);
    f_hiss = PV(P_HISS);
    f_age = PV(P_AGE);
    f_drop = PV(P_DROP);
    f_crush = PV(P_CRUSH);
    f_vinc = (int32_t)MEL_LFO[PV(P_VRATE)];
    f_vdep = d * d * 88 / 10000;
    f_vtone = MEL_TONE[PV(P_VTONE)];
    f_vmix = PV(P_VMIX) * 327;
    f_etime = (echo_ms(PV(P_ETIME)) * 441 / 10) << 8;
    f_efb = PV(P_EFB) * 320;
    f_etone = MEL_TONE[PV(P_ETONE)];
    f_emix = PV(P_EMIX) * 327;
    f_rfb = 22938 + PV(P_RSIZE) * 79;
    f_rdamp = 4915 + PV(P_RTONE) * 262;
    f_rpre = PV(P_RPRE) * 5292 / 100;
    f_rmix = PV(P_RMIX) * 327;
    f_glo = EQ_G[PV(P_EQLO) + 12];
    f_gmid = EQ_G[PV(P_EQMID) + 12];
    f_ghi = EQ_G[PV(P_EQHI) + 12];
    f_svf = MEL_SVF[PV(P_EQFREQ)];
    f_by_lofi = by_lofi;
    f_by_fx = by_fx;
}

/* ---- text */
#define KEY0 53u                                /* key 0 = F3 */
static int black(uint32_t note) { return (0x54Au >> (note % 12u)) & 1u; }
static char *put(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }
static char *put_int(char *p, int32_t v, int sign)
{
    char t[12];
    uint32_t n = 0, u = (uint32_t)(v < 0 ? -v : v);
    if (v < 0) *p++ = '-';
    else if (sign && v) *p++ = '+';
    do { t[n++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    while (n) *p++ = t[--n];
    *p = 0;
    return p;
}

/* ---- the pages: a button is a section (a group of pages); its knobs, what they show */
enum { PG_PLAY, PG_LAYER, PG_TAPE, PG_WEAR, PG_VIB, PG_ECHO, PG_VERB, PG_EQ, PG_SETUP, PG_SOUNDS, PG_ARP, PG_SEQ, NPG };
enum { G_PLAY, G_LAYER, G_LOFI, G_FX, G_EQ, G_SETUP, G_ARP, G_SEQ, NG };
static const uint8_t PG_GROUP[NPG] = {G_PLAY, G_LAYER, G_LOFI, G_LOFI, G_FX, G_FX, G_FX, G_EQ, G_SETUP, G_SETUP, G_ARP,
                                      G_SEQ};
static const uint8_t G_FIRST[NG] = {PG_PLAY, PG_LAYER, PG_TAPE, PG_VIB, PG_EQ, PG_SETUP, PG_ARP, PG_SEQ};
static const uint8_t G_COUNT[NG] = {1, 1, 2, 3, 1, 2, 1, 1};
static const char *const G_NAME[NG] = {"PLAY", "LAYER", "LOFI", "FX", "EQ", "SETUP", "ARP", "SEQ"};
static const char *const PG_NAME[NPG] = {"PLAY", "LAYER", "TAPE", "WEAR", "VIBRATO", "ECHO", "REVERB", "EQ", "SETUP",
                                         "SOUNDS", "ARP", "SEQ"};
enum { F_NUM, F_ST, F_DB, F_CT, F_PCT, F_EMS, F_PMS, F_VHZ, F_EQHZ, F_X, F_STEP, F_BPM, F_SND, F_OCT, F_ENUM };
typedef struct {
    uint8_t pid, fmt;
    const char *name;                           /* 0: no knob there */
    const char *const *names;                   /* F_ENUM */
} ctl_t;
static const char *const N_LEN[2] = {"HELD", "8 SEC"};
static const char *const N_LOOK[2] = {"CLEAN", "TAPE"};
static const char *const N_PMODE[2] = {"SHARED", "OWN"};
static const char *const ARP_MODE[5] = {"UP", "DOWN", "UP+DN", "RANDOM", "PLAYED"};
static const char *const ARP_RATE[6] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
static const ctl_t CTL[NPG][4] = {
    {{P_TONE, F_NUM, "TONE", 0}, {P_PITCH, F_ST, "PITCH", 0}, {P_ATK, F_NUM, "ATTACK", 0}, {P_REL, F_NUM, "RELEASE", 0}},
    {{P_BSND, F_SND, "SOUND", 0}, {P_BMIX, F_PCT, "MIX", 0}, {P_BOCT, F_OCT, "OCTAVE", 0}, {P_BDET, F_CT, "DETUNE", 0}},
    {{P_WOW, F_NUM, "WOW", 0}, {P_FLUT, F_NUM, "FLUTTER", 0}, {P_SAT, F_NUM, "DRIVE", 0}, {P_HISS, F_NUM, "HISS", 0}},
    {{P_AGE, F_NUM, "AGE", 0}, {P_DROP, F_NUM, "DROPOUT", 0}, {P_DRIFT, F_NUM, "DRIFT", 0}, {P_CRUSH, F_NUM, "CRUSH", 0}},
    {{P_VRATE, F_VHZ, "RATE", 0}, {P_VDEPTH, F_NUM, "DEPTH", 0}, {P_VTONE, F_NUM, "TONE", 0}, {P_VMIX, F_PCT, "MIX", 0}},
    {{P_ETIME, F_EMS, "TIME", 0}, {P_EFB, F_NUM, "REPEATS", 0}, {P_ETONE, F_NUM, "TONE", 0}, {P_EMIX, F_PCT, "MIX", 0}},
    {{P_RSIZE, F_NUM, "SIZE", 0}, {P_RTONE, F_NUM, "TONE", 0}, {P_RPRE, F_PMS, "PRE", 0}, {P_RMIX, F_PCT, "MIX", 0}},
    {{P_EQLO, F_DB, "LOW", 0}, {P_EQMID, F_DB, "MID", 0}, {P_EQFREQ, F_EQHZ, "FREQ", 0}, {P_EQHI, F_DB, "HIGH", 0}},
    {{P_BPM, F_BPM, "TEMPO", 0}, {P_TUNE, F_CT, "TUNE", 0}, {P_LEN, F_ENUM, "LENGTH", N_LEN},
     {P_LOOK, F_ENUM, "SCREEN", N_LOOK}},
    {{P_PMODE, F_ENUM, "PLAY", N_PMODE}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {{P_AMODE, F_ENUM, "MODE", ARP_MODE}, {P_ARATE, F_ENUM, "RATE", ARP_RATE}, {P_AOCT, F_X, "OCTAVES", 0},
     {P_AGATE, F_PCT, "GATE", 0}},
    {{P_SLEN, F_NUM, "LENGTH", 0}, {P_SGATE, F_PCT, "GATE", 0}, {P_STEP, F_STEP, "STEP", 0}, {P_SWING, F_PCT, "SWING", 0}},
};
static void fmt_val(const ctl_t *c, int32_t v, char *s)   /* a parameter as the plate shows it */
{
    switch (c->fmt) {
    case F_ST: put(put_int(s, v, 1), " st"); break;
    case F_DB: put(put_int(s, v, 1), " dB"); break;
    case F_CT: put(put_int(s, v, 1), " ct"); break;
    case F_PCT: put(put_int(s, v, 0), "%"); break;
    case F_EMS: put(put_int(s, echo_ms(v), 0), " ms"); break;
    case F_PMS: put(put_int(s, v * 120 / 100, 0), " ms"); break;
    case F_VHZ: {
        char *p = put_int(s, MEL_LFO_HZ10[v] / 10, 0);
        *p++ = '.';
        *p++ = (char)('0' + MEL_LFO_HZ10[v] % 10u);
        put(p, " Hz");
        break;
    }
    case F_EQHZ: {
        uint32_t hz = MEL_EQ_HZ[v];
        if (hz < 1000u) put(put_int(s, (int32_t)hz, 0), " Hz");
        else {
            char *p = put_int(s, (int32_t)(hz / 1000u), 0);
            *p++ = '.';
            *p++ = (char)('0' + hz / 100u % 10u);
            put(p, " kHz");
        }
        break;
    }
    case F_X: put_int(put(s, "x"), v, 0); break;
    case F_STEP: put_int(s, v + 1, 0); break;
    case F_SND: put(s, v ? MEL_SET_NAME[v - 1] : "OFF"); break;
    case F_OCT: put(put_int(s, v, 1), " oct"); break;
    case F_ENUM: put(s, c->names[v]); break;
    default: put_int(s, v, 0); break;
    }
}

/* ---- what the screen and the clock share */
static uint32_t page, redraw = 0xFFu;           /* redraw bits: 1 header, 2 stage, 4 tapes, 8 plate */
static uint32_t touched = 99;                   /* the knob turned last (0..3), lit a moment */
static uint32_t t_change, dirty, save_req;
static uint8_t hold, pedal, g_last[NG];
static char msg[32];
static uint32_t t_msg;
static void say(const char *s) { put(msg, s); t_msg = fm1_ms ? fm1_ms : 1; redraw |= 1u; }
static void changed(uint32_t what)              /* a parameter changed: to the sound, saved soon; what = the knob (99: none) */
{
    apply();
    dirty = 1;
    t_change = fm1_ms;
    redraw |= 2u;
    if (what < 4u) {
        touched = what;
        redraw |= 8u;
    }
}

#include "mel_seq.c"
#include "mel_ui.c"

/* encoders with a little acceleration: fast turns go further */
static int32_t turn(uint32_t e)
{
    static uint32_t last[7];
    int32_t d = fm1_enc_take(e);
    if (d) {
        if (fm1_ms - last[e] < 50u) d *= 3;
        last[e] = fm1_ms;
    }
    return d;
}
static int set_param(uint32_t pid, int32_t v)   /* clamped to its range: 1 if it moved */
{
    int32_t lo = PINFO[pid][0], hi = pid == P_STEP ? PV(P_SLEN) - 1 : PINFO[pid][1];
    v = v < lo ? lo : v > hi ? hi : v;
    if (v == PV(pid)) return 0;
    set.p[pid] = (int16_t)v;
    return 1;
}
static void say_ctl(const char *what, uint32_t pg, uint32_t k)
{
    char s[32], *p = put(s, what);
    *p++ = ' ';
    fmt_val(&CTL[pg][k], PV(CTL[pg][k].pid), p);
    say(s);
}
static void go_group(uint32_t g)                /* a section's button: its page, or (in it) its next page */
{
    if (PG_GROUP[page] == g)
        page = G_FIRST[g] + (page - G_FIRST[g] + 1u) % G_COUNT[g];
    else
        page = g_last[g] ? g_last[g] : G_FIRST[g];
    g_last[g] = (uint8_t)page;
    touched = 99;
    redraw |= 0xFu;
}

static uint32_t prev_keys;
static uint8_t held_note[27], midi_note[128];

static int16_t play_ok(uint32_t k, int32_t v)  /* a stored PLAY value, or its default if out of range */
{
    return (int16_t)(v >= PINFO[P_TONE + k][0] && v <= PINFO[P_TONE + k][1] ? v : PINFO[P_TONE + k][2]);
}
static void app_init(void)
{
    uint32_t k, n = 0;
    static mel_set_t s;                         /* (a version 6 loads into its start) */
    mel_set6_t *o = (mel_set6_t *)(void *)&s;
    for (k = 0; k < NP; k++)
        set.p[k] = PINFO[k][2];
    set.ver = SET_VER;
    set.np = NP;
    if (!settings_load(&s, sizeof s) && (s.ver == SET_VER || s.ver == 7u) && s.np == NP)   /* (else: the defaults) */
        n = NP;
    else if (!settings_load(o, sizeof *o) && o->ver == 6u && o->np == NP - 1u)
        n = NP - 1u;
    if (n) {
        const int16_t *p = n == NP ? s.p : o->p;
        const uint8_t *st = n == NP ? &s.steps[0][0] : &o->steps[0][0];
        set.half = s.half == 1u;
        for (k = 0; k < n; k++)
            if (p[k] >= PINFO[k][0] && p[k] <= PINFO[k][1])
                set.p[k] = p[k];
        for (k = 0; k < 64u; k++)
            set.steps[k / 4u][k % 4u] = st[k] < 128u ? st[k] : 0u;
    }
    for (k = 0; k < MEL_NSETS * NPLAY; k++) {   /* every sound's PLAY: its own, or (new) as PLAY was; the piano's
                                                 * from its own (before 8 it had the tapes') */
        uint32_t st = k / NPLAY, i = k % NPLAY;
        set.play[st][i] = n == NP && (s.ver == SET_VER || !MEL_SET_STRUCK[st]) ? play_ok(i, s.play[st][i])
                        : n && !MEL_SET_STRUCK[st] ? set.p[P_TONE + i] : (int16_t)MEL_SET_PLAY[st][i];
    }
    if (PV(P_PMODE) || !n)                      /* (OWN: the knobs are the sound playing's) */
        for (k = 0; k < NPLAY; k++)
            set.p[P_TONE + k] = set.play[PV(P_SOUND)][k];
    set.p[P_STEP] = 0;
    apply();
    for (k = 0; k < 27u; k++)
        held_note[k] = 255;
    clk_last = fm1_ms;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    say("v" APP_VER);
}

static void play_keep(void)                     /* OWN: the PLAY knobs to the sound playing's */
{
    uint32_t k;
    if (PV(P_PMODE))
        for (k = 0; k < NPLAY; k++)
            set.play[PV(P_SOUND)][k] = set.p[P_TONE + k];
}
static void play_take(void)                     /* OWN: the sound playing's to the PLAY knobs */
{
    uint32_t k;
    if (PV(P_PMODE))
        for (k = 0; k < NPLAY; k++)
            set.p[P_TONE + k] = set.play[PV(P_SOUND)][k];
}
static void to_sound(int32_t v)                 /* the sound v (A = 0), with its PLAY */
{
    char s[24] = {0, ' ', ' ', 0};
    play_keep();
    set.p[P_SOUND] = (int16_t)v;
    play_take();
    changed(99);
    redraw |= 8u;
    s[0] = (char)('A' + PV(P_SOUND));
    put(s + 3, MEL_SET_NAME[PV(P_SOUND)]);
    say(s);
}
static void buttons(void)
{
    uint32_t rel, b = fm1_input_edges(&rel);    /* pressed / released since the last call */
    static uint32_t t_sel, sel_long, t_fx, fx_long, t_lfo, lfo_long, t_edit, edit_long, t_env, env_long;
    if (b & (1u << BTN_OCT_DOWN) && set_param(P_OCT, PV(P_OCT) - 1)) { changed(99); say(PV(P_OCT) ? "OCTAVE -" : "OCTAVE 0"); }
    if (b & (1u << BTN_OCT_UP) && set_param(P_OCT, PV(P_OCT) + 1)) { changed(99); say(PV(P_OCT) ? "OCTAVE +" : "OCTAVE 0"); }
    if (b & (1u << BTN_HOME)) {                 /* HOME: to PLAY; there, the next sound */
        if (PG_GROUP[page] == G_PLAY) to_sound((PV(P_SOUND) + 1) % (int32_t)MEL_NSETS);   /* (round and round) */
        else go_group(G_PLAY);
    }
    /* LFO and FX: a press turns to the section (or its next page) when let go; held 0.6 s, its bypass instead */
    if (b & (1u << BTN_LFO)) { t_lfo = fm1_ms; lfo_long = 0; }
    if (fm1_in.buttons & (1u << BTN_LFO) && !lfo_long && fm1_ms - t_lfo > 600u) {
        lfo_long = 1;
        by_lofi = !by_lofi;
        apply();
        say(by_lofi ? "LOFI OUT" : "LOFI IN");
        redraw |= 3u;
    }
    if (rel & (1u << BTN_LFO) && !lfo_long) go_group(G_LOFI);
    if (b & (1u << BTN_FX)) { t_fx = fm1_ms; fx_long = 0; }
    if (fm1_in.buttons & (1u << BTN_FX) && !fx_long && fm1_ms - t_fx > 600u) {
        fx_long = 1;
        by_fx = !by_fx;
        apply();
        say(by_fx ? "FX OUT" : "FX IN");
        redraw |= 3u;
    }
    if (rel & (1u << BTN_FX) && !fx_long) go_group(G_FX);
    if (b & (1u << BTN_EDIT)) { t_edit = fm1_ms; edit_long = 0; }   /* EDIT the same; held, the EQ flat again */
    if (fm1_in.buttons & (1u << BTN_EDIT) && !edit_long && fm1_ms - t_edit > 600u) {
        uint32_t k;
        edit_long = 1;
        for (k = P_EQLO; k <= P_EQHI; k++)
            set_param(k, PINFO[k][2]);
        changed(99);
        say("EQ RESET");
        redraw |= 8u;
    }
    if (rel & (1u << BTN_EDIT) && !edit_long) go_group(G_EQ);
    if (b & (1u << BTN_GLO)) go_group(G_SETUP);
    if (b & (1u << BTN_SEQ)) go_group(G_SEQ);
    if (b & (1u << BTN_ARP)) {                  /* to the ARP page; there, ARP on / off */
        if (page == PG_ARP || !arp_on) {
            arp_enable(!arp_on);
            say(arp_on ? "ARP ON" : "ARP OFF");
        }
        go_group(G_ARP);
    }
    if (b & (1u << BTN_PLAY))
        seq_start(!seq_play);
    if (b & (1u << BTN_REC)) {
        seq_rec = !seq_rec;
        chord_open = 0;
        if (page != PG_SEQ) go_group(G_SEQ);
        say(seq_rec ? (seq_play ? "REC" : "STEP REC") : "REC OFF");
        redraw |= 0xFu;
    }
    if (b & (1u << BTN_SEL)) {
        t_sel = fm1_ms;
        sel_long = 0;
        if (seq_rec && !seq_play) {             /* step recording: a rest (held: the pattern cleared) */
            uint32_t k;
            for (k = 0; k < 4u; k++) set.steps[seq_cur][k] = 0;
            seq_cur = (seq_cur + 1u) % (uint32_t)PV(P_SLEN);
            chord_open = 0;
            changed(99);
            redraw |= 8u;
        } else if (page == PG_LAYER) {          /* on LAYER: the next SOUND, OFF A B C D E */
            set.p[P_BSND] = (int16_t)((PV(P_BSND) + 1) % ((int32_t)MEL_NSETS + 1));
            changed(0);
        } else
            go_group(G_LAYER);
    }
    if (seq_rec && !seq_play && fm1_in.buttons & (1u << BTN_SEL) && !sel_long && fm1_ms - t_sel > 1000u) {
        uint32_t k;
        for (k = 0; k < 64u; k++)
            set.steps[k / 4u][k % 4u] = 0;
        seq_cur = 0;
        sel_long = 1;
        changed(99);
        say("PATTERN CLEARED");
        redraw |= 8u;
    }
    if (b & (1u << BTN_ENV)) { t_env = fm1_ms; env_long = 0; }   /* ENV: HOLD when let go; held, HALF SPEED on / off */
    if (fm1_in.buttons & (1u << BTN_ENV) && !env_long && fm1_ms - t_env > 600u) {
        env_long = 1;
        set.half = !set.half;
        changed(99);
        say(set.half ? "HALF SPEED" : "FULL SPEED");
        redraw |= 3u;
    }
    if (rel & (1u << BTN_ENV) && !env_long) {
        hold = !hold;
        if (!hold) in_clear(S_LATCH);
        say(hold ? "HOLD ON" : "HOLD OFF");
    }
    if (b & (1u << BTN_SAVE)) {
        save_req = 1;
        say(quiet() ? "SAVED" : "SAVES WHEN SILENT");
    }
}

static void knobs(void)
{
    uint32_t k, g = PG_GROUP[page];
    int32_t d;
    if ((d = turn(ENC_PRESETS)) != 0) {         /* the sound, on every page: A .. E, and there it stops */
        int32_t v = PV(P_SOUND) + d, hi = (int32_t)MEL_NSETS - 1;
        v = v < 0 ? 0 : v > hi ? hi : v;
        if (v != PV(P_SOUND)) to_sound(v);
    }
    if ((d = turn(ENC_SELECT)) != 0 && G_COUNT[g] > 1u) {   /* SELECT: the section's pages */
        int32_t n = G_COUNT[g];
        page = G_FIRST[g] + (uint32_t)((((int32_t)(page - G_FIRST[g]) + d) % n + n) % n);
        g_last[g] = (uint8_t)page;
        touched = 99;
        redraw |= 0xFu;
    }
    if ((d = turn(ENC_ALGORITHM)) != 0 && set_param(P_BPM, PV(P_BPM) + d)) {   /* ALGORITHM: the tempo, anywhere */
        changed(99);
        say_ctl("TEMPO", PG_SETUP, 0);
        if (page == PG_SETUP) redraw |= 8u;
    }
    set.p[P_STEP] = (int16_t)seq_cur;
    for (k = 0; k < 4u; k++) {                  /* KNOB 1..4: this page's */
        const ctl_t *c = &CTL[page][k];
        if ((d = turn(ENC_KNOB1 + k)) != 0 && c->pid == P_PMODE && c->name) {   /* SOUNDS: OWN keeps, then takes */
            play_keep();
            if (set_param(P_PMODE, PV(P_PMODE) + d)) {
                play_take();
                changed(k);
            }
            continue;
        }
        if (d && c->name && set_param(c->pid, PV(c->pid) + d * PINFO[c->pid][3])) {
            if (c->pid == P_STEP) { seq_cur = (uint32_t)PV(P_STEP); chord_open = 0; touched = k; redraw |= 10u; continue; }
            if (c->pid == P_SLEN) {
                if (seq_cur >= (uint32_t)PV(P_SLEN)) seq_cur = 0;
                if (seq_step >= (uint32_t)PV(P_SLEN)) seq_step = 0;
            }
            if (c->pid == P_LEN) redraw |= 4u;
            changed(k);
        }
    }
}

static void app_loop(void)
{
    static uint32_t t_tapes, t_stage, t_head, head_lv;
    uint32_t keys, down, up, k, base, lit = 0, sum = 0;
    uint8_t st, d1, d2;

    base = (uint32_t)((int32_t)KEY0 + 12 * PV(P_OCT) + PV(P_PITCH));
    keys = fm1_in.notes;                        /* bit k: key k (0 = F3 .. 26 = G5) */
    down = keys & ~prev_keys;
    up = prev_keys & ~keys;
    if (down && hold && !(prev_keys & ~up))     /* HOLD: the first key of a new chord lets the last one go */
        in_clear(S_LATCH);
    for (k = 0; k < 27u; k++) {                 /* (a key stops the note it started: octave and pitch may change meanwhile) */
        if (down >> k & 1u) {
            held_note[k] = (uint8_t)(base + k);
            in_set(held_note[k], S_KEY | (hold ? S_LATCH : 0u), 0);
            rec_note(held_note[k]);
            midi_send(0x90, held_note[k], 100);
        }
        if (up >> k & 1u && held_note[k] < 128u) {
            in_set(held_note[k], pedal ? S_PED : 0u, S_KEY);
            midi_send(0x80, held_note[k], 0);
            held_note[k] = 255;
        }
    }
    prev_keys = keys;

    buttons();
    knobs();

    while (midi_get(&st, &d1, &d2)) {           /* USB-MIDI in, any channel; PITCH moves it as it moves the keys */
        uint32_t ty = st & 0xF0u;
        d1 &= 127u;
        if (ty == 0x90u && d2) {
            int32_t nt = (int32_t)d1 + PV(P_PITCH);
            midi_note[d1] = (uint8_t)(nt < 0 ? 0 : nt > 127 ? 127 : nt);
            in_set(midi_note[d1], S_MIDI | (hold ? S_LATCH : 0u), 0);
            rec_note(midi_note[d1]);
        } else if (ty == 0x80u || ty == 0x90u)
            in_set(midi_note[d1], pedal && in_get(midi_note[d1]) ? S_PED : 0u, S_MIDI);
        else if (ty == 0xB0u && d1 == 64u) {     /* the pedal holds what sounds, and what is let go while it is down */
            pedal = d2 >= 64u;
            if (pedal) {
                for (k = 0; k < 128u; k++)
                    if (in_get(k)) in_set(k, S_PED, 0);
            } else
                in_clear(S_PED);
        } else if (ty == 0xB0u && d1 == 1u)
            p_mod = d2;
        else if (ty == 0xB0u && (d1 == 120u || d1 == 123u))
            in_clear(S_MIDI | S_PED | S_LATCH);
        else if (ty == 0xE0u)
            p_bend16 = (((int32_t)d2 << 7 | d1) - 8192) * 25 / 64;   /* +-2 semitones, 1/16 cents */
    }
    if (!keys) {                                /* step recording: every key up ends the chord */
        for (k = 0; k < 128u && !(in_get(k) & (S_KEY | S_MIDI)); k++)
            ;
        if (k == 128u) rec_all_up();
    }
    clock_run();

    /* what sounds, on the keys of this octave */
    base = (uint32_t)((int32_t)KEY0 + 12 * PV(P_OCT) + PV(P_PITCH));
    for (k = 0; k < NV; k++) {
        if (voice[k].busy && voice[k].amp && voice[k].note >= base && voice[k].note < base + 27u)
            lit |= 1u << (voice[k].note - base);
        if (voice[k].busy) sum += (uint32_t)voice[k].amp >> 16;   /* 256 a voice */
    }
    for (k = 0; k < 27u; k++)
        fm1_led_key(KEY_LED(k), lit >> k & 1u);
    fm1_led_key(BTN_OCT_DOWN, PV(P_OCT) < 0);
    fm1_led_key(BTN_OCT_UP, PV(P_OCT) > 0);
    fm1_led_key(BTN_PLAY, seq_play && (seq_tick < 12u));   /* PLAY blinks in time */
    fm1_led_key(BTN_REC, seq_rec);
    fm1_led_key(BTN_ARP, arp_on);
    fm1_led_key(BTN_ENV, hold);
    fm1_led_key(BTN_HOME, PG_GROUP[page] == G_PLAY);       /* the section you are in */
    fm1_led_key(BTN_LFO, PG_GROUP[page] == G_LOFI ? (fm1_ms >> 8) & 1u : !by_lofi);   /* lit: in, dark: out, */
    fm1_led_key(BTN_FX, PG_GROUP[page] == G_FX ? (fm1_ms >> 8) & 1u : !by_fx);           /* blinking: its section */
    fm1_led_key(BTN_EDIT, PG_GROUP[page] == G_EQ);
    fm1_led_key(BTN_GLO, PG_GROUP[page] == G_SETUP);
    fm1_led_key(BTN_SEQ, PG_GROUP[page] == G_SEQ);
    fm1_led_key(BTN_SAVE, dirty);
    fm1_led_key(BTN_SEL, PV(P_BSND) != 0 || page == PG_LAYER);   /* lit: a layer sounds (or its page is up) */

    ui_lv += ((int32_t)(sum > 256u ? 256u : sum) - ui_lv) / 4;
    if (!sum && ui_lv < 4) ui_lv = 0;           /* (the division alone would leave it at 1..3: never still) */
    if (touched < 99u && fm1_ms - t_change > 1500u) {   /* the knob's light goes out */
        touched = 99;
        redraw |= 8u;
    }
    if (t_msg && fm1_ms - t_msg > 1400u) {
        t_msg = 0;
        redraw |= 1u;
    }
    if ((redraw & 1u) || (ui_lv && fm1_ms - t_head >= 100u) || (!ui_lv && head_lv)) {
        draw_header(ui_lv > 0);                 /* (the reel turns while something sounds) */
        t_head = fm1_ms;
        head_lv = (uint32_t)ui_lv;
    }
    ui_motion();
    if ((redraw & 2u) || (stage_moves() && fm1_ms - t_stage >= 50u)) {
        draw_stage();                           /* (~20 fps while something moves) */
        t_stage = fm1_ms;
    }
    if (redraw & 8u) draw_plate();
    if ((redraw & 4u) || fm1_ms - t_tapes >= 40u) {
        draw_tapes(base, (redraw & 4u) != 0);
        t_tapes = fm1_ms;
    }
    redraw = 0;

    if (((dirty && fm1_ms - t_change > 2000u) || save_req) && quiet()) {
        play_keep();
        settings_save(&set, sizeof set);        /* only when silent: an erase stalls the sound */
        if (save_req) say("SAVED");
        dirty = 0;
        save_req = 0;
    }
}

#ifndef DINGHY_WEB
static void app_main(void)
{
    app_init();
    for (;;) {
        platform_poll();
        app_loop();
    }
}
#endif
