/* SPDX-License-Identifier: GPL-3.0-only */
/* MEL's screen: an M400 (the white lacquered body, the brushed aluminium plate, black knobs, the brown oxide of
 * the tapes) played back from an old tape (scanlines; the picture sways with the wow, tears on a dropout, gets
 * grainy with AGE; SETUP > LOOK turns that off). Included by mel.c; main loop only.
 *
 *   0 ..  23   the body's top: the reel (turning while tapes run), the section and its page, the lamps that are on
 *  24 .. 131   the stage: what the page is about, drawn and moving with the sound
 * 132 .. 163   the tape frame: one strip per key, the oxide running past the head while it sounds
 * 164 .. 239   the plate: the four knobs, each with its name and its value */

#define C_BODY RGB(238, 233, 220)               /* white lacquer */
#define C_BODY2 RGB(222, 214, 196)              /* its shade */
#define C_INK RGB(38, 34, 30)                   /* print on the body and the plate */
#define C_INK2 RGB(120, 112, 100)
#define C_ALU RGB(186, 188, 190)
#define C_KNOB RGB(26, 26, 28)
#define C_SKIRT RGB(150, 152, 156)
#define C_OXIDE RGB(92, 54, 30)                 /* tape at rest */
#define C_OXIDE2 RGB(176, 104, 52)              /* tape running */
#define C_FRAME RGB(34, 30, 28)
#define C_LAMP RGB(232, 120, 40)                /* amber */
#define C_RED RGB(220, 44, 34)
#define C_GREEN RGB(70, 170, 90)
#define STAGE_Y 24
#define STAGE_H 108
#define TAPES_Y 132
#define PLATE_Y 164

/* ---- drawing on the canvas, with the background known (text blended onto it, not onto black) */
static uint16_t mix565(uint32_t a, uint32_t b, uint32_t t)   /* a .. b, t 0..16 */
{
    uint32_t r = ((a >> 11) * (16u - t) + (b >> 11) * t) >> 4;
    uint32_t g = (((a >> 5) & 63u) * (16u - t) + ((b >> 5) & 63u) * t) >> 4;
    uint32_t bl = ((a & 31u) * (16u - t) + (b & 31u) * t) >> 4;
    return (uint16_t)(r << 11 | g << 5 | bl);
}
static int32_t tx(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t fg, uint16_t bg)
{
    uint16_t ramp[16];
    uint32_t a;
    for (a = 0; a < 16u; a++)
        ramp[a] = mix565(bg, fg, a + (a > 7u));
    for (; *s; s++) {
        uint32_t gi = glyph(f, (uint8_t)*s), gx, gy, w = f->bw[gi], bpr = (w + 1u) / 2u;
        const uint8_t *gd = f->data + f->off[gi];
        for (gy = 0; gy < f->h; gy++)
            for (gx = 0; gx < w; gx++) {
                uint32_t v = gd[gy * bpr + gx / 2u];
                v = (gx & 1u) ? (v & 15u) : (v >> 4);
                if (v)
                    cv_pset(x - f->pad + (int32_t)gx, y + (int32_t)gy, ramp[v]);
            }
        x += f->adv[gi];
    }
    return x;
}
static void tx_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t fg, uint16_t bg)
{
    tx(cx - text_w(f, s) / 2, y, f, s, fg, bg);
}
static uint32_t isqrt(uint32_t v)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}
static void ellipse(int32_t cx, int32_t cy, int32_t rx, int32_t ry, uint16_t c)
{
    int32_t dy;
    if (rx <= 0 || ry <= 0) return;
    for (dy = -ry; dy <= ry; dy++) {
        int32_t dx = rx * (int32_t)isqrt((uint32_t)(ry * ry - dy * dy)) / ry;
        cv_rect(cx - dx, cy + dy, 2 * dx + 1, 1, c);
    }
}
static void disc(int32_t cx, int32_t cy, int32_t r, uint16_t c) { ellipse(cx, cy, r, r, c); }
static void rrect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
    int32_t i;
    if (2 * r > h) r = h / 2;
    if (2 * r > w) r = w / 2;
    cv_rect(x, y + r, w, h - 2 * r, c);
    for (i = 0; i < r; i++) {
        int32_t d = r - (int32_t)isqrt((uint32_t)(r * r - (r - i) * (r - i)));
        cv_rect(x + d, y + i, w - 2 * d, 1, c);
        cv_rect(x + d, y + h - 1 - i, w - 2 * d, 1, c);
    }
}
static void thick(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t w, uint16_t c)
{
    int32_t i, j;
    for (i = 0; i < w; i++)
        for (j = 0; j < w; j++)
            cv_line(x0 + i - w / 2, y0 + j - w / 2, x1 + i - w / 2, y1 + j - w / 2, c);
}
static int32_t sn(uint32_t ph, int32_t amp) { return (sine(ph) * amp) >> 15; }   /* sine(ph) scaled */
static int32_t cs(uint32_t ph, int32_t amp) { return (sine(ph + 0x40000000u) * amp) >> 15; }
#define MS_PH(ms, hz10) ((uint32_t)(ms) * (uint32_t)((hz10) * 429497u))   /* ms -> phase of hz10 / 10 Hz */
static int32_t pos256(uint32_t pid) { return (PV(pid) - PINFO[pid][0]) * 256 / (PINFO[pid][1] - PINFO[pid][0]); }

/* ---- the instruments, OP-1 style: flat shapes, a few colours, moving with the sound.
 * cx, cy the centre, s the size in 1/256, t milliseconds, lv how much it sounds (0..256) */
#define S(v) ((int32_t)(v) * s >> 8)

