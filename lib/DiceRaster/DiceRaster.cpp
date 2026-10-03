#include "DiceRaster.h"

#include <math.h>
#include <string.h>

// The board's FPU has no double: catch stray double literals.
#pragma GCC diagnostic warning "-Wdouble-promotion"

namespace {

constexpr float kD = DiceRaster::kCameraDistance;
constexpr int kW = DiceRaster::kWidth;
constexpr int kH = DiceRaster::kHeight;

// Direction towards the light: from the upper left, about 53 degrees above
// the table.
const Vec3 kLight = normalized(Vec3{-0.4f, -0.5f, 0.85f});

// The look picked in the prototype: ivory dice with black pips and a red
// one. Colors are linear (sRGB values raised to 2.2).
struct Style {
    Vec3 body, pip, pipOne;
    float pipRadius, pipOneRadius;
    float diffuse, ambient;
    float specular, shininess, sheen, sheenShininess;
    Vec3 envLow, envHigh; // what the die reflects near the horizon and above
    float softBox;        // brightness of the light's reflection
    float shadow;         // opacity of the shadow of a die on the table
};

Vec3 linear(float r, float g, float b) {
    return {powf(r, 2.2f), powf(g, 2.2f), powf(b, 2.2f)};
}

const Style kStyle = {
    linear(0.95f, 0.92f, 0.84f),
    linear(0.07f, 0.07f, 0.08f),
    linear(0.74f, 0.05f, 0.08f),
    0.18f,
    0.27f,
    0.75f,
    0.25f,
    0.35f,
    40.0f,
    0.03f,
    6.0f,
    {0.012f, 0.11f, 0.045f},
    {0.05f, 0.05f, 0.05f},
    5.0f,
    0.55f,
};

// 4 x 4 ordered dither thresholds, 0..15.
constexpr uint8_t kBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

inline uint16_t swap16(uint16_t c) { return static_cast<uint16_t>((c >> 8) | (c << 8)); }

// sRGB bytes to RGB565 with ordered dither at screen pixel (x, y), so the
// felt's smooth gradients do not band.
inline uint16_t encode565(int r, int g, int b, int x, int y) {
    const int d = kBayer[((y & 3) << 2) | (x & 3)];
    r += d >> 1;
    g += d >> 2;
    b += d >> 1;
    r = r > 255 ? 255 : r;
    g = g > 255 ? 255 : g;
    b = b > 255 ? 255 : b;
    return swap16(static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)));
}

inline void decode565(uint16_t stored, int &r, int &g, int &b) {
    const uint16_t c = swap16(stored);
    const int r5 = c >> 11, g6 = (c >> 5) & 0x3F, b5 = c & 0x1F;
    r = (r5 << 3) | (r5 >> 2);
    g = (g6 << 2) | (g6 >> 4);
    b = (b5 << 3) | (b5 >> 2);
}

uint32_t hash(int x, int y) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

float hash01(int x, int y) { return static_cast<float>(hash(x, y) >> 8) * (1.0f / 16777216.0f); }

// Felt in sRGB at a screen pixel: brightest under the light, with fibers.
Vec3 felt(int x, int y) {
    const float n = hash01(x, y) - 0.5f;
    const float dx = (static_cast<float>(x) - kW * 0.38f) / kW;
    const float dy = (static_cast<float>(y) - kH * 0.34f) / kH;
    const float r2 = dx * dx * 1.4f + dy * dy;
    float vignette = 1 - 1.1f * r2;
    vignette = vignette < 0.3f ? 0.3f : vignette;
    const float f = vignette * (1 + n * 0.12f + (hash01(x >> 1, y * 3) - 0.5f) * 0.08f);
    return {0.12f * f, 0.40f * f, 0.26f * f};
}

// Which of the 3 x 3 pip spots, (iu + 1) * 3 + (iv + 1), a value uses.
uint16_t pipMask(uint8_t value) {
    uint16_t mask = 0;
    const DiceMesh::Pip *p = DiceMesh::pips(value);
    for (int i = 0; i < value; ++i) {
        const int iu = p[i].u < -0.25f ? 0 : (p[i].u > 0.25f ? 2 : 1);
        const int iv = p[i].v < -0.25f ? 0 : (p[i].v > 0.25f ? 2 : 1);
        mask = static_cast<uint16_t>(mask | (1u << (iu * 3 + iv)));
    }
    return mask;
}

