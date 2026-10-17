/* SPDX-License-Identifier: GPL-3.0-only
 * SMK-37 port: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* MEL on the M-Vave SMK-37 (BOARD_SMK37; SMK37.md). The FM-1's panel keeps its jobs (the middle 27 keys, the 14 buttons
 * by printed name, the knobs: platform.c); this file adds what the SMK has more. Included by mel.c; main loop only.
 *
 *   keys        all 37, C3..C6 at OCTAVE 0 (keybed 5..31 are the FM-1's F3..G5, so the FM-1's keys stay where they
 *               are; C3-E3 and G#5-C6 go on beyond them). The M400 has 35 keys, G2..F5: with PITCH -5 the keyboard is
 *               G2..G5, its 35 notes and two more past its top (MEL pitches every note from its nearest tape).
 *               Velocity sets the tape's level (the head pressed harder on it): a soft key ~1/3, a hard one full;
 *               USB-MIDI in velocity too. ARP and SEQ play at the velocity of the last note struck
 *   pads        top row: 1-4 the SOUND (A FLUTE, B CHOIR, C CELLO, D VIBES), 5-8 the LAYER's sound (the lit one
 *               again: the layer off). Bottom row, a pad a switch: 9 LOFI in / out, 10 VIBRATO, 11 LESLIE, 12 LESLIE
 *               FAST / SLOW, 13 ECHO, 14 REVERB (an effect on / off: its MIX, remembered), 15 HALF SPEED, 16 LENGTH
 *               8 SEC / HELD. Lights: off dark orange, on light blue, active (the sound or effect sounding) bright
 *   faders      two banks, FADER BANK switches (lit on B). A: MIX (sound / layer), TONE, WOW, FLUTTER. B: AGE, DRIVE,
 *               ECHO MIX, REVERB MIX. A fader acts once it moves (also after a bank change), so nothing jumps; the
 *               header says the value, the plate lights the knob when its page is up
 *   wheels      pitch = the motor (tape speed, +-2 semitones, as a MIDI bend); mod = vibrato (as CC 1)
 *   < >         the pages, all of them in a row (PLAY .. SEQ)
 *   KNOB BANK   held: the panel check (flash part, calibration, ADC, faders, wheels, buttons, keys, pads)
 */
#include "smk_wheel.h"

static const uint8_t SMK_OFF[3] = {110, 38, 0}, SMK_ON[3] = {40, 120, 210}, SMK_ACT[3] = {190, 230, 255};
enum { SP_LOFI = 8, SP_VIB, SP_LES, SP_LFAST, SP_ECHO, SP_VERB, SP_HALF, SP_LEN };
static const uint8_t SMK_FXP[4] = {P_VMIX, P_LMIX, P_EMIX, P_RMIX};   /* pads 10, 11, 13, 14: these MIXes on / off */
static const uint8_t SMK_FXMIX0[4] = {100, 100, 30, 30};             /* (their MIX when switched on, until one is set) */
static const char *const SMK_FXNAME[4] = {"VIBRATO", "LESLIE", "ECHO", "REVERB"};
static const uint8_t SMK_FP[2][4] = {{P_BMIX, P_TONE, P_WOW, P_FLUT}, {P_AGE, P_SAT, P_EMIX, P_RMIX}};

static uint32_t smk_bank;                       /* FADER BANK: 0 = A (the tape), 1 = B (wear and space) */
static uint8_t smk_fad_last[4] = {0xFF, 0xFF, 0xFF, 0xFF};
static uint8_t smk_fxmem[4];                    /* each effect's MIX when it was switched off */
static uint8_t smk_ext_note[FM1_NKEY];          /* the note an outer key started (+1 = none: 0) */
static uint32_t smk_pad_t[16];                  /* when each pad last switched (a press counts once) */

static void smk_init(void)
{
    uint32_t i;
    smk_pad_events = 1;                         /* the pads are MEL's own (events, no notes) */
    for (i = 0; i < 128u; i++)
        smk_note_vel[i] = 100;
}

static int smk_fx_idx(uint32_t pid)
{
    uint32_t i;
    for (i = 0; i < 4u; i++)
        if (SMK_FXP[i] == pid) return (int)i;
    return -1;
}

