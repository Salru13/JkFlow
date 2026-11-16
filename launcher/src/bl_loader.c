/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
#include "bl_loader.h"
#include "bl_screens.h"
#include "bl_strings.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The logo is enlarged once and kept; per frame only a brightness changes. */
#define LOGO_W 760
static uint8_t *g_logo;
static int g_logo_h;

static const uint8_t *big_logo(void)
{
    if (!g_logo) {
        g_logo_h = bl_logo_h * LOGO_W / bl_logo_w;
        uint8_t *p = malloc((size_t)LOGO_W * (size_t)g_logo_h * 3);
        if (!p) return NULL;
        bl_image_scale(bl_logo_rgb, bl_logo_w, bl_logo_h, p, LOGO_W, g_logo_h);
        g_logo = p;
    }
    return g_logo;
}

/* One light trail sweeping left to right. Positions are fractions of the
 * frame; the head travels from -0.6 to 1.2 of the width per period. */
typedef struct Trail { uint32_t period_ms, delay_ms; float y, rise, length; uint32_t rgb; int thickness, alpha; } Trail;
static const Trail TRAILS[] = {
    { 2400,    0, 0.30f, -0.06f, 0.55f, 0xFF2A2A, 5, 230 },
    { 3100,  800, 0.62f, -0.05f, 0.65f, 0x2A7BFF, 4, 220 },
    { 2800, 1600, 0.70f, -0.04f, 0.45f, 0xFF3B2A, 3, 170 },
    { 3600, 2100, 0.24f, -0.09f, 0.35f, 0x1BFEFF, 3, 140 },
    { 3300,  400, 0.78f, -0.03f, 0.60f, 0xFF2A2A, 6, 190 },
};
#define NTRAILS ((int)(sizeof TRAILS / sizeof TRAILS[0]))

typedef struct Rect { int x0, y0, x1, y1; } Rect;   /* [x0, x1) x [y0, y1), clamped */

/* Fast path: the loader redraws only what changes, in buffers it drew
 * itself last time. A few remembered pixels show whether the buffer still
 * holds our previous frame; if not, the whole frame is drawn. */
#define SLOTS 4
#define PROBES 32
typedef struct Slot {
    uint8_t *pixels;
    uint32_t width, height, stride;
    Rect trails[NTRAILS];
    Rect text;                       /* the big line drawn last time */
    uint32_t probe[PROBES];
    int valid;
    unsigned age;
} Slot;
static Slot g_slots[SLOTS];
static unsigned g_clock;

void bl_loader_reset(void) { memset(g_slots, 0, sizeof g_slots); }

/* The logo at one pulse level, as ready BGRX rows: copied into the frame
 * each time and rebuilt only when the level changes (24 levels per pulse). */
#define PULSE_LEVELS 24
static uint32_t *g_layer;
static int g_layer_level = -1;

static const uint32_t *logo_layer(int level)
{
    const uint8_t *logo = big_logo();
    if (!logo) return NULL;
    if (!g_layer) {
        g_layer = malloc((size_t)LOGO_W * (size_t)g_logo_h * 4);
        if (!g_layer) return NULL;
        g_layer_level = -1;
    }
    if (level != g_layer_level) {
        unsigned b = 238u + (unsigned)level * 104u / (PULSE_LEVELS - 1);   /* 290 +- 52, about +-18 % */
        size_t n = (size_t)LOGO_W * (size_t)g_logo_h;
        for (size_t i = 0; i < n; i++) {
            unsigned r = (unsigned)logo[i * 3] * b >> 8, g = (unsigned)logo[i * 3 + 1] * b >> 8, bl = (unsigned)logo[i * 3 + 2] * b >> 8;
            g_layer[i] = (r > 255 ? 255u : r) << 16 | (g > 255 ? 255u : g) << 8 | (bl > 255 ? 255u : bl);
        }
        g_layer_level = level;
    }
    return g_layer;
}

void bl_loader_release(void)
{
    free(g_logo);
    g_logo = NULL;
    free(g_layer);
    g_layer = NULL;
    g_layer_level = -1;
    bl_loader_reset();
}

static Rect clamp_rect(const BlFrame *f, long x0, long y0, long x1, long y1)
{
    Rect r = { (int)(x0 < 0 ? 0 : x0), (int)(y0 < 0 ? 0 : y0),
               (int)(x1 > (long)f->width ? (long)f->width : x1), (int)(y1 > (long)f->height ? (long)f->height : y1) };
    if (r.x1 < r.x0) r.x1 = r.x0;
    if (r.y1 < r.y0) r.y1 = r.y0;
    return r;
}

static void clear_rect(BlFrame *f, Rect r)
{
    if (r.x1 <= r.x0) return;
    for (int y = r.y0; y < r.y1; y++) memset(f->pixels + (size_t)y * f->stride + (size_t)r.x0 * 4, 0, (size_t)(r.x1 - r.x0) * 4);
}

