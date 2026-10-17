/* SPDX-License-Identifier: GPL-3.0-only */
/* A fake FM-1 on a computer: the app (firmware/src/mel.c) runs unchanged on it. platform.c includes
 * this file instead of the hardware when DINGHY_HOST is set. A script presses keys over time; out come
 * the screen (PPM, at the times SHOT asks) and the sound (WAV, as the FM-1 sends it over USB).
 *
 *   tests/run_host.sh                          (builds it: build/host/mel.wav, build/host/mel-<ms>.png)
 *   build/host/mel OUT script.txt [FPS]        (your own script; with FPS: OUT-NNNNN.ppm, a frame every 1/FPS s:
 *                                             a film of your firmware, e.g. ffmpeg -r 30 -i OUT-%05d.ppm ...)
 *
 * Change SCRIPT below to try your own app. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef APP_NAME
#define APP_NAME "DINGHY"
#endif
#ifndef APP_VER
#define APP_VER "0.0"
#endif
#define FS 44100u
#define HALF_FRAMES 256u

static volatile uint32_t fm1_ms;
#if BOARD_SMK37
/* the SMK-37 (hal/smk_input.h's API, as mel_smk.h uses it): keys with velocity, pads as events, 4 faders, 2 wheels,
 * the outer keys, the pad LEDs; 18 buttons, 8 encoders (K5 = MASTER) */
#define FM1_NKEY 37u
#define SMK_KEY_F3 5u
#define HOST_NENC 8
#define HOST_NBTN 32u
static struct {
    uint32_t notes, buttons, frames, adc_ok, adc_miss;
    uint64_t keys;
    uint8_t key_vel[FM1_NKEY], fader[4];
    uint16_t wheel[2], batt, pad_raw[16], pad_base[16];
} fm1_in = {.wheel = {535, 620}};               /* (the wheels at rest, as a unit reads them) */
static uint8_t smk_store_cleared, smk_trim_ok, smk_pad_events;
static uint32_t smk_jedec = 0xEB6014u;
static int32_t smk_master = 724;
static int32_t host_q[2][64];                   /* 0: pad events, 1: outer keys (key << 8 | velocity, 0 = release) */
static uint32_t host_qw[2], host_qr[2];
static void host_qput(int i, int32_t v) { host_q[i][host_qw[i]++ % 64u] = v; }
static int32_t host_qtake(int i) { return host_qr[i] == host_qw[i] ? -1 : host_q[i][host_qr[i]++ % 64u]; }
static int32_t smk_pad_take(void) { return host_qtake(0); }
static int32_t smk_extkey_take(void) { return host_qtake(1); }
static uint32_t padled_rgb[16];                 /* as lcd.c: g << 16 | r << 8 | b */
static void padled_set(uint32_t pad, uint8_t r, uint8_t g, uint8_t b) { if (pad < 16u) padled_rgb[pad] = (uint32_t)g << 16 | (uint32_t)r << 8 | b; }
static void padled_flush(void) {}
static void host_dump(void);                    /* (mel_smk.h) */
#else
#define HOST_NENC 7
#define HOST_NBTN 16u
static struct { uint32_t notes, buttons; } fm1_in;
#endif
static uint32_t host_edges, host_rel, host_until[HOST_NBTN];   /* a button pressed: held until host_until[id], then let go */
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t b = host_edges;
    *released = host_rel;
    host_edges = host_rel = 0;
    return b;
}
static uint32_t fm1_input_note_edges(void) { return 0; }   /* (the script sets fm1_in.notes) */
static int32_t host_enc[HOST_NENC];
static int32_t fm1_enc_take(uint32_t e) { int32_t d = host_enc[e % HOST_NENC]; host_enc[e % HOST_NENC] = 0; return d; }
static void fm1_led_key(uint32_t id, int on) { (void)id; (void)on; }

/* the screen: 240 x 240 RGB565, as the ST7789 gets it (gfx.c sends the bytes swapped) */
static uint16_t fb[240 * 240];
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = c;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u) {
                uint16_t c = px[j * w + i];
                fb[(y + j) * 240u + x + i] = (uint16_t)(c >> 8 | c << 8);
            }
}
#ifndef CV_MAX
#define CV_MAX (240u * 64u)
#endif
#include "gfx.c"