/* a parameter set from the panel (a fader, a pad): to the sound; the plate lights its knob when its page is up */
static void smk_param(uint32_t pid, int32_t v, int show)
{
    uint32_t k, pg, kk = 99;
    char s[32], *p;
    if (!set_param(pid, v)) return;
    for (k = 0; k < 4u; k++)
        if (CTL[page][k].pid == pid) kk = k;
    changed(kk);
    redraw |= 2u;
    if (pid == P_LEN) redraw |= 4u;
    if (!show) return;
    for (pg = 0; pg < NPG; pg++)               /* the header: its name and value, as the plate shows them */
        for (k = 0; k < 4u; k++)
            if (CTL[pg][k].pid == pid) {
                p = put(s, pid == P_EMIX ? "ECHO" : pid == P_RMIX ? "REVERB" : CTL[pg][k].name);
                *p++ = ' ';
                fmt_val(&CTL[pg][k], PV(pid), p);
                say(s);
                return;
            }
}

/* ---- the outer keys (C3-E3, G#5-C6): the keys' path, with velocity */
static void smk_outer(uint32_t base)
{
    int32_t e;
    while ((e = smk_extkey_take()) >= 0) {
        uint32_t kk = ((uint32_t)e >> 8) % FM1_NKEY, vel = (uint32_t)e & 0x7Fu, n;
        if (vel) {
            int32_t nt = (int32_t)base + (int32_t)kk - (int32_t)SMK_KEY_F3, k;
            if (smk_ext_note[kk] || nt < 0 || nt > 127) continue;
            n = (uint32_t)nt;
            if (hold && !prev_keys) {           /* HOLD: the first key of a new chord lets the last one go */
                for (k = 0; k < (int32_t)FM1_NKEY && !smk_ext_note[k]; k++) ;
                if (k == (int32_t)FM1_NKEY) in_clear(S_LATCH);
            }
            smk_ext_note[kk] = (uint8_t)(n + 1u);
            smk_note_vel[n] = smk_vel_last = (uint8_t)vel;
            in_set(n, S_KEY | (hold ? S_LATCH : 0u), 0);
            rec_note(n);
            midi_send(0x90, n, vel);
        } else if (smk_ext_note[kk]) {
            n = smk_ext_note[kk] - 1u;
            smk_ext_note[kk] = 0;
            in_set(n, pedal ? S_PED : 0u, S_KEY);
            midi_send(0x80, n, 0);
        }
    }
}

/* ---- the pads: the sounds on top, a switch each below */
static void smk_pads(void)
{
    int32_t e;
    while ((e = smk_pad_take()) >= 0) {
        uint32_t p = ((uint32_t)e >> 8) & 15u, vel = (uint32_t)e & 0xFFu;
        if (!vel || fm1_ms - smk_pad_t[p] < 180u) continue;   /* (a press counts once; let go does nothing) */
        smk_pad_t[p] = fm1_ms;
        if (p < 4u) {                           /* SOUND A..D */
            if ((uint32_t)PV(P_SOUND) != p) next_sound((int32_t)p - PV(P_SOUND));
            else next_sound(0);
        } else if (p < 8u) {                    /* the LAYER's sound A..D; the one it is: off */
            uint32_t ly = (uint32_t)PV(P_BSND) == p - 3u ? 0u : p - 3u;
            char s[24];
            smk_param(P_BSND, (int32_t)ly, 0);
            put(put(s, "LAYER "), ly ? MEL_SET_NAME[ly - 1u] : "OFF");
            say(s);
        } else if (p == SP_LOFI) {
            by_lofi = !by_lofi;
            apply();
            say(by_lofi ? "LOFI OUT" : "LOFI IN");
            redraw |= 3u;
        } else if (p == SP_LFAST) {
            smk_param(P_LSPEED, PV(P_LSPEED) == 2 ? 1 : 2, 0);
            say(PV(P_LSPEED) == 2 ? "LESLIE FAST" : "LESLIE SLOW");
        } else if (p == SP_HALF) {
            set.half = !set.half;
            changed(99);
            say(set.half ? "HALF SPEED" : "FULL SPEED");
            redraw |= 3u;
        } else if (p == SP_LEN) {
            smk_param(P_LEN, !PV(P_LEN), 0);
            say(PV(P_LEN) ? "LENGTH 8 SEC" : "LENGTH HELD");
        } else {                                /* VIBRATO, LESLIE, ECHO, REVERB: the MIX on / off */
            int i = p == SP_VIB ? 0 : p == SP_LES ? 1 : p == SP_ECHO ? 2 : 3;
            uint32_t pid = SMK_FXP[i];
            char s[24];
            if (PV(pid)) {
                smk_fxmem[i] = (uint8_t)PV(pid);
                smk_param(pid, 0, 0);
            } else
                smk_param(pid, smk_fxmem[i] ? smk_fxmem[i] : SMK_FXMIX0[i], 0);
            put(put(s, SMK_FXNAME[i]), PV(pid) ? " ON" : " OFF");
            say(s);
        }
    }
}

