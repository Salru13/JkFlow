/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
#include "bl_screens.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __SSE2__
#include <emmintrin.h>
#endif

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static inline uint32_t *px_at(BlFrame *f, int x, int y)
{
    return (uint32_t *)(void *)(f->pixels + (size_t)y * f->stride) + x;
}

static inline uint32_t blend(uint32_t d, uint32_t s, float a)
{
    if (a <= 0.0f) return d;
    if (a >= 1.0f) return s;
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        float dc = (float)((d >> sh) & 255), sc = (float)((s >> sh) & 255);
        out |= (uint32_t)(dc + (sc - dc) * a + 0.5f) << sh;
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* Pictograms as signed distance fields in a [-1, 1] box              */
/* ------------------------------------------------------------------ */

typedef enum Shape {
    SH_PLAY, SH_WRENCH, SH_GEAR, SH_INFO, SH_GLOBE, SH_USER, SH_CONSOLE, SH_CHART, SH_LOG,
    SH_CROSS, SH_CIRCLE, SH_TRIANGLE, SH_SQUARE, SH_LEFTRIGHT, SH_UPDOWN, SH_MONITOR
} Shape;

static float sd_segment(float px, float py, float ax, float ay, float bx, float by, float r)
{
    float pax = px - ax, pay = py - ay, bax = bx - ax, bay = by - ay;
    float h = clampf((pax * bax + pay * bay) / (bax * bax + bay * bay), 0.0f, 1.0f);
    float dx = pax - bax * h, dy = pay - bay * h;
    return sqrtf(dx * dx + dy * dy) - r;
}

/* Convex polygon, either winding: max of the edge distances (positive outside). */
static float sd_poly(float px, float py, const float *v, int n)
{
    float area = 0.0f;
    for (int i = 0; i < n; i++)
        area += v[2 * i] * v[2 * ((i + 1) % n) + 1] - v[2 * ((i + 1) % n)] * v[2 * i + 1];
    float sign = area < 0.0f ? -1.0f : 1.0f;
    float d = -1e9f;
    for (int i = 0; i < n; i++) {
        float ax = v[2 * i], ay = v[2 * i + 1], bx = v[2 * ((i + 1) % n)], by = v[2 * ((i + 1) % n) + 1];
        float ex = bx - ax, ey = by - ay, len = sqrtf(ex * ex + ey * ey);
        float e = sign * ((px - ax) * ey - (py - ay) * ex) / len;
        if (e > d) d = e;
    }
    return d;
}

static float sd_box(float px, float py, float hx, float hy)
{
    float qx = fabsf(px) - hx, qy = fabsf(py) - hy;
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0.0f);
}

static float shape_sd(Shape s, float u, float v)
{
    switch (s) {
    case SH_PLAY: {
        static const float tri[] = { -0.42f, -0.62f, -0.42f, 0.62f, 0.66f, 0.0f };
        return sd_poly(u, v, tri, 3) - 0.04f;
    }
    case SH_INFO:
        return fminf(sqrtf(u * u + (v + 0.52f) * (v + 0.52f)) - 0.17f, sd_box(u, v - 0.22f, 0.15f, 0.42f) - 0.04f);
    case SH_GEAR: {
        float r = sqrtf(u * u + v * v), a = atan2f(v, u);
        float tooth = 0.5f + 0.5f * cosf(8.0f * a);
        tooth = clampf((tooth - 0.35f) * 4.0f, 0.0f, 1.0f);
        float outer = r - (0.58f + 0.2f * tooth);
        return fmaxf(outer, 0.26f - r);
    }
    case SH_WRENCH: {
        float handle = sd_segment(u, v, -0.62f, 0.62f, 0.12f, -0.12f, 0.13f);
        float head = sqrtf((u - 0.3f) * (u - 0.3f) + (v + 0.3f) * (v + 0.3f)) - 0.34f;
        float notch = sd_segment(u, v, 0.42f, -0.42f, 0.9f, -0.9f, 0.13f);
        return fmaxf(fminf(handle, head), -notch);
    }
    case SH_GLOBE: {
        float r = sqrtf(u * u + v * v);
        float ring = fabsf(r - 0.7f) - 0.09f;
        float eq = fmaxf(fabsf(v) - 0.07f, r - 0.7f);
        float ex = 0.34f, mer = fmaxf(fabsf(sqrtf((u / ex) * (u / ex) + (v / 0.7f) * (v / 0.7f)) - 1.0f) * ex - 0.07f, r - 0.72f);
        return fminf(ring, fminf(eq, mer));
    }
    case SH_USER:
        return fminf(sqrtf(u * u + (v + 0.32f) * (v + 0.32f)) - 0.3f,
                     fmaxf(sqrtf(u * u + (v - 0.85f) * (v - 0.85f)) - 0.68f, v - 0.72f));
    case SH_CONSOLE: {
        static const float body[] = { -0.75f, -0.3f, 0.85f, -0.3f, 0.7f, 0.3f, -0.9f, 0.3f };
        return fminf(sd_poly(u, v, body, 4), sd_box(u + 0.05f, v - 0.52f, 0.7f, 0.07f));
    }
    case SH_CHART:
        return fminf(fminf(sd_box(u + 0.5f, v - 0.35f, 0.14f, 0.3f), sd_box(u, v - 0.12f, 0.14f, 0.53f)),
                     fminf(sd_box(u - 0.5f, v + 0.1f, 0.14f, 0.75f), sd_box(u, v - 0.78f, 0.8f, 0.06f)));
    case SH_LOG: {
        float page = sd_box(u, v, 0.55f, 0.75f) - 0.06f;
        float lines = fminf(fminf(sd_box(u, v + 0.35f, 0.33f, 0.06f), sd_box(u, v, 0.33f, 0.06f)), sd_box(u + 0.1f, v - 0.35f, 0.23f, 0.06f));
        return fmaxf(page, -lines);
    }
    case SH_CROSS:
        return fminf(sd_segment(u, v, -0.5f, -0.5f, 0.5f, 0.5f, 0.11f), sd_segment(u, v, -0.5f, 0.5f, 0.5f, -0.5f, 0.11f));
    case SH_CIRCLE:
        return fabsf(sqrtf(u * u + v * v) - 0.52f) - 0.11f;
    case SH_TRIANGLE: {
        static const float tri[] = { 0.0f, -0.62f, -0.62f, 0.5f, 0.62f, 0.5f };
        return fabsf(sd_poly(u, v, tri, 3) + 0.1f) - 0.1f;
    }
    case SH_SQUARE:
        return fabsf(sd_box(u, v, 0.5f, 0.5f)) - 0.1f;
    case SH_LEFTRIGHT: {
        static const float l[] = { -0.85f, 0.0f, -0.15f, 0.45f, -0.15f, -0.45f };
        static const float r[] = { 0.85f, 0.0f, 0.15f, -0.45f, 0.15f, 0.45f };
        return fminf(sd_poly(u, v, l, 3), sd_poly(u, v, r, 3));
    }
    case SH_UPDOWN: {
        static const float t[] = { 0.0f, -0.85f, -0.45f, -0.15f, 0.45f, -0.15f };
        static const float b[] = { 0.0f, 0.85f, 0.45f, 0.15f, -0.45f, 0.15f };
        return fminf(sd_poly(u, v, t, 3), sd_poly(u, v, b, 3));
    }
    case SH_MONITOR: {
        float screen = fabsf(sd_box(u, v + 0.18f, 0.74f, 0.47f)) - 0.09f;
        return fminf(screen, fminf(sd_box(u, v - 0.5f, 0.07f, 0.14f), sd_box(u, v - 0.7f, 0.4f, 0.07f)));
    }
    }
    return 1.0f;
}

/* Fill a shape anti-aliased into a box of sx x sy pixels centred on (cx, cy). */
static void draw_shape(BlFrame *f, Shape s, int cx, int cy, int sx, int sy, uint32_t rgb)
{
    if (!f || !f->pixels || sx <= 0 || sy <= 0) return;
    int x0 = cx - sx / 2 - 1, y0 = cy - sy / 2 - 1, x1 = cx + sx / 2 + 2, y1 = cy + sy / 2 + 2;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)f->width) x1 = (int)f->width;
    if (y1 > (int)f->height) y1 = (int)f->height;
    float half = (float)(sx < sy ? sx : sy) * 0.5f;     /* pixels per unit */
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            float u = ((float)x + 0.5f - (float)cx) / ((float)sx * 0.5f);
            float v = ((float)y + 0.5f - (float)cy) / ((float)sy * 0.5f);
            float cov = clampf(0.5f - shape_sd(s, u, v) * half, 0.0f, 1.0f);
            if (cov > 0.0f) { uint32_t *p = px_at(f, x, y); *p = blend(*p, rgb, cov); }
        }
}

void bl_icon(BlFrame *f, BlIcon icon, int cx, int cy, int size, uint32_t rgb)
{
    static const Shape map[] = { SH_PLAY, SH_WRENCH, SH_GEAR, SH_INFO, SH_GLOBE, SH_USER, SH_CONSOLE, SH_CHART, SH_LOG, SH_MONITOR };
    if ((int)icon < 0 || (int)icon >= (int)(sizeof map / sizeof map[0])) return;
    draw_shape(f, map[icon], cx, cy, size, size, rgb);
}