static uint8_t host_store[256 + 4];
static int settings_load(void *p, uint32_t n)
{
    if (host_store[0] != 1 || n > 256u) return -1;
    memcpy(p, host_store + 4, n);
    return 0;
}
static int settings_save(const void *p, uint32_t n)
{
    host_store[0] = 1;
    memcpy(host_store + 4, p, n);
    printf("%6u ms  settings saved (%u B)\n", fm1_ms, n);
    return 0;
}

static uint8_t mq[64][3];
static uint32_t mq_w, mq_r;
static int midi_get(uint8_t *st, uint8_t *d1, uint8_t *d2)
{
    if (mq_r == mq_w) return 0;
    *st = mq[mq_r % 64u][0]; *d1 = mq[mq_r % 64u][1]; *d2 = mq[mq_r % 64u][2];
    mq_r++;
    return 1;
}
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2) { printf("%6u ms  MIDI out %02X %u %u\n", fm1_ms, st, d1, d2); }

static void app_audio(int32_t *out, uint32_t n);
static void app_main(void);

/* the script: at t ms, keys down (bit k = key k, 0 = F3), a button edge, an encoder turned, a MIDI message. A script
 * file (the second argument) replaces it, a line each: "T keys MASK" (hex or decimal), "T btn ID" (a tap), "T hold ID MS"
 * (held MS ms), "T enc ID N", "T midi ST D1 D2", "T end". SMK-37 builds (BOARD_SMK37) also: "T vel V" (the velocity of
 * the keys pressed from now), "T pad P V" (hardware pad P hit at V, 0 = let go), "T fader F V" (0..127), "T wheel W RAW"
 * (0 pitch, 1 mod; raw ADC 0..1023), "T xkey K V" (keybed key K 0..36 at V, 0 = let go: the outer keys 0..4, 32..36),
 * "T dump" (pads and state); any build: "T shot" (the screen at T -> OUT-T.ppm) */
enum { HS_NONE, HS_VEL, HS_PAD, HS_FADER, HS_WHEEL, HS_XKEY, HS_DUMP, HS_SHOT };
typedef struct { uint32_t t, keys; int btn, enc, n; uint8_t st, d1, d2, smk, sa; uint16_t sb; } host_ev_t;   /* (btn: n = how long held, 0: a tap) */
#define K(k) (1u << (k))                                        /* key k: 7 C4, 11 E4, 14 G4, 16 A4, 19 C5 */
static const host_ev_t SCRIPT0[] = {
    {200, K(7) | K(11) | K(14), -1, -1, 0, 0, 0, 0},            /* A FLUTE: C major */
    {2000, 0, -1, -1, 0, 0, 0, 0},
    {2200, K(14), -1, -1, 0, 0, 0, 0},                          /* a tune */
    {2600, K(16), -1, -1, 0, 0, 0, 0},
    {3000, K(19), -1, -1, 0, 0, 0, 0},
    {3800, 0, -1, -1, 0, 0, 0, 0},
    {4000, 0, -1, ENC_PRESETS, 1, 0, 0, 0},                     /* -> B CHOIR */
    {4100, K(4) | K(7) | K(11), -1, -1, 0, 0, 0, 0},            /* A minor */
    {6600, 0, -1, -1, 0, 0, 0, 0},
    {6800, 0, -1, ENC_PRESETS, 1, 0, 0, 0},                     /* -> C CELLO */
    {6900, K(0) | K(7), -1, -1, 0, 0, 0, 0},                    /* F3 + C4 */
    {9200, 0, -1, -1, 0, 0, 0, 0},
};
static const host_ev_t *script = SCRIPT0;
static uint32_t script_n = sizeof SCRIPT0 / sizeof SCRIPT0[0], end_ms = 11000, frame_fps;
static const uint32_t SHOT[] = {1000, 5000, 8000};