/* ---- FADER BANK, < > (main loop, with the other buttons) */
static void smk_buttons(uint32_t b)
{
    if (b & (1u << BTN_FADER_BANK)) {
        uint32_t i;
        smk_bank ^= 1u;
        for (i = 0; i < 4u; i++) smk_fad_last[i] = 0xFF;   /* a new bank: each fader waits until it moves */
        say(smk_bank ? "FADERS B: WEAR" : "FADERS A: TAPE");
    }
    if (b & ((1u << BTN_LEFT) | (1u << BTN_RIGHT))) {
        page = (page + (b & (1u << BTN_RIGHT) ? 1u : NPG - 1u)) % NPG;
        g_last[PG_GROUP[page]] = (uint8_t)page;
        touched = 99;
        redraw |= 0xFu;
    }
}

/* ---- the faders: two banks, a fader acts once it moves */
static void smk_faders(void)
{
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        uint32_t v = fm1_in.fader[i], pid = SMK_FP[smk_bank][i];
        int32_t lo = PINFO[pid][0], hi = PINFO[pid][1];
        if (smk_fad_last[i] == 0xFF || fm1_in.frames < 200u) {   /* (the first readings settle) */
            smk_fad_last[i] = (uint8_t)v;
            continue;
        }
        if (v == smk_fad_last[i]) continue;
        smk_fad_last[i] = (uint8_t)v;
        smk_param(pid, lo + ((hi - lo) * (int32_t)v + 63) / 127, 1);
    }
}

/* ---- the wheels: the motor and the vibrato */
static void smk_wheels(void)
{
    enum { DEAD = 12, MOD_LO = 485, MOD_HI = 615 };
    static smk_pw_t pw;
    static int32_t mlo, mhi, last_b = 0x7FFFFFFF, last_m = -1;
    int32_t m = fm1_in.wheel[1], mw, b;
    if (fm1_in.frames < 300u)                   /* (the readings settle) */
        return;
    if (!mhi) {
        mlo = m < MOD_LO ? m : MOD_LO;
        mhi = m > MOD_HI ? m : MOD_HI;
    }
    if (m < mlo) mlo = m;
    if (m > mhi) mhi = m;
    mw = mhi - m <= DEAD ? 0 : (mhi - m - DEAD) * 127 / (mhi - mlo - DEAD > 1 ? mhi - mlo - DEAD : 1);   /* up = lower raw */
    mw = mw < 0 ? 0 : mw > 127 ? 127 : mw;
    b = smk_pw_bend(&pw, fm1_in.wheel[0], fm1_ms) * 1600 / 4096;   /* 1/4096 semitone -> 1/16 cent, +-2 semitones */
    if (b != last_b) { last_b = b; p_bend16 = b; }   /* (only on a change: a MIDI bend in still works at rest) */
    if (mw != last_m) { last_m = mw; p_mod = mw; }
}

/* ---- the pads' and the extra buttons' lights */
static void smk_pad_rgb(uint32_t p, const uint8_t *c) { padled_set(p, c[0], c[1], c[2]); }
static void smk_leds(void)
{
    uint32_t p, snd = (uint32_t)PV(P_SOUND), ly = (uint32_t)PV(P_BSND), lv = ui_lv > 0;
    for (p = 0; p < 4u; p++) {
        smk_pad_rgb(p, p == snd ? (lv ? SMK_ACT : SMK_ON) : SMK_OFF);
        smk_pad_rgb(4u + p, ly == p + 1u ? (lv ? SMK_ACT : SMK_ON) : SMK_OFF);
    }
    smk_pad_rgb(SP_LOFI, by_lofi ? SMK_OFF : lv ? SMK_ACT : SMK_ON);
    for (p = 0; p < 4u; p++) {
        uint32_t pad = p == 0u ? SP_VIB : p == 1u ? SP_LES : p == 2u ? SP_ECHO : SP_VERB;
        smk_pad_rgb(pad, !PV(SMK_FXP[p]) ? SMK_OFF : lv && !by_fx ? SMK_ACT : SMK_ON);
    }
    smk_pad_rgb(SP_LFAST, PV(P_LSPEED) != 2 ? SMK_OFF : lv && PV(P_LMIX) && !by_fx ? SMK_ACT : SMK_ON);
    smk_pad_rgb(SP_HALF, set.half ? SMK_ON : SMK_OFF);
    smk_pad_rgb(SP_LEN, PV(P_LEN) ? SMK_ON : SMK_OFF);
    fm1_led_key(BTN_FADER_BANK, smk_bank != 0u);
    fm1_led_key(BTN_KNOB_BANK, fm1_in.buttons >> BTN_KNOB_BANK & 1u);   /* (lit while the panel check is up) */
    fm1_led_key(BTN_LEFT, 1);
    fm1_led_key(BTN_RIGHT, 1);
    padled_flush();                             /* (only when a colour changed; between LCD transfers) */
}

