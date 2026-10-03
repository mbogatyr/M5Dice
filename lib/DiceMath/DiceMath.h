#pragma once

#include <math.h>
#include <stdint.h>
#include <string.h>

// Square roots and divisions are library calls on the ESP32-S3 (no compiler
// flag inlines them) and cost about a hundred cycles each. In per-pixel code
// use these, built from multiplies only, and multiply by precomputed
// reciprocals instead of dividing.

// 1 / sqrt(x) for x > 0, relative error below 5e-6.
inline float fastRsqrt(float x) {
    uint32_t i;
    memcpy(&i, &x, 4);
    i = 0x5f3759dfu - (i >> 1);
    float y;
    memcpy(&y, &i, 4);
    const float half = 0.5f * x;
    y = y * (1.5f - half * y * y);
    y = y * (1.5f - half * y * y);
    return y;
}

inline float fastSqrt(float x) { return x > 0 ? x * fastRsqrt(x) : 0; }

// 1 / x for x != 0, through fastRsqrt.
inline float fastRecip(float x) {
    const float r = fastRsqrt(x * x);
    return x < 0 ? -r : r;
}

// floorf and ceilf are library calls too.
inline int floorInt(float x) {
    const int i = static_cast<int>(x);
    return x < static_cast<float>(i) ? i - 1 : i;
}
inline int ceilInt(float x) {
    const int i = static_cast<int>(x);
    return x > static_cast<float>(i) ? i + 1 : i;
}

// Small vector and quaternion math for the dice, single precision only:
// the ESP32-S3 FPU has no double, and a stray double is several times
// slower.
//
// World axes, shared by everything in lib/: x to the right of the screen,
// y down the screen, z out of the table towards the viewer. The table is
// the plane z = 0 and the world origin is under the middle of the screen.

struct Vec3 {
    float x, y, z;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec3 a) { return sqrtf(dot(a, a)); }

// A zero vector stays zero instead of turning into NaN.
inline Vec3 normalized(Vec3 a) {
    const float l = length(a);
    return l > 1e-12f ? a * (1.0f / l) : Vec3{0, 0, 0};
}

struct Quat {
    float w, x, y, z;
};

inline Quat identityQuat() { return {1, 0, 0, 0}; }

// Hamilton product: applying b first, then a.
inline Quat operator*(Quat a, Quat b) {
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}

inline Quat conjugate(Quat q) { return {q.w, -q.x, -q.y, -q.z}; }

inline Quat normalized(Quat q) {
    const float l = sqrtf(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    return l > 1e-12f ? Quat{q.w / l, q.x / l, q.y / l, q.z / l} : identityQuat();
}

// Rotation by angle (radians, right-handed) around axis. A zero axis gives
// no rotation.
inline Quat axisAngle(Vec3 axis, float angle) {
    const Vec3 a = normalized(axis);
    if (a.x == 0 && a.y == 0 && a.z == 0) {
        return identityQuat();
    }
    const float s = sinf(angle * 0.5f);
    return {cosf(angle * 0.5f), a.x * s, a.y * s, a.z * s};
}

// Shortest-path interpolation from a (t = 0) to b (t = 1).
inline Quat slerp(Quat a, Quat b, float t) {
    float d = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
    if (d < 0) {
        d = -d;
        b = {-b.w, -b.x, -b.y, -b.z};
    }
    float wa, wb;
    if (d > 0.9995f) {
        wa = 1 - t;
        wb = t;
    } else {
        const float th = acosf(d);
        const float s = sinf(th);
        wa = sinf((1 - t) * th) / s;
        wb = sinf(t * th) / s;
    }
    return normalized(Quat{a.w * wa + b.w * wb, a.x * wa + b.x * wb,
                           a.y * wa + b.y * wb, a.z * wa + b.z * wb});
}

// Row-major 3x3 rotation matrix.
struct Mat3 {
    float m[9];
};

inline Mat3 toMat3(Quat q) {
    const float w = q.w, x = q.x, y = q.y, z = q.z;
    return {{1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y),
             2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x),
             2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)}};
}

inline Vec3 operator*(const Mat3 &r, Vec3 v) {
    return {r.m[0] * v.x + r.m[1] * v.y + r.m[2] * v.z,
            r.m[3] * v.x + r.m[4] * v.y + r.m[5] * v.z,
            r.m[6] * v.x + r.m[7] * v.y + r.m[8] * v.z};
}

inline Vec3 rotate(Quat q, Vec3 v) { return toMat3(q) * v; }

constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = 6.28318531f;

inline float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

inline float smoothstep(float a, float b, float x) {
    const float t = clamp01((x - a) / (b - a));
    return t * t * (3 - 2 * t);
}