/* ------------------------------------------------------------------ */
/* List-screen parts: pill header, panel rows, segments, fields       */
/* ------------------------------------------------------------------ */

void bl_pill_header(BlFrame *f, BlIcon icon, const char *title)
{
    const int x = 150, y = 180;
    bl_glow_dot(f, x + 37, y + 37, 56, BL_RGB_SUBTITLE, 120);
    bl_round_rect(f, x, y, 74, 74, 37, 0x123A80, 0x0B286C, 200, BL_RGB_SUBTITLE, 4, BL_RGB_SUBTITLE, 14);
    bl_icon(f, icon, x + 37, y + 37, 42, BL_RGB_WHITE);
    int tw = bl_text_width(BL_FONT_ITEM, title);
    bl_round_rect(f, x + 92, y + 11, tw + 120, 54, 10, 0x2C86FF, 0x0D4FB0, 255, 0x0A2A74, 3, BL_RGB_BUTTON, 18);
    bl_text(f, BL_FONT_ITEM, x + 92 + 60, y + 11 + (54 - bl_font_line_height(BL_FONT_ITEM)) / 2, title, BL_RGB_WHITE);
}

void bl_panel_title(BlFrame *f, int x, int y, const char *title)
{
    bl_text_glow(f, BL_FONT_HEAD, x, y, title, BL_RGB_CREAM, 0x806A40);
}

void bl_row(BlFrame *f, int x, int y, int w, const char *label, int focused)
{
    const int h = 62;
    if (focused) bl_round_rect(f, x, y, w, h, 12, 0x1B7FB0, 0x134A9A, 90, BL_RGB_CYAN, 2, 0, 0);
    bl_text(f, BL_FONT_BODY, x + 20, y + (h - bl_font_line_height(BL_FONT_BODY)) / 2, label,
            focused ? BL_RGB_WHITE : 0xC9D6EE);
}

int bl_segments(BlFrame *f, int right_x, int y, const BlStringId *options, int count, int selected)
{
    return bl_segments_greyed(f, right_x, y, options, count, selected, 0);
}

int bl_segments_greyed(BlFrame *f, int right_x, int y, const BlStringId *options, int count, int selected, unsigned greyed)
{
    const int h = 44, pad = 22, gap = 10;
    int widths[8], total = 0;
    if (count > 8) count = 8;
    for (int i = 0; i < count; i++) { widths[i] = bl_text_width(BL_FONT_NOTE, bl_str(options[i])) + 2 * pad; total += widths[i] + (i ? gap : 0); }
    int x = right_x - total, left = x;
    for (int i = 0; i < count; i++) {
        bl_pill(f, x, y + 9, widths[i], h, i == selected);
        bl_text(f, BL_FONT_NOTE, x + pad, y + 9 + (h - bl_font_line_height(BL_FONT_NOTE)) / 2, bl_str(options[i]),
                i == selected ? BL_RGB_WHITE : ((greyed >> i) & 1u) ? 0x4A5674 : 0x8FA3C8);
        x += widths[i] + gap;
    }
    return left;
}

void bl_field(BlFrame *f, int right_x, int y, int w, const char *text, int dim)
{
    const int h = 46;
    bl_round_rect(f, right_x - w, y + 8, w, h, 10, 0x030A1E, 0x030A1E, 190, 0x4696FF, 2, 0, 0);
    char clipped[160];
    snprintf(clipped, sizeof clipped, "%s", text ? text : "");
    while (clipped[0] && bl_text_width(BL_FONT_BODY, clipped) > w - 36) clipped[strlen(clipped) - 1] = 0;
    bl_text(f, BL_FONT_BODY, right_x - w + 18, y + 8 + (h - bl_font_line_height(BL_FONT_BODY)) / 2, clipped,
            dim ? 0x6F80A8 : BL_RGB_WHITE);
}

int bl_table(BlFrame *f, int x, int y, int width, const int *col_x, int cols, const char *const *cells,
             const uint32_t *colours, int rows, BlFont font)
{
    int lh = bl_font_line_height(font) + 12;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            const char *s = cells[r * cols + c];
            if (!s) continue;
            uint32_t rgb = colours && colours[r * cols + c] ? colours[r * cols + c] : (r == 0 ? BL_RGB_SUBTITLE : 0xE0E8F8);
            bl_text(f, r == 0 ? BL_FONT_NOTE : font, x + col_x[c], y + 6, s, rgb);
        }
        y += lh;
        bl_fill_alpha(f, x, y - 2, width, 1, 0x4696FF, 64);
    }
    return y;
}

int bl_note(BlFrame *f, int x, int y, int w, const char *text, uint32_t rgb, BlFont font)
{
    char line[512] = "", trial[512];
    const char *p = text;
    int lh = bl_font_line_height(font);
    while (p && *p) {
        const char *sp = strchr(p, ' ');
        size_t wl = sp ? (size_t)(sp - p) : strlen(p);
        size_t ln = strlen(line);
        if (wl > 200) wl = 200;                      /* one absurdly long word: cut it */
        /* A line that would not fit the buffer is drawn first, like one too wide. */
        int full = ln && ln + 1 + wl >= sizeof trial;
        size_t t = ln;
        if (!full) {
            memcpy(trial, line, ln);
            if (ln) trial[t++] = ' ';
            memcpy(trial + t, p, wl);
            trial[t + wl] = 0;
        }
        if (full || (ln && bl_text_width(font, trial) > w)) {
            bl_text(f, font, x, y, line, rgb);
            y += lh;
            memcpy(line, p, wl);
            line[wl] = 0;
        } else {
            memcpy(line, trial, t + wl + 1);
        }
        p = sp ? sp + 1 : NULL;
    }
    if (line[0]) { bl_text(f, font, x, y, line, rgb); y += lh; }
    return y;
}

void bl_button(BlFrame *f, int x, int y, const char *text, int focused)
{
    int w = bl_text_width(BL_FONT_BODY, text) + 92, h = 66;
    if (focused) bl_round_rect(f, x, y, w, h, 12, 0x2C86FF, 0x0D4FB0, 255, BL_RGB_CYAN, 3, BL_RGB_CYAN, 22);
    else bl_round_rect(f, x, y, w, h, 12, 0x050F28, 0x050F28, 150, 0x4682DC, 2, 0, 0);
    bl_text(f, BL_FONT_BODY, x + 46, y + (h - bl_font_line_height(BL_FONT_BODY)) / 2, text, focused ? BL_RGB_WHITE : 0xA9BBDC);
}

/* Cyan icon disc of the game's cards: radial cyan with a soft glow. */
static void icon_disc(BlFrame *f, int cx, int cy, int rx, int ry)
{
    if (rx <= 0 || ry <= 0) return;
    bl_glow_dot(f, cx, cy, (rx > ry ? rx : ry) + 30, BL_RGB_CYAN, 70);
    int x0 = cx - rx - 1, x1 = cx + rx + 2, y0 = cy - ry - 1, y1 = cy + ry + 2;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)f->width) x1 = (int)f->width;
    if (y1 > (int)f->height) y1 = (int)f->height;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            float u = ((float)x + 0.5f - (float)cx) / (float)rx, v = ((float)y + 0.5f - (float)cy) / (float)ry;
            float r = sqrtf(u * u + v * v);
            float cov = clampf((1.0f - r) * (float)(rx < ry ? rx : ry) + 0.5f, 0.0f, 1.0f);
            if (cov <= 0.0f) continue;
            uint32_t c = r < 0.45f ? blend(0x7FFCFF, BL_RGB_CYAN, r / 0.45f) : blend(BL_RGB_CYAN, 0x12B8F0, (r - 0.45f) / 0.55f);
            uint32_t *p = px_at(f, x, y);
            *p = blend(*p, c, cov);
        }
}

/* ------------------------------------------------------------------ */
/* Shared frame parts                                                 */
/* ------------------------------------------------------------------ */

#define SMALL_LOGO_H 120
static uint8_t *g_small_logo;
static int g_small_logo_w;

static void small_logo(BlFrame *f, int x, int y)
{
    if (!g_small_logo) {
        g_small_logo_w = bl_logo_w * SMALL_LOGO_H / bl_logo_h;
        g_small_logo = malloc((size_t)g_small_logo_w * SMALL_LOGO_H * 3);
        if (!g_small_logo) return;
        bl_image_scale(bl_logo_rgb, bl_logo_w, bl_logo_h, g_small_logo, g_small_logo_w, SMALL_LOGO_H);
    }
    bl_blit_add(f, g_small_logo, g_small_logo_w, SMALL_LOGO_H, x, y, 256);
}

/* The top bar in its drawing order: badge disc, badge icon, logo and bar,
 * then the status items on the bar. Owner finding 33: the logo sits right
 * of the badge and its glow (it used to start at x 202, over the disc), the
 * bar right of the logo; nothing overlaps. */