static void read_script(const char *path)
{
    static host_ev_t ev[4096];
    char line[256], what[16];
    FILE *f = fopen(path, "r");
    uint32_t n = 0;
    if (!f) { perror(path); exit(1); }
    while (fgets(line, sizeof line, f) && n < 4096u) {
        host_ev_t e = {0, 0, -1, -1, 0, 0, 0, 0};
        unsigned a = 0, b = 0, c = 0;
        if (line[0] == '#' || sscanf(line, "%u %15s", &e.t, what) != 2) continue;
        if (!strcmp(what, "end")) { end_ms = e.t; continue; }
        if (!strcmp(what, "keys")) { sscanf(line, "%*u %*s %i", (int *)&e.keys); }
        else if (!strcmp(what, "btn")) { sscanf(line, "%*u %*s %d", &e.btn); }
        else if (!strcmp(what, "hold")) { sscanf(line, "%*u %*s %d %d", &e.btn, &e.n); }
        else if (!strcmp(what, "enc")) { sscanf(line, "%*u %*s %d %d", &e.enc, &e.n); }
        else if (!strcmp(what, "midi")) { sscanf(line, "%*u %*s %i %u %u", (int *)&a, &b, &c); e.st = (uint8_t)a; e.d1 = (uint8_t)b; e.d2 = (uint8_t)c; }
        else if (!strcmp(what, "vel")) { sscanf(line, "%*u %*s %u", &a); e.smk = HS_VEL; e.sb = (uint16_t)a; }
        else if (!strcmp(what, "pad")) { sscanf(line, "%*u %*s %u %u", &a, &b); e.smk = HS_PAD; e.sa = (uint8_t)a; e.sb = (uint16_t)b; }
        else if (!strcmp(what, "fader")) { sscanf(line, "%*u %*s %u %u", &a, &b); e.smk = HS_FADER; e.sa = (uint8_t)a; e.sb = (uint16_t)b; }
        else if (!strcmp(what, "wheel")) { sscanf(line, "%*u %*s %u %u", &a, &b); e.smk = HS_WHEEL; e.sa = (uint8_t)a; e.sb = (uint16_t)b; }
        else if (!strcmp(what, "xkey")) { sscanf(line, "%*u %*s %u %u", &a, &b); e.smk = HS_XKEY; e.sa = (uint8_t)a; e.sb = (uint16_t)b; }
        else if (!strcmp(what, "dump")) e.smk = HS_DUMP;
        else if (!strcmp(what, "shot")) e.smk = HS_SHOT;
        else continue;
        ev[n++] = e;
    }
    fclose(f);
    script = ev;
    script_n = n;
}

static const char *host_out = "build/host/mel";
static int16_t *wav;
static uint32_t wav_n;

static void write_ppm_as(const char *name)
{
    FILE *f;
    uint32_t i;
    if (!(f = fopen(name, "wb"))) { perror(name); exit(1); }
    fprintf(f, "P6 240 240 255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = fb[i];
        fputc((c >> 11) * 255 / 31, f);
        fputc((c >> 5 & 63) * 255 / 63, f);
        fputc((c & 31) * 255 / 31, f);
    }
    fclose(f);
}
static void write_ppm(uint32_t t)
{
    char name[512];
    snprintf(name, sizeof name, "%s-%u.ppm", host_out, t);
    write_ppm_as(name);
    printf("%6u ms  screen -> %s\n", t, name);
}

