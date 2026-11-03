/* SPDX-License-Identifier: GPL-3.0-only
 * SMK-37 port: Copyright (C) 2026 M. Geisler (genkigenki/makkuro); the panel's layout: AntonioAltea */
/* MEL on the M-Vave SMK-37 (BOARD_SMK37; SMK37.md). The FM-1's panel keeps its jobs (the middle 27 keys, the 14 buttons
 * by printed name, the four page knobs: platform.c); this file adds what the SMK has more. Included by mel.c; main
 * loop only. Every control on the surface does one thing, always; a light means something is not as it was.
 *
 *   keys      all 37, C3..C6 at OCTAVE 0 (keybed 5..31 are the FM-1's F3..G5); velocity = the tape's level,
 *             SETUP's VELOCITY how much (mel.c note_on)
 *   pads      a column is a sound, a row its part: top 1-6 the SOUND A..F, bottom 9-14 the LAYER's (the lit one
 *             again: no layer). 7 / 15 held: only the top / the bottom row heard. 8 HOLD, 16 HALF SPEED (lit: on):
 *             a tap switches it, held it is the other way only while held
 *             A sound's pad is dark until chosen, then its colour, brighter with what it plays
 *   faders    as the signal runs: MIX (up: the top row, down: the bottom), LOFI and FX (how much), VOLUME. Where a
 *             fader is, is the value (the four are not saved)
 *   knobs     K5..K8 the page's four (the plate); K1 TONE, K2 SELECT (the hand on the reel), K3 TEMPO, K4 PITCH
 *   wheels    pitch = the motor (+-2 semitones), mod = the vibrato's DEPTH (up to all of it); the PEDAL jack = sustain (either polarity)
 *   < >       the section's pages, back and on
 *   FADER BANK  UNDO: the last knob or pad change back (again: redone); lit while there is one
 *   KNOB BANK   held at power-on: the panel check */
#include "smk_wheel.h"

static const uint8_t SMK_COL[6][3] = {          /* each sound's colour, its column's: its drawing's, as an LED shows it */
    {60, 70, 255},                              /* A FLUTE: the bird's blue */
    {255, 90, 0},                               /* B CHOIR: the fox's orange */
    {190, 40, 10},                              /* C CELLO: the cello's brown */
    {0, 160, 255},                              /* D VIBES: the vibraphone's blue */
    {255, 170, 0},                              /* E SAX: brass */
    {40, 220, 20}};                             /* F PIANO: the frog's green */
enum { SP_SOLO_A = 6, SP_HOLD = 7, SP_SOLO_B = 14, SP_HALF = 15 };

static uint8_t smk_fad_last[4] = {0xFF, 0xFF, 0xFF, 0xFF};
static uint8_t smk_ext_note[FM1_NKEY];          /* the note an outer key started (+1 = none: 0) */
static uint32_t smk_pad_t[16];                  /* when each pad last switched (a press counts once) */
#define SMK_MOMENT_MS 400u                      /* HOLD, HALF SPEED: held longer than this, back when let go */
static uint8_t smk_check_on;                    /* the panel check is up */
static int32_t undo_pid = -1, undo_old;         /* UNDO: the parameter last changed and its value before */
static uint32_t undo_t;
static uint16_t ped_rest = 0xFFFF;              /* the pedal jack at rest (learnt at power-on) */

static void smk_init(void)
{
    uint32_t i;
    smk_pad_events = 1;                         /* the pads are MEL's own (events, no notes) */
    for (i = 0; i < 128u; i++)
        smk_note_vel[i] = 100;
}