// The span [from, to] of row y inside a triangle, clipped to [x0, x1], and
// the barycentric weights of a and b at its first pixel. False if empty.
inline bool rowSpan(int y, int x0, int x1, float ax, float ay, float bx, float by, float cx,
                    float cy, float ia, float d0x, float d1x, float d2x, float inv0, float inv1,
                    float inv2, int &from, int &to, float &w0, float &w1) {
    const float py = y + 0.5f;
    const float px0 = x0 + 0.5f;
    w0 = ((bx - px0) * (cy - py) - (cx - px0) * (by - py)) * ia;
    w1 = ((cx - px0) * (ay - py) - (ax - px0) * (cy - py)) * ia;
    const float w2 = 1 - w0 - w1;
    // w + d * j >= -eps for all three weights, solved for j.
    float lo = 0, hi = static_cast<float>(x1 - x0);
    const float w[3] = {w0, w1, w2}, d[3] = {d0x, d1x, d2x}, inv[3] = {inv0, inv1, inv2};
    for (int i = 0; i < 3; ++i) {
        const float j = (-1e-4f - w[i]) * inv[i];
        if (d[i] > 0) {
            lo = j > lo ? j : lo;
        } else if (d[i] < 0) {
            hi = j < hi ? j : hi;
        } else if (w[i] < -1e-4f) {
            return false;
        }
    }
    if (lo > hi) {
        return false;
    }
    const int jFrom = ceilInt(lo), jTo = floorInt(hi);
    if (jFrom > jTo) {
        return false;
    }
    w0 += d0x * jFrom;
    w1 += d1x * jFrom;
    from = x0 + jFrom;
    to = x0 + jTo;
    return true;
}

// Highlights roll off instead of clipping.
float toneMap(float c) { return c < 0.85f ? c : 0.85f + 0.15f * (1 - expf(-(c - 0.85f) / 0.15f)); }

uint8_t toByte(float v) {
    v = v * 255 + 0.5f;
    return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

} // namespace

Rect unite(Rect a, Rect b) {
    if (a.empty()) {
        return b;
    }
    if (b.empty()) {
        return a;
    }
    return {a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1,
            a.y1 > b.y1 ? a.y1 : b.y1};
}

Rect intersect(Rect a, Rect b) {
    Rect r{a.x0 > b.x0 ? a.x0 : b.x0, a.y0 > b.y0 ? a.y0 : b.y0, a.x1 < b.x1 ? a.x1 : b.x1,
           a.y1 < b.y1 ? a.y1 : b.y1};
    return r.empty() ? Rect{0, 0, 0, 0} : r;
}

void DiceRaster::begin() {
    DiceMesh::build(mesh_);
    for (int tri = 0; tri < DiceMesh::kTriangles; ++tri) {
        bool flat = true;
        for (int k = 0; k < 3; ++k) {
            const int v = mesh_.triangle[tri * 3 + k];
            const float limit = 1 - DiceMesh::kRounding + 1e-4f;
            flat = flat && fabsf(mesh_.uv[v * 2]) <= limit && fabsf(mesh_.uv[v * 2 + 1]) <= limit;
        }
        flatTriangle_[tri] = flat ? 1 : 0;
    }
    for (uint8_t value = 1; value <= 6; ++value) {
        pipMasks_[value] = pipMask(value);
    }
    for (int i = 0; i < 2048; ++i) {
        toSrgb_[i] = toByte(powf(toneMap(static_cast<float>(i) / 1024.0f), 1 / 2.2f));
    }
    for (int i = 0; i < 4096; ++i) {
        linearToSrgb_[i] = toByte(powf(static_cast<float>(i) / 4095.0f, 1 / 2.2f));
    }
    for (int i = 0; i < 256; ++i) {
        srgbToLinear_[i] =
            static_cast<uint16_t>(powf(static_cast<float>(i) / 255.0f, 2.2f) * 65535.0f + 0.5f);
        // The shadow takes away light (linear); the frame holds sRGB.
        shadowKeep_[i] = static_cast<uint16_t>(
            powf(1 - static_cast<float>(i) / 255.0f, 1 / 2.2f) * 256.0f + 0.5f);
    }
    for (int i = 0; i < 1024; ++i) {
        const float x = static_cast<float>(i) / 1023.0f;
        specular_[i] = kStyle.specular * powf(x, kStyle.shininess) +
                       kStyle.sheen * powf(x, kStyle.sheenShininess);
    }
}

void DiceRaster::paintBackground(uint16_t *background) {
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const Vec3 c = felt(x, y);
            background[y * kW + x] = encode565(toByte(c.x), toByte(c.y), toByte(c.z), x, y);
        }
    }
}

Rect DiceRaster::bounds(const DiePose &die) const {
    const Vec3 p = die.position;
    const float s = die.size;
    // The die: its farthest point is 1.57 half sizes from the center.
    const float reach = 1.6f * s;
    const float near = p.z + reach;
    const float persp = kD / (kD - (near < kD - 1 ? near : kD - 1));
    const float cx = kW * 0.5f + p.x * kD / (kD - p.z);
    const float cy = kH * 0.5f + p.y * kD / (kD - p.z);
    const float r = reach * persp + 2;
    Rect dieRect{static_cast<int>(floorf(cx - r)), static_cast<int>(floorf(cy - r)),
                 static_cast<int>(ceilf(cx + r)), static_cast<int>(ceilf(cy + r))};

    // The shadow on the table, as drawShadows() lays it out.
    const float lift = p.z - s > 0 ? p.z - s : 0;
    const float kx = -kLight.x / kLight.z;
    const float ky = -kLight.y / kLight.z;
    const float margin = s * 1.45f + 2 * (2.5f + 0.3f * lift) + 2;
    const float ax = p.x + kx * lift, ay = p.y + ky * lift;
    const float bx = ax + 2 * s * kx, by = ay + 2 * s * ky;
    Rect shadowRect{static_cast<int>(floorf(kW * 0.5f + (ax < bx ? ax : bx) - margin)),
                    static_cast<int>(floorf(kH * 0.5f + (ay < by ? ay : by) - margin)),
                    static_cast<int>(ceilf(kW * 0.5f + (ax > bx ? ax : bx) + margin)),
                    static_cast<int>(ceilf(kH * 0.5f + (ay > by ? ay : by) + margin))};
    return intersect(unite(dieRect, shadowRect), Rect{0, 0, kW, kH});
}

