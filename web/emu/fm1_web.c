/* SPDX-License-Identifier: GPL-3.0-only */
/* A fake FM-1 in the browser: the app (firmware/src/mel.c) compiled to WebAssembly, unchanged, against this
 * file instead of the hardware (platform.c includes it when DINGHY_WEB is set). The page's AudioWorklet asks
 * for audio; producing it runs the device's clock forward a millisecond at a time: the app's main loop once
 * per millisecond, its audio ISR every 256 frames, as on the FM-1. The page sends the panel (keys, buttons,
 * encoders, MASTER, MIDI) and reads back the screen, the LEDs and the saved settings.
 * The idea (the device clock driven by the audio the worklet asks for) is X0X's / Felucca's / FoMni's
 * web/emu (GPL-3.0). Not here: USB, the updater, the flash (but for the settings, kept by the page).
 *   web/emu/build.sh  ->  build/emu/{mel.wasm, index.html, worklet.js} */
#include <stdint.h>

#ifndef APP_NAME
#define APP_NAME "DINGHY"
#endif
#ifndef APP_VER
#define APP_VER "web"
#endif
#define FS 44100u
#define HALF_FRAMES 256u
#define EXPORT __attribute__((used, visibility("default")))

#include "libc.c"                               /* memset / memcpy: there is no libc here either */

static volatile uint32_t fm1_ms;
static struct { uint32_t notes, buttons; } fm1_in;
static uint32_t web_pressed, web_released;
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t p = web_pressed;
    *released = web_released;
    web_pressed = web_released = 0;
    return p;
}
static uint32_t fm1_input_note_edges(void) { return 0; }
static int32_t web_enc_acc[7];
static int32_t fm1_enc_take(uint32_t e) { int32_t d = web_enc_acc[e % 7u]; web_enc_acc[e % 7u] = 0; return d; }
static uint32_t web_led[2];                     /* bit id: buttons 0..13, keys 14..40 */
static void fm1_led_key(uint32_t id, int on)
{
    if (id < 64u) {
        if (on) web_led[id >> 5] |= 1u << (id & 31u);
        else web_led[id >> 5] &= ~(1u << (id & 31u));
    }
}

/* the screen: 240 x 240 RGB565 */
static uint16_t web_fb[240 * 240];
static uint32_t web_draws;
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            web_fb[j * 240u + i] = c;
    web_draws++;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u) {
                uint16_t c = px[j * w + i];
                web_fb[(y + j) * 240u + x + i] = (uint16_t)(c >> 8 | c << 8);   /* gfx.c keeps them byte-swapped */
            }
    web_draws++;
}
#ifndef CV_MAX
#define CV_MAX (240u * 64u)
#endif
#include "gfx.c"

/* settings: the page keeps them (localStorage) and hands them back before boot */
static uint8_t web_set[256];
static int32_t web_set_len = -1;
static uint32_t web_set_writes;
static int settings_load(void *p, uint32_t n)
{
    if (web_set_len != (int32_t)n) return -1;
    memcpy(p, web_set, n);
    return 0;
}
static int settings_save(const void *p, uint32_t n)
{
    if (n > sizeof web_set) return -1;
    memcpy(web_set, p, n);
    web_set_len = (int32_t)n;
    web_set_writes++;
    return 0;
}

static uint8_t web_mq[256][3];
static uint32_t web_mq_w, web_mq_r;
static int midi_get(uint8_t *st, uint8_t *d1, uint8_t *d2)
{
    if (web_mq_r == web_mq_w) return 0;
    *st = web_mq[web_mq_r % 256u][0];
    *d1 = web_mq[web_mq_r % 256u][1];
    *d2 = web_mq[web_mq_r % 256u][2];
    web_mq_r++;
    return 1;
}
static uint32_t web_batt = 620;                /* the battery: full (web_battery sets it, to try the icon) */
static int32_t batt_raw(void) { return (int32_t)web_batt; }
static int usb_powered(void) { return 0; }
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }

static void platform_poll(void) {}
static void app_audio(int32_t *out, uint32_t n);
static void app_init(void);
static void app_loop(void);

/* ---- the clock: the worklet's audio drives it */
#define RING 8192u
static float ring[RING * 2u], out_l[1024], out_r[1024];
static uint32_t ring_w, ring_r, owed, booted;
static int32_t master_q12 = 2048;
static void run_ms(void)
{
    static int32_t blk[2u * HALF_FRAMES];
    uint32_t i;
    fm1_ms++;
    app_loop();
    owed += FS;                                 /* the frames this millisecond is worth, in blocks as the ISR renders */
    while (owed >= 1000u * HALF_FRAMES) {
        owed -= 1000u * HALF_FRAMES;
        for (i = 0; i < 2u * HALF_FRAMES; i++)
            blk[i] = 0;
        app_audio(blk, HALF_FRAMES);
        for (i = 0; i < 2u * HALF_FRAMES; i++) {                 /* as platform.c: MASTER, the clip */
            int32_t v = (blk[i] * master_q12) >> 12;
            v = v > 32767 ? 32767 : v < -32767 ? -32767 : v;
            ring[(ring_w * 2u + i) % (RING * 2u)] = (float)v * (1.0f / 32768.0f);
        }
        ring_w += HALF_FRAMES;
    }
}

EXPORT void web_boot(void) { if (!booted) { booted = 1; app_init(); } }
EXPORT float *web_out_l(void) { return out_l; }
EXPORT float *web_out_r(void) { return out_r; }
EXPORT void web_render(uint32_t n)              /* n frames (<= 1024) into out_l / out_r */
{
    uint32_t k;
    if (n > 1024u) n = 1024u;
    while (ring_w - ring_r < n)
        run_ms();
    for (k = 0; k < n; k++, ring_r++) {
        out_l[k] = ring[(ring_r % RING) * 2u];
        out_r[k] = ring[(ring_r % RING) * 2u + 1u];
    }
}
EXPORT void web_keys(uint32_t mask) { fm1_in.notes = mask & 0x7FFFFFFu; }
EXPORT void web_button(uint32_t id, uint32_t down)
{
    uint32_t bit = 1u << (id & 15u);
    if (down) { fm1_in.buttons |= bit; web_pressed |= bit; }
    else { fm1_in.buttons &= ~bit; web_released |= bit; }
}
EXPORT void web_enc(uint32_t id, int32_t n) { web_enc_acc[id % 7u] += n; }
EXPORT void web_battery(uint32_t v) { web_batt = v; }
EXPORT void web_master(uint32_t v)              /* 0..1023 as the ADC reads it; the curve as platform.c's */
{
    uint32_t k = v > 1023u ? 1023u : v;
    master_q12 = (int32_t)((k * k) >> 8);
}
EXPORT void web_midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (web_mq_w - web_mq_r < 256u) {
        web_mq[web_mq_w % 256u][0] = (uint8_t)st;
        web_mq[web_mq_w % 256u][1] = (uint8_t)d1;
        web_mq[web_mq_w % 256u][2] = (uint8_t)d2;
        web_mq_w++;
    }
}
EXPORT uint16_t *web_screen(void) { return web_fb; }
EXPORT uint32_t web_screen_draws(void) { return web_draws; }
EXPORT uint32_t web_leds(uint32_t half) { return web_led[half & 1u]; }
EXPORT uint8_t *web_settings(void) { return web_set; }
EXPORT int32_t web_settings_len(void) { return web_set_len; }
EXPORT void web_settings_set_len(int32_t n) { web_set_len = n < 0 || n > 256 ? -1 : n; }
EXPORT uint32_t web_settings_writes(void) { return web_set_writes; }
EXPORT uint32_t web_now_ms(void) { return fm1_ms; }
EXPORT uint32_t web_sample_rate(void) { return FS; }
