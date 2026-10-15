/* SPDX-License-Identifier: GPL-3.0-only */
/* MEL's screen: an M400 (the white lacquered body, the brushed aluminium plate, black knobs, the brown oxide of
 * the tapes) played back from an old tape (scanlines; the picture sways with the wow, tears on a dropout, gets
 * grainy with AGE; SETUP > LOOK turns that off). Included by mel.c; main loop only.
 *
 *   0 ..  23   the body's top: the reel (turning while tapes run), the section and its page, the lamps that are on
 *  24 .. 147   the stage: what the page is about, drawn and moving with the sound
 * 148 .. 179   the keyboard: the keys lit while they sound (with LENGTH 8 S, draining as their tapes run out)
 * 180 .. 239   the plate: the four knobs' names, their values and where they stand */

#define C_BODY RGB(238, 233, 220)               /* white lacquer */
#define C_BODY2 RGB(222, 214, 196)              /* its shade */
#define C_INK RGB(38, 34, 30)                   /* print on the body and the plate */
#define C_INK2 RGB(100, 92, 82)
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
#define STAGE_H 124                             /* (the pages are drawn for 108, centred in it) */
#define TAPES_Y 148
#define PLATE_Y 180

/* ---- drawing on the canvas, with the background known (text blended onto it, not onto black) */
static uint16_t mix565(uint32_t a, uint32_t b, uint32_t t)   /* a .. b, t 0..16 */
{
    uint32_t r = ((a >> 11) * (16u - t) + (b >> 11) * t) >> 4;
    uint32_t g = (((a >> 5) & 63u) * (16u - t) + ((b >> 5) & 63u) * t) >> 4;
    uint32_t bl = ((a & 31u) * (16u - t) + (b & 31u) * t) >> 4;
    return (uint16_t)(r << 11 | g << 5 | bl);
}
static int32_t tx(int32_t x, int32_t y, const mel_font_t *f, const char *s, uint16_t fg)   /* (mel_fonts.h) */
{
    uint32_t bpr = (f->w + 7u) / 8u, gx, gy;
    for (; *s; s++, x += f->w) {
        uint32_t ch = (uint8_t)*s;
        const uint8_t *gd;
        if (ch >= 'a' && ch <= 'z' && f->last < 'a') ch -= 32u;   /* (the big ones have no lower case) */
        if (ch < f->first || ch > f->last) ch = '?';
        gd = f->data + (ch - f->first) * bpr * f->h;
        for (gy = 0; gy < f->h; gy++)
            for (gx = 0; gx < f->w; gx++)
                if (gd[gy * bpr + gx / 8u] >> (7u - gx % 8u) & 1u)
                    cv_pset(x + (int32_t)gx, y + (int32_t)gy, fg);
    }
    return x;
}
static int32_t tw(const mel_font_t *f, const char *s)
{
    int32_t n = 0;
    while (s[n]) n++;
    return n * f->w;
}
static void tx_c(int32_t cx, int32_t y, const mel_font_t *f, const char *s, uint16_t fg)
{
    tx(cx - tw(f, s) / 2, y, f, s, fg);
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
static void spr_unpack(const sprite_t *sp, uint32_t f, uint8_t *px)
{
    const uint8_t *r = sp->frame[f];
    uint32_t i = 0, n, end = (uint32_t)sp->w * sp->h;
    for (; i < end; r += 2)
        for (n = r[0]; n--; ) px[i++] = r[1];
}
static const uint8_t *spr_frame(const sprite_t *sp, uint32_t f)   /* a frame unpacked (the last one kept) */
{
    static uint8_t px[SPR_MAXPX] __attribute__((section(".pool")));
    static const sprite_t *had;
    static uint32_t had_f;
    if (had != sp || had_f != f) {
        spr_unpack(sp, f, px);
        had = sp;
        had_f = f;
    }
    return px;
}
static void spr_draw(const sprite_t *sp, const uint8_t *px, int32_t cx, int32_t cy, int32_t s, uint32_t parts)
{                                               /* unpacked pixels of a sprite, its loose parts up to parts */
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
                    uint32_t v = px[sy * sp->w + sx];
                    if ((v & 15u) && (v >> 4) <= parts && (!c || (v & 15u) == 1u)) c = v & 15u;
                }
            if (c) cv_pset(x0 + dx, y0 + dy, sp->pal[c]);
        }
    }
}
static void spr_pix(const sprite_t *sp, uint32_t f, int32_t cx, int32_t cy, int32_t s, uint32_t parts)   /* a drawn
                                                       * sprite (mel_sprites.h): frame f, its loose parts up to parts */
{
    spr_draw(sp, spr_frame(sp, f), cx, cy, s, parts);
}
static void spr_choir(int32_t cx, int32_t cy, int32_t s, uint32_t t, int32_t lv)   /* a mouse, a magpie and a fox
                                                                                     * (drawn), their AAAAA coming */
{
    uint32_t ph = t % 800u;                     /* three frames, round and round: the dots, the small As, all (held) */
    if (!lv) spr_pix(&SPR_CHOIR, 0, cx, cy, s, 0);   /* silent: mouths shut, no AAAAA */
    else spr_pix(&SPR_CHOIR, 1, cx, cy, s, ph < 200u ? 4u : ph < 400u ? 7u : 10u);
}
static void spr_note(const sprite_t *sp, int32_t cx, int32_t cy, int32_t s, int32_t nx, int32_t ny)   /* a quaver
                                                       * in a sprite's pixels (nx, ny its top left), its outline colour */
{
    static const uint8_t Q[9] = {0x08, 0x0C, 0x0A, 0x09, 0x08, 0x18, 0x38, 0x38, 0x10};   /* 6 wide, bit 5 left */
    int32_t x0 = cx - (sp->w * s >> 8) / 2, y0 = cy - (sp->h * s >> 8) / 2, i, j;
    for (j = 0; j < 9; j++)
        for (i = 0; i < 6; i++)
            if (Q[j] >> (5 - i) & 1u) {
                int32_t x = (nx + i) * s >> 8, y = (ny + j) * s >> 8;
                int32_t w = ((nx + i + 1) * s >> 8) - x, h = ((ny + j + 1) * s >> 8) - y;
                cv_rect(x0 + x, y0 + y, w > 0 ? w : 1, h > 0 ? h : 1, sp->pal[1]);
            }
}
static void spr_flute(int32_t cx, int32_t cy, int32_t s, uint32_t t, int32_t lv)   /* a blackbird with a flute (drawn),
                                                                                     * notes coming off its end */
{
    static const int8_t NOTES[2][3][2] = {{{12, 22}, {24, 10}, {-99, 0}}, {{4, 26}, {16, 14}, {28, 2}}};
    uint32_t ph = t % 750u, k, f = !lv ? 0u : ph >= 500u ? 2u : 1u;   /* frame 1, notes; frame 1, other notes; frame 2 */
    spr_pix(&SPR_FLUTE, f, cx, cy, s, 15);    /* (frame 0: silent) */
    if (f == 1u)
        for (k = 0; k < 3u; k++)
            if (NOTES[ph / 250u][k][0] > -99)
                spr_note(&SPR_FLUTE, cx, cy, s, NOTES[ph / 250u][k][0], NOTES[ph / 250u][k][1]);
}
static void spr_cello(int32_t cx, int32_t cy, int32_t s, uint32_t t, int32_t lv)   /* a raccoon at the cello
                                                                                     * (drawn), bowing */
{
    spr_pix(&SPR_CELLO, lv ? 1u + t / 250u % 3u : 0u, cx, cy, s, 15);   /* (silent: the bow up; 250 ms a frame) */
}
static void spr_vibes(int32_t cx, int32_t cy, int32_t s, uint32_t t, int32_t lv)   /* a rabbit at the vibraphone
                                                                                     * (drawn), its mallets going */
{
    static const uint8_t F[4] = {2, 1, 3, 1};   /* playing: one mallet, both down, the other, both down (150 ms each) */
    spr_pix(&SPR_VIBES, lv ? F[t / 150u % 4u] : 0u, cx, cy, s, 15);   /* (silent: the mallets up) */
}
static void spr_piano(int32_t cx, int32_t cy, int32_t s, uint32_t t, int32_t lv)   /* an upright piano (a stand-in
                                                                                     * till it is drawn), notes on its stand */
{
    static const int8_t NOTES[2][2][2] = {{{25, 23}, {45, 25}}, {{35, 25}, {55, 23}}};
    uint32_t ph = t % 600u, k, f = !lv ? 0u : 1u + ph / 300u;   /* (frame 0: silent; 1, 2: other keys down) */
    spr_pix(&SPR_PIANO, f, cx, cy, s, 15);
    if (f)
        for (k = 0; k < 2u; k++)
            spr_note(&SPR_PIANO, cx, cy, s, NOTES[f - 1u][k][0], NOTES[f - 1u][k][1]);
}
#undef S
static void spr_sound(const char *nm, int32_t cx, int32_t cy, int32_t sc, uint32_t t, int32_t lv)   /* a sound's
                                                         * drawing, by its name, centred on cx cy, sc/256 of its size */
{
    if (nm[0] == 'F') spr_flute(cx, cy, sc, t, lv);
    else if (nm[0] == 'C' && nm[1] == 'H') spr_choir(cx, cy, sc, t, lv);
    else if (nm[0] == 'V' && nm[2] == 'B') spr_vibes(cx, cy, sc, t, lv);
    else if (nm[0] == 'V') spr_violins(cx, cy + 10, 230 * sc >> 8, t, lv);
    else if (nm[0] == 'P') spr_piano(cx, cy, sc, t, lv);
    else spr_cello(cx, cy, sc, t, lv);
}