void DiceRaster::fillBackground(const Target &t, const uint16_t *background) const {
    if (t.scale == 1) {
        for (int y = t.rowFrom; y < t.rowTo; ++y) {
            const int row = (t.originY + y) * kW + t.originX;
            memcpy(t.frame + row, background + row, static_cast<size_t>(t.width) * 2);
        }
        return;
    }
    for (int y = t.rowFrom; y < t.rowTo; ++y) {
        const uint16_t *src = background + (t.originY + y / t.scale) * kW + t.originX;
        uint8_t *dst = t.rgb + static_cast<size_t>(y) * t.width * 3;
        for (int x = 0; x < t.width; ++x) {
            int r, g, b;
            decode565(src[x / t.scale], r, g, b);
            dst[x * 3] = static_cast<uint8_t>(r);
            dst[x * 3 + 1] = static_cast<uint8_t>(g);
            dst[x * 3 + 2] = static_cast<uint8_t>(b);
        }
    }
}

void DiceRaster::darken(const Target &t, int x, int y, int alpha) const {
    const int keep = shadowKeep_[alpha];
    if (t.scale == 1) {
        // Straight on the 565 fields: the felt's grain hides the steps.
        uint16_t &px = t.frame[(t.originY + y) * kW + t.originX + x];
        const uint16_t c = swap16(px);
        const int r = ((c >> 11) * keep) >> 8, g = (((c >> 5) & 0x3F) * keep) >> 8;
        const int b = ((c & 0x1F) * keep) >> 8;
        px = swap16(static_cast<uint16_t>((r << 11) | (g << 5) | b));
        return;
    }
    uint8_t *p = t.rgb + (static_cast<size_t>(y) * t.width + x) * 3;
    p[0] = static_cast<uint8_t>((p[0] * keep) >> 8);
    p[1] = static_cast<uint8_t>((p[1] * keep) >> 8);
    p[2] = static_cast<uint8_t>((p[2] * keep) >> 8);
}

void DiceRaster::put(const Target &t, int x, int y, uint8_t r, uint8_t g, uint8_t b) const {
    if (t.scale == 1) {
        const int sx = t.originX + x, sy = t.originY + y;
        t.frame[sy * kW + sx] = encode565(r, g, b, sx, sy);
        return;
    }
    uint8_t *p = t.rgb + (static_cast<size_t>(y) * t.width + x) * 3;
    p[0] = r;
    p[1] = g;
    p[2] = b;
}

