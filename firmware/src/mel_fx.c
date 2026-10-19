/* SPDX-License-Identifier: GPL-3.0-only */
/* MEL's tape and effects (the audio ISR), in the order the sound goes through them. Included by mel.c.
 *
 *   the tape     (LOFI)  AGE: a copy of a copy: the band narrows, two poles each side (9 kHz .. 1.1 kHz, 20 .. 380 Hz),
 *                        and every copy brings the copying deck's hiss, flutter and drive (added to the knobs')
 *                        DROPOUT: the oxide gone in places, dips of 30..200 ms (past half way the motor drags too)
 *                        the M400's TONE; DRIVE, after the JHS 424 (a Portastudio's cassette preamp): 0 .. +36 dB into
 *                        a soft clipper, a bias for even harmonics, a low-pass closing as it drives, the level kept;
 *                        HISS, CRUSH (fewer bits, a lower rate)
 *                        WOW and FLUTTER are the motor: irregular, in the tapes' speed (fx_pitch16), not a vibrato;
 *                        flutter shakes the level a little too (that roughness is what makes it heard); at HALF SPEED
 *                        they come at half the rate (the capstan and the pinch roller turn half as fast)
 *   EQ                   a low shelf (150 Hz), a bell (200 Hz .. 5 kHz), a high shelf (2.5 kHz), +-12 dB; a shelf
 *                        is half way (in dB) at its frequency whatever its gain (MEL_EQ_KLS / KHS: at a fixed 5 kHz
 *                        HIGH was over all there is of the tapes, and -12 dB of it took 1 dB off them)
 *   VIBRATO      (FX)    a delay swept by a sine: RATE, DEPTH, TONE (its colour), MIX (half way: a chorus)
 *   ECHO                 a tape echo: TIME 60 ms .. 1 s, REPEATS darker and dirtier each time, TONE, MIX
 *   REVERB               two models in one memory (TYPE): HALL, after Felucca's, and PLATE, Dattorro's: SIZE,
 *                        TONE, TYPE, MIX
 *   and last a soft limiter, so nothing ever clips hard.
 * Two bypasses, each on its own (a long press of the section's button): LFO takes the tape (LOFI) out, leaving the
 * M400's TONE, a steady motor and keys in tune; FX takes the pedals out, the echo and the reverb ringing out
 * (trails). Each fades in ~6 ms. The EQ stays.
 * Integers only. The main loop sets the f_ values (apply()); the screen reads the fxv_ values. */

static volatile int32_t f_wow, f_flut, f_sat, f_hiss, f_age, f_drop, f_crush, f_tone;
static volatile int32_t f_vinc, f_vdep, f_vtone, f_vmix;
static volatile int32_t f_etime, f_efb, f_etone, f_emix;
static volatile int32_t f_rfb, f_rdamp, f_rtype, f_rmix;
static volatile int16_t f_hg[8];                /* HALL: its lines' gains (Q14), for SIZE (rv_hall_gains) */
static volatile int32_t f_glo, f_gmid, f_ghi, f_svf, f_by_lofi, f_by_fx, f_kls, f_khs;
static int32_t lofi_bp, fx_bp;                  /* ISR: how bypassed, 0 (all in) .. 32767 (all out), ramped */
static volatile int32_t fxv_wow, fxv_drop;      /* ISR -> screen: the wow now (1/16 cent), how deep a dropout is (0..32768) */
static volatile int32_t fxv_flut;               /* ISR -> screen: the flutter now (flut_am: +-1200 at 100) */
static int32_t flut_am;                         /* ISR: flutter's level shake now, Q15 around 0 */
static volatile uint32_t fxv_vib;               /* the vibrato's phase */

#define K_RHP 2119                              /* 450 Hz, below which the reverb gets less (no mud) */
#define RV_OUT 12                               /* the reverb's level out: all wet (MIX 100) as loud as all dry */
#define VIB_N 512u
#define ECHO_N 44100u                           /* 1 s */
static int32_t vib_buf[VIB_N];
static int16_t echo_buf[ECHO_N] __attribute__((section(".pool")));