static void spr_fiddle(int32_t cx, int32_t cy, int32_t s, uint32_t t, int32_t lv, int big, uint16_t wood)
{
    uint16_t dark = RGB(48, 26, 14), str = RGB(232, 222, 196);
    int32_t bow = sn(MS_PH(t, big ? 9 : 13), S(big ? 16 : 13)) * lv >> 8, by = big ? S(20) : S(10);
    if (big)                                    /* the cello's endpin, on the floor */
        thick(cx, cy + S(30), cx, cy + S(42), 2, RGB(140, 140, 146));
    cv_rect(cx - S(3), cy - S(big ? 58 : 50), S(6) + 1, S(big ? 46 : 40), dark);   /* the neck */
    disc(cx, cy - S(big ? 60 : 52), S(5), dark);                                    /* the scroll */
    ellipse(cx, cy - S(10), S(big ? 17 : 13), S(big ? 15 : 11), wood);              /* upper bout */
    ellipse(cx, cy + S(12), S(big ? 21 : 16), S(big ? 19 : 14), wood);              /* lower bout */
    ellipse(cx - S(big ? 20 : 15), cy + S(1), S(4), S(5), C_BODY);                 /* the waist */
    ellipse(cx + S(big ? 20 : 15), cy + S(1), S(4), S(5), C_BODY);
    cv_rect(cx - S(2), cy - S(26), S(4) + 1, S(30), dark);                          /* fingerboard */
    cv_rect(cx - S(8), cy + S(2), 2, S(10), dark);                                  /* f-holes */
    cv_rect(cx + S(7), cy + S(2), 2, S(10), dark);
    cv_rect(cx - S(4), cy + S(18), S(8) + 1, S(10), dark);                          /* tailpiece */
    cv_line(cx - 1, cy - S(big ? 56 : 48), cx - 1, cy + S(18), str);                /* strings */
    cv_line(cx + 1, cy - S(big ? 56 : 48), cx + 1, cy + S(18), str);
    thick(cx - S(38) + bow, cy + by - S(6), cx + S(38) + bow, cy + by + S(2), 2, RGB(90, 50, 24));   /* the bow */
    cv_line(cx - S(36) + bow, cy + by - S(3), cx + S(36) + bow, cy + by + S(5), RGB(250, 246, 232));
    if (lv > 24) {                              /* the sound coming off it */
        int32_t k, r = S(26) + (int32_t)((t / 8u) % 12u);
        for (k = 0; k < 3; k++) {
            int32_t q = r + k * 6;
            cv_pset(cx + q, cy - S(16) - k * 3, wood);
            cv_pset(cx + q + 1, cy - S(16) - k * 3 - 1, wood);
            cv_pset(cx - q, cy - S(16) - k * 3, wood);
            cv_pset(cx - q - 1, cy - S(16) - k * 3 - 1, wood);
        }
    }
}
static void spr_violins(int32_t cx, int32_t cy, int32_t s, uint32_t t, int32_t lv)   /* the MkII's three violins */
{
    spr_fiddle(cx - S(30), cy - S(6), S(170), t + 300u, lv, 0, RGB(176, 94, 40));
    spr_fiddle(cx + S(30), cy - S(6), S(170), t + 700u, lv, 0, RGB(176, 94, 40));
    spr_fiddle(cx, cy + S(4), S(220), t, lv, 0, RGB(214, 120, 46));
}
static void spr_cello(int32_t cx, int32_t cy, int32_t s, uint32_t t, int32_t lv)
{
    ellipse(cx, cy + S(36), S(30), S(4), C_BODY2);                                   /* its shadow */
    spr_fiddle(cx, cy - S(2), S(205), t, lv, 1, RGB(150, 70, 30));
}
static void note_glyph(int32_t x, int32_t y, uint16_t c)
{
    ellipse(x, y, 3, 2, c);
    cv_rect(x + 2, y - 9, 1, 9, c);
    cv_rect(x + 3, y - 9, 3, 2, c);
}
static void spr_flute(int32_t cx, int32_t cy, int32_t s, uint32_t t, int32_t lv)
{
    uint16_t sil = RGB(206, 210, 216), sh = RGB(140, 146, 156), key = RGB(70, 74, 82);
    int32_t k, x0 = cx - S(56), w = S(112);
    rrect(x0, cy - S(4), w, S(9) + 1, 3, sil);                                       /* the tube */
    cv_rect(x0 + 2, cy + S(3), w - 4, 2, sh);
    cv_rect(x0 + S(14), cy - S(7), S(10), S(3) + 1, sh);                             /* lip plate */
    disc(x0 + S(18), cy - S(3), 2, key);                                            /* embouchure */
    for (k = 0; k < 6; k++) {                   /* the keys, fingered as it plays */
        int32_t kx = cx - S(8) + k * S(10);
        int pressed = lv > 24 && ((t / 160u + (uint32_t)k * 7u) % 5u) < 2u;
        disc(kx, cy, S(4), key);
        disc(kx, cy, S(4) - 1, pressed ? key : sil);
    }
    if (lv > 8) {                               /* breath and notes rising */
        for (k = 0; k < 4; k++) {
            uint32_t a = (t / 12u + (uint32_t)k * 31u) % 90u;
            int32_t px = x0 + S(10) - (int32_t)a / 4 + sn(MS_PH(t, 20) + (uint32_t)k * 0x3000000u, 3);
            int32_t py = cy - S(10) - (int32_t)a / 2;
            if ((int32_t)a < 30 + lv / 4)
                disc(px, py, 1 + (int32_t)(a < 30u), RGB(200, 206, 214));
        }
        for (k = 0; k < 3; k++) {
            uint32_t a = (t / 20u + (uint32_t)k * 37u) % 110u;
            if (lv > 40 && a < 70u)
                note_glyph(cx + S(40) + k * 10 + sn((uint32_t)a << 25, 4), cy - S(8) - (int32_t)a / 2,
                           mix565(C_INK, C_BODY, a > 40u ? (a - 40u) / 2u : 0u));
        }
    }
}
static void spr_pix(const sprite_t *sp, int32_t cx, int32_t cy, int32_t s, uint32_t parts)   /* a drawn sprite
                                                                    * (mel_sprites.h), its loose parts up to parts */
{
    int32_t w = sp->w * s >> 8, h = sp->h * s >> 8, x0 = cx - w / 2, y0 = cy - h / 2, dx, dy;
    for (dy = 0; dy < h; dy++) {
        int32_t sy0 = dy * sp->h / h, sy1 = (dy + 1) * sp->h / h;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (dx = 0; dx < w; dx++) {
            int32_t sx0 = dx * sp->w / w, sx1 = (dx + 1) * sp->w / w, sx, sy;
            uint32_t c = 0;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            for (sy = sy0; sy < sy1 && c != 1u; sy++)   /* shrunk: the outline (colour 1) wins, it stays whole */
                for (sx = sx0; sx < sx1 && c != 1u; sx++) {
                    uint32_t v = sp->px[sy * sp->w + sx];
                    if ((v & 15u) && (v >> 4) <= parts && (!c || (v & 15u) == 1u)) c = v & 15u;
                }
            if (c) cv_pset(x0 + dx, y0 + dy, sp->pal[c]);
        }
    }
}
static void spr_choir(int32_t cx, int32_t cy, int32_t s, uint32_t t, int32_t lv)   /* a mouse, a magpie and a fox
                                                                                     * (drawn), their AAAAA coming */
{
    uint32_t ph = t % 800u;                     /* three frames, round and round: the dots, the small As, all (held) */
    spr_pix(&SPR_CHOIR, cx, cy, s, !lv ? 0u : ph < 200u ? 4u : ph < 400u ? 7u : 10u);
}
static void spr_vibes(int32_t cx, int32_t cy, int32_t s, uint32_t t, int32_t lv)   /* bars, tubes, fans, two mallets */
{
    uint16_t bar = RGB(206, 210, 216), bar2 = RGB(150, 154, 162), tube = RGB(204, 164, 70), dark = RGB(60, 52, 46);
    int32_t k, x0 = cx - S(58), hit[2];
    for (k = 0; k < 2; k++)                     /* which bar each mallet is on */
        hit[k] = (int32_t)((t / 230u + (uint32_t)k * 4u + (uint32_t)k * (t / 690u)) % 9u);
    cv_rect(x0 - S(4), cy - S(2), S(124), S(3) + 1, dark);                         /* the frame */
    for (k = 0; k < 9; k++) {
        int32_t x = x0 + k * S(13), len = S(40 - k * 2), tl = S(30 - k * 2);
        int lit = lv > 24 && (k == hit[0] || k == hit[1]) && (t % 230u) < 90u;
        rrect(x, cy + S(4), S(8), tl, S(4), tube);                                 /* the resonator */
        disc(x + S(4), cy + S(8), S(3), dark);                                     /* its fan, turning */
        thick(x + S(4) - cs(t * 0x00800000u * (uint32_t)(lv > 0), S(3)), cy + S(8),
              x + S(4) + cs(t * 0x00800000u * (uint32_t)(lv > 0), S(3)), cy + S(8), 1, bar);
        rrect(x - S(1), cy - len / 2 - S(10), S(10), len, S(3), lit ? C_LAMP : bar);   /* the bar */
        cv_rect(x + S(7), cy - len / 2 - S(8), 1, len - S(4), lit ? C_LAMP : bar2);
    }
    for (k = 0; k < 2; k++) {                   /* the mallets */
        int32_t bx = x0 + hit[k] * S(13) + S(4), up = lv ? (int32_t)((t + (uint32_t)k * 115u) % 230u) : 120;
        int32_t by = cy - S(40 - hit[k] * 2) / 2 - S(14) - (up < 90 ? 0 : (up - 90) * S(14) / 140);
        thick(bx, by, bx + S(26) + k * S(8), by - S(30), 2, RGB(170, 130, 80));
        disc(bx, by, S(5), k ? RGB(140, 50, 70) : RGB(90, 60, 140));
    }
}
#undef S
static void spr_sound(const char *nm, int32_t cx, int32_t sc, uint32_t t, int32_t lv)   /* a sound's drawing, by its
                                                                                       * name, sc/256 of its size */
{
    if (nm[0] == 'F') spr_flute(cx, 48, 300 * sc >> 8, t, lv);
    else if (nm[0] == 'C' && nm[1] == 'H') spr_choir(cx, 40, sc, t, lv);
    else if (nm[0] == 'V' && nm[2] == 'B') spr_vibes(cx, 40, 230 * sc >> 8, t, lv);
    else if (nm[0] == 'V') spr_violins(cx, 50, 230 * sc >> 8, t, lv);
    else spr_cello(cx, 52, 232 * sc >> 8, t, lv);
}