void DiceRaster::shadowPass(const Target &t, float cx, float cy, float sweepX, float sweepY,
                            float yaw, float half, float rounding, float softness,
                            float opacity) const {
    if (opacity <= 0.003f) {
        return;
    }
    rounding = rounding < half ? rounding : half;
    const float reach = half * 1.4143f + softness;
    const float k = static_cast<float>(t.scale);
    // Table coordinates of the sample (0, 0) and the step between samples.
    const float worldX0 = (0.5f / k) + t.originX - kW * 0.5f;
    const float worldY0 = (0.5f / k) + t.originY - kH * 0.5f;
    const float step = 1 / k;
    const float minX = (sweepX < 0 ? cx + sweepX : cx) - reach;
    const float maxX = (sweepX > 0 ? cx + sweepX : cx) + reach;
    const float minY = (sweepY < 0 ? cy + sweepY : cy) - reach;
    const float maxY = (sweepY > 0 ? cy + sweepY : cy) + reach;
    int x0 = floorInt((minX - worldX0) * k);
    int x1 = ceilInt((maxX - worldX0) * k) + 1;
    int y0 = floorInt((minY - worldY0) * k);
    int y1 = ceilInt((maxY - worldY0) * k) + 1;
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < t.rowFrom ? t.rowFrom : y0;
    x1 = x1 > t.width ? t.width : x1;
    y1 = y1 > t.rowTo ? t.rowTo : y1;
    if (y0 >= y1) {
        return;
    }

    // The footprint swept along the light is a hexagon (the square slid
    // along a segment) rounded by `rounding`. A convex polygon is where all
    // its edge planes agree, so the distance outside it is the largest plane
    // distance, or the distance to a corner when two planes stick out past
    // it: no square root except at the corners.
    int y_ = 0; // the row being drawn
    const float c = cosf(yaw), s = sinf(yaw);
    const float inner = half - rounding;
    float nx[6], ny[6], lift[6];
    int planes = 0;
    auto addPlanes = [&](float ax, float ay) {
        for (int sign = -1; sign <= 1; sign += 2) {
            const float x = ax * sign, y = ay * sign;
            const float along = x * sweepX + y * sweepY;
            nx[planes] = x;
            ny[planes] = y;
            lift[planes] = inner * (fabsf(x * c + y * s) + fabsf(-x * s + y * c)) +
                           (along > 0 ? along : 0);
            ++planes;
        }
    };
    addPlanes(c, s);  // the square's sides
    addPlanes(-s, c);
    const float sweep2 = sweepX * sweepX + sweepY * sweepY;
    if (sweep2 > 1e-6f) {
        const float inv = fastRsqrt(sweep2);
        addPlanes(-sweepY * inv, sweepX * inv); // the long sides of the sweep
    }
    // 1 / sin^2 of the angle between each two planes, for the corners.
    float cosine[6][6], invSin2[6][6];
    for (int i = 0; i < planes; ++i) {
        for (int j = 0; j < planes; ++j) {
            const float cs = nx[i] * nx[j] + ny[i] * ny[j];
            cosine[i][j] = cs;
            invSin2[i][j] = 1 - cs * cs > 1e-4f ? 1 / (1 - cs * cs) : 0;
        }
    }

    const float invSoft = 0.5f / softness;
    const int full = static_cast<int>(opacity * 255 + 0.5f);
    float stepX[6], invNx[6];
    for (int i = 0; i < planes; ++i) {
        stepX[i] = nx[i] * step;
        invNx[i] = fabsf(nx[i]) > 1e-6f ? 1 / nx[i] : 0;
    }
    // Samples where every plane is within `limit` of its edge: a span of the
    // row, [from, to).
    auto span = [&](float py, float limit, int &from, int &to) {
        float lo = -1e9f, hi = 1e9f;
        for (int i = 0; i < planes; ++i) {
            const float bound = limit + lift[i] - ny[i] * py; // nx * px <= bound
            if (invNx[i] > 0) {
                const float e = bound * invNx[i];
                hi = e < hi ? e : hi;
            } else if (invNx[i] < 0) {
                const float e = bound * invNx[i];
                lo = e > lo ? e : lo;
            } else if (bound < 0) {
                from = to = 0;
                return;
            }
        }
        if (lo > hi) {
            from = to = 0;
            return;
        }
        from = ceilInt((lo + cx - worldX0) * k);
        to = floorInt((hi + cx - worldX0) * k) + 1;
        from = from < x0 ? x0 : from;
        to = to > x1 ? x1 : to;
    };
    // In frames in motion one alpha serves two neighbouring pixels: the
    // shadow is soft, and the grain hides the steps.
    const int reuse = t.scale == 1 ? 2 : 1;
    auto edgeBand = [&](int from, int to, float py) {
        if (from >= to) {
            return;
        }
        float d[6];
        const float px = worldX0 + from * step - cx;
        for (int i = 0; i < planes; ++i) {
            d[i] = nx[i] * px + ny[i] * py - lift[i];
        }
        for (int x = from; x < to; x += reuse) {
            float d1 = d[0];
            int i1 = 0;
            for (int i = 1; i < planes; ++i) {
                if (d[i] > d1) {
                    d1 = d[i];
                    i1 = i;
                }
            }
            float dist = d1;
            if (d1 > 0) {
                // Outside: near a corner the distance is to the corner.
                int i2 = i1 == 0 ? 1 : 0;
                for (int i = 0; i < planes; ++i) {
                    i2 = i != i1 && d[i] > d[i2] ? i : i2;
                }
                const float d2 = d[i2], cs = cosine[i1][i2];
                if (d2 > 0 && d2 > d1 * cs && invSin2[i1][i2] > 0) {
                    dist = fastSqrt((d1 * d1 + d2 * d2 - 2 * d1 * d2 * cs) * invSin2[i1][i2]);
                }
            }
            dist -= rounding;
            int alpha = full;
            if (dist >= softness) {
                alpha = 0;
            } else if (dist > -softness) {
                const float e = (dist + softness) * invSoft;
                alpha = static_cast<int>(opacity * (1 - e * e * (3 - 2 * e)) * 255 + 0.5f);
            }
            if (alpha > 0) {
                ++scratch_[t.worker].shadow;
                darken(t, x, y_, alpha);
                if (reuse == 2 && x + 1 < to) {
                    darken(t, x + 1, y_, alpha);
                }
            }
            for (int i = 0; i < planes; ++i) {
                d[i] += stepX[i] * reuse;
            }
        }
    };
    for (y_ = y0; y_ < y1; ++y_) {
        const float py = worldY0 + y_ * step - cy;
        // Outside the polygon grown by rounding + softness there is no
        // shadow (corners mitred, so this is a little generous); deep enough
        // inside the shadow is full: only the band between needs the
        // distance.
        int outFrom, outTo, inFrom, inTo;
        span(py, rounding + softness, outFrom, outTo);
        if (outFrom >= outTo) {
            continue;
        }
        // Inside the polygon itself, or corners would be cut square.
        span(py, rounding - softness < 0 ? rounding - softness : 0, inFrom, inTo);
        if (inFrom >= inTo) {
            edgeBand(outFrom, outTo, py);
            continue;
        }
        edgeBand(outFrom, inFrom, py);
        for (int x = inFrom; x < inTo; ++x) {
            darken(t, x, y_, full);
        }
        scratch_[t.worker].shadow += static_cast<uint32_t>(inTo - inFrom);
        edgeBand(inTo, outTo, py);
    }
}

