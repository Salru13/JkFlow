/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * Blur launcher software renderer.
 *
 * Draws into a CPU frame in BGRX byte order (one uint32_t per pixel,
 * value 0x00RRGGBB on little-endian). Every function clips to the frame,
 * so callers may pass rectangles that are partly or fully off screen.
 * Colours are 0xRRGGBB. Alpha values are 0..255.
 *
 * The look follows Blur's own menus (design doc section 7): glowing blue
 * glass cards and panels, pill buttons, red light trails over a night
 * street, rounded heavy type.
 */
#ifndef BL_UI_H
#define BL_UI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BlFrame {
    uint8_t *pixels;       /* first byte of row 0 */
    uint32_t width, height;
    uint32_t stride;       /* bytes per row, >= width * 4 */
} BlFrame;

/* Palette sampled from Blur (2010) menu screenshots, design doc section 7. */
enum {
    BL_RGB_CARD      = 0x0B286C,
    BL_RGB_TOPBAR    = 0x133763,
    BL_RGB_BUTTON    = 0x136DE0,
    BL_RGB_RIM       = 0x0A7CFC,
    BL_RGB_BADGE     = 0x4278D7,
    BL_RGB_CYAN      = 0x1BFEFF,
    BL_RGB_SUBTITLE  = 0x21BEFF,
    BL_RGB_CREAM     = 0xFFF4DC,
    BL_RGB_WHITE     = 0xFFFFFF,
    BL_RGB_DIM       = 0x818681,
    BL_RGB_AMBER     = 0xFFC955,
    BL_RGB_OFF       = 0x412E65,
    BL_RGB_OFF_TEXT  = 0x6A5AA0,
    BL_RGB_TRAIL     = 0xFF2A2A,
    BL_RGB_ERROR     = 0xFF3B3B
};

typedef enum BlFont {
    BL_FONT_NOTE = 0,  /* 22 px, ExtraBold */
    BL_FONT_BODY,      /* 28 px, ExtraBold */
    BL_FONT_ITEM,      /* 34 px, Black */
    BL_FONT_HEAD,      /* 40 px, Black */
    BL_FONT_TITLE,     /* 70 px, Black */
    BL_FONT_BAND,      /* 32 px, ExtraBold Italic */
    BL_FONT_COUNT
} BlFont;

typedef enum BlBackground {
    BL_BG_START = 0,   /* night street, horizontal red trails (main menu) */
    BL_BG_LIST,        /* night street, diagonal red trails (list menus) */
    BL_BG_BLACK        /* plain black (loader, start failed) */
} BlBackground;

void bl_fill(BlFrame *f, int x, int y, int w, int h, uint32_t rgb);
void bl_fill_alpha(BlFrame *f, int x, int y, int w, int h, uint32_t rgb, int alpha);
/* bl_fill_alpha with an alpha per column: alpha[0..w-1] (<= 0: unchanged). */
void bl_fill_alpha_cols(BlFrame *f, int x, int y, int w, int h, uint32_t rgb, const int *alpha);
void bl_gradient_v(BlFrame *f, int x, int y, int w, int h, uint32_t top, uint32_t bottom);

/* Glass card or panel: fill 0B286C, 3 px rim, outer blue glow and an inner
 * cyan glow strongest along the bottom edge. Focused: cyan rim, stronger glow. */
void bl_glass(BlFrame *f, int x, int y, int w, int h, int radius, int focused);

/* Side card of the Start carousel, turned away from the viewer.
 * turn in -1..1: negative = card on the left (its left edge is farther away). */
void bl_glass_tilted(BlFrame *f, int x, int y, int w, int h, int radius, float turn);

/* General rounded rectangle: vertical gradient fill (top->bottom) blended at
 * fill_alpha, a rim of rim_w pixels, and an additive outer glow of glow_r
 * pixels (glow_r 0: none). Used for the top bar, key caps and badges. */
void bl_round_rect(BlFrame *f, int x, int y, int w, int h, int radius,
                   uint32_t top, uint32_t bottom, int fill_alpha,
                   uint32_t rim, int rim_w, uint32_t glow, int glow_r);

/* Pill button or a value in a segmented row. */
void bl_pill(BlFrame *f, int x, int y, int w, int h, int selected);

/* Additive light trail from tail (x0,y0) to head (x1,y1), soft edges. */
void bl_trail(BlFrame *f, int x0, int y0, int x1, int y1, int thickness,
              uint32_t rgb, int alpha, int hot_head);

/* Soft round glow (street light, bokeh), additive. */
void bl_glow_dot(BlFrame *f, int cx, int cy, int radius, uint32_t rgb, int alpha);

/* Additive blit of an RGB image (3 bytes per pixel, rows packed): black stays
 * invisible. brightness_256: 256 = unchanged. */
void bl_blit_add(BlFrame *f, const uint8_t *rgb, int w, int h, int x, int y,
                 int brightness_256);

/* Bilinear resize of a packed RGB image into dst (dw*dh*3 bytes). */
void bl_image_scale(const uint8_t *src, int w, int h, uint8_t *dst, int dw, int dh);

/* Text. (x, y) is the top-left of the line box; the baseline is at
 * y + ascent. Returns the full advance in pixels (also for text that is
 * clipped). Invalid UTF-8 bytes and code points without a glyph are skipped. */
int bl_text(BlFrame *f, BlFont font, int x, int y, const char *utf8, uint32_t rgb);
int bl_text_glow(BlFrame *f, BlFont font, int x, int y, const char *utf8,
                 uint32_t rgb, uint32_t glow);
int bl_text_width(BlFont font, const char *utf8);
int bl_font_line_height(BlFont font);
int bl_font_ascent(BlFont font);

/* Glowing text keeps its halo images (a few hundred KB at most); this frees
 * them. bl_ui_cache_bytes: what they hold. */
void bl_ui_release(void);
size_t bl_ui_cache_bytes(void);

/* Static, deterministic background for a whole frame. */
void bl_background(BlFrame *f, BlBackground kind);

/* The original Blur logo with its glow (from the package icon), RGB on black. */
extern const uint8_t bl_logo_rgb[];
extern const int bl_logo_w, bl_logo_h;

#ifdef __cplusplus
}
#endif

#endif