/* ---- the motor: wow (slow, wandering) and flutter (fast, rough), the drag of a dropout; per 32 frames, 1/16 cents */
static int32_t drag, drag_t, drop_t = 32768;
static uint32_t drop_left;
static int32_t fx_pitch16(void)
{
    static uint32_t w1, w2, f1, f2;
    static int32_t walk;
    int32_t wow, fl, wd, fd, c;
    uint32_t sp = (uint32_t)(256 + motor16 * 128 / 19200);   /* the motor's speed, Q8: 256 full .. 128 half */
    w1 += (1869949u >> 8) * sp;                 /* 0.60 Hz (per 32 frames: f * 2^32 * 32 / 44100) */
    w2 += (3521737u >> 8) * sp;                 /* 1.13 Hz */
    f1 += (25555964u >> 8) * sp;                /* 8.2 Hz */
    f2 += (40203895u >> 8) * sp;                /* 12.9 Hz */
    walk += (int32_t)rnd() >> 22;
    walk -= walk >> 7;
    wow = sine(w1) + (sine(w2) >> 1) + (walk > 16000 ? 16000 : walk < -16000 ? -16000 : walk);
    fl = sine(f1) + (sine(f2) >> 1) + ((int32_t)rnd() >> 19);
    {
        int32_t fv = f_flut + f_age * 2 / 5;    /* (AGE: the copying deck's flutter too) */
        fv = fv > 100 ? 100 : fv;
        wd = f_wow * f_wow * 1280 / 10000;      /* squared: gentle low on the knob, wild at the top */
        fd = fv * (fv + 60) * 405 / 16000;      /* nearly straight: heard from the first quarter */
        flut_am = ((fl >> 6) * fv) >> 6;        /* up to +-6 % of the level at 100 */
        fxv_flut = flut_am;
    }
    c = ((wow >> 6) * wd) >> 9;
    fxv_wow = c;
    c += ((fl >> 6) * fd) >> 9;
    drag += (drag_t - drag) >> 3;
    return ((c + drag) * (32767 - lofi_bp)) >> 15;   /* (LOFI bypassed: a steady motor) */
}

/* ---- per block: dropouts now and then (DROPOUT), a dip of 30..200 ms; past half way the motor drags with it */
static void fx_block(void)
{
    int32_t d = f_drop;
    if (drop_left) {
        if (!--drop_left) { drop_t = 32768; drag_t = 0; }
    } else if (d && (rnd() >> 16) < (uint32_t)(d * d / 5)) {
        drop_left = 5u + (rnd() >> 27);
        drop_t = 32768 - (int32_t)((rnd() >> 17) * (uint32_t)(40 + d / 2) / 100u);
        if (d > 50 && (rnd() >> 30) == 0u)
            drag_t = -(int32_t)(400u + (rnd() >> 22) * (uint32_t)d / 100u);
    }
}

static int32_t drive_clip(int32_t u)            /* DRIVE's soft clipper, |u| up to 8 x full scale */
{
    uint32_t a = (uint32_t)(u < 0 ? -u : u), i, f;
    int32_t y;
    if (a >= 262143u)
        y = 32767;
    else {
        i = a >> 10;
        f = a & 1023u;
        y = (int32_t)MEL_CLIP[i] + ((((int32_t)MEL_CLIP[i + 1u] - (int32_t)MEL_CLIP[i]) * (int32_t)f) >> 10);
    }
    return u < 0 ? -y : y;
}

/* a delay line read between two samples: d in 1/256 samples behind w */
#define TAP(buf, n, w, d, out) do { \
        uint32_t ri_ = ((w) + (n) - ((uint32_t)(d) >> 8)) % (n), f_ = (uint32_t)(d) & 255u; \
        int32_t a_ = (buf)[ri_], b_ = (buf)[ri_ ? ri_ - 1u : (n) - 1u]; \
        (out) = a_ + (((b_ - a_) * (int32_t)f_) >> 8); } while (0)