enum { BADGE_CX = 178, BADGE_CY = 94, BADGE_GLOW = 74,
       LOGO_X = 266, LOGO_Y = 34,                      /* the glow ends at x 252 */
       BAR_X = 528, BAR_Y = 79, BAR_R = 1790, BAR_H = 30 };   /* the logo ends at 499, the bar glow starts at 512 */

static void top_bar_disc(BlFrame *f)
{
    bl_glow_dot(f, BADGE_CX, BADGE_CY, BADGE_GLOW, 0x4278D7, 150);
    bl_round_rect(f, BADGE_CX - 48, BADGE_CY - 48, 96, 96, 48, 0x5C93FF, 0x1D47A8, 255, 0x5C93FF, 0, 0, 0);
}

static void top_bar_icon(BlFrame *f, BlIcon badge) { bl_icon(f, badge, BADGE_CX, BADGE_CY, 52, BL_RGB_WHITE); }

static void top_bar_logo(BlFrame *f) { small_logo(f, LOGO_X, LOGO_Y); }

static void top_bar_line(BlFrame *f)
{
    /* Thin bar with a bright blue rim. */
    bl_round_rect(f, BAR_X, BAR_Y, BAR_R - BAR_X, BAR_H, 15, 0x1D4C8A, 0x0B2244, 255, BL_RGB_RIM, 2, BL_RGB_RIM, 16);
}

static void top_bar_rest(BlFrame *f)
{
    top_bar_logo(f);
    top_bar_line(f);
}

void bl_top_bar_part(BlFrame *f, BlTopPart part, BlIcon badge)
{
    if (part == BL_TOP_BADGE) { top_bar_disc(f); top_bar_icon(f, badge); }
    else if (part == BL_TOP_LOGO) top_bar_logo(f);
    else if (part == BL_TOP_BAR) top_bar_line(f);
}

void bl_top_bar_frame(BlFrame *f, BlIcon badge)
{
    /* Badge: glowing blue disc with a white pictogram. */
    top_bar_disc(f);
    top_bar_icon(f, badge);
    top_bar_rest(f);
}

void bl_top_bar(BlFrame *f, BlIcon badge, const BlTopItem *items, int count)
{
    bl_top_bar_frame(f, badge);
    bl_top_bar_items(f, items, count);
}

void bl_top_bar_items(BlFrame *f, const BlTopItem *items, int count)
{
    const int bx = BAR_X, by = BAR_Y, bw = BAR_R - BAR_X, bh = BAR_H;
    int x = bx + bw - 26;
    for (int i = count - 1; i >= 0; i--) {
        if (!items[i].text) continue;
        int w = bl_text_width(BL_FONT_NOTE, items[i].text);
        x -= w;
        bl_text(f, BL_FONT_NOTE, x, by + (bh - bl_font_line_height(BL_FONT_NOTE)) / 2, items[i].text, items[i].rgb);
        if (items[i].tick) {
            uint32_t c = items[i].tick == 2 ? BL_RGB_AMBER : BL_RGB_CYAN;
            x -= 18;
            bl_glow_dot(f, x, by + bh / 2, 14, c, 160);
            bl_round_rect(f, x - 7, by + bh / 2 - 7, 14, 14, 7, c, c, 255, c, 0, 0, 0);
            x -= 8;
        }
        x -= 34;
    }
}

void bl_band(BlFrame *f, const char *text)
{
    bl_band_fill(f);
    bl_band_text(f, text);
}

void bl_band_fill(BlFrame *f)
{
    const int W = (int)f->width, y = 872, h = 64;
    enum { MAXW = 4096 };
    if (W > MAXW) {
        for (int x = 0; x < W; x++) {
            float t = (float)x / (float)(W - 1), a;
            if (t < 0.12f || t > 0.88f) continue;
            a = t < 0.35f ? (t - 0.12f) / 0.23f : t > 0.65f ? (0.88f - t) / 0.23f : 1.0f;
            bl_fill_alpha(f, x, y, 1, h, 0x3C5AA0, (int)(107.0f * a));
        }
        return;
    }
    /* The alpha of each column once, then whole rows (each pixel is mixed
     * once, as before; columns outside the band stay as they are). */
    static int alpha[MAXW];
    for (int x = 0; x < W; x++) {
        float t = (float)x / (float)(W - 1), a;
        if (t < 0.12f || t > 0.88f) { alpha[x] = 0; continue; }
        a = t < 0.35f ? (t - 0.12f) / 0.23f : t > 0.65f ? (0.88f - t) / 0.23f : 1.0f;
        alpha[x] = (int)(107.0f * a);
    }
    bl_fill_alpha_cols(f, 0, y, W, h, 0x3C5AA0, alpha);
}

int bl_band_text_bottom(const BlFrame *f, const char *text)
{
    const int W = (int)f->width, y = 872, h = 64;
    if (text && *text && bl_text_width(BL_FONT_BAND, text) > W - 400)
        return bl_note(NULL, 200, y + 2, W - 400, text, BL_RGB_WHITE, BL_FONT_NOTE);
    return y + h;
}

void bl_band_text(BlFrame *f, const char *text)
{
    const int W = (int)f->width, y = 872, h = 64;
    const char *mark = text ? strstr(text, "{square}") : NULL;
    if (mark) {   /* the Help key cap where {square} stands (the font has no square glyph) */
        char before[256], after[256];
        snprintf(before, sizeof before, "%.*s", (int)(mark - text), text);
        snprintf(after, sizeof after, "%s", mark + 8);
        const int lh = bl_font_line_height(BL_FONT_BAND), cap = bl_key_cap(NULL, BL_KEY_SQUARE, 0, 0);
        const int wb = bl_text_width(BL_FONT_BAND, before), wa = bl_text_width(BL_FONT_BAND, after);
        int x = (W - (wb + cap + wa)) / 2, ty = y + (h - lh) / 2;
        bl_text(f, BL_FONT_BAND, x + 2, ty + 2, before, 0x000000);
        bl_text(f, BL_FONT_BAND, x, ty, before, BL_RGB_WHITE);
        bl_key_cap(f, BL_KEY_SQUARE, x + wb, y + (h - 36) / 2);
        bl_text(f, BL_FONT_BAND, x + wb + cap + 2, ty + 2, after, 0x000000);
        bl_text(f, BL_FONT_BAND, x + wb + cap, ty, after, BL_RGB_WHITE);
        return;
    }
    if (text && *text && bl_text_width(BL_FONT_BAND, text) > W - 400) {
        /* A long reason (e.g. how to copy the game in): smaller, on two lines. */
        bl_note(f, 200, y + 2, W - 400, text, BL_RGB_WHITE, BL_FONT_NOTE);
    } else if (text && *text) {
        int lh = bl_font_line_height(BL_FONT_BAND);
        int tw = bl_text_width(BL_FONT_BAND, text);
        bl_text(f, BL_FONT_BAND, (W - tw) / 2 + 2, y + (h - lh) / 2 + 2, text, 0x000000);
        bl_text(f, BL_FONT_BAND, (W - tw) / 2, y + (h - lh) / 2, text, BL_RGB_WHITE);
    }
}

int bl_key_cap(BlFrame *f, BlKey key, int x, int y)
{
    static const Shape map[] = { SH_CROSS, SH_CIRCLE, SH_TRIANGLE, SH_SQUARE, SH_LEFTRIGHT, SH_UPDOWN };
    int kw = key >= BL_KEY_LEFTRIGHT ? 52 : 36;
    if (!f) return kw;
    bl_round_rect(f, x, y, kw, 36, 7, 0xE9EDF3, 0x9AA3AD, 255, 0, 0, 0, 0);
    if ((int)key >= 0 && (int)key <= BL_KEY_UPDOWN)
        draw_shape(f, map[key], x + kw / 2, y + 18, kw - 12, 24, 0x1A2030);
    return kw;
}

void bl_hints(BlFrame *f, const BlHint *hints, int count)
{
    int x = 140;
    const int y = (int)f->height - 68 - 36;
    for (int i = 0; i < count; i++) {
        x += bl_key_cap(f, hints[i].key, x, y) + 10;
        const char *s = bl_str(hints[i].text);
        int lh = bl_font_line_height(BL_FONT_BODY);
        x += bl_text(f, BL_FONT_BODY, x, y + 18 - lh / 2, s, BL_RGB_WHITE) + 34;
    }
}

/* ------------------------------------------------------------------ */
/* Caches (owner 2026-11-15: the launcher must redraw smoothly)       */
/* ------------------------------------------------------------------ */
/*
 * Every redraw used to paint the whole 1920x1080 frame on the CPU. Now each
 * screen keeps the parts that do not change (background, darkening, glass
 * panels, top bar, band, hints) in a cached image: a redraw copies it and
 * paints only what changed. The cached image holds exactly the pixels the
 * full drawing produces: either the static parts come first in the drawing
 * order, or the screen checks that the changing parts drawn before them do
 * not touch them (else it draws everything directly, as before).
 *
 * Memory: the title process cannot malloc more than about 12 MB at once.
 * Every buffer here is at most one frame (1920x1080x4 = 8.3 MB), the total
 * is capped (BL_CACHE_CAP) and a failed allocation only means drawing
 * directly.
 */