void DiceRaster::drawShadows(const Target &t, const DiePose &die) const {
    const float s = die.size;
    const Vec3 p = die.position;
    const float lift = p.z - s > 0 ? p.z - s : 0;
    const Mat3 r = toMat3(die.orientation);
    const float yaw = atan2f(r.m[3], r.m[0]);
    const float kx = -kLight.x / kLight.z;
    const float ky = -kLight.y / kLight.z;
    // The bottom of the die and its top both cast: the shadow is the
    // footprint swept between their two projections.
    shadowPass(t, p.x + kx * lift, p.y + ky * lift, 2 * s * kx, 2 * s * ky, yaw, s * 0.95f,
               s * 0.3f + lift * 0.15f, 1.5f + lift * 0.12f,
               kStyle.shadow * 70 / (70 + lift));
    if (lift < 20) {
        shadowPass(t, p.x, p.y, 0, 0, yaw, s * 1.02f, s * 0.4f, 2.5f + lift * 0.3f,
                   0.45f * (1 - lift / 20));
    }
}

Vec3 DiceRaster::lightLinear(const Shading &sh, float nx, float ny, float nz, Vec3 base,
                             float occlusion) const {
    const Style &st = kStyle;
    const float nl = nx * kLight.x + ny * kLight.y + nz * kLight.z;
    const float ndl = nl > 0 ? nl : 0;
    float ndv = nx * sh.view.x + ny * sh.view.y + nz * sh.view.z;
    ndv = ndv > 0 ? ndv : 0;
    float ndh = nx * sh.halfway.x + ny * sh.halfway.y + nz * sh.halfway.z;
    ndh = ndh > 0 ? ndh : 0;
    const float spec = specular_[static_cast<int>(ndh * 1023)];
    // The mirror direction r = 2 (n.v) n - v, only through dots.
    const float rDotL = 2 * ndv * nl - sh.viewDotLight;
    const float rz = 2 * ndv * nz - sh.view.z;
    const float sky = rz > 0 ? rz : 0;
    const float g1 = 1 - ndv, g2 = g1 * g1;
    const float fresnel = 0.04f + 0.96f * g2 * g2 * g1;
    const float b01 = clamp01((rDotL - 0.9f) * (1 / 0.07f));
    const float box = b01 * b01 * (3 - 2 * b01) * st.softBox;
    const float lit = (st.ambient * (0.5f + 0.5f * nz) + st.diffuse * ndl) * occlusion;
    return {base.x * lit + spec + fresnel * (st.envLow.x * (1 - sky) + st.envHigh.x * sky + box),
            base.y * lit + spec + fresnel * (st.envLow.y * (1 - sky) + st.envHigh.y * sky + box),
            base.z * lit + spec + fresnel * (st.envLow.z * (1 - sky) + st.envHigh.z * sky + box)};
}

void DiceRaster::toBytes(Vec3 c, uint8_t out[3]) const {
    const float v[3] = {c.x, c.y, c.z};
    for (int k = 0; k < 3; ++k) {
        const int i = static_cast<int>(v[k] * 1024);
        out[k] = toSrgb_[i < 0 ? 0 : (i > 2047 ? 2047 : i)];
    }
}