/* ---- REVERB: two models (TYPE) in one memory, run at half the rate (22.05 kHz: twice the time in the same
 * memory, at half the work; the tail stops near 10 kHz, as a room's air does), int16 lines; a product that
 * feeds back is rounded towards zero (a loop of floors holds an offset and a little noise for ever).
 *   HALL   after Felucca's (firmware/src/fx.c, Leo Kuroshita, Hügelton Instruments, GPL-3.0): 2 allpass
 *          diffusers, then 8 lines (mutually prime, 21 .. 34 ms) that all feed each other through a Paley
 *          conference matrix (every line the 7 others, +-1, none itself: no flutter at a line's own length);
 *          lines 1 2 4 7 read through slow sines (the resonances drift instead of ringing), 0 3 5 6 through
 *          TONE's low-pass; every line's gain from its length (all decay alike): SIZE, 0.5 .. 9 s. Smooth, dense
 *   PLATE  Dattorro's ("Effect Design, Part 1", JAES 1997; after FoMni's, Charles Vestal, GPL-3.0): a short
 *          pre-delay, the input's bandwidth, 4 allpass diffusers, then a figure-eight tank of two halves, each a
 *          modulated allpass, a delay, TONE's low-pass, an allpass and a delay, feeding the other; 7 taps a side.
 *          SIZE its decay. The studio plate of the records the M400 was on: bright, at once dense, wide
 * Changing TYPE clears the memory: the old one's tail stops. */
#define DS(n) ((uint32_t)(((n) * 7409 + 5000) / 10000))   /* the plate's lengths (29761 Hz) at 22050 Hz */
#define EXC DS(16)                              /* its tank allpasses' swing, samples */
enum { R_PRE, R_IN1, R_IN2, R_IN3, R_IN4, R_APL, R_D1L, R_AP2L, R_D2L, R_APR, R_D1R, R_AP2R, R_D2R, R_NL };
static const uint16_t PL_LEN[R_NL] = {221, DS(142), DS(107), DS(379), DS(277), DS(672) + EXC + 2, DS(4453), DS(1800),
                                      DS(3720), DS(908) + EXC + 2, DS(4217), DS(2656), DS(3163)};
static const uint16_t HL_LEN[8] = {467, 509, 547, 593, 631, 673, 709, 743}, HL_AP[2] = {131, 307};
#define RV_MEM 16928u                           /* the plate's (the hall's is a third of it) */
static int16_t rv_mem[RV_MEM] __attribute__((section(".pool")));
typedef struct { uint16_t o, n, i; } rvl_t;     /* a line in rv_mem: where, how long, where it writes next */
static rvl_t rvl[R_NL];
static int32_t rv_lp[8], rv_bw;                 /* the low-passes (HALL: lines 0 3 5 6; PLATE: 0, 1 the tank's) */
static uint32_t rv_ph;                          /* the drift */