#define FRAME_BYTES_MAX ((size_t)1920 * 1080 * 4)
#define BL_CACHE_CAP ((size_t)48 << 20)

static int g_cache_on = 1;
static size_t g_cache_bytes;                 /* everything below, plus the small logo */

static void *cache_alloc(size_t n)
{
    if (!n || n > FRAME_BYTES_MAX || g_cache_bytes + n > BL_CACHE_CAP) return NULL;
    void *p = malloc(n);
    if (p) g_cache_bytes += n;
    return p;
}

static void cache_free(void *p, size_t n)
{
    if (!p) return;
    free(p);
    g_cache_bytes -= n;
}

static int frame_fits(const BlFrame *f)
{
    return f && f->pixels && f->width && f->height && f->stride >= f->width * 4 &&
           (size_t)f->width * f->height * 4 <= FRAME_BYTES_MAX;
}

static int frame_cacheable(const BlFrame *f) { return g_cache_on && frame_fits(f); }

/* A whole-frame image. */
typedef struct Image { uint32_t w, h; uint8_t *px; } Image;

static void image_free(Image *im)
{
    cache_free(im->px, (size_t)im->w * im->h * 4);
    memset(im, 0, sizeof *im);
}

static int image_fits(const Image *im, const BlFrame *f) { return im->px && im->w == f->width && im->h == f->height; }

static void image_put(const Image *im, BlFrame *f)
{
    size_t row = (size_t)im->w * 4;
    for (uint32_t y = 0; y < im->h; y++) memcpy(f->pixels + (size_t)y * f->stride, im->px + y * row, row);
}

/* Store f in im (allocated or reused). 0 when there is no memory. */
static int image_take(Image *im, const BlFrame *f)
{
    if (!image_fits(im, f)) {
        image_free(im);
        im->px = cache_alloc((size_t)f->width * f->height * 4);
        if (!im->px) return 0;
        im->w = f->width; im->h = f->height;
    }
    size_t row = (size_t)im->w * 4;
    for (uint32_t y = 0; y < im->h; y++) memcpy(im->px + y * row, f->pixels + (size_t)y * f->stride, row);
    return 1;
}

static Image g_bg[2];                        /* BL_BG_START, BL_BG_LIST (black is not cached) */
static Image g_start_base;                   /* the Start screen's static parts (below) */

void bl_screen_background(BlFrame *f, BlBackground kind)
{
    if (!f || !f->pixels || (int)kind < 0 || (int)kind > 2) return;
    /* Direct drawing (tests) does not keep the Start background twice. */
    int spare = kind == BL_BG_START && !g_cache_on && g_start_base.px;
    if (kind == BL_BG_BLACK || !frame_fits(f) || spare) { bl_background(f, kind); return; }
    Image *c = &g_bg[kind];
    if (!image_fits(c, f)) {
        bl_background(f, kind);
        image_take(c, f);
        return;
    }
    image_put(c, f);
}

/* Screen layers: the static parts of one screen, under a key. Two slots
 * (the screen shown and the question over it), least recently used goes. */
typedef struct Layer { uint64_t key; unsigned used; Image im; } Layer;
static Layer g_layers[2];
static unsigned g_layer_clock;

int bl_layer_restore(BlFrame *f, uint64_t key)
{
    if (!frame_cacheable(f)) return 0;
    for (int i = 0; i < 2; i++)
        if (g_layers[i].key == key && image_fits(&g_layers[i].im, f)) {
            image_put(&g_layers[i].im, f);
            g_layers[i].used = ++g_layer_clock;
            return 1;
        }
    return 0;
}

void bl_layer_save(const BlFrame *f, uint64_t key)
{
    if (!frame_cacheable(f)) return;
    Layer *l = &g_layers[0];
    for (int i = 0; i < 2; i++) if (g_layers[i].key == key) { l = &g_layers[i]; goto found; }
    for (int i = 1; i < 2; i++) if (g_layers[i].used < l->used) l = &g_layers[i];
found:
    l->key = 0;
    if (!image_take(&l->im, f)) {
        /* No memory: give up the other slot and try once more. */
        for (int i = 0; i < 2; i++) if (&g_layers[i] != l) { image_free(&g_layers[i].im); g_layers[i].key = 0; }
        if (!image_take(&l->im, f)) return;
    }
    l->key = key;
    l->used = ++g_layer_clock;
}

int bl_layer_cached(uint64_t key, const BlFrame *f)
{
    if (!frame_cacheable(f)) return 0;
    for (int i = 0; i < 2; i++)
        if (g_layers[i].key == key && image_fits(&g_layers[i].im, f)) return 1;
    return 0;
}

/* dst += (spr - base) * w / 128 per byte, saturating; w 0..128. With w 128
 * the change is added whole. spr and base may be dst itself (a mix). */
static void add_change_weighted(uint32_t *dst, const uint32_t *spr, const uint32_t *base, size_t n, int w)
{
    size_t x = 0;
#ifdef __SSE2__
    const __m128i zero = _mm_setzero_si128(), wv = _mm_set1_epi16((short)w);
    for (; x + 4 <= n; x += 4) {
        __m128i s = _mm_loadu_si128((const __m128i *)(const void *)(spr + x));
        __m128i b = _mm_loadu_si128((const __m128i *)(const void *)(base + x));
        __m128i d = _mm_loadu_si128((const __m128i *)(const void *)(dst + x));
        __m128i lo = _mm_srai_epi16(_mm_mullo_epi16(_mm_sub_epi16(_mm_unpacklo_epi8(s, zero), _mm_unpacklo_epi8(b, zero)), wv), 7);
        __m128i hi = _mm_srai_epi16(_mm_mullo_epi16(_mm_sub_epi16(_mm_unpackhi_epi8(s, zero), _mm_unpackhi_epi8(b, zero)), wv), 7);
        lo = _mm_add_epi16(_mm_unpacklo_epi8(d, zero), lo);
        hi = _mm_add_epi16(_mm_unpackhi_epi8(d, zero), hi);
        _mm_storeu_si128((__m128i *)(void *)(dst + x), _mm_packus_epi16(lo, hi));
    }
#endif
    for (; x < n; x++) {
        uint32_t s = spr[x], b = base[x], d = dst[x], r = 0;
        for (int sh = 0; sh < 32; sh += 8) {
            int c = (int)((d >> sh) & 255) + ((((int)((s >> sh) & 255) - (int)((b >> sh) & 255)) * w) >> 7);
            r |= (uint32_t)(c < 0 ? 0 : c > 255 ? 255 : c) << sh;
        }
        dst[x] = r;
    }
}

void bl_frame_mix(BlFrame *f, const uint8_t *from, int w)
{
    if (!f || !f->pixels || !from || w <= 0) return;
    if (w > 128) w = 128;
    for (uint32_t y = 0; y < f->height; y++) {
        uint32_t *d = (uint32_t *)(void *)(f->pixels + (size_t)y * f->stride);
        add_change_weighted(d, (const uint32_t *)(const void *)(from + (size_t)y * f->stride), d, f->width, w);
    }
}

/* One saved rectangle of a frame (what lies under a panel). */
static struct { uint64_t key; int x, y, w, h; uint32_t *px; } g_rect;

static void rect_free(void)
{
    if (g_rect.px) cache_free(g_rect.px, (size_t)g_rect.w * (size_t)g_rect.h * 4);
    memset(&g_rect, 0, sizeof g_rect);
}

int bl_rect_save(const BlFrame *f, uint64_t key, int x, int y, int w, int h)
{
    if (!frame_cacheable(f) || w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > (int)f->width || y + h > (int)f->height) return 0;
    if (!g_rect.px || g_rect.w != w || g_rect.h != h) {
        rect_free();
        g_rect.px = cache_alloc((size_t)w * (size_t)h * 4);
        if (!g_rect.px) return 0;
        g_rect.w = w; g_rect.h = h;
    }
    g_rect.key = key; g_rect.x = x; g_rect.y = y;
    for (int r = 0; r < h; r++) memcpy(g_rect.px + (size_t)r * (size_t)w, px_at((BlFrame *)f, x, y + r), (size_t)w * 4);
    return 1;
}

int bl_rect_restore(BlFrame *f, uint64_t key, int x, int y, int w, int h)
{
    if (!frame_cacheable(f) || !g_rect.px || g_rect.key != key || g_rect.x != x || g_rect.y != y || g_rect.w != w || g_rect.h != h)
        return 0;
    for (int r = 0; r < h; r++) memcpy(px_at(f, x, y + r), g_rect.px + (size_t)r * (size_t)w, (size_t)w * 4);
    return 1;
}

uint64_t bl_layer_key(const char *screen, uint64_t extra)
{
    uint64_t h = 1469598103934665603ull;
    for (const unsigned char *p = (const unsigned char *)screen; *p; p++) h = (h ^ *p) * 1099511628211ull;
    for (int i = 0; i < 8; i++) h = (h ^ ((extra >> (8 * i)) & 255)) * 1099511628211ull;
    return h | 1;                                         /* never 0 */
}

/* What the frame shows, set by each screen's draw: a question drawn over it
 * right after can use its own cached layer. */
static uint64_t g_frame_key;
static const uint8_t *g_frame_px;

