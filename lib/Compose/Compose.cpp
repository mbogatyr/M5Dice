#include "Compose.h"

#include <math.h>

namespace {

inline uint16_t swap16(uint16_t c) { return static_cast<uint16_t>((c >> 8) | (c << 8)); }

// Mixes color over a stored (swapped) pixel by alpha 0..255.
inline void blendPixel(uint16_t &stored, uint16_t color, int alpha) {
    const uint16_t bg = swap16(stored);
    const int br = bg >> 11, bgn = (bg >> 5) & 0x3F, bb = bg & 0x1F;
    const int fr = color >> 11, fg = (color >> 5) & 0x3F, fb = color & 0x1F;
    const int r = br + ((fr - br) * alpha + 127) / 255;
    const int g = bgn + ((fg - bgn) * alpha + 127) / 255;
    const int b = bb + ((fb - bb) * alpha + 127) / 255;
    stored = swap16(static_cast<uint16_t>((r << 11) | (g << 5) | b));
}

} // namespace

void blendMask(const Canvas &c, const AlphaMask &mask, int x, int y, uint16_t color,
               uint8_t opacity) {
    if (opacity == 0) {
        return;
    }
    for (int my = 0; my < mask.height; ++my) {
        const int py = y + my;
        if (py < c.clipY0 || py >= c.clipY1) {
            continue;
        }
        const uint8_t *row = mask.alpha + my * mask.width;
        uint16_t *out = c.pixels + py * c.width;
        for (int mx = 0; mx < mask.width; ++mx) {
            const int px = x + mx;
            if (px < c.clipX0 || px >= c.clipX1 || row[mx] == 0) {
                continue;
            }
            blendPixel(out[px], color, row[mx] * opacity / 255);
        }
    }
}

void fillCapsule(const Canvas &c, float x, float y, float w, float h, uint16_t color,
                 uint8_t opacity) {
    const float r = h * 0.5f;
    const float cy = y + r;
    const float left = x + r, right = x + w - r; // centers of the end circles
    int y0 = static_cast<int>(floorf(y)), y1 = static_cast<int>(ceilf(y + h));
    int x0 = static_cast<int>(floorf(x)), x1 = static_cast<int>(ceilf(x + w));
    y0 = y0 < c.clipY0 ? c.clipY0 : y0;
    x0 = x0 < c.clipX0 ? c.clipX0 : x0;
    y1 = y1 > c.clipY1 ? c.clipY1 : y1;
    x1 = x1 > c.clipX1 ? c.clipX1 : x1;
    for (int py = y0; py < y1; ++py) {
        const float dy = py + 0.5f - cy;
        for (int px = x0; px < x1; ++px) {
            const float fx = px + 0.5f;
            const float nearest = fx < left ? left : (fx > right ? right : fx);
            const float dx = fx - nearest;
            const float d = sqrtf(dx * dx + dy * dy) - r; // < 0 inside
            float cover = 0.5f - d;
            cover = cover < 0 ? 0 : (cover > 1 ? 1 : cover);
            if (cover > 0) {
                blendPixel(c.pixels[py * c.width + px], color,
                           static_cast<int>(cover * opacity + 0.5f));
            }
        }
    }
}

void fillRect(const Canvas &c, int x, int y, int w, int h, uint16_t color, uint8_t opacity) {
    const int x0 = x < c.clipX0 ? c.clipX0 : x;
    const int y0 = y < c.clipY0 ? c.clipY0 : y;
    const int x1 = x + w > c.clipX1 ? c.clipX1 : x + w;
    const int y1 = y + h > c.clipY1 ? c.clipY1 : y + h;
    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            blendPixel(c.pixels[py * c.width + px], color, opacity);
        }
    }
}

void tint(const Canvas &c, int y0, int y1, uint16_t color, uint8_t opacity) {
    fillRect(c, 0, y0, c.width, y1 - y0, color, opacity);
}