/* ---- the body's top */
static int32_t ui_lv;                           /* how much sounds, 0..256, smoothed */
static void lamp(int32_t *x, const char *s, uint16_t c, int on)   /* (only the lamps that are on) */
{
    int32_t w = text_w(&FONT_S, s) + 6;
    if (!on) return;
    *x -= w;
    rrect(*x, 4, w, 16, 4, c);
    tx(*x + 3, 4, &FONT_S, s, RGB(255, 252, 244), c);
    *x -= 3;
}
static void draw_header(int sounding)
{
    uint32_t g = PG_GROUP[page], k;
    int32_t x = 236, xe;
    uint32_t a = sounding ? fm1_ms * 0x00400000u : 0u;
    cv_begin(240, 24, C_BODY);
    disc(12, 12, 9, C_SKIRT);                   /* the reel */
    disc(12, 12, 3, C_BODY);
    for (k = 0; k < 3u; k++) {
        uint32_t ph = a + k * 0x55555555u;
        disc(12 + sn(ph, 5), 12 + cs(ph, 5), 2, C_INK);
    }
    xe = tx(26, 4, &FONT_S, G_NAME[g], C_INK, C_BODY);
    if (t_msg)
        tx(xe + 8, 4, &FONT_S, msg, C_RED, C_BODY);
    else {
        if (G_COUNT[g] > 1u) {                  /* the page, and where it is among the section's */
            xe = tx(xe + 8, 4, &FONT_S, PG_NAME[page], C_INK2, C_BODY) + 5;
            for (k = 0; k < G_COUNT[g]; k++)
                disc(xe + (int32_t)k * 7, 12, 2, G_FIRST[g] + k == page ? C_LAMP : C_BODY2);
        }
        lamp(&x, "REC", C_RED, seq_rec);
        lamp(&x, "SEQ", C_GREEN, seq_play);
        lamp(&x, "ARP", C_LAMP, arp_on);
        lamp(&x, "HOLD", C_LAMP, hold);
        lamp(&x, "FX", C_INK, !by_fx);          /* (the sections that are in) */
        lamp(&x, "LOFI", C_INK, !by_lofi);
    }
    cv_rect(0, 23, 240, 1, C_BODY2);
    cv_blit(0, 0);
}