void DiceRaster::drawDie(const Target &t, const DiePose &die) {
    using namespace DiceMesh;
    Scratch &ws = scratch_[t.worker];
    const uint32_t dieStart = now();
    const Mat3 r = toMat3(die.orientation);
    const float s = die.size;
    const Vec3 p = die.position;
    const float k = static_cast<float>(t.scale);
    const float baseX = (kW * 0.5f - t.originX) * k;
    const float baseY = (kH * 0.5f - t.originY) * k;

    for (int i = 0; i < kVertices; ++i) {
        const Vec3 local{mesh_.position[i * 3], mesh_.position[i * 3 + 1],
                         mesh_.position[i * 3 + 2]};
        const Vec3 w = r * local * s + p;
        const float persp = k * kD / (kD - w.z);
        ws.sx[i] = baseX + w.x * persp;
        ws.sy[i] = baseY + w.y * persp;
        const Vec3 n = r * Vec3{mesh_.normal[i * 3], mesh_.normal[i * 3 + 1],
                                mesh_.normal[i * 3 + 2]};
        ws.nx[i] = n.x;
        ws.ny[i] = n.y;
        ws.nz[i] = n.z;
    }

    // The die is small against the camera distance, so the view direction
    // is taken once per die.
    Shading sh;
    sh.view = normalized(Vec3{-p.x, -p.y, kD - p.z});
    sh.halfway = normalized(kLight + sh.view);
    sh.viewDotLight = dot(sh.view, kLight);

    const Style &st = kStyle;
    FaceFrame faces[kFaces];
    bool faceVisible[kFaces];
    uint8_t flatColor[kFaces][3];
    for (int f = 0; f < kFaces; ++f) {
        const Face &face = kFaceList[f];
        faces[f] = {r * face.normal, r * face.tangent, r * face.bitangent, face.value};
        // A rounded edge bends at most 45 degrees away from its face, so a
        // face turned this far from the camera has nothing to show.
        faceVisible[f] = dot(faces[f].normal, sh.view) > -0.72f;
        // The flat middle of a face has one normal, hence one color.
        toBytes(lightLinear(sh, faces[f].normal.x, faces[f].normal.y, faces[f].normal.z,
                            st.body, 1),
                flatColor[f]);
    }

    const float pixelUv = (kD - p.z) / (s * k * kD); // face units per sample
    const float invPixelUv = 1 / pixelUv;
    const float invPip = 1 / st.pipRadius, invPipOne = 1 / st.pipOneRadius;
    // The bent edges are lit at their vertices and the colors blended across
    // (Gouraud): their triangles are a couple of pixels across the bend, so
    // it looks the same as lighting every pixel, at a fraction of the cost.
    ++ws.stamp;
    auto vertexColor = [&](int i) -> const uint8_t * {
        if (ws.litAt[i] != ws.stamp) {
            ws.litAt[i] = ws.stamp;
            toBytes(lightLinear(sh, ws.nx[i], ws.ny[i], ws.nz[i], st.body, 1), ws.lit[i]);
        }
        return ws.lit[i];
    };
    // The flat colors, dithered once into a 4 x 4 pattern of finished pixels.
    uint16_t flatPattern[kFaces][16];
    if (t.scale == 1) {
        for (int f = 0; f < kFaces; ++f) {
            for (int i = 0; i < 16; ++i) {
                flatPattern[f][i] =
                    encode565(flatColor[f][0], flatColor[f][1], flatColor[f][2], i & 3, i >> 2);
            }
        }
    }

    const uint32_t setupDone = now();
    ws.stageUs[2] += setupDone - dieStart;
    for (int tri = 0; tri < kTriangles; ++tri) {
        const int f = mesh_.face[tri];
        if (!faceVisible[f]) {
            continue;
        }
        const int a = mesh_.triangle[tri * 3];
        const int b = mesh_.triangle[tri * 3 + 1];
        const int c = mesh_.triangle[tri * 3 + 2];
        const float ax = ws.sx[a], ay = ws.sy[a], bx = ws.sx[b], by = ws.sy[b];
        const float cx = ws.sx[c], cy = ws.sy[c];
        const float area = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
        if (area <= 1e-6f) {
            continue; // facing away (or edge on)
        }
        float fx0 = ax < bx ? ax : bx, fx1 = ax > bx ? ax : bx;
        float fy0 = ay < by ? ay : by, fy1 = ay > by ? ay : by;
        fx0 = cx < fx0 ? cx : fx0;
        fx1 = cx > fx1 ? cx : fx1;
        fy0 = cy < fy0 ? cy : fy0;
        fy1 = cy > fy1 ? cy : fy1;
        int x0 = floorInt(fx0), x1 = ceilInt(fx1);
        int y0 = floorInt(fy0), y1 = ceilInt(fy1);
        x0 = x0 < 0 ? 0 : x0;
        y0 = y0 < t.rowFrom ? t.rowFrom : y0;
        x1 = x1 > t.width - 1 ? t.width - 1 : x1;
        y1 = y1 > t.rowTo - 1 ? t.rowTo - 1 : y1;
        if (x0 > x1 || y0 > y1) {
            continue; // off the screen, or in the other worker's rows
        }

        ++ws.triangles;
        const float ia = fastRecip(area);
        // Barycentric weights change linearly along a row.
        const float d0x = (by - cy) * ia, d1x = (cy - ay) * ia, d2x = -d0x - d1x;
        const float inv0 = d0x != 0 ? fastRecip(d0x) : 0;
        const float inv1 = d1x != 0 ? fastRecip(d1x) : 0;
        const float inv2 = d2x != 0 ? fastRecip(d2x) : 0;
        // Attributes as c + w0 * (a - c) + w1 * (b - c).
        const float ua = mesh_.uv[a * 2], ub = mesh_.uv[b * 2], uc = mesh_.uv[c * 2];
        const float va = mesh_.uv[a * 2 + 1], vb = mesh_.uv[b * 2 + 1], vc = mesh_.uv[c * 2 + 1];
        const float du0 = ua - uc, du1 = ub - uc, dv0 = va - vc, dv1 = vb - vc;
        const FaceFrame &face = faces[f];
        const bool flat = flatTriangle_[tri] != 0;

        if (!flat) {
            // A bent edge: the vertex colors blended, in 8.8 fixed point,
            // one add per channel a pixel.
            const uint8_t *ca = vertexColor(a), *cb = vertexColor(b), *cc = vertexColor(c);
            float rowColor[3][3]; // per channel: at c, along w0, along w1
            int step[3];
            for (int k = 0; k < 3; ++k) {
                rowColor[k][0] = cc[k];
                rowColor[k][1] = static_cast<float>(ca[k] - cc[k]);
                rowColor[k][2] = static_cast<float>(cb[k] - cc[k]);
                step[k] = static_cast<int>((d0x * rowColor[k][1] + d1x * rowColor[k][2]) * 256);
            }
            for (int y = y0; y <= y1; ++y) {
                int from, to;
                float w0, w1;
                if (!rowSpan(y, x0, x1, ax, ay, bx, by, cx, cy, ia, d0x, d1x, d2x, inv0, inv1,
                             inv2, from, to, w0, w1)) {
                    continue;
                }
                int col[3];
                for (int k = 0; k < 3; ++k) {
                    col[k] = static_cast<int>(
                        (rowColor[k][0] + w0 * rowColor[k][1] + w1 * rowColor[k][2]) * 256 + 128);
                }
                ws.drawn += static_cast<uint32_t>(to - from + 1);
                for (int x = from; x <= to; ++x) {
                    put(t, x, y, static_cast<uint8_t>(col[0] >> 8), static_cast<uint8_t>(col[1] >> 8),
                        static_cast<uint8_t>(col[2] >> 8));
                    col[0] += step[0];
                    col[1] += step[1];
                    col[2] += step[2];
                }
            }
            continue;
        }

        // The flat middle: one color, except the pips. Only the nearest pip
        // spot is worth a look.
        const uint8_t *flatRgb = flatColor[f];
        const uint16_t *pattern = flatPattern[f];
        const uint16_t mask = pipMasks_[face.value];
        const float pipR = face.value == 1 ? st.pipOneRadius : st.pipRadius;
        const Vec3 pipColor = face.value == 1 ? st.pipOne : st.pip;
        const float pipReach2 = (pipR + pixelUv) * (pipR + pixelUv);
        const float invPipR = face.value == 1 ? invPipOne : invPip;
        const float dimple = 0.8f * invPipR; // how far the dimple's wall tilts
        const float dudx = d0x * du0 + d1x * du1, dvdx = d0x * dv0 + d1x * dv1;

        for (int y = y0; y <= y1; ++y) {
            int from, to;
            float w0, w1;
            if (!rowSpan(y, x0, x1, ax, ay, bx, by, cx, cy, ia, d0x, d1x, d2x, inv0, inv1, inv2,
                         from, to, w0, w1)) {
                continue;
            }
            ws.drawn += static_cast<uint32_t>(to - from + 1);
            float u = uc + w0 * du0 + w1 * du1;
            float v = vc + w0 * dv0 + w1 * dv1;
            uint16_t *row = t.scale == 1 ? t.frame + (t.originY + y) * kW + t.originX : nullptr;
            const uint16_t *patternRow = pattern + (((t.originY + y) & 3) << 2);
            for (int x = from; x <= to; ++x, u += dudx, v += dvdx) {
                float pu, pv;
                bool near = true;
                if (face.value == 1) {
                    pu = u;
                    pv = v;
                } else {
                    const int iu = u < -0.25f ? 0 : (u > 0.25f ? 2 : 1);
                    const int iv = v < -0.25f ? 0 : (v > 0.25f ? 2 : 1);
                    near = (mask & (1u << (iu * 3 + iv))) != 0;
                    pu = u - (iu - 1) * 0.5f;
                    pv = v - (iv - 1) * 0.5f;
                }
                if (!near || pu * pu + pv * pv >= pipReach2) {
                    if (row != nullptr) {
                        row[x] = patternRow[(t.originX + x) & 3];
                    } else {
                        put(t, x, y, flatRgb[0], flatRgb[1], flatRgb[2]);
                    }
                    continue;
                }

                // A pip: lit pixel by pixel, for the dimple.
                const float d2 = pu * pu + pv * pv;
                const float d = fastSqrt(d2);
                const float m = clamp01((pipR + pixelUv * 0.5f - d) * invPixelUv);
                const Vec3 base = st.body + (pipColor - st.body) * m;
                // A spherical dimple: its wall faces back towards the pip's
                // center.
                const float qu = pu * dimple, qv = pv * dimple;
                const float floor2 = 1 - qu * qu - qv * qv;
                const float hh = fastSqrt(floor2 > 0.05f ? floor2 : 0.05f);
                const Vec3 dn = face.tangent * -qu + face.bitangent * -qv + face.normal * hh;
                float nx = face.normal.x + (dn.x - face.normal.x) * m;
                float ny = face.normal.y + (dn.y - face.normal.y) * m;
                float nz = face.normal.z + (dn.z - face.normal.z) * m;
                const float inv = fastRsqrt(nx * nx + ny * ny + nz * nz);
                nx *= inv;
                ny *= inv;
                nz *= inv;
                const float occlusion = 1 - m * 0.3f * clamp01(d * invPipR);
                ++ws.shaded;
                uint8_t rgb[3];
                toBytes(lightLinear(sh, nx, ny, nz, base, occlusion), rgb);
                put(t, x, y, rgb[0], rgb[1], rgb[2]);
            }
        }
    }
    ws.stageUs[3] += now() - setupDone;
}