uint64_t bl_hash_bytes(uint64_t h, const void *p, size_t n)
{
    const unsigned char *c = p;
    for (size_t i = 0; i < n; i++) h = (h ^ c[i]) * 1099511628211ull;
    return h;
}

void bl_frame_key_set(const BlFrame *f, uint64_t key)
{
    g_frame_key = key;
    g_frame_px = f ? f->pixels : NULL;
}

/* The key of what f shows (0 unknown); it is used once. */
static uint64_t frame_key_take(const BlFrame *f)
{
    uint64_t k = f && f->pixels == g_frame_px ? g_frame_key : 0;
    g_frame_key = 0;
    g_frame_px = NULL;
    return k;
}

void bl_screens_set_cache(int on) { g_cache_on = on != 0; }

static void start_release(void);
static void start_release_base(void);

void bl_screens_release_layers(void)
{
    for (int i = 0; i < 2; i++) { image_free(&g_layers[i].im); g_layers[i].key = 0; g_layers[i].used = 0; }
    rect_free();
    start_release();
}

void bl_screens_release(void)
{
    bl_screens_release_layers();
    start_release_base();
    for (int i = 0; i < 2; i++) image_free(&g_bg[i]);
    free(g_small_logo);
    g_small_logo = NULL;
    bl_ui_release();
}

size_t bl_screens_cache_bytes(void)
{
    size_t n = g_cache_bytes + bl_ui_cache_bytes();
    if (g_small_logo) n += (size_t)g_small_logo_w * SMALL_LOGO_H * 3;
    return n;
}

/* ------------------------------------------------------------------ */
/* Start carousel                                                     */
/* ------------------------------------------------------------------ */

static const BlStringId CARD_NAME[BL_CARD_COUNT] = { BL_S_CARD_SETTINGS, BL_S_CARD_PLAY, BL_S_CARD_PATCH };
static const BlStringId CARD_BAND[BL_CARD_COUNT] = { BL_S_BAND_SETTINGS, BL_S_BAND_PLAY, BL_S_BAND_PATCH };
static const BlIcon CARD_ICON[BL_CARD_COUNT] = { BL_ICON_GEAR, BL_ICON_PLAY, BL_ICON_WRENCH };

void bl_start_init(BlStartState *st)
{
    memset(st, 0, sizeof *st);
    st->focus = st->target = BL_CARD_PLAY;
    st->pos = st->from = (float)BL_CARD_PLAY;
    st->band = BL_S_BAND_PLAY;
}

BlStartEvent bl_start_input(BlStartState *st, uint32_t pressed, int readiness)
{
    if (pressed & BL_PAD_LEFT && st->focus > 0) { st->focus--; st->band = CARD_BAND[st->focus]; }
    if (pressed & BL_PAD_RIGHT && st->focus < BL_CARD_COUNT - 1) { st->focus++; st->band = CARD_BAND[st->focus]; }
    if (!(pressed & BL_PAD_CROSS)) return BL_START_NONE;
    if (st->focus == BL_CARD_PLAY) {
        if (readiness == BL_READY_OK) return BL_START_PLAY;
        st->band = readiness == BL_READY_UNPATCHED ? BL_S_GAME_UNPATCHED
                 : readiness == BL_READY_UNSUPPORTED ? BL_S_GAME_UNSUPPORTED : BL_S_GAME_NOT_FOUND;
        st->focus = BL_CARD_PATCH;
        return BL_START_NONE;
    }
    return BL_START_OPEN;                    /* patch or settings */
}

/* Owner finding 34: time-based, eased out (cubic), whatever the frame
 * rate; a new focus during a slide starts again from where the carousel is. */
int bl_start_animate(BlStartState *st, uint64_t now_ns)
{
    if (st->target != st->focus) { st->target = st->focus; st->from = st->pos; st->t0 = now_ns; }
    float target = (float)st->focus;
    if (st->pos == target) return 0;
    uint64_t dt = now_ns > st->t0 ? now_ns - st->t0 : 0;
    if (dt >= BL_SLIDE_NS) { st->pos = target; return 1; }
    float x = 1.0f - (float)dt / (float)BL_SLIDE_NS;
    st->pos = st->from + (target - st->from) * (1.0f - x * x * x);
    return 1;
}

/* Interpolated geometry for a card at carousel offset k (0 = centre). */
typedef struct Slot { float cx, w, h, tilt; } Slot;

static Slot slot_at(float k)
{
    static const Slot S[3] = { { 0.0f, 416, 456, 0.0f }, { 425.0f, 330, 380, 0.8f }, { 740.0f, 260, 300, 1.0f } };
    float a = fabsf(k);
    int i = a >= 2.0f ? 1 : (int)a;
    float t = a >= 2.0f ? 1.0f : a - (float)i;
    Slot r = { S[i].cx + (S[i + 1].cx - S[i].cx) * t, S[i].w + (S[i + 1].w - S[i].w) * t,
               S[i].h + (S[i + 1].h - S[i].h) * t, S[i].tilt + (S[i + 1].tilt - S[i].tilt) * t };
    if (k < 0) { r.cx = -r.cx; r.tilt = -r.tilt; }
    return r;
}

/* oy: f starts at frame row oy (whole-row shifts draw the same pixels). */
static void draw_card(BlFrame *f, int card, float k, int oy)
{
    Slot s = slot_at(k);
    int W = (int)f->width, cy = 540 - oy;
    int cx = W / 2 + (int)lroundf(s.cx), w = (int)lroundf(s.w), h = (int)lroundf(s.h);
    int x = cx - w / 2, y = cy - h / 2;
    int centre = fabsf(k) < 0.5f;
    if (fabsf(s.tilt) < 0.02f) bl_glass(f, x, y, w, h, 26, centre);
    else bl_glass_tilted(f, x, y, w, h, 26, s.tilt);
    float squash = 1.0f - 0.3f * fabsf(s.tilt);
    int rx = (int)(0.29f * (float)w * squash), ry = (int)(0.29f * (float)w * (1.0f - 0.08f * fabsf(s.tilt)));
    icon_disc(f, cx, cy, rx, ry);
    bl_icon(f, CARD_ICON[card], cx, cy, (int)(1.15f * (float)(rx < ry ? rx : ry)), BL_RGB_CARD);

    const char *name = bl_str(CARD_NAME[card]);
    BlFont font = centre ? BL_FONT_TITLE : BL_FONT_HEAD;
    int tw = bl_text_width(font, name), lh = bl_font_line_height(font);
    int ty = y - lh + (centre ? 8 : 0);
    if (centre) bl_text_glow(f, font, cx - tw / 2, ty, name, BL_RGB_CREAM, 0x806A40);
    else bl_text_glow(f, font, cx - tw / 2, ty, name, 0x3D86FF, 0x1A4AA0);
}

/* Where a card at an integer slot draws: glass with its glow, the icon
 * disc and the title above (with room for the title glow). */
typedef struct Box { int x0, y0, x1, y1; } Box;

static Box card_box(const BlFrame *f, int card, int slot)
{
    Slot s = slot_at((float)slot);
    int W = (int)f->width;
    int cx = W / 2 + (int)lroundf(s.cx), w = (int)lroundf(s.w), h = (int)lroundf(s.h);
    int x = cx - w / 2, y = 540 - h / 2, centre = slot == 0;
    BlFont font = centre ? BL_FONT_TITLE : BL_FONT_HEAD;
    int tw = bl_text_width(font, bl_str(CARD_NAME[card])), lh = bl_font_line_height(font);
    int ty = y - lh + (centre ? 8 : 0);
    Box b = { x - 36, ty - 12, x + w + 36, y + h + 36 };
    if (cx - tw / 2 - 12 < b.x0) b.x0 = cx - tw / 2 - 12;
    if (cx - tw / 2 + tw + 12 > b.x1) b.x1 = cx - tw / 2 + tw + 12;
    if (b.x0 < 0) b.x0 = 0;
    if (b.y0 < 0) b.y0 = 0;
    if (b.x1 > (int)f->width) b.x1 = (int)f->width;
    if (b.y1 > (int)f->height) b.y1 = (int)f->height;
    return b;
}

/* The Start screen without the badge icon, the status, the cards and the
 * band text: background, badge disc, logo, bar, band and hints. The base
 * keeps the icon's box as it was before the icon (the logo is clear of it). */
enum { ICON_X0 = BADGE_CX - 26 - 1, ICON_Y0 = BADGE_CY - 26 - 1, ICON_W = 55, ICON_H = 55 };
static uint32_t g_start_icon_under[ICON_W * ICON_H];

/* A card at an integer slot exactly as drawn over the Start base (only its
 * box). Where it differs from the base is the card. */
typedef struct Sprite {
    Box b;
    uint32_t *px;
    uint16_t *ink;     /* per row: first and one past the last column that differs from the base */
} Sprite;
static Sprite g_sprite[BL_CARD_COUNT][5];          /* slot -2..2 */
static unsigned g_sprite_keep[BL_CARD_COUNT];      /* bit per slot: used by the frame being drawn */