/* ---- the stages */
static uint32_t sel_angle(uint32_t k)          /* the track selector's positions, spread from -90 to +90 degrees */
{
    return MEL_NSETS > 1u ? (uint32_t)((int32_t)0xC0000000u + (int32_t)(k * (0x80000000u / (MEL_NSETS - 1u)))) : 0u;
}
static void stage_play(void)                    /* the sound (and the layer, sized by the MIX), the track selector */
{
    uint32_t snd = (uint32_t)PV(P_SOUND), ly = (uint32_t)PV(P_BSND), k, t = ui_lv ? fm1_ms : 0u;
    int32_t mix = PV(P_BMIX);
    char nm[24];
    if (ly) {
        spr_sound(MEL_SET_NAME[snd], 112, 118 + (100 - mix) * 52 / 100, t, ui_lv);   /* (two fit beside the selector) */
        spr_sound(MEL_SET_NAME[ly - 1u], 196, 118 + mix * 52 / 100, t + 400u, ui_lv);
        put(put(put(nm, MEL_SET_NAME[snd]), " + "), MEL_SET_NAME[ly - 1u]);
    } else {
        spr_sound(MEL_SET_NAME[snd], 140, 256, t, ui_lv);
        put(nm, MEL_SET_NAME[snd]);
    }
    tx_c(140, 88, &FONT_S, nm, C_INK, C_BODY);
    for (k = 0; k < MEL_NSETS && k < 6u; k++) { /* A B C D round the selector: the sound's needle, the layer's amber */
        uint32_t ph = sel_angle(k);
        char l[2] = {(char)('A' + k), 0};
        tx_c(34 + sn(ph, 26), 44 - cs(ph, 26) - 8, &FONT_S, l, k == snd ? C_INK : ly == k + 1u ? C_LAMP : C_INK2, C_BODY);
    }
    disc(34, 52, 14, C_SKIRT);
    disc(34, 52, 11, C_KNOB);
    if (ly) {
        uint32_t ph = sel_angle(ly - 1u);
        thick(34 + sn(ph, 3), 52 - cs(ph, 3), 34 + sn(ph, 10), 52 - cs(ph, 10), 2, C_LAMP);
    }
    {
        uint32_t ph = sel_angle(snd);
        thick(34 + sn(ph, 3), 52 - cs(ph, 3), 34 + sn(ph, 10), 52 - cs(ph, 10), 2, RGB(244, 244, 240));
    }
    tx_c(34, 72, &FONT_S, "TRACK", C_INK2, C_BODY);
}

static void stage_layer(void)                   /* LAYER: the sound and the layer, the MIX between them */
{
    uint32_t snd = (uint32_t)PV(P_SOUND), ly = (uint32_t)PV(P_BSND), t = ui_lv ? fm1_ms : 0u;
    int32_t mix = PV(P_BMIX), x;
    spr_sound(MEL_SET_NAME[snd], 64, ly ? 130 + (100 - mix) * 80 / 100 : 210, t, ui_lv);
    tx_c(64, 70, &FONT_S, MEL_SET_NAME[snd], C_INK, C_BODY);
    if (ly) {
        spr_sound(MEL_SET_NAME[ly - 1u], 180, 130 + mix * 80 / 100, t + 400u, ui_lv);
        tx_c(180, 70, &FONT_S, MEL_SET_NAME[ly - 1u], C_LAMP, C_BODY);
    } else {
        rrect(146, 16, 68, 40, 6, C_BODY2);
        tx_c(180, 28, &FONT_S, "OFF", C_INK2, C_BODY2);
        tx_c(180, 70, &FONT_S, "SEL: A LAYER", C_INK2, C_BODY);
    }
    tx(8, 88, &FONT_S, "A", C_INK, C_BODY);     /* the MIX: A .. B */
    tx(224, 88, &FONT_S, "B", ly ? C_LAMP : C_INK2, C_BODY);
    rrect(24, 92, 192, 8, 4, C_BODY2);
    x = 24 + mix * 184 / 100;
    if (ly) {
        rrect(24, 92, x - 24 + 8, 8, 4, C_INK2);
        rrect(x, 92, 216 - x, 8, 4, C_OXIDE2);
    }
    disc(x + 4, 96, 6, ly ? C_INK : C_INK2);
}

static uint32_t reel_a, counter;                /* the cassette: its reels' angle, its counter */
static void hub(int32_t cx, int32_t cy, int32_t pack, uint32_t a)
{
    int32_t k;
    disc(cx, cy, pack, C_OXIDE);
    disc(cx, cy, 6, RGB(236, 234, 228));
    for (k = 0; k < 3; k++) {
        uint32_t ph = a + (uint32_t)k * 0x55555555u;
        thick(cx, cy, cx + sn(ph, 5), cy + cs(ph, 5), 2, RGB(70, 66, 62));
    }
    disc(cx, cy, 2, RGB(70, 66, 62));
}
static void stage_tape(void)                    /* TAPE: a cassette, its reels turning as the tapes run (wow), the
                                                 * tape quivering (flutter) */
{
    char s[4];
    int32_t y, x, pk = 6 + (int32_t)(counter % 1000u) * 7 / 1000;
    int32_t fl = ui_lv && !by_lofi ? fxv_flut : 0;   /* the flutter: the tape quivers, the reels tug */
    int32_t bow = fl / 250 > 4 ? 4 : fl / 250 < -4 ? -4 : fl / 250;
    uint32_t tug = (uint32_t)(fl * 100000);
    rrect(28, 4, 184, 100, 8, RGB(56, 52, 50));
    disc(34, 10, 2, RGB(120, 118, 116));
    disc(205, 10, 2, RGB(120, 118, 116));
    rrect(36, 10, 168, 60, 4, C_BODY);          /* the label */
    cv_rect(36, 14, 168, 4, C_LAMP);
    cv_rect(36, 18, 168, 2, C_RED);
    tx(42, 21, &FONT_S, "MEL C-60", C_INK, C_BODY);
    tx(116, 21, &FONT_S, PV(P_LEN) ? "8S" : "INF", C_INK2, C_BODY);
    s[0] = (char)('0' + counter / 100u % 10u);  /* the counter */
    s[1] = (char)('0' + counter / 10u % 10u);
    s[2] = (char)('0' + counter % 10u);
    s[3] = 0;
    rrect(170, 21, 28, 16, 2, C_INK);
    tx(172, 21, &FONT_S, s, C_LAMP, C_INK);
    rrect(66, 38, 108, 28, 8, RGB(34, 30, 30)); /* the window */
    hub(92, 52, 13 - pk, reel_a + tug);
    hub(148, 52, pk, reel_a * 13u / 10u + tug);
    for (x = 0; x < 80; x++)                    /* the tape between them, bowing like a plucked string */
        cv_rect(80 + x, 63 + sn((uint32_t)x * 26843545u, bow), 1, 2, C_OXIDE);
    for (y = 76; y < 104; y++) {                /* the bottom, where the heads go in */
        int32_t i = (y - 76) * 12 / 28;
        cv_rect(60 + i, y, 120 - 2 * i, 1, RGB(40, 38, 36));
    }
    cv_rect(84, 92, 8, 10, RGB(20, 20, 20));
    cv_rect(116, 90, 8, 12, RGB(20, 20, 20));
    cv_rect(148, 92, 8, 10, RGB(20, 20, 20));
}