/* ---- the body's top */
static int32_t ui_lv;                           /* how much sounds, 0..256, smoothed */
static int32_t spr_lv(void)                     /* for the drawings: ui_lv, or the ARP has notes (they go on playing
                                                 * through its gaps) */
{
    return ui_lv ? ui_lv : arp_on && arp_n ? 1 : 0;
}
static uint32_t spr_t(void)                     /* the drawings' clock, ms: it runs with the motor (HALF SPEED: half as
                                                 * fast, slowing down and speeding up with the tapes) */
{
    static uint32_t t_last, acc;                /* acc: ms, Q8 */
    uint32_t dt = fm1_ms - t_last;
    t_last = fm1_ms;
    acc += dt * (uint32_t)(256 + motor16 * 128 / 19200);
    return acc >> 8;
}
static void lamp(int32_t *x, const char *s, uint16_t c, int on)   /* (only the lamps that are on) */
{
    int32_t w = tw(&F16, s) + 6;
    if (!on) return;
    *x -= w;
    rrect(*x, 4, w, 16, 4, c);
    tx(*x + 3, 4, &F16, s, RGB(255, 252, 244));
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
    xe = tx(26, 2, &F20, G_NAME[g], C_INK);
    if (t_msg)
        tx(xe + 8, 2, &F20, msg, C_RED);
    else {
        if (G_COUNT[g] > 1u) {                  /* the page, and where it is among the section's */
            xe = tx(xe + 8, 5, &F16, PG_NAME[page], C_INK2) + 5;
            for (k = 0; k < G_COUNT[g]; k++)
                disc(xe + (int32_t)k * 7, 12, 2, G_FIRST[g] + k == page ? C_LAMP : C_BODY2);
        }
        lamp(&x, "REC", C_RED, seq_rec);
        lamp(&x, "SEQ", C_GREEN, seq_play);
        lamp(&x, "ARP", C_LAMP, arp_on);
        lamp(&x, "HOLD", C_LAMP, hold);
        lamp(&x, "HALF", C_LAMP, set.half);
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
static void stage_play(void)                    /* the sound (and the layer), the track selector */
{
    uint32_t snd = (uint32_t)PV(P_SOUND), ly = (uint32_t)PV(P_BSND), k, t = spr_lv() ? spr_t() : 0u;
    if (ly) {                                   /* (two fit beside the selector at their size; one, half as big again) */
        spr_sound(MEL_SET_NAME[snd], 104, 50, 256, t, spr_lv());
        spr_sound(MEL_SET_NAME[ly - 1u], 192, 50, 256, t + 400u, spr_lv());
        tx_c(34, 100, &F16, MEL_SET_NAME[ly - 1u], C_LAMP);
    } else
        spr_sound(MEL_SET_NAME[snd], 150, 54, 384, t, spr_lv());
    tx_c(34, 86, &F16, MEL_SET_NAME[snd], C_INK);   /* the names, small, under the selector */
    for (k = 0; k < MEL_NSETS && k < 6u; k++) { /* A B C D E round the selector: the sound's needle, the layer's amber */
        uint32_t ph = sel_angle(k);
        char l[2] = {(char)('A' + k), 0};
        tx_c(34 + sn(ph, 26), 44 - cs(ph, 26) - 8, &F16, l, k == snd ? C_INK : ly == k + 1u ? C_LAMP : C_INK2);
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
    tx_c(34, 68, &F16, "TRACK", C_INK2);
}

static void stage_layer(void)                   /* LAYER: the sound and the layer, the MIX between them */
{
    uint32_t snd = (uint32_t)PV(P_SOUND), ly = (uint32_t)PV(P_BSND), t = spr_lv() ? spr_t() : 0u;
    int32_t mix = PV(P_BMIX), x;
    spr_sound(MEL_SET_NAME[snd], 62, 38, 320, t, spr_lv());   /* (whole, a quarter bigger: the MIX is the slider) */
    tx_c(62, 86, &F16, MEL_SET_NAME[snd], C_INK);
    if (ly) {
        spr_sound(MEL_SET_NAME[ly - 1u], 178, 38, 320, t + 400u, spr_lv());
        tx_c(178, 86, &F16, MEL_SET_NAME[ly - 1u], C_LAMP);
    } else {
        rrect(140, 10, 76, 50, 6, C_BODY2);
        tx_c(178, 25, &F20, "OFF", C_INK2);
        tx_c(178, 86, &F16, "SEL: A LAYER", C_INK2);
    }
    tx(8, 100, &F16, "A", C_INK);               /* the MIX: A .. B */
    tx(224, 100, &F16, "B", ly ? C_LAMP : C_INK2);
    rrect(24, 104, 192, 8, 4, C_BODY2);
    x = 24 + mix * 184 / 100;
    if (ly) {
        rrect(24, 104, x - 24 + 8, 8, 4, C_INK2);
        rrect(x, 104, 216 - x, 8, 4, C_OXIDE2);
    }
    disc(x + 4, 108, 6, ly ? C_INK : C_INK2);
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
    tx(42, 21, &F16, "MEL C-60", C_INK);
    tx(116, 21, &F16, PV(P_LEN) ? "8S" : "INF", C_INK2);
    s[0] = (char)('0' + counter / 100u % 10u);  /* the counter */
    s[1] = (char)('0' + counter / 10u % 10u);
    s[2] = (char)('0' + counter % 10u);
    s[3] = 0;
    rrect(170, 21, 28, 16, 2, C_INK);
    tx(172, 21, &F16, s, C_LAMP);
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
    tx_c(120, 86, &F20, GEN[age == 0 ? 0 : age < 30 ? 1 : age < 60 ? 2 : age < 85 ? 3 : 4], C_INK2);
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
    tx_c(120, 70, &F20, !mix ? "OFF" : mix < 70 ? "CHORUS" : "VIBRATO", mix ? C_INK : C_INK2);
    tx(8, 88, &F16, "DRY", C_INK2);
    tx(208, 88, &F16, "WET", C_INK2);
    rrect(40, 92, 160, 8, 4, C_BODY2);
    if (mix) rrect(40, 92, 8 + mix * 152 / 100, 8, 4, C_OXIDE2);
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
    tx_c(fx < 40 ? 40 : fx > 200 ? 200 : fx, 86, &F20, s, C_INK2);
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
        tx(12, y, &F16, NAME[k], C_INK2);
        tx(72, y, &F16, s, C_INK);
    }
    cv_rect(8, 72, 224, 1, C_BODY2);
    tx(12, 75, &F16, HELP[h][0], C_INK);
    tx(12, 91, &F16, HELP[h][1], C_INK);
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
    tx_c(120, 4, &F24, ARP_MODE[PV(P_AMODE)], arp_on ? C_INK : C_INK2);
    if (!n) {
        tx_c(120, 66, &F20, arp_on ? "PLAY A CHORD" : "ARP IS OFF: PRESS ARP", C_INK2);
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
    tx_c(120, 88, &F20, s, seq_rec ? C_RED : C_INK2);
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
    if (spr_lv() || lv_drawn) return 1;
    switch (page) {
    case PG_VIB: case PG_SETUP: return 1;
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
        int32_t sp = (40 + ui_lv / 4 + fxv_wow / 64) >> set.half;   /* (HALF SPEED: half as fast) */
        reel_a += (uint32_t)(sp > 0 ? sp : 0) * dt * 0x00080000u;
        if (fm1_ms - t_cnt >= 150u) { t_cnt = fm1_ms; counter = (counter + 1u) % 1000u; }
    }
}
static void draw_stage(void)
{
    static void (*const STAGE[NPG])(void) = {stage_play, stage_layer, stage_tape, stage_wear, stage_vib, stage_echo, stage_verb,
                                             stage_eq, stage_setup, stage_arp, stage_seq};
    cv_begin(240, STAGE_H, C_BODY);
    cv_oy = (STAGE_H - 108) / 2;
    STAGE[page]();
    if ((PG_GROUP[page] == G_LOFI && by_lofi) || (PG_GROUP[page] == G_FX && by_fx)) {   /* this section is out */
        rrect(40, 40, 160, 24, 6, C_INK);
        tx_c(120, 42, &F20, PG_GROUP[page] == G_FX ? "OUT - HOLD FX" : "OUT - HOLD LFO", C_BODY);
    }
    cv_oy = 0;
    tape_look(STAGE_H);
    cv_blit(0, STAGE_Y);
    lv_drawn = spr_lv();
}

/* ---- the keyboard: the 27 keys, lit while they sound; with LENGTH 8 S the light drains as the key's tape runs out */
static uint32_t tapes_sig = 1;
static void draw_tapes(uint32_t base, int force)
{
    uint8_t lit[27];                            /* how much of the key is lit, 0..24 */
    uint32_t k, v, sig = 2166136261u;
    int32_t wx[27], nw = 0;
    for (k = 0; k < 27u; k++) {
        uint32_t note = base + k, a = 0, t = note < 128u ? tpos[note] : 0;
        for (v = 0; v < NV; v++)
            if (voice[v].busy && voice[v].note == note && (uint32_t)voice[v].amp > a)
                a = (uint32_t)voice[v].amp;
        lit[k] = (uint8_t)(!a ? 0u : PV(P_LEN) ? 1u + (TAPE_LEN - (t > TAPE_LEN ? TAPE_LEN : t)) * 23u / TAPE_LEN : 24u);
        sig = (sig ^ lit[k]) * 16777619u;
    }
    if (sig == tapes_sig && !force)
        return;
    tapes_sig = sig;
    for (k = 0; k < 27u; k++)                   /* where each key is: the white ones side by side */
        wx[k] = black(base + k) ? nw * 240 / 16 : nw++ * 240 / 16;
    cv_begin(240, 32, C_FRAME);
    for (k = 0; k < 27u; k++)                   /* the white keys */
        if (!black(base + k)) {
            int32_t x = wx[k], w = (wx[k] + 240 / 16) - x - 1;
            rrect(x + 1, -4, w, 34, 3, C_BODY);
            cv_rect(x + 1, 2, w, 1, C_BODY2);   /* (the felt's shadow) */
            if (lit[k]) rrect(x + 1, 26 - lit[k] * 28 / 24, w, lit[k] * 28 / 24 + 4, 3, C_LAMP);
        }
    for (k = 0; k < 27u; k++)                   /* the black ones, over them */
        if (black(base + k)) {
            int32_t x = wx[k] - 5;
            cv_rect(x, 0, 10, 19, C_FRAME);
            cv_rect(x + 1, 0, 8, 18, lit[k] ? RGB(150, 70, 20) : C_KNOB);
            if (lit[k]) cv_rect(x + 1, 18 - lit[k] * 18 / 24, 8, lit[k] * 18 / 24, C_LAMP);
            cv_rect(x + 2, 14, 6, 2, lit[k] ? RGB(250, 170, 90) : RGB(70, 70, 76));   /* (its front edge, caught by light) */
        }
    cv_rect(0, 0, 240, 2, C_FRAME);             /* the felt */
    cv_rect(0, 1, 240, 1, RGB(120, 30, 34));
    cv_blit(0, TAPES_Y);
}

/* ---- the plate: aluminium; for each of the four knobs, its name, its value and where it stands */
static void plate_val(int32_t cx, int32_t y, const char *v, uint16_t c)   /* in the window from y down (27 px): the
                                                * number big, its unit small on its baseline; smaller if too wide */
{
    static const mel_font_t *const BIG[3] = {&F24, &F20, &F16};
    static const int8_t NUM_Y[3] = {2, 4, 7}, UNIT_Y[3] = {9, 8, 7};   /* (their capitals centred) */
    char num[16];
    const char *unit = v;
    int32_t n = 0, k, w = 0;
    while (*unit && *unit != ' ' && n < 15) num[n++] = *unit++;
    num[n] = 0;
    while (*unit == ' ') unit++;
    for (k = 0; k < 3; k++) {
        int lower = 0;
        for (n = 0; num[n]; n++) lower |= num[n] >= 'a' && num[n] <= 'z';
        w = tw(BIG[k], num) + (*unit ? 2 + tw(&F16, unit) : 0);
        if (w <= 56 && !(lower && BIG[k]->last < 'a')) break;   /* (x1 stays x1) */
    }
    if (k == 3) { tx_c(cx, y + NUM_Y[2], &F16, v, c); return; }
    tx(cx - w / 2, y + NUM_Y[k], BIG[k], num, c);
    if (*unit) tx(cx - w / 2 + tw(BIG[k], num) + 2, y + UNIT_Y[k], &F16, unit, c);
}
static void plate_bar(int32_t cx, int32_t y, uint32_t pid, int32_t p256, uint16_t bg, uint16_t fg)   /* where the
                                       * knob stands, a thin bar (from the middle for a value around 0: PITCH, the EQ) */
{
    int32_t x0 = cx - 24, w = 48, at = p256 * w >> 8, z = 0;
    if (PINFO[pid][0] < 0 && PINFO[pid][1] > 0)
        z = (0 - PINFO[pid][0]) * w / (PINFO[pid][1] - PINFO[pid][0]);
    cv_rect(x0, y, w + 1, 3, bg);
    if (at >= z) cv_rect(x0 + z, y, at - z + 1, 3, fg);
    else cv_rect(x0 + at, y, z - at + 1, 3, fg);
}
static void draw_plate(void)                    /* the aluminium plate: the knobs' names; under them one dark
                                                 * window in four, each a value and where it stands */
{
    uint32_t k;
    cv_begin(240, 60, RGB(212, 213, 214));
    cv_rect(0, 0, 240, 1, C_WHITE);
    rrect(2, 20, 236, 38, 4, C_INK);            /* the window */
    for (k = 0; k < 4u; k++) {
        const ctl_t *c = &CTL[page][k];
        char v[16];
        int32_t cx = 30 + (int32_t)k * 60;
        int lit = touched == k;
        fmt_val(c, PV(c->pid), v);
        tx_c(cx, 3, &F16, c->name, C_INK);      /* the name, printed black on the plate */
        if (lit) {                              /* the knob just turned: its quarter amber (round only at the ends) */
            int32_t x0 = k ? cx - 29 : 2, x1 = k < 3u ? cx + 30 : 238;
            rrect(x0, 20, x1 - x0, 38, 4, C_LAMP);
            if (k) cv_rect(x0, 20, 4, 38, C_LAMP);
            if (k < 3u) cv_rect(x1 - 4, 20, 4, 38, C_LAMP);
        }
        if (k) cv_rect(cx - 30, 23, 1, 32, RGB(90, 84, 76));   /* the dividers */
        plate_val(cx, 21, v, lit ? C_INK : C_BODY);
        plate_bar(cx, 51, c->pid, c->pid == P_STEP ? (int32_t)seq_cur * 256 / 15 : pos256(c->pid),
                  lit ? RGB(176, 92, 30) : RGB(84, 78, 72), lit ? C_INK : C_BODY);
    }
    cv_blit(0, PLATE_Y);
}