void DiceRaster::drawRows(const Target &t, const uint16_t *background, const DiePose *dice,
                          int count) {
    const uint32_t t0 = now();
    Scratch &ws = scratch_[t.worker];
    fillBackground(t, background);
    const uint32_t t1 = now();
    for (int i = 0; i < count; ++i) {
        drawShadows(t, dice[i]);
    }
    const uint32_t t2 = now();
    ws.stageUs[0] = t1 - t0;
    ws.stageUs[1] = t2 - t1;
    ws.stageUs[2] = ws.stageUs[3] = 0;
    ws.scanned = ws.drawn = ws.triangles = 0;
    // Far to near; a die in the air is nearer.
    int order[kMaxDice] = {0, 1};
    if (count == 2 && dice[1].position.z < dice[0].position.z) {
        order[0] = 1;
        order[1] = 0;
    }
    for (int i = 0; i < count && i < kMaxDice; ++i) {
        drawDie(t, dice[order[i]]);
    }
    scratch_[t.worker].us = now() - t0;
}

void DiceRaster::runJob(void *arg) {
    Job &job = *static_cast<Job *>(arg);
    job.raster->drawRows(job.target, job.background, job.dice, job.count);
}

int DiceRaster::splitRow(const Target &t, const DiePose *dice, int count) const {
    // Rows of each die's screen box, in the target's samples.
    float top[kMaxDice], bottom[kMaxDice];
    for (int i = 0; i < count; ++i) {
        const Rect r = bounds(dice[i]);
        top[i] = static_cast<float>((r.y0 - t.originY) * t.scale);
        bottom[i] = static_cast<float>((r.y1 - t.originY) * t.scale);
    }
    float cut;
    if (count == 2) {
        const int upper = top[0] <= top[1] ? 0 : 1;
        const int lower = 1 - upper;
        // Apart: cut between them, one die each. Overlapping: halve both.
        cut = bottom[upper] <= top[lower] ? (bottom[upper] + top[lower]) * 0.5f
                                          : (top[upper] + bottom[upper] + top[lower] + bottom[lower]) * 0.25f;
    } else if (count == 1) {
        cut = (top[0] + bottom[0]) * 0.5f;
    } else {
        cut = t.height * 0.5f;
    }
    const int row = static_cast<int>(cut);
    return row < 0 ? 0 : (row > t.height ? t.height : row);
}