static void stage_wear(void)                    /* WEAR: a phrase of wave, the master and the worn copy */
{
    static const char *const GEN[5] = {"MASTER TAPE", "2ND GENERATION", "3RD GENERATION", "4TH GENERATION",
                                       "COPY OF A COPY"};
    int32_t raw[224], x, py = 0, acc = 0, w = 1 + PV(P_AGE) / 9, hold_n = 1 + PV(P_CRUSH) / 8;
    int32_t q = 1 << (PV(P_CRUSH) / 25), yv = 0, age = PV(P_AGE);
    uint32_t t = ui_lv ? fm1_ms : 0u;
    for (x = 0; x < 224; x++) {
        uint32_t ph = (uint32_t)x * 0x03400000u + t * 0x00200000u
                    + (uint32_t)(sn((uint32_t)x * 0x00C00000u + t * 0x00080000u, PV(P_DRIFT) / 3) << 24);   /* DRIFT */
        int32_t v = sn(ph, 20) + sn(ph * 3u + 0x1000000u, 7), gap = ((uint32_t)(x / 16) * 2654435761u + t / 120u * 40503u) >> 25;
        cv_pset(8 + x, 44 - sn((uint32_t)x * 0x03400000u + t * 0x00200000u, 20) - sn(((uint32_t)x * 0x03400000u + t * 0x00200000u) * 3u + 0x1000000u, 7), C_BODY2);
        if ((int32_t)gap < PV(P_DROP) * 128 / 200)   /* DROPOUT */
            v /= 5;
        raw[x] = v;
    }
    for (x = 0; x < 224; x++) {                 /* AGE: the edges go soft; CRUSH: held and stepped */
        int32_t y;
        acc += raw[x] - (x >= w ? raw[x - w] : 0);
        if (x % hold_n == 0) yv = acc / (x + 1 < w ? x + 1 : w) / q * q;
        y = 44 - yv;
        if (x) thick(8 + x - 1, py, 8 + x, y, 2, C_OXIDE2);
        py = y;
    }
    tx_c(120, 86, &FONT_S, GEN[age == 0 ? 0 : age < 30 ? 1 : age < 60 ? 2 : age < 85 ? 3 : 4], C_INK2, C_BODY);
}

static void stage_vib(void)                     /* VIBRATO: its wave; how much of it is in the sound */
{
    int32_t x, py = 0, a = 4 + PV(P_VDEPTH) * 22 / 100, mix = PV(P_VMIX);
    uint32_t inc = MEL_LFO[PV(P_VRATE)] * 300u, ph0 = fxv_vib;   /* (as many waves across as Hz, about) */
    uint16_t c = mix565(C_BODY2, C_OXIDE2, (uint32_t)(4 + mix * 12 / 100));
    for (x = 0; x < 224; x++) {
        int32_t y = 40 - sn(ph0 - (uint32_t)x * inc, a);
        if (x) thick(8 + x - 1, py, 8 + x, y, 2, c);
        py = y;
    }
    tx_c(120, 70, &FONT_S, !mix ? "OFF" : mix < 70 ? "CHORUS" : "VIBRATO", mix ? C_INK : C_INK2, C_BODY);
    tx(8, 88, &FONT_S, "DRY", C_INK2, C_BODY);
    tx(208, 88, &FONT_S, "WET", C_INK2, C_BODY);
    rrect(40, 92, 160, 8, 4, C_BODY2);
    if (mix) rrect(40, 92, 8 + mix * 152 / 100, 8, 4, C_OXIDE2);
}

static void stage_les(void)                     /* LESLIE: the cabinet, the horn and the drum turning, the mics */
{
    static const char *const SP[3] = {"STOP", "SLOW", "FAST"};
    int32_t k, sp = PV(P_LSPREAD), mx = 46 + sp * 20 / 100;
    uint32_t ph = fxv_horn;
    rrect(84, 4, 72, 100, 5, RGB(122, 74, 40));
    for (k = 0; k < 6; k++)
        cv_rect(90, 10 + k * 6, 60, 2, RGB(86, 50, 26));
    thick(120 - cs(ph, 24), 30 - sn(ph, 5), 120 + cs(ph, 24), 30 + sn(ph, 5), 3, RGB(30, 28, 26));   /* the horn */
    disc(120 + cs(ph, 24), 30 + sn(ph, 5), 5, RGB(30, 28, 26));
    disc(120, 76, 18, RGB(40, 36, 34));         /* the drum */
    disc(120 + cs(fxv_drum, 11), 76 + sn(fxv_drum, 3), 4, RGB(190, 180, 160));
    for (k = -1; k <= 1; k += 2) {              /* the microphones, SPREAD apart */
        disc(120 + k * mx, 40, 4, C_INK);
        cv_rect(120 + k * mx - 1, 44, 3, 14, C_INK2);
    }
    tx_c(30, 82, &FONT_S, SP[PV(P_LSPEED)], PV(P_LMIX) ? C_LAMP : C_INK2, C_BODY);
    tx_c(210, 82, &FONT_S, PV(P_LMIX) ? "ON" : "OFF", PV(P_LMIX) ? C_INK : C_INK2, C_BODY);
}

