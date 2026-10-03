#pragma once

#include <stdint.h>

// Drawing the screen's text and shapes over a finished RGB565 frame
// (byte-swapped, as an M5GFX sprite keeps it). The text arrives as alpha
// masks rendered on the host (tools/make_ui_assets.py), so no font reaches
// the firmware, and every edge is antialiased.

struct AlphaMask {
    int16_t width;
    int16_t height;
    int16_t baseline; // row of the text baseline, from the top
    const uint8_t *alpha;
};

// A frame and the part of it that may be drawn on, [clipX0, clipX1) x
// [clipY0, clipY1): text is blended, so drawing it twice over the same
// pixels would thicken it.
struct Canvas {
    uint16_t *pixels;
    int width;
    int height;
    int clipX0, clipY0, clipX1, clipY1;
};

// Plain (not swapped) RGB565 from 8-bit channels.
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

// Paints color through the mask with its top left corner at (x, y);
// opacity scales the whole mask. Everything is clipped to the canvas clip.
void blendMask(const Canvas &c, const AlphaMask &mask, int x, int y, uint16_t color,
               uint8_t opacity);

// A rounded rectangle whose ends are half circles, at fractional pixels.
void fillCapsule(const Canvas &c, float x, float y, float w, float h, uint16_t color,
                 uint8_t opacity);

void fillRect(const Canvas &c, int x, int y, int w, int h, uint16_t color, uint8_t opacity);

// Fades rows [y0, y1) of the canvas towards color.
void tint(const Canvas &c, int y0, int y1, uint16_t color, uint8_t opacity);
