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
 *   EQ                   a low shelf (150 Hz), a bell (200 Hz .. 5 kHz), a high shelf (5 kHz), +-12 dB
 *   VIBRATO      (FX)    a delay swept by a sine: RATE, DEPTH, TONE (its colour), MIX (half way: a chorus)
 *   LESLIE               the drum below 800 Hz, the horn above, Doppler and level, STOP / SLOW / FAST with their
 *                        inertia, DRIVE (its amplifier), SPREAD (the microphones), MIX
 *   ECHO                 a tape echo: TIME 60 ms .. 1 s, REPEATS darker and dirtier each time, TONE, MIX
 *   REVERB               four combs and two allpasses a side: SIZE, TONE, PRE-delay, MIX
 *   and last a soft limiter, so nothing ever clips hard.
 * Two bypasses, each on its own (a long press of the section's button): LFO takes the tape (LOFI) out, leaving the
 * M400's TONE, a steady motor and keys in tune; FX takes the pedals out, the echo and the reverb ringing out
 * (trails). Each fades in ~6 ms. The EQ stays.
 * Integers only. The main loop sets the f_ values (apply()); the screen reads the fxv_ values. */

static volatile int32_t f_wow, f_flut, f_sat, f_hiss, f_age, f_drop, f_crush, f_tone;
static volatile int32_t f_vinc, f_vdep, f_vtone, f_vmix;
static volatile int32_t f_lspeed, f_ldrive, f_lspread, f_lmix;
static volatile int32_t f_etime, f_efb, f_etone, f_emix;
static volatile int32_t f_rfb, f_rdamp, f_rpre, f_rmix;
static volatile int32_t f_glo, f_gmid, f_ghi, f_svf, f_by_lofi, f_by_fx;
static int32_t lofi_bp, fx_bp;                  /* ISR: how bypassed, 0 (all in) .. 32767 (all out), ramped */
static volatile int32_t fxv_wow, fxv_drop;      /* ISR -> screen: the wow now (1/16 cent), how deep a dropout is (0..32768) */
static volatile int32_t fxv_flut;               /* ISR -> screen: the flutter now (flut_am: +-1200 at 100) */
static int32_t flut_am;                         /* ISR: flutter's level shake now, Q15 around 0 */
static volatile uint32_t fxv_horn, fxv_drum, fxv_vib;   /* the rotors' and the vibrato's phase */

#define K_LS 692                                /* one-poles, Q15: 150 Hz (low shelf) */
#define K_HS 16674                              /* 5 kHz (high shelf) */
#define K_LES 3530                              /* 800 Hz (the Leslie's crossover) */
#define K_RHP 2119                              /* 450 Hz, below which the reverb gets less (no mud) */
#define VIB_N 512u
#define ECHO_N 44100u                           /* 1 s */
#define PRE_N 5376u                             /* 122 ms */
#define LES_N 64u
static int32_t vib_buf[VIB_N], les_hb[LES_N], les_db[LES_N];
static int16_t echo_buf[ECHO_N] __attribute__((section(".pool")));
static int16_t pre_buf[PRE_N] __attribute__((section(".pool")));

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

/* ---- the chain, per frame: mix (mono, the voices) -> out (stereo) */
static void fx_run(const int32_t *mix, int32_t *out, uint32_t n, int32_t act)
{
    static int32_t lg, lg2, hg, hg2, lp1, lp2, hiss, hiss2, drop_g = 32768, held, lc, am, dc_x, dc_y, dlp, d1, d2;
    static int32_t ls, hs, sv_lp, sv_bp, vt, les_x, e_lp, e_d = 8000 << 8, r_hp;
    static uint32_t rnd2 = 0x9E3779B9u, vw, li, ew, pw, hinc = 80000u, dinc = 65000u;
    int32_t age = f_age, kg = MEL_GENLP[age] >> 1, kh = MEL_GENHP[age] >> 1, kt = f_tone >> 1;
    int32_t sv_ = f_sat + age / 4 > 100 ? 100 : f_sat + age / 4, hv = f_hiss + age * 2 / 5 > 100 ? 100 : f_hiss + age * 2 / 5;
    int32_t dg = MEL_DRV[sv_], dmk = MEL_DRVMK[sv_], dlk = MEL_DRVLP[sv_] >> 1, dbias = sv_ * 49;   /* DRIVE */
    int32_t hl = (hv * hv * 13 / 100 * (8192 + act * 3 / 4)) >> 15;
    int32_t blt = f_by_lofi ? 32767 : 0, bl = lofi_bp, bft = f_by_fx ? 32767 : 0, bf = fx_bp;
    int32_t amt = flut_am, agl = 4096 + age * 14;   /* (AGE: the level the narrowing takes, given back) */
    int32_t crush = f_crush, hold_n = 1 + crush * 7 / 100, mask = ~((1 << (crush * 9 / 100)) - 1);
    int32_t glo = f_glo - 4096, gmid = f_gmid - 4096, ghi = f_ghi - 4096, svf = f_svf;
    int32_t vmix = f_vmix, vdep = f_vdep, vtk = f_vtone >> 1, lmix = f_lmix, ldr = 256 + f_ldrive * 10;
    int32_t lmk = 32768 - f_ldrive * 150, lsp = f_lspread, emix = f_emix, efb = f_efb, etk = f_etone >> 1;
    int32_t rfb = f_rfb, rdamp = f_rdamp, rmix = f_rmix, rpre = f_rpre, e_tgt = f_etime, k;
    uint32_t i, vinc = (uint32_t)f_vinc, ht = 0, dt = 0;
    if (p_mod) {                                /* the mod wheel: vibrato, whatever the page says */
        int32_t m = p_mod * 258;
        if (vmix < m) vmix = m;
        if (vdep < p_mod / 3) vdep = p_mod / 3;
    }
    if (f_lspeed == 1) { ht = 80000u; dt = 65000u; }                       /* SLOW: 0.82 / 0.67 Hz */
    else if (f_lspeed == 2) { ht = 662000u; dt = 555000u; }                /* FAST: 6.8 / 5.7 Hz */
    hinc += (uint32_t)((int32_t)(ht - hinc) >> 7);   /* the horn gets there in ~1 s, the drum in ~4 s */
    dinc += (uint32_t)((int32_t)(dt - dinc) >> 9);
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
        if (glo) { ls += ((y - ls) * (K_LS >> 1)) >> 14; y += (ls * glo) >> 12; }
        if (ghi) { hs += ((y - hs) * (K_HS >> 1)) >> 14; y += ((y - hs) * ghi) >> 12; }
        if (gmid) {                             /* the bell: a state-variable band-pass (Q 1), added or taken away */
            int32_t sv = y >> 1, hp = sv - sv_lp - sv_bp;
            sv_bp += (hp * svf) >> 14;
            sv_lp += (sv_bp * svf) >> 14;
            y += ((sv_bp << 1) * gmid) >> 12;
        }
        y = y > 65535 ? 65535 : y < -65535 ? -65535 : y;
        /* the pedals (FX) */
        y0 = y;
        if (bf >= 32640 && bft) {               /* out: the rotors turn on (the screen shows them) */
            fxv_horn += hinc;
            fxv_drum += dinc;
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
        /* LESLIE */
        if (lmix) {
            int32_t lo, hi, dh, dd, sh, sd, ph, pd, ah, ad, yd = (sat((y * ldr) >> 8) * lmk) >> 15;
            les_x += ((yd - les_x) * (K_LES >> 1)) >> 14;
            lo = les_x;
            hi = yd - lo;
            li = (li + 1u) & (LES_N - 1u);
            les_hb[li] = hi;
            les_db[li] = lo;
            fxv_horn += hinc;
            fxv_drum += dinc;
            sh = sine(fxv_horn);
            sd = sine(fxv_drum);
            dh = (24 << 8) + ((sh * 14) >> 7);  /* delay, Q8 samples: 24 +- 14 (horn), 24 +- 5 (drum) */
            dd = (24 << 8) + ((sd * 5) >> 7);
            TAP(les_hb, LES_N, li, dh, ph);
            TAP(les_db, LES_N, li, dd, pd);
            ah = ((sh >> 2) * lsp) / 100;       /* the horn faces one microphone, then the other */
            ad = ((sd >> 3) * lsp) / 100;
            l = ((ph * (24576 + ah)) >> 15) + ((pd * (28672 + ad)) >> 15);
            r = ((ph * (24576 - ah)) >> 15) + ((pd * (28672 - ad)) >> 15);
            l = y + (((l - y) * lmix) >> 15);
            r = y + (((r - y) * lmix) >> 15);
        } else {
            fxv_horn += hinc;
            fxv_drum += dinc;
            l = r = y;
        }
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
            int32_t pi = (int32_t)pw - rpre;
            in = (((l + r) >> 4) * wetf) >> 15; /* (FX bypassed: nothing new goes in, it rings out) */
            r_hp += ((in - r_hp) * (K_RHP >> 1)) >> 14;
            pre_buf[pw] = (int16_t)(in - (r_hp >> 1));
            in = pre_buf[pi < 0 ? pi + (int32_t)PRE_N : pi];
            if (++pw >= PRE_N) pw = 0;
            wl = 0;
            for (k = 0; k < 4; k++) {           /* combs */
                int32_t *c = &rv_c[k][rv_ci[k]], o = *c;
                rv_f[k] += ((o - rv_f[k]) * rdamp) >> 15;
                *c = in + ((rv_f[k] * rfb) >> 15);
                if (++rv_ci[k] >= RV_CL[k]) rv_ci[k] = 0;
                wl += o;
            }
            wr = wl;
            for (k = 0; k < 4; k++) {           /* allpasses: 0, 1 left, 2, 3 right */
                int32_t *p = &rv_a[k][rv_ai[k]], o = *p, *w = k < 2 ? &wl : &wr;
                *p = *w + (o >> 1);
                *w = o - *w;
                if (++rv_ai[k] >= RV_AL[k]) rv_ai[k] = 0;
            }
            l += ((wl >> 4) * (rmix >> 2)) >> 10;
            r += ((wr >> 4) * (rmix >> 2)) >> 10;
        }
        out[2u * i] += sat(l);                  /* the limiter: untouched up to 0.6 of full scale, then round */
        out[2u * i + 1u] += sat(r);
    }
    if (!vmix) fxv_vib += vinc * n;             /* (the screen's vibrato keeps its rate) */
    lofi_bp = bl;
    fx_bp = bf;
    fxv_drop = 32768 - drop_g;
}