static uint32_t pixel(const BlFrame *f, int i)
{
    int x = (int)((uint64_t)f->width * (unsigned)((i * 37) % PROBES) / PROBES + (unsigned)i % 7);
    int y = (int)((uint64_t)f->height * (unsigned)i / PROBES + (unsigned)(i * 13) % 11);
    if (x >= (int)f->width) x = (int)f->width - 1;
    if (y >= (int)f->height) y = (int)f->height - 1;
    uint32_t v;
    memcpy(&v, f->pixels + (size_t)y * f->stride + (size_t)x * 4, 4);
    return v;
}

static Slot *slot_for(const BlFrame *f)
{
    Slot *oldest = &g_slots[0];
    for (int i = 0; i < SLOTS; i++) {
        Slot *s = &g_slots[i];
        if (s->valid && s->pixels == f->pixels && s->width == f->width && s->height == f->height && s->stride == f->stride) {
            for (int k = 0; k < PROBES; k++) if (pixel(f, k) != s->probe[k]) { s->valid = 0; break; }
            return s;
        }
        if (s->age < oldest->age) oldest = s;
    }
    memset(oldest, 0, sizeof *oldest);
    return oldest;
}

void bl_loader_draw(BlFrame *f, uint64_t t_ms) { bl_loader_draw_text(f, t_ms, bl_str(BL_S_LOADER)); }

void bl_loader_draw_text(BlFrame *f, uint64_t t_ms, const char *big)
{
    if (!f || !f->pixels) return;
    const int W = (int)f->width, H = (int)f->height;
    float pulse = sinf(6.2831853f * (float)(t_ms % 3200u) / 3200.0f);
    int level = (int)((pulse + 1.0f) * 0.5f * (float)(PULSE_LEVELS - 1) + 0.5f);
    const uint32_t *layer = logo_layer(level);
    const int lx = (W - LOGO_W) / 2, ly = H * 44 / 100 - g_logo_h / 2;
    const char *s = big ? big : bl_str(BL_S_LOADER);
    const int tw = bl_text_width(BL_FONT_BAND, s), tx = (W - tw) / 2, ty = H * 70 / 100;

    Slot *slot = slot_for(f);
    if (slot->valid) {
        /* Everything else is still black from the previous frame. */
        for (int i = 0; i < NTRAILS; i++) clear_rect(f, slot->trails[i]);
        clear_rect(f, slot->text);
    } else {
        for (int y = 0; y < H; y++) memset(f->pixels + (size_t)y * f->stride, 0, (size_t)W * 4);
    }

    /* The logo goes in first as a copy; trails add light on top (light adds
     * up the same in any order, so this matches drawing trails first). */
    if (layer) {
        Rect r = clamp_rect(f, lx, ly, lx + LOGO_W, ly + g_logo_h);
        for (int y = r.y0; y < r.y1; y++)
            memcpy(f->pixels + (size_t)y * f->stride + (size_t)r.x0 * 4,
                   layer + (size_t)(y - ly) * LOGO_W + (size_t)(r.x0 - lx), (size_t)(r.x1 - r.x0) * 4);
    }
    for (int i = 0; i < NTRAILS; i++) {
        const Trail *t = &TRAILS[i];
        float phase = (float)((t_ms + t->delay_ms) % t->period_ms) / (float)t->period_ms;
        float head = -0.6f + 1.8f * phase;
        float fade = phase < 0.1f ? phase / 0.1f : phase > 0.85f ? (1.0f - phase) / 0.15f : 1.0f;
        int x0 = (int)((head - t->length) * (float)W), x1 = (int)(head * (float)W);
        int y1 = (int)(t->y * (float)H), y0 = (int)((float)y1 - t->rise * (float)H * t->length);
        bl_trail(f, x0, y0, x1, y1, t->thickness, t->rgb, (int)((float)t->alpha * fade), 1);
        int pad = t->thickness / 2 + 4;
        slot->trails[i] = clamp_rect(f, (x0 < x1 ? x0 : x1) - pad, (y0 < y1 ? y0 : y1) - pad,
                                     (x0 > x1 ? x0 : x1) + pad + 1, (y0 > y1 ? y0 : y1) + pad + 1);
    }
    bl_text(f, BL_FONT_BAND, tx, ty, s, BL_RGB_WHITE);
    slot->text = clamp_rect(f, tx - 16, ty - 16, tx + tw + 16, ty + bl_font_line_height(BL_FONT_BAND) + 16);

    slot->pixels = f->pixels; slot->width = f->width; slot->height = f->height; slot->stride = f->stride;
    for (int k = 0; k < PROBES; k++) slot->probe[k] = pixel(f, k);
    slot->valid = 1;
    slot->age = ++g_clock;
}