/* ---- UNDO: a change of pid from v_old, by hand; one gesture (the same parameter, turned on) is one change */
static void smk_undo_note(uint32_t pid, int32_t v_old)
{
    if ((int32_t)pid != undo_pid || fm1_ms - undo_t > 1500u) {
        undo_pid = (int32_t)pid;
        undo_old = v_old;
    }
    undo_t = fm1_ms;
}
static void smk_undo(void)
{
    int32_t now;
    char s[32];
    uint32_t pg, k;
    if (undo_pid < 0) return;
    now = PV(undo_pid);
    if (undo_pid == P_SOUND) to_sound(undo_old);
    else if (undo_pid == P_BSND) {
        set_param(P_BSND, undo_old);
        changed(99);
        redraw |= 0xFu;
        put(put(s, "LAYER "), PV(P_BSND) ? MEL_SET_NAME[PV(P_BSND) - 1] : "OFF");
        say(s);
    } else {
        set_param((uint32_t)undo_pid, undo_old);
        changed(99);
        redraw |= 0xFu;
        for (pg = 0; pg < NPG; pg++)            /* the header: its name and value, as the plate shows them */
            for (k = 0; k < 4u; k++)
                if (CTL[pg][k].pid == (uint32_t)undo_pid && CTL[pg][k].name) {
                    char *p = put(s, CTL[pg][k].name);
                    *p++ = ' ';
                    fmt_val(&CTL[pg][k], PV(undo_pid), p);
                    say(s);
                    pg = NPG;
                    break;
                }
    }
    undo_old = now;                             /* (again: redone) */
    undo_t = 0;
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

static void smk_half(void)
{
    set.half = !set.half;
    changed(99);
    say(set.half ? "HALF SPEED" : "FULL SPEED");
    redraw |= 3u;
}

/* ---- the pads: a column a sound, the top row the SOUND, the bottom the LAYER's */
static void smk_pads(void)
{
    int32_t e;
    while ((e = smk_pad_take()) >= 0) {
        uint32_t p = ((uint32_t)e >> 8) & 15u, vel = (uint32_t)e & 0xFFu, col = p & 7u;
        if (p == SP_SOLO_A || p == SP_SOLO_B) { /* held: one row only (let go: both again) */
            smk_solo = vel ? (p == SP_SOLO_A ? 1u : 2u) : smk_solo == (p == SP_SOLO_A ? 1u : 2u) ? 0u : smk_solo;
            apply();
            continue;
        }
        if (p == SP_HOLD || p == SP_HALF) {     /* a tap switches it; held, it switches back when let go */
            static uint8_t down[2];
            uint32_t i = p == SP_HALF;
            if (vel && !down[i] && fm1_ms - smk_pad_t[p] >= 180u) {
                down[i] = 1;
                smk_pad_t[p] = fm1_ms;
                if (i) smk_half(); else hold_switch();
            } else if (!vel && down[i]) {
                down[i] = 0;
                if (fm1_ms - smk_pad_t[p] >= SMK_MOMENT_MS) { if (i) smk_half(); else hold_switch(); }
            }
            continue;
        }
        if (!vel || fm1_ms - smk_pad_t[p] < 180u) continue;   /* (a press counts once; let go does nothing) */
        smk_pad_t[p] = fm1_ms;
        if (col < MEL_NSETS && p < 8u) {        /* SOUND A..F */
            if ((uint32_t)PV(P_SOUND) != col) {
                smk_undo_note(P_SOUND, PV(P_SOUND));
                to_sound((int32_t)col);
            }
        } else if (col < MEL_NSETS) {           /* the LAYER's sound A..F; the one it is: off */
            uint32_t ly = (uint32_t)PV(P_BSND) == col + 1u ? 0u : col + 1u;
            char s[24];
            smk_undo_note(P_BSND, PV(P_BSND));
            set_param(P_BSND, (int32_t)ly);
            changed(page == PG_LAYER ? 0u : 99u);
            redraw |= 0xFu;
            put(put(s, "LAYER "), ly ? MEL_SET_NAME[ly - 1u] : "OFF");
            say(s);
        }
    }
}

/* ---- < > and FADER BANK (main loop, with the other buttons) */
static void smk_buttons(uint32_t b)
{
    uint32_t g = PG_GROUP[page];
    if (b & (1u << BTN_FADER_BANK)) smk_undo();
    if (b & ((1u << BTN_LEFT) | (1u << BTN_RIGHT))) {   /* the section's pages, round */
        page = G_FIRST[g] + (page - G_FIRST[g] + (b & (1u << BTN_RIGHT) ? 1u : G_COUNT[g] - 1u)) % G_COUNT[g];
        g_last[g] = (uint8_t)page;
        touched = 99;
        redraw |= 0xFu;
    }
}

/* ---- the faders: where each is, is its value */
static void smk_faders(void)
{
    uint32_t i;
    if (fm1_in.frames < 200u) return;           /* (the first readings settle) */
    for (i = 0; i < 3u; i++) {                  /* (the fourth, VOLUME: platform.c's master, SMK_MASTER_FADER) */
        uint32_t v = fm1_in.fader[i], first = smk_fad_last[i] == 0xFF;
        char s[16];
        if (v == smk_fad_last[i]) continue;
        smk_fad_last[i] = (uint8_t)v;
        if (i == 0u) {                          /* MIX: up the top row, down the bottom */
            if (set_param(P_BMIX, 100 - ((int32_t)v * 100 + 63) / 127)) changed(PG_GROUP[page] == G_LAYER ? 1u : 99u);
            if (!first) put_int(put(s, "MIX "), 100 - PV(P_BMIX), 0);
        } else {
            int32_t a = (int32_t)(v * v * 32767u / (127u * 127u));   /* (squared: fine where it is little) */
            if (i == 1u) f_lofi_amt = a;
            else f_fx_amt = a;
            if (!first) put(put_int(put(s, i == 1u ? "LOFI " : "FX "), ((int32_t)v * 100 + 63) / 127, 0), "%");
        }
        if (!first) say(s);
    }
}

/* ---- the wheels and the pedal jack */
static void smk_wheels(void)
{
    enum { DEAD = 12, MOD_LO = 485, MOD_HI = 615 };
    static smk_pw_t pw;
    static int32_t mlo, mhi, last_b = 0x7FFFFFFF, last_m = -1, ped_was = -1;
    int32_t m = fm1_in.wheel[1], mw, b, pd;
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
    if (ped_rest == 0xFFFF) ped_rest = fm1_in.pedal;   /* the pedal at rest, open or closed: pressed is the other */
    pd = (int32_t)fm1_in.pedal - (int32_t)ped_rest;
    pd = pd < -300 || pd > 300;
    if (pd != ped_was) {
        if (ped_was >= 0) pedal_set(pd);
        ped_was = pd;
    }
}

/* ---- the pads' and the extra buttons' lights */
static void smk_rgb(uint32_t p, const uint8_t *c, uint32_t lv)   /* c at lv / 256 */
{
    padled_set(p, (uint8_t)(c[0] * lv >> 8), (uint8_t)(c[1] * lv >> 8), (uint8_t)(c[2] * lv >> 8));
}
static void smk_leds(void)
{
    static const uint8_t WHITE[3] = {255, 244, 230}, AMBER[3] = {255, 120, 0};
    static uint32_t t;
    uint32_t p, snd = (uint32_t)PV(P_SOUND), ly = (uint32_t)PV(P_BSND), lv = 120u + (uint32_t)ui_lv * 136u / 256u;
    if (fm1_ms - t < 30u) return;               /* (~1 ms a send: 30 a second is enough) */
    t = fm1_ms;
    for (p = 0; p < 6u; p++) {
        smk_rgb(p, SMK_COL[p], p == snd && p < MEL_NSETS && smk_solo != 2u ? lv : 0u);
        smk_rgb(8u + p, SMK_COL[p], ly == p + 1u && smk_solo != 1u ? lv : 0u);
    }
    smk_rgb(SP_SOLO_A, WHITE, smk_solo == 1u ? 256u : 0u);
    smk_rgb(SP_SOLO_B, WHITE, smk_solo == 2u ? 256u : 0u);
    smk_rgb(SP_HOLD, WHITE, hold ? 256u : 0u);
    smk_rgb(SP_HALF, AMBER, set.half ? 256u : 0u);
    fm1_led_key(BTN_FADER_BANK, undo_pid >= 0);
    fm1_led_key(BTN_KNOB_BANK, smk_check_on);
    fm1_led_key(BTN_LEFT, G_COUNT[PG_GROUP[page]] > 1u);   /* (lit: the section has more pages) */
    fm1_led_key(BTN_RIGHT, G_COUNT[PG_GROUP[page]] > 1u);
    padled_flush();                             /* (only when a colour changed; between LCD transfers) */
#ifdef DINGHY_HOST
    if (host_dump_req) {                        /* (tests/host.c: a shot) */
        host_dump_req = 0;
        printf("%6u ms  state page=%u sound=%d layer=%d bmix=%d tone=%d pitch=%d atk=%d rel=%d bpm=%d oct=%d hold=%u half=%u solo=%u"
               " lofi=%d fx=%d vol=%u bend=%d mod=%d pedal=%u undo=%d arp=%u play=%u rec=%u bylofi=%u byfx=%u spk=%d"
               " leds=%05X\n", fm1_ms, page, PV(P_SOUND), PV(P_BSND), PV(P_BMIX), PV(P_TONE), PV(P_PITCH), PV(P_ATK), PV(P_REL), PV(P_BPM),
               PV(P_OCT), hold, set.half, smk_solo, f_lofi_amt, f_fx_amt, fm1_in.fader[3], p_bend16, p_mod, pedal, undo_pid,
               arp_on, seq_play, seq_rec, by_lofi, by_fx, PV(P_SPK), (uint32_t)(host_leds & 0x3FFFFu));
    }
#endif
}

/* ---- the main loop's SMK part, after the middle keys */
static void smk_poll(uint32_t base)
{
    smk_outer(base);
    smk_pads();
    smk_faders();
    smk_wheels();
}

/* ---- KNOB BANK held at power-on: the panel check (MEL has no serial console; this is the bring-up screen) */
static int smk_check(void)
{
    static uint32_t t;
    static char ln[12][32];
    uint32_t i, k;
    char *p;
    if (!smk_check_on) {
        if (fm1_ms < 1500u && fm1_in.buttons >> BTN_KNOB_BANK & 1u) smk_check_on = 1;
        else return 0;
    }
    if (!(fm1_in.buttons >> BTN_KNOB_BANK & 1u)) {   /* let go: MEL */
        smk_check_on = 0;
        redraw = 0xFFu;
        tapes_sig ^= 1u;
        return 0;
    }
    if (fm1_ms - t < 100u) return 1;
    t = fm1_ms;
    for (i = 0; i < 12u; i++) ln[i][0] = 0;
    put(ln[0], "PANEL CHECK");
    p = put_int(put(ln[1], "ADC "), (int32_t)(fm1_in.adc_ok % 100000u), 0);
    put_int(put(p, " MISS "), (int32_t)(fm1_in.adc_miss % 10000u), 0);
    p = put(ln[2], "FADER");
    for (k = 0; k < 4u; k++) p = put_int(put(p, " "), fm1_in.fader[k], 0);
    p = put_int(put(ln[3], "WHEEL "), fm1_in.wheel[0], 0);
    p = put_int(put(p, " "), fm1_in.wheel[1], 0);
    put_int(put(p, " PED "), fm1_in.pedal, 0);
    p = put_int(put(ln[4], "BAT "), fm1_in.batt, 0);
    if (smk_store_cleared) put(p, " STORE CLEARED");
    for (k = 0; k < FM1_NKEY && !(fm1_in.keys >> k & 1u); k++) ;
    if (k < FM1_NKEY) { p = put_int(put(ln[5], "KEY "), (int32_t)k, 0); put_int(put(p, " V"), fm1_in.key_vel[k], 0); }
    for (i = 0; i < 4u; i++) {                  /* pads 1-4, 5-8 (top row), 9-12, 13-16: reading - baseline */
        p = put(put_int(put(put_int(ln[6 + i], (int32_t)(4u * i + 1u), 0), "-"), (int32_t)(4u * i + 4u), 0), ":");
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