/* ---- the main loop's SMK part, after the middle keys */
static void smk_poll(uint32_t base)
{
    smk_outer(base);
    smk_pads();
    smk_faders();
    smk_wheels();
}

/* ---- KNOB BANK held: the panel check (MEL has no serial console; this is the bring-up screen) */
static void smk_hex(char *s, uint32_t v, uint32_t n)
{
    uint32_t k;
    for (k = 0; k < n; k++) s[k] = "0123456789ABCDEF"[(v >> (4u * (n - 1u - k))) & 15u];
    s[n] = 0;
}
static int smk_check(void)
{
    static uint32_t on, t;
    static char ln[12][32];
    uint32_t i, k;
    char *p;
    if (!(fm1_in.buttons >> BTN_KNOB_BANK & 1u)) {
        if (on) { on = 0; redraw = 0xFFu; tapes_sig ^= 1u; }
        return 0;
    }
    if (on && fm1_ms - t < 100u) return 1;
    on = 1;
    t = fm1_ms;
    for (i = 0; i < 12u; i++) ln[i][0] = 0;
    put(ln[0], "PANEL CHECK");
    p = put(ln[1], "FLASH "); smk_hex(p, smk_jedec, 6); put(ln[1] + 12, smk_trim_ok ? " TRIM OK" : " TRIM DEFAULT");
    p = put_int(put(ln[2], "ADC "), (int32_t)(fm1_in.adc_ok % 100000u), 0); put_int(put(p, " MISS "), (int32_t)(fm1_in.adc_miss % 10000u), 0);
    p = put(ln[3], "FADER");
    for (k = 0; k < 4u; k++) p = put_int(put(p, " "), fm1_in.fader[k], 0);
    put_int(put(p, " BAT "), fm1_in.batt, 0);
    p = put_int(put(ln[4], "WHEEL "), fm1_in.wheel[0], 0); p = put_int(put(p, " "), fm1_in.wheel[1], 0);
    p = put_int(put(p, " B"), p_bend16 / 16, 0); put_int(put(p, " V"), p_mod, 0);
    p = put(ln[5], "BTN "); smk_hex(p, fm1_in.buttons, 5); p += 5;
    for (k = 0; k < FM1_NKEY && !(fm1_in.keys >> k & 1u); k++) ;
    if (k < FM1_NKEY) { p = put_int(put(p, " KEY "), (int32_t)k, 0); put_int(put(p, " V"), fm1_in.key_vel[k], 0); }
    if (smk_store_cleared) put(ln[6], "STORE: OTHER DATA CLEARED");
    put(ln[7], "PADS: READING - BASELINE");
    for (i = 0; i < 4u; i++) {                  /* pads 1-4, 5-8 (top row), 9-12, 13-16 */
        p = put(put_int(put(put_int(ln[8 + i], (int32_t)(4u * i + 1u), 0), "-"), (int32_t)(4u * i + 4u), 0), ":");
        for (k = 4u * i; k < 4u * i + 4u; k++)
            p = put_int(put(p, " "), (int32_t)fm1_in.pad_raw[k] - (int32_t)fm1_in.pad_base[k], 0);
    }
    for (k = 0; k < 2u; k++) {
        cv_begin(240, 120, C_FRAME);
        for (i = 0; i < 6u; i++) tx(6, (int32_t)i * 19 + 4, &F16, ln[6u * k + i], k == 0u && i == 0u ? C_LAMP : C_BODY);
        cv_blit(0, 120u * k);
    }
    return 1;
}

#ifdef DINGHY_HOST
/* the host test's "dump" (tests/host.c): the pads and the state */
static void host_dump(void)
{
    uint32_t p;
    printf("%6u ms  PADS", fm1_ms);
    for (p = 0; p < 16u; p++) printf(" %06X", padled_rgb[p]);
    printf("\n%6u ms  STATE bank=%u page=%u sound=%d layer=%d bmix=%d tone=%d wow=%d flut=%d age=%d sat=%d vmix=%d"
           " lmix=%d lspeed=%d emix=%d rmix=%d half=%u len=%d lofi=%u bend=%d mod=%d master=%d\n", fm1_ms, smk_bank,
           page, PV(P_SOUND), PV(P_BSND), PV(P_BMIX), PV(P_TONE), PV(P_WOW), PV(P_FLUT), PV(P_AGE), PV(P_SAT),
           PV(P_VMIX), PV(P_LMIX), PV(P_LSPEED), PV(P_EMIX), PV(P_RMIX), set.half, PV(P_LEN), by_lofi, p_bend16, p_mod,
           smk_master);
}
#endif