void bl_loader_draw_progress(BlFrame *f, uint64_t t_ms, const char *big, unsigned done, unsigned total, const char *status)
{
    if (!f || !f->pixels) return;
    bl_loader_draw_text(f, t_ms, big);
    const int W = (int)f->width, H = (int)f->height, bw = 720, bx = (W - bw) / 2, by = H * 80 / 100;
    if (total) {
        unsigned d = done > total ? total : done;
        bl_round_rect(f, bx, by, bw, 16, 8, 0x050F28, 0x050F28, 210, BL_RGB_RIM, 2, 0, 0);
        int fill = (int)((unsigned long long)(bw - 4) * d / total);
        if (fill > 0) bl_round_rect(f, bx + 2, by + 2, fill, 12, 6, BL_RGB_RIM, BL_RGB_CYAN, 255, BL_RGB_CYAN, 0, BL_RGB_CYAN, 10);
    }
    if (status && *status) bl_text(f, BL_FONT_NOTE, (W - bl_text_width(BL_FONT_NOTE, status)) / 2, by + 30, status, 0xA9BBDC);
    bl_loader_reset();                      /* the bar and line are not part of the cached loader frame */
}

void bl_error_draw(BlFrame *f, const char *what, const char *log_name, const char *note)
{
    if (!f || !f->pixels) return;
    const int W = (int)f->width, H = (int)f->height;
    bl_background(f, BL_BG_BLACK);
    const uint8_t *logo = big_logo();
    if (logo) bl_blit_add(f, logo, LOGO_W, g_logo_h, (W - LOGO_W) / 2, H * 30 / 100 - g_logo_h / 2, 90);

    const char *title = bl_str(BL_S_ERROR_TITLE);
    bl_text_glow(f, BL_FONT_TITLE, (W - bl_text_width(BL_FONT_TITLE, title)) / 2, H * 52 / 100,
                 title, BL_RGB_CREAM, 0x806A40);
    const char *body = bl_str(BL_S_ERROR_BODY);
    int y = H * 52 / 100 + bl_font_line_height(BL_FONT_TITLE) + 10;
    bl_text(f, BL_FONT_BODY, (W - bl_text_width(BL_FONT_BODY, body)) / 2, y, body, 0xA9BBDC);
    y += bl_font_line_height(BL_FONT_BODY) + 6;
    char line[256];
    if (what && *what && !strchr(what, '/')) {
        bl_text(f, BL_FONT_NOTE, (W - bl_text_width(BL_FONT_NOTE, what)) / 2, y, what, BL_RGB_DIM);
        y += bl_font_line_height(BL_FONT_NOTE) + 4;
    }
    if (log_name && *log_name && !strchr(log_name, '/')) {
        snprintf(line, sizeof line, bl_str(BL_S_ERROR_LOG), log_name);
        bl_text(f, BL_FONT_BODY, (W - bl_text_width(BL_FONT_BODY, line)) / 2, y, line, BL_RGB_WHITE);
        y += bl_font_line_height(BL_FONT_BODY) + 4;
    }
    if (note && *note) bl_text(f, BL_FONT_NOTE, (W - bl_text_width(BL_FONT_NOTE, note)) / 2, y, note, BL_RGB_CYAN);
    const BlHint hints[] = { { BL_KEY_CROSS, BL_S_HINT_START_AGAIN }, { BL_KEY_SQUARE, BL_S_HINT_BUNDLE },
                             { BL_KEY_CIRCLE, BL_S_HINT_BACK_TO_START } };
    bl_hints(f, hints, 3);
}

int bl_notice_draw(BlFrame *f, const char *text, const char *hint)
{
    if (!f || !f->pixels) return 0;
    const int W = (int)f->width, H = (int)f->height;
    bl_background(f, BL_BG_BLACK);
    const uint8_t *logo = big_logo();
    if (logo) bl_blit_add(f, logo, LOGO_W, g_logo_h, (W - LOGO_W) / 2, H * 30 / 100 - g_logo_h / 2, 90);
    const char *title = bl_str(BL_S_ERROR_TITLE);
    bl_text_glow(f, BL_FONT_TITLE, (W - bl_text_width(BL_FONT_TITLE, title)) / 2, H * 52 / 100,
                 title, BL_RGB_CREAM, 0x806A40);
    int y = H * 52 / 100 + bl_font_line_height(BL_FONT_TITLE) + 10;
    if (text && *text) y = bl_note(f, (W - 1600) / 2, y, 1600, text, 0xA9BBDC, BL_FONT_BODY);
    if (hint && *hint) {
        y += 12;
        bl_text(f, BL_FONT_NOTE, (W - bl_text_width(BL_FONT_NOTE, hint)) / 2, y, hint, BL_RGB_WHITE);
        y += bl_font_line_height(BL_FONT_NOTE);
    }
    bl_loader_reset();                      /* not a loader frame */
    return y;
}