static inline int32_t tz(int32_t m, uint32_t sh) { return (m + ((m >> 31) & ((1 << sh) - 1))) >> sh; }
static inline int32_t rv_tap(const rvl_t *l, uint32_t k)   /* written k samples ago, 1 .. n */
{
    return rv_mem[l->o + (l->i >= k ? l->i - k : l->i + l->n - k)];
}
static inline int32_t rv_tapf(const rvl_t *l, uint32_t k8)   /* k8 / 256 samples ago, between two */
{
    int32_t a = rv_tap(l, k8 >> 8), b = rv_tap(l, (k8 >> 8) + 1u);
    return a + (((b - a) * (int32_t)(k8 & 255u)) >> 8);
}
static inline void rv_push(rvl_t *l, int32_t x)
{
    rv_mem[l->o + l->i] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
    if (++l->i >= l->n) l->i = 0;
}
static inline int32_t rv_ap(rvl_t *l, int32_t z, int32_t x, int32_t g)   /* an allpass, z its line's end, g Q15 */
{
    int32_t v = x - tz(z * g, 15);
    rv_push(l, v);
    return z + tz(v * g, 15);
}
static void rv_setup(int32_t type)              /* the lines laid out for TYPE, all silent */
{
    uint32_t k, o = 0, n = type ? R_NL : 10u;
    for (k = 0; k < n; k++) {
        rvl[k].o = (uint16_t)o;
        rvl[k].n = type ? PL_LEN[k] : k < 8u ? HL_LEN[k] : HL_AP[k - 8u];
        rvl[k].i = 0;
        o += rvl[k].n;
    }
    for (k = 0; k < RV_MEM; k++)
        rv_mem[k] = 0;
    for (k = 0; k < 8u; k++)
        rv_lp[k] = 0;
    rv_bw = 0;
}
/* 2^-x, x Q16 (0 .. 16): Q15 (Felucca's exp2_neg: 2^u by a cubic) */
static uint32_t exp2_neg(uint32_t x)
{
    uint32_t i = x >> 16, u = 32768u - ((x & 0xFFFFu) >> 1), p = 2579u;
    p = ((p * u) >> 15) + 7409u;
    p = ((p * u) >> 15) + 22780u;
    p = ((p * u) >> 15) + 32768u;
    return i < 15u ? p >> (i + 1u) : 0u;
}
static void rv_hall_gains(int32_t s)            /* (main loop) HALL's gains for SIZE s (0 .. 127): RT60 = 0.5 s x
                                                 * 2^(s / 30); a line of D samples loses 60 dB in RT60 x 22050 / D
                                                 * passes; at most 0.99, over sqrt(7) (the matrix's rows) */
{
    uint32_t q = exp2_neg((uint32_t)s * 65536u / 30u), k, g;
    for (k = 0; k < 8u; k++) {
        g = exp2_neg((HL_LEN[k] * 59u * q) >> 15);
        f_hg[k] = (int16_t)(((g > 32440u ? 32440u : g) * 6193u) >> 15);
    }
}
/* one step (half rate): x in, the left and right out */
static void rv_hall(int32_t x, int32_t kd, int32_t *ol, int32_t *or_)
{
    int32_t y[8], t, p0, p1, p2, p3, p4, p5, p6;
    uint32_t k;
    x = rv_ap(&rvl[8], rv_tap(&rvl[8], HL_AP[0]), x, 22938);   /* the diffusers: 0.7, 0.625 */
    x = rv_ap(&rvl[9], rv_tap(&rvl[9], HL_AP[1]), x, 20480);
    rv_ph += 72944u;                            /* (0.37 Hz, 0.74 on the odd lines) */
    for (k = 0; k < 8u; k++) {
        int32_t e, g = f_hg[k];
        if ((0x69u >> k) & 1u) {                /* 0 3 5 6: TONE's low-pass at the line's end */
            e = rv_tap(&rvl[k], HL_LEN[k]);
            rv_lp[k] += tz((e - rv_lp[k]) * kd, 15);
            e = rv_lp[k];
        } else                                  /* 1 2 4 7: read 2 .. 18 samples short of its end, drifting */
            e = rv_tapf(&rvl[k], ((HL_LEN[k] - 10u) << 8) +
                                 (uint32_t)((sine(rv_ph * (1u + (k & 1u)) + k * 0x2C000000u) * 8) >> 7));
        y[k] = e > 15 || e < -15 ? (e * g + 8192) >> 14 : tz(e * g, 14);   /* (loud: to the nearest, the tail
                                                 * decays as it should; quiet: towards zero, it ends in silence) */
    }
    *ol = y[0] + y[3] + y[4] - y[7];
    *or_ = y[1] + y[2] + y[5] + y[6];
    /* the matrix: C = [0 1; -1 Q], Q (7 x 7) the quadratic character mod 7 (+1 at distances 1 2 4, -1 at 3 5 6) */
    t = y[1] + y[2] + y[3] + y[4] + y[5] + y[6] + y[7];
    p0 = y[2] + y[3] + y[5], p1 = y[3] + y[4] + y[6], p2 = y[4] + y[5] + y[7], p3 = y[5] + y[6] + y[1];
    p4 = y[6] + y[7] + y[2], p5 = y[7] + y[1] + y[3], p6 = y[1] + y[2] + y[4];
    y[0] += t;
    rv_push(&rvl[0], t + x);                    /* (+ the input: signs spread over the rows) */
    rv_push(&rvl[1], 2 * p0 + y[1] - y[0] + x);
    rv_push(&rvl[2], 2 * p1 + y[2] - y[0] + x);
    rv_push(&rvl[3], 2 * p2 + y[3] - y[0] + x);
    rv_push(&rvl[4], 2 * p3 + y[4] - y[0] + x);
    rv_push(&rvl[5], 2 * p4 + y[5] - y[0] + x);
    rv_push(&rvl[6], 2 * p5 + y[6] - y[0] + x);
    rv_push(&rvl[7], 2 * p6 + y[7] - y[0] - x);
}
static void rv_plate(int32_t x, int32_t kd, int32_t dec, int32_t *ol, int32_t *or_)
{
    int32_t a, b, dd2 = dec + 4915 > 16384 ? 16384 : dec + 4915 < 8192 ? 8192 : dec + 4915;   /* 0.25 .. 0.5 */
    int32_t fbl = rv_tap(&rvl[R_D2R], DS(3163)), fbr = rv_tap(&rvl[R_D2L], DS(3720)), ms, mc;
    rv_push(&rvl[R_PRE], x);
    x = rv_tap(&rvl[R_PRE], 221u);
    rv_bw += tz((x - rv_bw) * 29818, 15);       /* the input's bandwidth */
    x = rv_ap(&rvl[R_IN1], rv_tap(&rvl[R_IN1], DS(142)), rv_bw, 24576);   /* the diffusers: .75 .75 .625 .625 */
    x = rv_ap(&rvl[R_IN2], rv_tap(&rvl[R_IN2], DS(107)), x, 24576);
    x = rv_ap(&rvl[R_IN3], rv_tap(&rvl[R_IN3], DS(379)), x, 20480);
    x = rv_ap(&rvl[R_IN4], rv_tap(&rvl[R_IN4], DS(277)), x, 20480);
    rv_ph += 175304u;                           /* (the tank's swing: 0.9 Hz, sine and cosine for its halves) */
    ms = (sine(rv_ph) * (int32_t)EXC) >> 7;     /* (1/256 samples) */
    mc = (sine(rv_ph + 0x40000000u) * (int32_t)EXC) >> 7;
    a = rv_ap(&rvl[R_APL], rv_tapf(&rvl[R_APL], (DS(672) << 8) + (uint32_t)ms), x + tz(fbl * dec, 15), -22938);
    rv_push(&rvl[R_D1L], a);
    b = rv_tap(&rvl[R_D1L], DS(4453));
    rv_lp[0] += tz((b - rv_lp[0]) * kd, 15);
    b = rv_ap(&rvl[R_AP2L], rv_tap(&rvl[R_AP2L], DS(1800)), tz(rv_lp[0] * dec, 15), dd2);
    rv_push(&rvl[R_D2L], b);
    a = rv_ap(&rvl[R_APR], rv_tapf(&rvl[R_APR], (DS(908) << 8) + (uint32_t)mc), x + tz(fbr * dec, 15), -22938);
    rv_push(&rvl[R_D1R], a);
    b = rv_tap(&rvl[R_D1R], DS(4217));
    rv_lp[1] += tz((b - rv_lp[1]) * kd, 15);
    b = rv_ap(&rvl[R_AP2R], rv_tap(&rvl[R_AP2R], DS(2656)), tz(rv_lp[1] * dec, 15), dd2);
    rv_push(&rvl[R_D2R], b);
    *ol = (rv_tap(&rvl[R_D1R], DS(266)) + rv_tap(&rvl[R_D1R], DS(2974)) - rv_tap(&rvl[R_AP2R], DS(1913)) +
          rv_tap(&rvl[R_D2R], DS(1996)) - rv_tap(&rvl[R_D1L], DS(1990)) - rv_tap(&rvl[R_AP2L], DS(187)) -
          rv_tap(&rvl[R_D2L], DS(1066))) * 13 >> 5;   /* (13 / 32: as loud as the hall) */
    *or_ = (rv_tap(&rvl[R_D1L], DS(353)) + rv_tap(&rvl[R_D1L], DS(3627)) - rv_tap(&rvl[R_AP2L], DS(1228)) +
           rv_tap(&rvl[R_D2L], DS(2673)) - rv_tap(&rvl[R_D1R], DS(2111)) - rv_tap(&rvl[R_AP2R], DS(335)) -
           rv_tap(&rvl[R_D2R], DS(121))) * 13 >> 5;
}

