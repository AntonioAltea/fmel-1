/* SPDX-License-Identifier: GPL-3.0-only */
/* MEL's arpeggiator and step sequencer, on one clock (96 ticks a beat, (uint32_t)PV(P_BPM)). Main loop only; included by
 * mel.c after the notes (src_set) and the settings.
 *
 * ARP: while it is on, the keys (and MIDI, HOLD, the pedal) do not sound themselves: they make the arpeggio.
 *   MODE UP DOWN UP+DN RANDOM PLAYED, RATE 1/4 .. 1/32, OCT 1..3 octaves, GATE 10..100 %.
 * SEQ: 16 steps of 1/16, up to 4 notes each (a chord: it is a Mellotron), LENGTH 1..16, GATE, SWING (the off-beat
 *   sixteenths late: 0 straight .. 100 a triplet feel).
 *   PLAY starts / stops. REC, stopped: step recording (play a note or a chord, let go: the next step; SEL: a rest).
 *   REC while playing: what you play goes into the step it falls on. SEL held 1 s: the pattern is cleared. */

enum { S_ARP = 16, S_SEQ = 32 };
#define S_IN (S_KEY | S_MIDI | S_LATCH | S_PED)
static const uint8_t RATE_TICKS[6] = {96, 48, 32, 24, 16, 12};

static uint8_t arp_on, seq_play, seq_rec;
static uint8_t ain[128];                        /* ARP's input: what holds each note (S_IN bits) */
static uint32_t aord[128], aord_n;              /* when each came in (PLAYED) */
static uint8_t arp_list[48];
static uint32_t arp_n, arp_i, arp_pos_shown, arp_tick, arp_gate_left, arp_cur = 255;
static uint32_t seq_step, seq_tick, seq_cur, seq_gate_left;
static uint8_t seq_sounding[4];
static uint32_t clk_acc, clk_last;
static uint8_t chord_open;                      /* step recording: a chord is being played into seq_cur */

/* ---- input: to the voices, or to the arpeggio */
static void in_set(uint32_t note, uint32_t set_b, uint32_t clr_b)
{
    uint32_t was, now;
    if (note > 127u) return;
    if (!arp_on) {
        src_set(note, set_b, clr_b);
        return;
    }
    was = ain[note];
    now = (was | set_b) & ~clr_b;
    ain[note] = (uint8_t)now;
    if (!was && now) aord[note] = ++aord_n;
}
static uint8_t in_get(uint32_t note) { return arp_on ? ain[note] : (uint8_t)(src[note] & S_IN); }
static void in_clear(uint32_t bits)
{
    uint32_t n;
    for (n = 0; n < 128u; n++)
        if (in_get(n) & bits)
            in_set(n, 0, bits);
}

static void arp_release(void)
{
    if (arp_cur < 128u) src_set(arp_cur, 0, S_ARP);
    arp_cur = 255;
}
static void arp_enable(int on)                  /* what is held moves over: nothing hangs, nothing is lost */
{
    uint32_t n;
    if (on == arp_on) return;
    if (on) {
        for (n = 0; n < 128u; n++) {
            uint32_t b = src[n] & S_IN;
            ain[n] = 0;
            if (b) {
                src_set(n, 0, S_IN);
                ain[n] = (uint8_t)b;
                aord[n] = ++aord_n;
            }
        }
        arp_on = 1;
        arp_n = 0;
    } else {
        arp_release();
        arp_on = 0;
        for (n = 0; n < 128u; n++)
            if (ain[n]) {
                src_set(n, ain[n], 0);
                ain[n] = 0;
            }
        arp_n = 0;
    }
}

static void arp_build(void)                     /* the notes held -> the arpeggio, over OCT octaves */
{
    uint32_t n, k, o, m = 0, tmp[16];
    for (n = 0; n < 128u && m < 16u; n++)       /* low to high */
        if (ain[n]) tmp[m++] = n;
    if (PV(P_AMODE) == 4)                     /* PLAYED: in the order they came */
        for (n = 1; n < m; n++)
            for (k = n; k > 0 && aord[tmp[k - 1u]] > aord[tmp[k]]; k--) {
                uint32_t x = tmp[k]; tmp[k] = tmp[k - 1u]; tmp[k - 1u] = x;
            }
    arp_n = 0;
    for (o = 0; o < (uint32_t)PV(P_AOCT); o++)
        for (k = 0; k < m; k++)
            if (tmp[k] + 12u * o < 128u)
                arp_list[arp_n++] = (uint8_t)(tmp[k] + 12u * o);
}
static void arp_step(void)
{
    uint32_t i, n = arp_n, ticks = RATE_TICKS[PV(P_ARATE) % 6u];
    arp_release();
    if (!n) return;
    switch (PV(P_AMODE)) {
    case 1: i = n - 1u - arp_i % n; break;                          /* DOWN */
    case 2: {                                                       /* UP+DN: the ends once */
        uint32_t p = n > 1u ? 2u * n - 2u : 1u, j = arp_i % p;
        i = j < n ? j : p - j;
        break;
    }
    case 3: i = (fm1_ms * 2654435761u + arp_i * 40503u) >> 16; i %= n; break;   /* RANDOM */
    default: i = arp_i % n; break;                                  /* UP, PLAYED */
    }
    arp_i++;
    arp_pos_shown = i;
    arp_cur = arp_list[i];
    src_set(arp_cur, S_ARP, 0);
    arp_gate_left = ticks * (uint32_t)PV(P_AGATE) / 100u;
    if (!arp_gate_left) arp_gate_left = 1;
    if (page == PG_ARP) redraw |= 2u;
}