static size_t sprite_bytes(const Sprite *s)
{
    return ((size_t)(s->b.x1 - s->b.x0) + 1) * (size_t)(s->b.y1 - s->b.y0) * 4;   /* pixels, then the ink spans */
}

static inline const uint32_t *base_px(int x, int y)
{
    return (const uint32_t *)(const void *)(g_start_base.px + ((size_t)y * g_start_base.w + (size_t)x) * 4);
}

/* The ink span of each row of a finished sprite. */
static void sprite_ink(Sprite *s)
{
    size_t bw = (size_t)(s->b.x1 - s->b.x0), bh = (size_t)(s->b.y1 - s->b.y0);
    s->ink = (uint16_t *)(void *)(s->px + bw * bh);
    for (size_t y = 0; y < bh; y++) {
        const uint32_t *sp = s->px + y * bw, *bp = base_px(s->b.x0, s->b.y0 + (int)y);
        size_t l = 0, r = bw;
        while (l < bw && sp[l] == bp[l]) l++;
        while (r > l && sp[r - 1] == bp[r - 1]) r--;
        s->ink[2 * y] = (uint16_t)l; s->ink[2 * y + 1] = (uint16_t)r;
    }
}

static void start_release_base(void) { image_free(&g_start_base); }

static void build_abort(void);

static void start_release(void)
{
    build_abort();
    for (int c = 0; c < BL_CARD_COUNT; c++)
        for (int k = 0; k < 5; k++) {
            if (g_sprite[c][k].px) cache_free(g_sprite[c][k].px, sprite_bytes(&g_sprite[c][k]));
            memset(&g_sprite[c][k], 0, sizeof g_sprite[c][k]);
        }
}

static const BlHint START_HINTS[] = { { BL_KEY_CROSS, BL_S_HINT_SELECT }, { BL_KEY_LEFTRIGHT, BL_S_HINT_BROWSE }, { BL_KEY_SQUARE, BL_S_HINT_HELP } };

static int start_base(BlFrame *f)
{
    if (image_fits(&g_start_base, f)) return 1;
    bl_screen_background(f, BL_BG_START);
    top_bar_disc(f);
    for (int y = 0; y < ICON_H; y++) memcpy(g_start_icon_under + y * ICON_W, px_at(f, ICON_X0, ICON_Y0 + y), ICON_W * 4);
    top_bar_rest(f);
    bl_band_fill(f);
    bl_hints(f, START_HINTS, 3);
    if (!image_take(&g_start_base, f)) return 0;
    image_free(&g_bg[BL_BG_START]);                  /* only needed to build this */
    return 1;
}

/* The badge icon over the base copy: the box as before the icon, the icon. */
static void start_icon(BlFrame *f, BlIcon icon)
{
    for (int y = 0; y < ICON_H; y++) memcpy(px_at(f, ICON_X0, ICON_Y0 + y), g_start_icon_under + y * ICON_W, ICON_W * 4);
    top_bar_icon(f, icon);
}


static inline uint32_t sat_sub8(uint32_t a, uint32_t b)          /* per byte, saturating at 0 */
{
    uint32_t r = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        int v = (int)((a >> sh) & 255) - (int)((b >> sh) & 255);
        r |= (uint32_t)(v > 0 ? v : 0) << sh;
    }
    return r;
}

static inline uint32_t sat_add8(uint32_t a, uint32_t b)          /* per byte, saturating at 255 */
{
    uint32_t r = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        int v = (int)((a >> sh) & 255) + (int)((b >> sh) & 255);
        r |= (uint32_t)(v < 255 ? v : 255) << sh;
    }
    return r;
}

/* Still: the card's own pixels (where it differs from the base). */
static void paste_where_differs(uint32_t *dst, const uint32_t *spr, const uint32_t *base, size_t n)
{
    size_t x = 0;
#ifdef __SSE2__
    for (; x + 4 <= n; x += 4) {
        __m128i s = _mm_loadu_si128((const __m128i *)(const void *)(spr + x));
        __m128i b = _mm_loadu_si128((const __m128i *)(const void *)(base + x));
        __m128i d = _mm_loadu_si128((const __m128i *)(const void *)(dst + x));
        __m128i same = _mm_cmpeq_epi32(s, b);
        _mm_storeu_si128((__m128i *)(void *)(dst + x), _mm_or_si128(_mm_and_si128(same, d), _mm_andnot_si128(same, s)));
    }
#endif
    for (; x < n; x++) if (spr[x] != base[x]) dst[x] = spr[x];
}

/* Draw the card over the base (in a scratch strip of the box's rows) and
 * keep its box. */
static const Sprite *sprite_get(int card, int slot)
{
    if (slot < -2 || slot > 2 || !g_start_base.px) return NULL;
    Sprite *s = &g_sprite[card][slot + 2];
    if (s->px) return s;
    BlFrame geo = { g_start_base.px, g_start_base.w, g_start_base.h, g_start_base.w * 4 };
    Box b = card_box(&geo, card, slot);
    if (b.x1 <= b.x0 || b.y1 <= b.y0 || b.y0 < 170 || b.y1 > 872) return NULL;   /* clear of the top bar and band */
    size_t bw = (size_t)(b.x1 - b.x0), bh = (size_t)(b.y1 - b.y0), n = (bw + 1) * bh;   /* pixels and ink spans */
    uint32_t *px = cache_alloc(n * 4);
    if (!px) {
        /* Over the cap: the list screens' layers go first, then the card
         * images this frame does not use. */
        for (int i = 0; i < 2; i++) { image_free(&g_layers[i].im); g_layers[i].key = 0; }
        px = cache_alloc(n * 4);
        for (int c = 0; !px && c < BL_CARD_COUNT; c++)
            for (int k = 0; k < 5; k++) {
                Sprite *o = &g_sprite[c][k];
                if (!o->px || (g_sprite_keep[c] >> k & 1)) continue;
                cache_free(o->px, sprite_bytes(o));
                memset(o, 0, sizeof *o);
            }
        if (!px) px = cache_alloc(n * 4);
        if (!px) return NULL;
    }
    BlFrame strip = { malloc((size_t)geo.width * bh * 4), geo.width, (uint32_t)bh, geo.width * 4 };
    if (!strip.pixels) { cache_free(px, n * 4); return NULL; }
    for (size_t y = 0; y < bh; y++) memcpy(px_at(&strip, b.x0, (int)y), base_px(b.x0, b.y0 + (int)y), bw * 4);
    draw_card(&strip, card, (float)slot, b.y0);
    for (size_t y = 0; y < bh; y++) memcpy(px + y * bw, px_at(&strip, b.x0, (int)y), bw * 4);
    free(strip.pixels);
    s->b = b; s->px = px;
    sprite_ink(s);
    return s;
}

/* Build one card image the slides from this focus will need (idle time).
 * 1: one was built (more may follow); 0: nothing left to do. */
/* An idle-time card image in the making: drawn into a strip of the box's
 * rows one band of rows per call (each band runs the card's whole drawing
 * clipped to its rows: the same pixels), so no idle step stalls input. */
enum { BUILD_BAND = 40 };
static struct { int active, card, slot, row; Box b; BlFrame strip; } g_build;

static void build_abort(void)
{
    free(g_build.strip.pixels);
    memset(&g_build, 0, sizeof g_build);
}

/* One band of the build; 1 while building. */
static int build_step(void)
{
    int bh = g_build.b.y1 - g_build.b.y0, rows = bh - g_build.row < BUILD_BAND ? bh - g_build.row : BUILD_BAND;
    BlFrame band = { g_build.strip.pixels + (size_t)g_build.row * g_build.strip.stride, g_build.strip.width, (uint32_t)rows,
                     g_build.strip.stride };
    draw_card(&band, g_build.card, (float)g_build.slot, g_build.b.y0 + g_build.row);
    g_build.row += rows;
    if (g_build.row < bh) return 1;
    Sprite *s = &g_sprite[g_build.card][g_build.slot + 2];
    size_t bw = (size_t)(g_build.b.x1 - g_build.b.x0), n = (bw + 1) * (size_t)bh;
    uint32_t *px = s->px ? NULL : cache_alloc(n * 4);      /* built meanwhile by a slide: keep that one */
    if (px) {
        for (int y = 0; y < bh; y++) memcpy(px + (size_t)y * bw, px_at(&g_build.strip, g_build.b.x0, y), bw * 4);
        s->b = g_build.b; s->px = px;
        sprite_ink(s);
    }
    build_abort();
    return 1;
}