static void stage_echo(void)                    /* ECHO: the tape loop and its heads; the repeats, fading */
{
    int32_t k, ms = echo_ms(PV(P_ETIME)), fb = PV(P_EFB), gap = 6 + ms * 30 / 1000, h = 30, mix = PV(P_EMIX);
    rrect(40, 4, 160, 50, 22, C_OXIDE);         /* the loop */
    rrect(45, 9, 150, 40, 18, C_BODY);
    for (k = 0; k < 3; k++)                     /* record, play, play */
        rrect(92 + k * 22, 48, 12, 10, 2, RGB(150, 152, 156));
    if (mix) {
        uint32_t a = fm1_ms * (uint32_t)(140000 / ms) * 4096u;
        disc(120 + cs(a, 74), 29 + sn(a, 22), 3, C_LAMP);
    }
    for (k = 0; k < 14 && h > 1; k++) {         /* the repeats: one every TIME, each REPEATS weaker */
        int32_t x = 20 + k * gap;
        if (x > 228) break;
        rrect(x, 104 - h, 5, h, 2, k ? mix565(C_BODY2, C_OXIDE2, (uint32_t)(4 + h * 12 / 30)) : C_INK);
        h = h * (20 + fb * 75 / 100) / 100;
    }
}

static void stage_verb(void)                    /* REVERB: the pre-delay, then the reflections dying away */
{
    int32_t x, x0 = 12 + PV(P_RPRE) * 40 / 100, len = 40 + PV(P_RSIZE) * 170 / 100, top = 20 + PV(P_RMIX) * 60 / 100;
    uint16_t c = mix565(C_OXIDE, C_LAMP, (uint32_t)(PV(P_RTONE) * 16 / 100));
    uint32_t r = 12345u + (ui_lv ? fm1_ms / 60u : 0u);
    rrect(8, 100 - 70, 3, 70, 1, C_INK);        /* the sound */
    for (x = x0; x < x0 + len && x < 232; x += 3) {
        int32_t e = top * (x0 + len - x) / len, h;
        r = r * 1103515245u + 12345u;
        h = e * (int32_t)(40u + (r >> 25)) / 168;
        if (h > 0) cv_rect(x, 100 - h, 2, h, c);
    }
    cv_rect(8, 100, 224, 1, C_INK2);
}

static void stage_eq(void)                      /* EQ: the curve: two shelves and the bell at FREQ */
{
    int32_t x, py = 0, k, lo = PV(P_EQLO), mid = PV(P_EQMID), hi = PV(P_EQHI), fx = 8 + 61 + PV(P_EQFREQ) * 113 / 15;
    char s[12];
    for (k = -12; k <= 12; k += 6)
        cv_rect(8, 44 - k * 3, 224, 1, k ? C_BODY2 : C_INK2);
    for (x = 8; x < 232; x++) {                 /* (log frequency across: 20 Hz .. 20 kHz) */
        int32_t wl = x <= 55 ? 256 : x >= 95 ? 0 : (95 - x) * 256 / 40;
        int32_t wh = x <= 167 ? 0 : x >= 207 ? 256 : (x - 167) * 256 / 40;
        int32_t d = x - fx, wb = d * d < 1600 ? 256 - d * d * 256 / 1600 : 0;
        int32_t y = 44 - ((lo * wl + hi * wh + mid * wb) * 3 >> 8);
        if (x > 8) thick(x - 1, py, x, y, 2, C_OXIDE2);
        py = y;
    }
    disc(fx, 44 - mid * 3, 3, C_LAMP);
    fmt_val(&CTL[PG_EQ][2], PV(P_EQFREQ), s);
    tx_c(fx < 40 ? 40 : fx > 200 ? 200 : fx, 86, &FONT_S, s, C_INK2, C_BODY);
}

static void stage_setup(void)                   /* SETUP: the four settings, and what the one you touch does */
{
    static const char *const NAME[4] = {"TEMPO", "TUNE", "LENGTH", "SCREEN"};
    static const char *const HELP[7][2] = {
        {"Settings for the whole MEL.", "Turn a knob: it says here."},
        {"ARP and SEQ follow it.", "ALGORITHM sets it anywhere."},
        {"Fine tuning, in cents, to", "play along with others."},
        {"A note sounds as long as", "you hold it (the tape loops)."},
        {"As on a real M400: a tape", "runs out after 8 seconds."},
        {"A clean, steady picture.", ""},
        {"The screen as an old tape:", "scanlines, sway, tears, grain."}};
    char s[24], *p;
    int32_t k, tune = PV(P_TUNE), hz10, h = touched > 3u ? 0 : touched == 0u ? 1 : touched == 1u ? 2
                                                    : touched == 2u ? 3 + PV(P_LEN) : 5 + PV(P_LOOK);
    uint32_t beat = 60000u / (uint32_t)PV(P_BPM), ph = (fm1_ms % (2u * beat)) * 0x80000000u / beat;
    hz10 = tune >= 0 ? (int32_t)(((MEL_POW2[tune] >> 14) * 4400u) >> 16)
                     : (int32_t)(((MEL_POW2[1200 + tune] >> 14) * 4400u) >> 17);
    for (k = 0; k < 4; k++) {
        int32_t y = 2 + k * 17;
        switch (k) {
        case 0: put(put_int(s, PV(P_BPM), 0), " BPM"); break;
        case 1: p = put_int(put(s, "A4 = "), hz10 / 10, 0); *p++ = '.'; *p++ = (char)('0' + hz10 % 10); put(p, " Hz"); break;
        case 2: put(s, PV(P_LEN) ? "8 SECONDS" : "AS HELD"); break;
        default: put(s, PV(P_LOOK) ? "OLD TAPE" : "CLEAN"); break;
        }
        if ((uint32_t)k == touched) rrect(2, y + 4, 6, 8, 3, C_LAMP);
        tx(12, y, &FONT_S, NAME[k], C_INK2, C_BODY);
        tx(72, y, &FONT_S, s, C_INK, C_BODY);
        tx(73, y, &FONT_S, s, C_INK, C_BODY);
    }
    cv_rect(8, 72, 224, 1, C_BODY2);
    tx(12, 75, &FONT_S, HELP[h][0], C_INK, C_BODY);
    tx(12, 91, &FONT_S, HELP[h][1], C_INK, C_BODY);
    rrect(196, 4, 36, 64, 5, RGB(122, 74, 40)); /* the metronome, at the tempo */
    {
        int32_t sw = sn(ph, 12);
        thick(214, 62, 214 + sw, 14, 2, C_INK);
        disc(214 + sw * 3 / 4, 26, 3, C_LAMP);
    }
}