/* ---- the sequencer */
static void seq_release(void)
{
    uint32_t k;
    for (k = 0; k < 4u; k++)
        if (seq_sounding[k]) {
            src_set(seq_sounding[k], 0, S_SEQ);
            seq_sounding[k] = 0;
        }
}
static void seq_trigger(void)
{
    uint32_t k;
    seq_release();
    for (k = 0; k < 4u; k++) {
        uint32_t nt = set.steps[seq_step][k];
        if (nt) {
            seq_sounding[k] = (uint8_t)nt;
            src_set(nt, S_SEQ, 0);
        }
    }
    seq_gate_left = 24u * (uint32_t)PV(P_SGATE) / 100u;
    if (!seq_gate_left) seq_gate_left = 1;
    if (page == PG_SEQ) redraw |= 2u;
}
static void seq_start(int on)
{
    if (on && !seq_play) {
        seq_play = 1;
        seq_step = 0;
        seq_tick = 0;
        clk_acc = 0;
        seq_trigger();
    } else if (!on && seq_play) {
        seq_play = 0;
        seq_release();
    }
    redraw |= 3u;
}
static int step_add(uint32_t st, uint32_t note)
{
    uint32_t k;
    for (k = 0; k < 4u; k++)
        if (set.steps[st][k] == note) return 1;
    for (k = 0; k < 4u; k++)
        if (!set.steps[st][k]) { set.steps[st][k] = (uint8_t)note; return 1; }
    return 0;
}
static void rec_note(uint32_t note)            /* a note played while REC is on */
{
    if (!seq_rec || note > 127u) return;
    if (seq_play) {                             /* into the step it falls on */
        uint32_t st = seq_tick < 12u ? seq_step : (seq_step + 1u) % (uint32_t)PV(P_SLEN);
        step_add(st, note);
    } else {                                    /* step recording: the first note of a chord clears the step */
        if (!chord_open) {
            uint32_t k;
            for (k = 0; k < 4u; k++) set.steps[seq_cur][k] = 0;
            chord_open = 1;
        }
        step_add(seq_cur, note);
    }
    changed(99);
    redraw |= 2u;
}
static void rec_all_up(void)                   /* every key let go: step recording goes on to the next step */
{
    if (chord_open) {
        chord_open = 0;
        seq_cur = (seq_cur + 1u) % (uint32_t)PV(P_SLEN);
        redraw |= 10u;
    }
}

/* ---- the clock: 96 ticks a beat, run from the main loop on fm1_ms */
static void clock_run(void)
{
    uint32_t dt = fm1_ms - clk_last, held = 0, n;
    clk_last = fm1_ms;
    if (dt > 50u) dt = 50u;
    if (arp_on) {
        for (n = 0; n < 128u; n++)
            if (ain[n]) { held = 1; break; }
        if (!held) {
            arp_release();
            if (arp_n) { arp_n = 0; if (page == PG_ARP) redraw |= 2u; }
            arp_i = 0;
            arp_tick = 0;
        } else {
            uint32_t was = arp_n;
            arp_build();
            if (!was) {                         /* the first note: at once */
                arp_tick = 0;
                arp_step();
                if (page == PG_ARP) redraw |= 2u;
            }
        }
    }
    if (!seq_play && !(arp_on && held)) {
        clk_acc = 0;
        return;
    }
    clk_acc += dt * (uint32_t)PV(P_BPM) * 96u;
    while (clk_acc >= 60000u) {                 /* one tick */
        clk_acc -= 60000u;
        if (arp_on && arp_n) {
            if (arp_gate_left && !--arp_gate_left) arp_release();
            if (++arp_tick >= RATE_TICKS[PV(P_ARATE) % 6u]) {
                arp_tick = 0;
                arp_step();
            }
        }
        if (seq_play) {
            uint32_t sw = (uint32_t)PV(P_SWING) * 8u / 100u, len = seq_step & 1u ? 24u - sw : 24u + sw;
            if (seq_gate_left && !--seq_gate_left) seq_release();
            if (++seq_tick >= len) {
                seq_tick = 0;
                seq_step = (seq_step + 1u) % (uint32_t)PV(P_SLEN);
                seq_trigger();
            }
        }
    }
}
