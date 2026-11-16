/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * Visual check of the bl_ui primitives: draws a Start-like and a list-like
 * composition and writes them as PNG, to compare by eye with
 * docs/launcher-mockups/index.html frames 1 and 4. Not a screen
 * implementation (that is bl_screens, plan Task 3/6).
 *
 *   preview_ui OUTDIR
 */
#include "bl_loader.h"
#include "bl_png.h"
#include "bl_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 1920
#define H 1080

static void centred(BlFrame *f, BlFont font, int cx, int y, const char *s, uint32_t rgb, uint32_t glow)
{
    int w = bl_text_width(font, s);
    if (glow) bl_text_glow(f, font, cx - w / 2, y, s, rgb, glow);
    else bl_text(f, font, cx - w / 2, y, s, rgb);
}

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "usage: preview_ui OUTDIR\n"); return 2; }
    BlFrame f = { malloc((size_t)W * H * 4), W, H, W * 4 };
    char path[4096];

    /* Start-like composition. */
    bl_background(&f, BL_BG_START);
    int lw = bl_logo_w * 120 / bl_logo_h;
    uint8_t *small = malloc((size_t)lw * 120 * 3);
    bl_image_scale(bl_logo_rgb, bl_logo_w, bl_logo_h, small, lw, 120);
    bl_glow_dot(&f, 178, 94, 60, BL_RGB_BADGE, 255);
    bl_blit_add(&f, small, lw, 120, 210, 34, 256);
    bl_pill(&f, 440, 79, 1350, 30, 0);
    bl_text(&f, BL_FONT_NOTE, 1500, 80, "PS4 Slim", BL_RGB_AMBER);
    bl_glass_tilted(&f, 90, 400, 260, 300, 26, -1.0f);
    bl_glass_tilted(&f, 390, 370, 330, 380, 26, -0.8f);
    bl_glass(&f, 752, 312, 416, 456, 26, 1);
    bl_glass_tilted(&f, 1220, 370, 330, 380, 26, 0.8f);
    bl_glow_dot(&f, 960, 540, 120, BL_RGB_CYAN, 255);
    centred(&f, BL_FONT_TITLE, 960, 220, "play", BL_RGB_CREAM, 0x806A40);
    centred(&f, BL_FONT_HEAD, 555, 296, "settings", 0x3D86FF, 0x1A4AA0);
    centred(&f, BL_FONT_HEAD, 1385, 296, "patch blur", 0x3D86FF, 0x1A4AA0);
    bl_fill_alpha(&f, 230, 872, 1460, 64, 0x3C5AA0, 107);
    centred(&f, BL_FONT_BAND, 960, 884, "Start Blur with your settings", BL_RGB_WHITE, 0);
    bl_text(&f, BL_FONT_BODY, 190, 990, "Select    Browse", BL_RGB_WHITE);
    snprintf(path, sizeof path, "%s/preview-start.png", argv[1]);
    bl_png_write(path, &f);

    /* List-like composition. */
    bl_background(&f, BL_BG_LIST);
    bl_pill(&f, 245, 192, 230, 54, 1);
    bl_text(&f, BL_FONT_ITEM, 290, 196, "settings", BL_RGB_WHITE);
    const char *items[] = { "Online server", "Player name", "Console model", "Stats overlay", "Logging" };
    for (int i = 0; i < 5; i++) {
        int y = 300 + i * 102;
        bl_glow_dot(&f, 190, y + 30, 34, 0x3C6EFF, 200);
        bl_text_glow(&f, BL_FONT_ITEM, 240, y, items[i], i == 0 ? BL_RGB_WHITE : BL_RGB_DIM, i == 0 ? 0x404040 : 0);
        if (i == 0) bl_text(&f, BL_FONT_BAND, 240, y + 44, "Private server  blur.example.net", BL_RGB_SUBTITLE);
    }
    bl_glass(&f, 760, 180, 1010, 670, 24, 0);
    bl_text_glow(&f, BL_FONT_HEAD, 804, 214, "online server", BL_RGB_CREAM, 0x806A40);
    bl_text(&f, BL_FONT_BODY, 824, 300, "Mode", 0xC9D6EE);
    bl_pill(&f, 1300, 292, 150, 44, 0); bl_text(&f, BL_FONT_NOTE, 1330, 300, "Official", 0x8FA3C8);
    bl_pill(&f, 1460, 292, 200, 44, 1); bl_text(&f, BL_FONT_NOTE, 1480, 300, "Private server", BL_RGB_WHITE);
    bl_pill(&f, 1670, 292, 70, 44, 0); bl_text(&f, BL_FONT_NOTE, 1688, 300, "Off", 0x8FA3C8);
    snprintf(path, sizeof path, "%s/preview-list.png", argv[1]);
    bl_png_write(path, &f);

    /* Loader-like composition: logo on black with light trails. */
    bl_background(&f, BL_BG_BLACK);
    int bw = 760, bh = bl_logo_h * 760 / bl_logo_w;
    uint8_t *big = malloc((size_t)bw * bh * 3);
    bl_image_scale(bl_logo_rgb, bl_logo_w, bl_logo_h, big, bw, bh);
    bl_trail(&f, -200, 340, 1100, 270, 5, BL_RGB_TRAIL, 230, 1);
    bl_trail(&f, 300, 700, 1700, 640, 4, 0x2A7BFF, 220, 1);
    bl_trail(&f, 900, 790, 2000, 740, 6, BL_RGB_TRAIL, 200, 1);
    bl_blit_add(&f, big, bw, bh, (W - bw) / 2, 475 - bh / 2, 290);
    centred(&f, BL_FONT_BAND, 960, 760, "Starting Blur\xE2\x80\xA6", BL_RGB_WHITE, 0);
    snprintf(path, sizeof path, "%s/preview-loader.png", argv[1]);
    bl_png_write(path, &f);

    /* The real loader and start-failed screens. */
    bl_loader_draw(&f, 1200);
    snprintf(path, sizeof path, "%s/preview-bl-loader.png", argv[1]);
    bl_png_write(path, &f);
    bl_error_draw(&f, NULL, "session-12.log");
    snprintf(path, sizeof path, "%s/preview-bl-error.png", argv[1]);
    bl_png_write(path, &f);

    free(big); free(small); free(f.pixels);
    return 0;
}