int bl_start_prepare(const BlStartState *st)
{
    if (!g_cache_on || !g_start_base.px || st->pos != (float)st->focus) return 0;
    if (g_build.active) return build_step();
    /* Every card image any focus uses (9, about 8 MB), the focus itself
     * first, then the ones next to it, then the far ones. */
    for (int d = 0; d < BL_CARD_COUNT; d++)
        for (int side = -1; side <= 1; side += 2) {
            int focus = st->focus + side * d;
            if (focus < 0 || focus >= BL_CARD_COUNT || (d == 0 && side > 0)) continue;
            for (int c = 0; c < BL_CARD_COUNT; c++) {
                int k = c - focus;
                if (k < -2 || k > 2 || g_sprite[c][k + 2].px) continue;
                BlFrame geo = { g_start_base.px, g_start_base.w, g_start_base.h, g_start_base.w * 4 };
                Box b = card_box(&geo, c, k);
                if (b.x1 <= b.x0 || b.y1 <= b.y0 || b.y0 < 170 || b.y1 > 872) return 0;
                size_t bw = (size_t)(b.x1 - b.x0), bh = (size_t)(b.y1 - b.y0);
                g_build.strip = (BlFrame){ malloc((size_t)geo.width * bh * 4), geo.width, (uint32_t)bh, geo.width * 4 };
                if (!g_build.strip.pixels) return 0;
                for (size_t y = 0; y < bh; y++) memcpy(px_at(&g_build.strip, b.x0, (int)y), base_px(b.x0, b.y0 + (int)y), bw * 4);
                g_build.active = 1; g_build.card = c; g_build.slot = k; g_build.row = 0; g_build.b = b;
                return build_step();
            }
        }
    return 0;
}

static void sprite_paste(BlFrame *f, const Sprite *s)
{
    size_t bw = (size_t)(s->b.x1 - s->b.x0);
    for (int y = s->b.y0; y < s->b.y1; y++) {
        size_t r = (size_t)(y - s->b.y0), l = s->ink[2 * r], e = s->ink[2 * r + 1];
        if (l < e) paste_where_differs(px_at(f, s->b.x0 + (int)l, y), s->px + r * bw + l, base_px(s->b.x0 + (int)l, y), e - l);
    }
}

/* One sprite's change, moved by (mx, my), added with weight w (0..128). */
static void sprite_move(BlFrame *f, const Sprite *s, int mx, int my, int w)
{
    if (w <= 0) return;
    int sw = s->b.x1 - s->b.x0;
    int X0 = s->b.x0 + mx, X1 = s->b.x1 + mx, Y0 = s->b.y0 + my, Y1 = s->b.y1 + my;
    int cx0 = X0 < 0 ? 0 : X0, cx1 = X1 > (int)f->width ? (int)f->width : X1;
    int cy0 = Y0 < 0 ? 0 : Y0, cy1 = Y1 > (int)f->height ? (int)f->height : Y1;
    if (cx1 <= cx0 || cy1 <= cy0) return;
    for (int Y = cy0; Y < cy1; Y++) {
        int sy = Y - my, r = sy - s->b.y0;
        int a = s->b.x0 + s->ink[2 * r] + mx, e = s->b.x0 + s->ink[2 * r + 1] + mx;   /* the row's ink, moved */
        if (a < cx0) a = cx0;
        if (e > cx1) e = cx1;
        if (a >= e) continue;
        add_change_weighted(px_at(f, a, Y), s->px + (size_t)r * (size_t)sw + (size_t)(a - mx - s->b.x0),
                            base_px(a - mx, sy), (size_t)(e - a), w);
    }
}

/* Slide: the card between two slots. The images at both slots move to the
 * card's place between them and cross-fade from one to the other by t (a
 * true per-pixel blend, owner finding 34: no dither pattern). Only their
 * change to the base is added, so the street behind stays in place. */
static void sprite_slide(BlFrame *f, const Sprite *a, Box abox, const Sprite *b, Box bbox, float t)
{
    float acx = (float)(abox.x0 + abox.x1) * 0.5f, acy = (float)(abox.y0 + abox.y1) * 0.5f;
    float bcx = (float)(bbox.x0 + bbox.x1) * 0.5f, bcy = (float)(bbox.y0 + bbox.y1) * 0.5f;
    float cx = acx + (bcx - acx) * t, cy = acy + (bcy - acy) * t;
    int wb = (int)lroundf(t * 128.0f);            /* b's weight */
    if (!a || !b) wb = a ? 0 : 128;               /* one image not built yet: the other only */
    if (a) sprite_move(f, a, (int)lroundf(cx - acx), (int)lroundf(cy - acy), 128 - wb);
    if (b) sprite_move(f, b, (int)lroundf(cx - bcx), (int)lroundf(cy - bcy), wb);
}

uint64_t bl_start_key(const BlStartState *st, const BlStatus *status)
{
    /* What a frame shows: not when its slide began. */
    uint64_t h = bl_layer_key("start", 0);
    h = bl_hash_bytes(h, &st->focus, sizeof st->focus);
    h = bl_hash_bytes(h, &st->pos, sizeof st->pos);
    h = bl_hash_bytes(h, &st->band, sizeof st->band);
    return bl_hash_bytes(h, status, sizeof *status);
}

int bl_start_last_band = -1;
BlStatus bl_start_last_status;
float bl_start_last_pos;

static int start_items(const BlStatus *status, BlTopItem *items, char *net, size_t net_cap, char *pads, size_t pads_cap)
{
    int n = 0;
    if (status->logging) items[n++] = (BlTopItem){ bl_str(BL_S_STATUS_LOGGING_ON), BL_RGB_AMBER, 2 };
    if (status->readiness == BL_READY_NO_GAME) {
        items[n++] = (BlTopItem){ bl_str(BL_S_STATUS_NOT_FOUND), BL_RGB_AMBER, 2 };
    } else {
        items[n++] = (BlTopItem){ bl_str(BL_S_STATUS_FOUND), 0xE8F0FF, 1 };
        /* Plain words: Not patched / Ready - offline / Ready - online and
         * offline / Online files missing - see Help. */
        if (status->online == BL_ONLINE_PARTIAL)
            items[n++] = (BlTopItem){ bl_str(BL_S_STATUS_ONLINE_MISSING), BL_RGB_AMBER, 2 };
        else if (status->readiness != BL_READY_OK)
            items[n++] = (BlTopItem){ bl_str(BL_S_STATUS_NOT_PATCHED), BL_RGB_AMBER, 2 };
        else
            items[n++] = (BlTopItem){ bl_str(status->online == BL_ONLINE_READY ? BL_S_STATUS_READY_ONLINE : BL_S_STATUS_PATCHED), 0xE8F0FF, 1 };
    }
    if (status->net_up) {
        snprintf(net, net_cap, "%s %s", bl_str(BL_S_STATUS_ONLINE), status->ipv4);
        items[n++] = (BlTopItem){ net, 0xE8F0FF, 1 };
    } else {
        items[n++] = (BlTopItem){ bl_str(BL_S_STATUS_OFFLINE), BL_RGB_AMBER, 2 };
    }
    if (status->pads_known) {   /* split screen: each pad needs a signed-in user */
        snprintf(pads, pads_cap, bl_str_n(BL_S_STATUS_PAD_ONE, BL_S_STATUS_PADS, status->pads), status->pads);
        items[n++] = (BlTopItem){ pads, status->pads ? 0xE8F0FF : BL_RGB_AMBER, status->pads ? 1 : 2 };
    }
    return n;
}

/* Cards far first, the centre card last. */
static void card_order(const BlStartState *st, int *order)
{
    for (int i = 0; i < BL_CARD_COUNT; i++) order[i] = i;
    for (int i = 0; i < BL_CARD_COUNT; i++)
        for (int j = i + 1; j < BL_CARD_COUNT; j++)
            if (fabsf((float)order[j] - st->pos) > fabsf((float)order[i] - st->pos)) { int t = order[i]; order[i] = order[j]; order[j] = t; }
}

/* The cached way; 0 when it cannot be used (f then holds nothing useful). */
static int start_draw_cached(BlFrame *f, const BlStartState *st, const BlTopItem *items, int n)
{
    if (!frame_cacheable(f) || f->width < 1280 || f->height < 1000 || st->focus < 0 || st->focus >= BL_CARD_COUNT) return 0;
    const char *band = bl_str((BlStringId)st->band);
    if (bl_band_text_bottom(f, band) > (int)f->height - 104) return 0;       /* would reach the hints */
    if (!start_base(f)) return 0;
    /* The sprites this frame needs (built over the base, f is scratch). */
    int order[BL_CARD_COUNT];
    const Sprite *sa[BL_CARD_COUNT] = { 0 }, *sb[BL_CARD_COUNT] = { 0 };
    float tt[BL_CARD_COUNT] = { 0 };
    int ka_of[BL_CARD_COUNT], kb_of[BL_CARD_COUNT];
    card_order(st, order);
    for (int i = 0; i < BL_CARD_COUNT; i++) {
        int c = order[i];
        float k = (float)c - st->pos;
        ka_of[i] = kb_of[i] = 99;
        g_sprite_keep[i] = 0;
        if (fabsf(k) > 2.5f) continue;
        int ka = (int)floorf(k), kb = ka + 1;
        float t = k - (float)ka;
        if (ka < -2) { ka = -2; kb = -2; }
        if (kb > 2) kb = 2;
        if (t == 0.0f) kb = ka;
        ka_of[i] = ka; kb_of[i] = kb; tt[i] = t;
    }
    for (int i = 0; i < BL_CARD_COUNT; i++)
        if (ka_of[i] != 99) g_sprite_keep[order[i]] |= 1u << (ka_of[i] + 2) | 1u << (kb_of[i] + 2);
    /* A still frame needs every image (it is exact). A slide frame builds
     * at most one missing image; until the other is there a card moves
     * with the one it has (idle time builds them all, bl_start_prepare). */
    int still = st->pos == (float)st->focus, budget = 1;
    BlFrame geo = { g_start_base.px, g_start_base.w, g_start_base.h, g_start_base.w * 4 };
    for (int i = 0; i < BL_CARD_COUNT; i++) {
        int c = order[i], ka = ka_of[i], kb = kb_of[i];
        if (ka == 99) continue;
        if (still || kb == ka) {
            if (!(sa[i] = sprite_get(c, ka))) return 0;
            continue;
        }
        sa[i] = g_sprite[c][ka + 2].px ? &g_sprite[c][ka + 2] : NULL;
        sb[i] = g_sprite[c][kb + 2].px ? &g_sprite[c][kb + 2] : NULL;
        if (!sa[i] && budget > 0) { budget--; sa[i] = sprite_get(c, ka); }
        if (!sb[i] && budget > 0) { budget--; sb[i] = sprite_get(c, kb); }
        if (!sa[i] && !sb[i] && !(sa[i] = sprite_get(c, ka))) return 0;
    }
    image_put(&g_start_base, f);
    start_icon(f, CARD_ICON[st->focus]);
    bl_top_bar_items(f, items, n);
    for (int i = 0; i < BL_CARD_COUNT; i++) {
        int c = order[i];
        if (ka_of[i] == 99) continue;
        if (ka_of[i] == kb_of[i]) sprite_paste(f, sa[i]);
        else sprite_slide(f, sa[i], card_box(&geo, c, ka_of[i]), sb[i], card_box(&geo, c, kb_of[i]), tt[i]);
    }
    bl_band_text(f, band);
    return 1;
}