static void write_wav(void)
{
    char name[512];
    FILE *f;
    uint32_t bytes = wav_n * 4u, v[] = {16, 0x00020001u, FS, FS * 4u, 0x00100004u};
    snprintf(name, sizeof name, "%s.wav", host_out);
    if (!(f = fopen(name, "wb"))) { perror(name); exit(1); }
    fwrite("RIFF", 1, 4, f); bytes += 36; fwrite(&bytes, 4, 1, f); bytes -= 36;
    fwrite("WAVEfmt ", 1, 8, f); fwrite(v, 4, 5, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    fwrite(wav, 4, wav_n, f);
    fclose(f);
    printf("sound  -> %s (%u frames)\n", name, wav_n);
}

static void platform_poll(void)                 /* one call = one millisecond */
{
    static uint32_t step, shot, owed, frame;
    static int32_t blk[2u * HALF_FRAMES];
    fm1_ms++;
#if BOARD_SMK37
    fm1_in.frames++;                            /* (the SMK scans ~625 frames a second; enough for "settled") */
#endif
    while (step < script_n && script[step].t <= fm1_ms) {
        if (script[step].smk == HS_SHOT) {
            write_ppm(fm1_ms);
        } else if (script[step].smk) {
#if BOARD_SMK37
            static uint32_t vel = 100;
            const host_ev_t *e = &script[step];
            uint32_t k;
            switch (e->smk) {
            case HS_VEL: vel = e->sb ? e->sb : 1u; for (k = 0; k < FM1_NKEY; k++) fm1_in.key_vel[k] = (uint8_t)vel; break;
            case HS_PAD: host_qput(0, (int32_t)((e->sa & 15u) << 8 | (e->sb & 0x7Fu))); break;
            case HS_FADER: fm1_in.fader[e->sa & 3u] = (uint8_t)(e->sb > 127u ? 127u : e->sb); break;
            case HS_WHEEL: fm1_in.wheel[e->sa & 1u] = e->sb; break;
            case HS_XKEY: if (e->sb) fm1_in.key_vel[e->sa % FM1_NKEY] = (uint8_t)e->sb; host_qput(1, (int32_t)((e->sa % FM1_NKEY) << 8 | (e->sb & 0x7Fu))); break;
            case HS_DUMP: host_dump(); break;
            }
#else
            fprintf(stderr, "script: an SMK-37 event in an FM-1 build (BOARD_SMK37=0), ignored\n");
#endif
        } else if (script[step].st) {
            mq[mq_w % 64u][0] = script[step].st; mq[mq_w % 64u][1] = script[step].d1; mq[mq_w % 64u][2] = script[step].d2;
            mq_w++;
        } else if (script[step].btn >= 0) {
            host_edges |= 1u << script[step].btn;
            fm1_in.buttons |= 1u << script[step].btn;
            host_until[script[step].btn % HOST_NBTN] = fm1_ms + (script[step].n ? (uint32_t)script[step].n : 30u);
        }
        else if (script[step].enc >= 0)
            host_enc[script[step].enc % HOST_NENC] += script[step].n;
        else
            fm1_in.notes = script[step].keys;
        step++;
    }
    {
        uint32_t id;
        for (id = 0; id < HOST_NBTN; id++)      /* buttons let go */
            if (fm1_in.buttons >> id & 1u && fm1_ms >= host_until[id]) {
                fm1_in.buttons &= ~(1u << id);
                host_rel |= 1u << id;
            }
    }
    owed += FS;                                 /* the frames this millisecond is worth, in blocks as the ISR renders */
    while (owed >= 1000u * HALF_FRAMES) {
        uint32_t i;
        owed -= 1000u * HALF_FRAMES;
        memset(blk, 0, sizeof blk);
        app_audio(blk, HALF_FRAMES);
        wav = realloc(wav, (wav_n + HALF_FRAMES) * 4u);
        for (i = 0; i < 2u * HALF_FRAMES; i++) {
            int32_t v = blk[i];                 /* (USB audio at the MASTER knob all the way up) */
            wav[2u * wav_n + i] = (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
        }
        wav_n += HALF_FRAMES;
    }
    if (frame_fps) {                            /* a film: a frame every 1/fps s, as the screen is then */
        if ((uint64_t)frame * 1000u <= (uint64_t)fm1_ms * frame_fps) {
            char name[512];
            snprintf(name, sizeof name, "%s-%05u.ppm", host_out, frame++);
            write_ppm_as(name);
        }
    } else if (shot < sizeof SHOT / sizeof SHOT[0] && fm1_ms == SHOT[shot])
        write_ppm(SHOT[shot++]);
    if (fm1_ms >= end_ms) {
        write_wav();
        exit(0);
    }
}

int main(int argc, char **argv)
{
#if BOARD_SMK37
    memset((void *)fm1_in.key_vel, 100, sizeof fm1_in.key_vel);   /* (the keys at 100 until a "vel" line) */
#endif
    if (argc > 1) host_out = argv[1];
    if (argc > 2) read_script(argv[2]);
    if (argc > 3) frame_fps = (uint32_t)atoi(argv[3]);
    app_main();
    return 0;
}