/* ---- the chain, per frame: mix (mono, the voices) -> out (stereo) */
static void fx_run(const int32_t *mix, int32_t *out, uint32_t n, int32_t act)
{
    static int32_t lg, lg2, hg, hg2, lp1, lp2, hiss, hiss2, drop_g = 32768, held, lc, am, dc_x, dc_y, dlp, d1, d2;
    static int32_t ls, hs, sv_lp, sv_bp, vt, e_lp, e_d = 8000 << 8, r_hp;
    static uint32_t rnd2 = 0x9E3779B9u, vw, ew, rv_odd;
    static int32_t rv_x, rv_ol, rv_or, rv_type = -1;
    int32_t age = f_age, kg = MEL_GENLP[age] >> 1, kh = MEL_GENHP[age] >> 1, kt = f_tone >> 1;
    int32_t sv_ = f_sat + age / 4 > 100 ? 100 : f_sat + age / 4, hv = f_hiss + age * 2 / 5 > 100 ? 100 : f_hiss + age * 2 / 5;
    int32_t dg = MEL_DRV[sv_], dmk = MEL_DRVMK[sv_], dlk = MEL_DRVLP[sv_] >> 1, dbias = sv_ * 49;   /* DRIVE */
    int32_t hl = (hv * hv * 13 / 100 * (8192 + act * 3 / 4)) >> 15;
    int32_t blt = f_by_lofi ? 32767 : 0, bl = lofi_bp, bft = f_by_fx ? 32767 : 0, bf = fx_bp;
    int32_t amt = flut_am, agl = 4096 + age * 14;   /* (AGE: the level the narrowing takes, given back) */
    int32_t crush = f_crush, hold_n = 1 + crush * 7 / 100, mask = ~((1 << (crush * 9 / 100)) - 1);
    int32_t glo = f_glo - 4096, gmid = f_gmid - 4096, ghi = f_ghi - 4096, svf = f_svf, kls = f_kls, khs = f_khs;
    int32_t vmix = f_vmix, vdep = f_vdep, vtk = f_vtone >> 1, emix = f_emix, efb = f_efb, etk = f_etone >> 1;
    int32_t rfb = f_rfb, rdamp = f_rdamp, rmix = f_rmix, rtype = f_rtype, e_tgt = f_etime, k;
    uint32_t i, vinc = (uint32_t)f_vinc;
    if (p_mod) {                                /* the mod wheel: vibrato, whatever the page says */
        int32_t m = p_mod * 258;
        if (vmix < m) vmix = m;
        if (vdep < p_mod / 3) vdep = p_mod / 3;
    }
    for (i = 0; i < n; i++) {
        int32_t x = mix[i], y, y0, l, r, in, wl, wr, dry = 0, wetl = 32767 - bl, wetf = 32767 - bf;
        x = x > 65535 ? 65535 : x < -65535 ? -65535 : x;
        bl += (blt - bl) >> 8;                  /* the bypasses, ramped (~6 ms) */
        bf += (bft - bf) >> 8;
        /* the tape (LOFI) */
        if (bl) {                               /* its dry: the M400's TONE alone */
            d1 += ((x - d1) * kt) >> 14;
            d2 += ((d1 - d2) * kt) >> 14;
            dry = d2;
        }
        if (bl >= 32640 && blt)
            y = dry;
        else {
            lg += ((x - lg) * kg) >> 14;        /* AGE: two poles down from the top, two up from the bottom */
            lg2 += ((lg - lg2) * kg) >> 14;
            hg += ((lg2 - hg) * kh) >> 14;
            hg2 += ((lg2 - hg - hg2) * kh) >> 14;
            y = ((lg2 - hg - hg2) * agl) >> 12;
            am += (amt - am) >> 6;              /* FLUTTER's roughness */
            y += (y * am) >> 15;
            drop_g += (drop_t - drop_g) >> 7;   /* DROPOUT */
            y = (y * (drop_g >> 1)) >> 14;
            lp1 += ((y - lp1) * kt) >> 14;      /* TONE: two poles */
            lp2 += ((lp1 - lp2) * kt) >> 14;
            {                                   /* DRIVE: gain, bias, the clipper, the DC out, warm, the level back */
                int32_t u = (lp2 * dg) >> 8;
                u = u > 4000000 ? 4000000 : u < -4000000 ? -4000000 : u;
                y = drive_clip(u + dbias);
                dc_y = y - dc_x + ((dc_y * 32735) >> 15);
                dc_x = y;
                dlp += ((dc_y - dlp) * dlk) >> 14;
                y = (dlp * dmk) >> 15;
            }
            rnd2 ^= rnd2 << 13;
            rnd2 ^= rnd2 >> 17;
            rnd2 ^= rnd2 << 5;
            hiss += (((int32_t)rnd2 >> 17) - hiss) >> 1;   /* HISS: soft, not fizzy */
            hiss2 += (hiss - hiss2) >> 2;
            y += (hiss2 * hl) >> 14;
            if (crush) {                        /* CRUSH */
                if (++lc >= hold_n) { lc = 0; held = y & mask; }
                y = held;
            }
            if (bl)                             /* on its way in or out: the dry and the tape, crossfaded */
                y = dry + (((y - dry) * (wetl >> 1)) >> 14);
        }
        /* EQ (always) */
        if (glo) { ls += ((y - ls) * (kls >> 1)) >> 14; y += (ls * glo) >> 12; }
        if (ghi) { hs += ((y - hs) * (khs >> 1)) >> 14; y += ((y - hs) * ghi) >> 12; }
        if (gmid) {                             /* the bell: a state-variable band-pass (Q 1), added or taken away */
            int32_t sv = y >> 1, hp = sv - sv_lp - sv_bp;
            sv_bp += (hp * svf) >> 14;
            sv_lp += (sv_bp * svf) >> 14;
            y += ((sv_bp << 1) * gmid) >> 12;
        }
        y = y > 65535 ? 65535 : y < -65535 ? -65535 : y;
        /* the pedals (FX) */
        y0 = y;
        if (bf >= 32640 && bft) {               /* out */
            l = r = y;
            goto trails;
        }
        /* VIBRATO */
        if (vmix) {
            int32_t d, wet;
            vib_buf[vw] = y;
            fxv_vib += vinc;
            d = (220 << 8) + (sine(fxv_vib) >> 5) * vdep / 4;   /* 5 ms +- vdep samples (0..88: 2 ms) */
            TAP(vib_buf, VIB_N, vw, d, wet);
            vw = (vw + 1u) & (VIB_N - 1u);
            vt += ((wet - vt) * vtk) >> 14;
            y += ((vt - y) * vmix) >> 15;
        }
        l = r = y;
        if (bf) {                               /* on its way in or out: the dry and the pedals, crossfaded */
            l = y0 + (((l - y0) * (wetf >> 1)) >> 14);
            r = y0 + (((r - y0) * (wetf >> 1)) >> 14);
        }
trails:
        /* ECHO */
        if (emix) {
            int32_t d, rd;
            e_d += (e_tgt - e_d) >> 13;         /* a new time slides there, pitching like tape */
            d = e_d + ((fxv_wow * (e_d >> 8)) >> 11);
            d = d < 256 ? 256 : d > (int32_t)(ECHO_N - 2u) << 8 ? (int32_t)(ECHO_N - 2u) << 8 : d;
            TAP(echo_buf, ECHO_N, ew, d, rd);
            e_lp += ((rd - e_lp) * etk) >> 14;  /* each repeat darker */
            echo_buf[ew] = (int16_t)sat((((l + r) >> 2) * (wetf >> 1) >> 13) + ((e_lp * efb) >> 15));   /* and dirtier;
                                                                       * (FX bypassed: nothing new goes in, the repeats go on) */
            if (++ew >= ECHO_N) ew = 0;
            l += (e_lp * emix) >> 15;
            r += (e_lp * emix) >> 15;
        }
        /* REVERB */
        if (rmix) {
            if (rtype != rv_type) { rv_setup(rtype); rv_type = rtype; }
            in = (((l + r) >> 4) * wetf) >> 15; /* (FX bypassed: nothing new goes in, it rings out) */
            r_hp += ((in - r_hp) * (K_RHP >> 1)) >> 14;
            in = (in - (r_hp >> 1)) >> 1;
            if ((rv_odd ^= 1u) != 0u) {         /* (every other sample: the two in, one step, the half way out) */
                if (rtype) rv_plate((rv_x + in) >> 1, rdamp, rfb, &wl, &wr);
                else rv_hall((rv_x + in) >> 1, rdamp, &wl, &wr);
                y = wl, wl = (rv_ol + wl) >> 1, rv_ol = y;
                y = wr, wr = (rv_or + wr) >> 1, rv_or = y;
            } else {
                rv_x = in;
                wl = rv_ol;
                wr = rv_or;
            }
            k = 32767 - ((rmix * (wetf >> 1)) >> 14);   /* MIX: the dry less as it goes all wet (MIX 100: no dry;
                                                 * FX bypassed: all of it, the tail ringing out over it) */
            l = (((l >> 1) * (k >> 1)) >> 13) + ((wl * (rmix >> 4)) >> 11) * RV_OUT;
            r = (((r >> 1) * (k >> 1)) >> 13) + ((wr * (rmix >> 4)) >> 11) * RV_OUT;
        }
        out[2u * i] += sat(l);                  /* the limiter: untouched up to 0.6 of full scale, then round */
        out[2u * i + 1u] += sat(r);
    }
    if (!vmix) fxv_vib += vinc * n;             /* (the screen's vibrato keeps its rate) */
    lofi_bp = bl;
    fx_bp = bf;
    fxv_drop = 32768 - drop_g;
}