void bl_start_draw(BlFrame *f, const BlStartState *st, const BlStatus *status)
{
    if (!f || !f->pixels || !st || !status) return;
    bl_start_last_band = st->band;
    bl_start_last_status = *status;
    bl_start_last_pos = st->pos;

    char net[64], pads[48];
    BlTopItem items[7];
    int n = start_items(status, items, net, sizeof net, pads, sizeof pads);
    bl_frame_key_set(f, bl_start_key(st, status));
    if (!start_draw_cached(f, st, items, n)) {
        bl_screen_background(f, BL_BG_START);
        bl_top_bar(f, CARD_ICON[st->focus], items, n);
        int order[BL_CARD_COUNT];
        card_order(st, order);
        for (int i = 0; i < BL_CARD_COUNT; i++) {
            float k = (float)order[i] - st->pos;
            if (fabsf(k) <= 2.5f) draw_card(f, order[i], k, 0);
        }
        bl_band(f, bl_str((BlStringId)st->band));
        bl_hints(f, START_HINTS, 3);
    }
    /* No build label (owner finding 6, 2026-11-15): the build id is in
     * events.log and the support bundle only. */
}

/* ---- a question over the current screen ---- */

BlDialog bl_dialog_last_drawn;

void bl_dialog_init(BlDialog *d, int body, int first, int second, int circle)
{
    memset(d, 0, sizeof *d);
    d->body = body; d->first = first; d->second = second;
    d->circle = circle == 1 ? 1 : 2;
}

int bl_dialog_input(BlDialog *d, uint32_t pressed)
{
    if (pressed & BL_PAD_LEFT) d->focus = 0;
    if (pressed & BL_PAD_RIGHT && d->second >= 0) d->focus = 1;
    if (pressed & BL_PAD_CROSS) return d->focus + 1;
    if (pressed & BL_PAD_CIRCLE) return d->circle;
    return 0;
}

/* The question's parts in drawing order: dimming and glass; the question
 * and buttons; the strip over the screen's hints and the dialog's hints. */
typedef struct DialogGeo { int W, H, x, y, w, h, bx, by; const char *body, *a, *b; } DialogGeo;

static DialogGeo dialog_geo(const BlFrame *f, const BlDialog *d)
{
    DialogGeo g;
    g.W = (int)f->width; g.H = (int)f->height; g.w = 1100; g.h = 420;
    g.x = (g.W - g.w) / 2; g.y = (g.H - g.h) / 2 - 40;
    g.body = d->body_text[0] ? d->body_text : bl_str((BlStringId)d->body);
    g.a = bl_str((BlStringId)d->first); g.b = d->second >= 0 ? bl_str((BlStringId)d->second) : NULL;
    g.bx = g.x + 60; g.by = g.y + g.h - 120;
    return g;
}

static const BlHint DIALOG_HINTS[] = { { BL_KEY_CROSS, BL_S_HINT_SELECT }, { BL_KEY_LEFTRIGHT, BL_S_HINT_CHANGE } };

static void dialog_question(BlFrame *f, const BlDialog *d, const DialogGeo *g)
{
    bl_note(f, g->x + 60, g->y + 60, g->w - 120, g->body, BL_RGB_WHITE, BL_FONT_BODY);
    bl_button(f, g->bx, g->by, g->a, d->focus == 0);
    if (g->b) bl_button(f, g->bx + bl_text_width(BL_FONT_BODY, g->a) + 160, g->by, g->b, d->focus == 1);
}

static void dialog_hints(BlFrame *f, const DialogGeo *g)
{
    bl_fill_alpha(f, 0, g->H - 68 - 50, g->W, 64, 0x05080F, 235);   /* the screen's own hints go */
    bl_hints(f, DIALOG_HINTS, g->b ? 2 : 1);                         /* one button: nothing to change */
}

/* The question and buttons stay clear of the hint strip: then the strip
 * can be in the layer although it is drawn after them. */
static int dialog_clear(const DialogGeo *g)
{
    return bl_note(NULL, g->x + 60, g->y + 60, g->w - 120, g->body, BL_RGB_WHITE, BL_FONT_BODY) < g->H - 68 - 50 &&
           g->by + 66 + 22 < g->H - 68 - 50;
}

int bl_dialog_draw_cached(BlFrame *f, const BlDialog *d, uint64_t under)
{
    if (!f || !f->pixels || !d || !under) return 0;
    DialogGeo g = dialog_geo(f, d);
    if (!dialog_clear(&g) || !bl_layer_restore(f, bl_layer_key(g.b ? "dialog" : "dialog1", under))) return 0;
    bl_dialog_last_drawn = *d;
    dialog_question(f, d, &g);
    return 1;
}

void bl_dialog_draw(BlFrame *f, const BlDialog *d)
{
    bl_dialog_last_drawn = *d;
    DialogGeo g = dialog_geo(f, d);
    /* Cached: the screen under it (known by its key) dimmed with the glass,
     * the hint strip and hints; then the question and buttons. */
    uint64_t under = frame_key_take(f);
    int clear = under && dialog_clear(&g);
    uint64_t key = bl_layer_key(g.b ? "dialog" : "dialog1", under);
    if (clear && bl_layer_restore(f, key)) {
        dialog_question(f, d, &g);
        return;
    }
    bl_fill_alpha(f, 0, 0, g.W, g.H, 0x05080F, 150);
    bl_glass(f, g.x, g.y, g.w, g.h, 24, 1);
    if (clear) {
        dialog_hints(f, &g);
        bl_layer_save(f, key);
        dialog_question(f, d, &g);
        return;
    }
    dialog_question(f, d, &g);
    dialog_hints(f, &g);
}

/* bl_note with key caps: {square} and {cross} drawn as the hint bar's caps
 * (the font has no such glyphs), the words wrapped to w. f NULL: measure. */
int bl_note_keys(BlFrame *f, int x, int y, int w, const char *text, uint32_t rgb, BlFont font)
{
    static const struct { const char *token; BlKey key; } KEYS[] = { { "{square}", BL_KEY_SQUARE }, { "{cross}", BL_KEY_CROSS } };
    const int lh = bl_font_line_height(font), cap_h = 36, space = bl_text_width(font, " ");
    const int row = lh > cap_h ? lh : cap_h;
    int cx = x;
    const char *p = text ? text : "";
    while (*p) {
        int key = -1;
        size_t tl = 0;
        for (size_t k = 0; k < sizeof KEYS / sizeof KEYS[0]; k++)
            if (!strncmp(p, KEYS[k].token, strlen(KEYS[k].token))) { key = (int)KEYS[k].key; tl = strlen(KEYS[k].token); }
        const char *end;
        char word[96];
        int ww;
        if (key >= 0) { end = p + tl; ww = bl_key_cap(NULL, (BlKey)key, 0, 0); word[0] = 0; }
        else {
            end = p;
            while (*end && *end != ' ' && !(end > p && *end == '{')) end++;
            size_t n = (size_t)(end - p) < sizeof word - 1 ? (size_t)(end - p) : sizeof word - 1;
            memcpy(word, p, n); word[n] = 0;
            ww = bl_text_width(font, word);
        }
        if (cx > x && cx + ww > x + w) { cx = x; y += row; }
        if (key >= 0) bl_key_cap(f, (BlKey)key, cx, y + (row - cap_h) / 2);
        else if (f) bl_text(f, font, cx, y + (row - lh) / 2, word, rgb);
        cx += ww;
        p = end;
        while (*p == ' ') { cx += space; p++; }
    }
    return y + row;
}