static void stage_arp(void)                     /* ARP: the mode, the arpeggio (the note sounding lit) */
{
    uint32_t i, n = arp_n, lo = 127;
    tx_c(120, 4, &FONT_L, ARP_MODE[PV(P_AMODE)], arp_on ? C_INK : C_INK2, C_BODY);
    if (!n) {
        tx_c(120, 66, &FONT_S, arp_on ? "PLAY A CHORD" : "ARP IS OFF: PRESS ARP", C_INK2, C_BODY);
        return;
    }
    if (n > 24u) n = 24u;
    for (i = 0; i < n; i++)
        if (arp_list[i] < lo) lo = arp_list[i];
    for (i = 0; i < n; i++) {
        int32_t x = 120 - (int32_t)n * 5 + (int32_t)i * 10, h = 6 + (int32_t)(arp_list[i] - lo) * 2;
        if (h > 56) h = 56;
        rrect(x, 102 - h, 8, h, 2, i == arp_pos_shown ? C_LAMP : C_OXIDE);
    }
}

static void stage_seq(void)                     /* SEQ: the 16 steps, the one playing lit, the cursor under */
{
    uint32_t i, k, len = (uint32_t)PV(P_SLEN);
    char s[40];
    for (i = 0; i < 16u; i++) {
        int32_t x = 4 + (int32_t)(i % 8u) * 29, y = 4 + (int32_t)(i / 8u) * 42;
        int on = i < len, cur = seq_play && i == seq_step, cursor = i == seq_cur;
        rrect(x, y, 27, 38, 4, !on ? C_BODY : cur ? C_LAMP : C_BODY2);
        if (cursor)
            cv_rect(x, y + 36, 27, 2, seq_rec ? C_RED : C_INK);
        for (k = 0; k < 4u; k++) {
            uint32_t nt = set.steps[i][k];
            if (nt) {
                int32_t ny = y + 30 - (int32_t)(((nt + 128u - 36u) % 128u) * 26u / 60u);
                ny = ny < y + 2 ? y + 2 : ny > y + 30 ? y + 30 : ny;
                rrect(x + 3 + (int32_t)k * 6, ny, 5, 5, 2, on ? (cur ? C_BODY : C_OXIDE) : C_BODY2);
            }
        }
        if (on && !(i % 4u))
            cv_rect(x + 12, y - 2, 3, 1, C_INK2);
    }
    put(s, seq_rec ? (seq_play ? "PLAYING  REC" : "STEP REC") : seq_play ? "PLAYING" : "STOPPED");
    tx_c(120, 90, &FONT_S, s, seq_rec ? C_RED : C_INK2, C_BODY);
}

/* ---- the picture as an old tape plays it: scanlines; the wow sways it, a dropout tears a band, AGE is grain */
static uint32_t ui_rnd = 12345u;
static uint32_t urnd(void) { ui_rnd ^= ui_rnd << 13; ui_rnd ^= ui_rnd >> 17; ui_rnd ^= ui_rnd << 5; return ui_rnd; }
static uint16_t dim(uint16_t sw)               /* a stored (byte-swapped) pixel, an eighth darker */
{
    uint16_t c = swap16(sw);
    c = (uint16_t)(c - ((c >> 3) & 0x18E3u));
    return swap16(c);
}
static void tape_look(uint32_t h)   /* (the wow sways it slowly, the flutter shakes it) */
{
    static uint16_t row[240];
    int32_t y, x, amp = ui_lv ? PV(P_WOW) * 3 / 100 : 0, off = ui_lv ? fxv_wow / 160 : 0, drop = fxv_drop;
    int32_t jit = ui_lv ? (fxv_flut + (fxv_flut < 0 ? -150 : 150)) / 300 : 0;   /* the flutter: a nervous shake */
    uint32_t i;
    if (!PV(P_LOOK)) return;
    if (by_lofi) amp = off = drop = jit = 0;   /* (LOFI out: no sway, no tears, no grain: a clean tape) */
    off = off > 4 ? 4 : off < -4 ? -4 : off;
    off += jit > 3 ? 3 : jit < -3 ? -3 : jit;
    for (y = 0; y < (int32_t)h; y++) {
        uint16_t *p = &cv_px[(uint32_t)y * 240u];
        int32_t sh = off + (amp ? sn(fm1_ms * 0x00300000u + (uint32_t)y * 0x03000000u, amp) : 0);
        if (sh) {                               /* the sway */
            for (x = 0; x < 240; x++) row[x] = p[(x - sh + 240) % 240];
            for (x = 0; x < 240; x++) p[x] = row[x];
        }
        if (y & 1)                              /* scanlines */
            for (x = 0; x < 240; x++) p[x] = dim(p[x]);
    }
    if (ui_lv && drop > 2000) {                 /* a dropout: the tracking slips */
        int32_t y0 = (int32_t)(urnd() % h), hb = 3 + drop / 3000, s2 = 6 + (int32_t)(urnd() % 14u);
        for (y = y0; y < y0 + hb && y < (int32_t)h; y++) {
            uint16_t *p = &cv_px[(uint32_t)y * 240u];
            for (x = 0; x < 240; x++) row[x] = p[(x + s2) % 240];
            for (x = 0; x < 240; x++) p[x] = (x + y) & 3 ? row[x] : swap16(C_WHITE);
        }
    }
    for (i = 0; !by_lofi && i < (uint32_t)PV(P_AGE) * 3u; i++) {   /* AGE: grain */
        uint32_t at = (urnd() >> 8) % (240u * h);
        cv_px[at] = dim(dim(cv_px[at]));
    }
}