void DiceRaster::drawSplit(Target t, const uint16_t *background, const DiePose *dice,
                           int count) {
    scratch_[0].shaded = scratch_[0].shadow = scratch_[0].us = 0;
    scratch_[1].shaded = scratch_[1].shadow = scratch_[1].us = 0;
    t.worker = 0;
    t.rowFrom = 0;
    t.rowTo = t.height;
    if (parallel_ == nullptr) {
        drawRows(t, background, dice, count);
        return;
    }
    // Rows above the cut here, below it on the other core: two dice apart
    // go one to each, one die is halved.
    const int cut = splitRow(t, dice, count);
    Job job{this, t, background, dice, count};
    job.target.worker = 1;
    job.target.rowFrom = cut;
    parallel_->start(&DiceRaster::runJob, &job);
    t.rowTo = cut;
    drawRows(t, background, dice, count);
    parallel_->finish();
}

void DiceRaster::render(uint16_t *frame, const uint16_t *background, const DiePose *dice,
                        int count, Rect clip) {
    clip = intersect(clip, Rect{0, 0, kW, kH});
    if (clip.empty()) {
        return;
    }
    const uint32_t t0 = now();
    drawSplit({1, clip.x0, clip.y0, clip.width(), clip.height(), frame, nullptr, 0, 0, 0},
              background, dice, count);
    const uint32_t t1 = now();
    profile_ = {t1 - t0, {scratch_[0].us, scratch_[1].us}, {scratch_[0].stageUs[0], scratch_[0].stageUs[1], scratch_[0].stageUs[2], scratch_[0].stageUs[3]}, scratch_[0].scanned, scratch_[0].drawn, scratch_[0].triangles, scratch_[0].shaded + scratch_[1].shaded,
                scratch_[0].shadow + scratch_[1].shadow};
}

void DiceRaster::renderSmooth(uint16_t *frame, const uint16_t *background,
                              const DiePose *dice, int count, Rect clip, uint8_t *tile) {
    clip = intersect(clip, Rect{0, 0, kW, kH});
    if (clip.empty()) {
        return;
    }
    const Target t{2, clip.x0, clip.y0, clip.width() * 2, clip.height() * 2, nullptr, tile, 0, 0, 0};
    drawSplit(t, background, dice, count);

    // Average each 2 x 2 block in linear light.
    for (int y = 0; y < clip.height(); ++y) {
        const uint8_t *top = tile + static_cast<size_t>(y * 2) * t.width * 3;
        const uint8_t *bottom = top + static_cast<size_t>(t.width) * 3;
        const int sy = clip.y0 + y;
        for (int x = 0; x < clip.width(); ++x) {
            const int i = x * 6;
            int ch[3];
            for (int k = 0; k < 3; ++k) {
                const uint32_t sum = srgbToLinear_[top[i + k]] + srgbToLinear_[top[i + 3 + k]] +
                                     srgbToLinear_[bottom[i + k]] +
                                     srgbToLinear_[bottom[i + 3 + k]];
                ch[k] = linearToSrgb_[sum >> 6]; // / 4 samples, 16 -> 12 bits
            }
            const int sx = clip.x0 + x;
            frame[sy * kW + sx] = encode565(ch[0], ch[1], ch[2], sx, sy);
        }
    }
}
