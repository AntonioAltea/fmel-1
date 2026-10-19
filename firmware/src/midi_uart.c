/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MIDI IN on the TRS jack (after Felucca's midi_uart.c): PH8 -> input channel 1 -> UART1 RX, 31250
 * baud, RX DMA into a 128-byte ring, polled from the TIMER5 ISR (no UART IRQ), in usb_poll's context.
 * Channel messages go into midi_in_q next to USB's, as USB-MIDI packets (cable 0, CIN = status >> 4),
 * so midi_get sees them as it sees USB's; clock / start / continue / stop as USB's; other realtime,
 * system common and SysEx are ignored (no update over the TRS jack). */
#include "fm1_uart.h"

#define UM_RING 128u
static volatile uint8_t um_ring[UM_RING] __attribute__((aligned(16)));
static struct {
    uint32_t rd, pend;
    uint8_t st, need, got, d0, sysex;
} um;

static void uart_midi_init(void)                   /* before fm1_timer5_start: PORTH RMW */
{
    fm1_uart1_midi_init(um_ring, UM_RING);
}

static void um_put(uint32_t pkt)                   /* full: dropped (ep1_take leaves 8 slots for this) */
{
    if (mi_w - mi_r < MQ) {
        midi_in_q[mi_w % MQ] = pkt;
        RING_PUBLISH();
        mi_w++;
    }
}

static void um_byte(uint32_t b)
{
    if (b >= 0xF8u) {
        if (b == 0xF8u || b == 0xFAu || b == 0xFBu || b == 0xFCu)
            um_put(0xFu | b << 8);
        return;                                    /* realtime keeps running status */
    }
    if (b & 0x80u) {
        um.sysex = b == 0xF0u;
        um.st = b < 0xF0u ? (uint8_t)b : 0;        /* system common / SysEx cancel running status */
        um.need = (b & 0xE0u) == 0xC0u ? 1u : 2u;  /* Cx Dx: 1 data byte, else 2 */
        um.got = 0;
        return;
    }
    if (um.sysex || !um.st)
        return;
    if (um.need == 2u && !um.got) {
        um.d0 = (uint8_t)b;
        um.got = 1;
        return;
    }
    um.got = 0;
    if (um.need == 2u)
        um_put((um.st >> 4) | (uint32_t)um.st << 8 | (uint32_t)um.d0 << 16 | b << 24);
    else
        um_put((um.st >> 4) | (uint32_t)um.st << 8 | b << 16);
}

static void uart_midi_poll(void)                   /* TIMER5 ISR, as usb_poll */
{
    um.pend += fm1_uart1_rx_take();
    if (um.pend > UM_RING) {                       /* overrun: skip to the newest, wait for a new status */
        um.rd = (um.rd + um.pend - UM_RING) & (UM_RING - 1u);
        um.pend = UM_RING;
        um.st = um.got = um.sysex = 0;
    }
    while (um.pend) {
        um_byte(um_ring[um.rd]);
        um.rd = (um.rd + 1u) & (UM_RING - 1u);
        um.pend--;
    }
}