static int32_t lv_drawn;                        /* the stage as it was drawn: sounding or not */
static int stage_moves(void)                    /* does the stage need drawing again (~20 fps)? */
{
    if (ui_lv || lv_drawn) return 1;
    switch (page) {
    case PG_VIB: case PG_SETUP: return 1;
    case PG_LES: return PV(P_LSPEED) != 0;
    case PG_ECHO: return PV(P_EMIX) != 0;
    default: return 0;
    }
}
static void ui_motion(void)                     /* the cassette's reels and counter run with the tapes (and the wow) */
{
    static uint32_t t_reel, t_cnt;
    uint32_t dt = fm1_ms - t_reel;
    t_reel = fm1_ms;
    if (ui_lv) {
        int32_t sp = 40 + ui_lv / 4 + fxv_wow / 64;
        reel_a += (uint32_t)(sp > 0 ? sp : 0) * dt * 0x00080000u;
        if (fm1_ms - t_cnt >= 150u) { t_cnt = fm1_ms; counter = (counter + 1u) % 1000u; }
    }
}
static void draw_stage(void)
{
    static void (*const STAGE[NPG])(void) = {stage_play, stage_layer, stage_tape, stage_wear, stage_vib, stage_les, stage_echo,
                                             stage_verb, stage_eq, stage_setup, stage_arp, stage_seq};
    cv_begin(240, STAGE_H, C_BODY);
    STAGE[page]();
    if ((PG_GROUP[page] == G_LOFI && by_lofi) || (PG_GROUP[page] == G_FX && by_fx)) {   /* this section is out */
        rrect(40, 40, 160, 24, 6, C_INK);
        tx_c(120, 44, &FONT_S, PG_GROUP[page] == G_FX ? "OUT - HOLD FX" : "OUT - HOLD LFO", C_BODY, C_INK);
    }
    tape_look(STAGE_H);
    cv_blit(0, STAGE_Y);
    lv_drawn = ui_lv;
}

/* ---- the tape frame: one strip per key; the oxide runs past the head while it sounds */
static uint32_t tapes_sig = 1;
static void draw_tapes(uint32_t base, int force)
{
    uint8_t hgt[27], on[27], ph[27];
    uint32_t k, v, sig = 2166136261u;
    for (k = 0; k < 27u; k++) {
        uint32_t note = base + k, a = 0, t = note < 128u ? tpos[note] : 0;
        for (v = 0; v < NV; v++)
            if (voice[v].busy && voice[v].note == note && (uint32_t)voice[v].amp > a)
                a = (uint32_t)voice[v].amp;
        on[k] = a > 0u;
        hgt[k] = (uint8_t)(PV(P_LEN) ? (TAPE_LEN - (t > TAPE_LEN ? TAPE_LEN : t)) * 22u / TAPE_LEN : 22u);
        ph[k] = (uint8_t)(on[k] ? (t >> 9) & 7u : 0u);
        sig = (sig ^ hgt[k] ^ (uint32_t)on[k] << 7 ^ (uint32_t)ph[k] << 9) * 16777619u;
    }
    if (sig == tapes_sig && !force)
        return;
    tapes_sig = sig;
    cv_begin(240, 32, C_FRAME);
    for (k = 0; k < 27u; k++) {
        int32_t x = 12 + (int32_t)k * 8, y;
        cv_rect(x, 2, 7, 22, RGB(48, 44, 42));
        cv_rect(x, 24 - hgt[k], 7, hgt[k], on[k] ? C_OXIDE2 : C_OXIDE);
        if (on[k])                              /* the tape moving */
            for (y = 24 - hgt[k] + ph[k]; y < 24; y += 8)
                cv_rect(x, y, 7, 2, C_OXIDE);
        cv_rect(x, 26, 7, 4, black(base + k) ? RGB(20, 20, 22) : RGB(232, 230, 224));
    }
    cv_blit(0, TAPES_Y);
}

/* ---- the plate: brushed aluminium; four black knobs, each with its name and its value */
static void knob_gfx(int32_t cx, int32_t cy, int32_t p256, int lit)   /* p256 0..256 = -135 .. +135 degrees */
{
    uint32_t ph = (uint32_t)((int32_t)0xA0000000u + p256 * (int32_t)0x00C00000);
    disc(cx, cy + 1, 13, RGB(120, 122, 126));
    disc(cx, cy, 13, lit ? C_LAMP : C_SKIRT);
    disc(cx, cy, 10, C_KNOB);
    thick(cx + sn(ph, 3), cy - cs(ph, 3), cx + sn(ph, 9), cy - cs(ph, 9), 2, RGB(244, 244, 240));
}
static void draw_plate(void)                    /* knobs on brushed metal; under each, its name and its value */
{
    uint32_t k;
    int32_t y;
    cv_begin(240, 76, RGB(212, 213, 214));
    for (y = 0; y < 38; y++) {                  /* the brushed part, behind the knobs */
        uint32_t h = ((uint32_t)y * 2654435761u) >> 28;
        cv_rect(0, y, 240, 1, mix565(C_ALU, h & 1u ? C_WHITE : RGB(120, 122, 126), h >> 1));
    }
    cv_rect(0, 0, 240, 1, C_WHITE);
    cv_rect(0, 38, 240, 1, RGB(150, 152, 156));
    for (k = 0; k < 4u; k++) {
        const ctl_t *c = &CTL[page][k];
        char v[16];
        int32_t cx = 30 + (int32_t)k * 60, w;
        int lit = touched == k;
        fmt_val(c, PV(c->pid), v);
        knob_gfx(cx, 19, c->pid == P_STEP ? (int32_t)seq_cur * 256 / 15 : pos256(c->pid), lit);
        w = text_w(&FONT_S, c->name);           /* the name, printed black and bold on the plate */
        tx(cx - w / 2, 39, &FONT_S, c->name, C_INK, RGB(212, 213, 214));
        tx(cx - w / 2 + 1, 39, &FONT_S, c->name, C_INK, RGB(212, 213, 214));
        rrect(cx - 28, 56, 56, 18, 4, lit ? C_LAMP : C_INK);   /* the value, on a little dark window */
        w = text_w(&FONT_S, v);
        tx(cx - w / 2, 57, &FONT_S, v, lit ? C_INK : C_BODY, lit ? C_LAMP : C_INK);
        tx(cx - w / 2 + 1, 57, &FONT_S, v, lit ? C_INK : C_BODY, lit ? C_LAMP : C_INK);
    }
    cv_blit(0, PLATE_Y);
}
