#include "RollPlanner.h"

#pragma GCC diagnostic warning "-Wdouble-promotion"

namespace RollPlanner {

namespace {

// Shares of a die's own time: lift-off, two bounces, the landing that starts
// the slide, and the end of the slide; the rest is settling.
constexpr float kPhase[5] = {0.0f, 0.36f, 0.6f, 0.75f, 0.9f};
constexpr float kBounceStrength[3] = {1.0f, 0.6f, 0.32f};

float clampTo(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void clampToArea(const Area &a, float &x, float &y) {
    x = clampTo(x, -a.xRange, a.xRange);
    y = clampTo(y, a.yMin, a.yMax);
}

// Where this die is along its own time line, 0..1.
float progress(const DieTrack &d, uint32_t elapsedMs) {
    const float t = static_cast<float>(elapsedMs) / static_cast<float>(kDurationMs);
    return clamp01((t - d.delay) / (1 - kSecondDieDelay));
}

} // namespace

float dieSize(uint8_t count) { return count == 1 ? 27.0f : 22.0f; }

Area area(float size) {
    const float margin = 1.45f * size;
    return {kTableHalfWidth - margin, kTableTop + margin, kTableBottom - margin};
}

DiePose restingPose(float x, float y, uint8_t value, float yaw, float size) {
    return {{x, y, size}, DiceMesh::faceUp(value, yaw), size};
}

void readyLayout(uint8_t count, DiePose *out) {
    const float s = dieSize(count);
    const Area a = area(s);
    if (count == 1) {
        float x = 6, y = -8;
        clampToArea(a, x, y);
        out[0] = restingPose(x, y, 5, 0.35f, s);
        return;
    }
    float x0 = -20, y0 = -22, x1 = 22, y1 = 20;
    clampToArea(a, x0, y0);
    clampToArea(a, x1, y1);
    out[0] = restingPose(x0, y0, 3, 0.5f, s);
    out[1] = restingPose(x1, y1, 4, -0.35f, s);
}

void placeApart(float size, const DiePose *others, int count, Randomness &random, float &x,
                float &y) {
    const Area a = area(size);
    const float apart = 3.1f * size;
    for (int attempt = 0; attempt < 300; ++attempt) {
        x = random.uniform(-a.xRange, a.xRange);
        y = random.uniform(a.yMin, a.yMax);
        bool clear = true;
        for (int i = 0; i < count; ++i) {
            const float dx = x - others[i].position.x, dy = y - others[i].position.y;
            if (dx * dx + dy * dy < apart * apart) {
                clear = false;
                break;
            }
        }
        if (clear) {
            return;
        }
    }
    // Practically never: the opposite corner.
    x = count > 0 && others[0].position.x > 0 ? -a.xRange : a.xRange;
    y = count > 0 && others[0].position.y > 0 ? a.yMin : a.yMax;
}

RollPlan plan(const DiePose *start, uint8_t count, const uint8_t *values, Randomness &random) {
    RollPlan p{};
    p.count = count;
    const float s = dieSize(count);
    const Area a = area(s);

    DiePose finals[2];
    for (int i = 0; i < count; ++i) {
        float x, y;
        placeApart(s, finals, i, random, x, y);
        finals[i] = restingPose(x, y, values[i], random.uniform(0, kTwoPi), s);
    }

    for (int i = 0; i < count; ++i) {
        DieTrack &d = p.dice[i];
        d.value = values[i];
        d.size = s;
        d.startSize = start[i].size;
        d.startZ = start[i].position.z;
        d.finalOrientation = finals[i].orientation;
        d.delay = i * kSecondDieDelay;

        d.waypointX[0] = start[i].position.x;
        d.waypointY[0] = start[i].position.y;
        // Two dice keep to their own sides in the air, so they seldom fly
        // through each other.
        float xFrom = -a.xRange, xTo = a.xRange;
        if (count == 2) {
            const bool left = finals[i].position.x < finals[1 - i].position.x;
            xFrom = left ? -a.xRange : -0.15f * a.xRange;
            xTo = left ? 0.15f * a.xRange : a.xRange;
        }
        for (int w = 1; w <= 2; ++w) {
            d.waypointX[w] = random.uniform(xFrom, xTo);
            d.waypointY[w] = random.uniform(a.yMin, a.yMax);
        }
        const float fx = finals[i].position.x, fy = finals[i].position.y;
        const float slideAngle = random.uniform(0, kTwoPi);
        float lx = fx + cosf(slideAngle) * s * 1.1f;
        float ly = fy + sinf(slideAngle) * s * 1.1f;
        clampToArea(a, lx, ly);
        d.waypointX[3] = lx;
        d.waypointY[3] = ly;
        d.waypointX[4] = fx;
        d.waypointY[4] = fy;

        // Rolling along the slide turns the die around the axis across it.
        Vec3 slide{fx - lx, fy - ly, 0};
        if (length(slide) < 1e-3f) {
            slide = {cosf(slideAngle), sinf(slideAngle), 0};
        }
        slide = normalized(slide);
        d.rollAxis = {slide.y, -slide.x, 0};
        d.tumbleAxis = normalized(Vec3{random.uniform(-1, 1), random.uniform(-1, 1),
                                       random.uniform(-0.3f, 0.3f)});
        d.rollAngle = kTwoPi * random.uniform(2.2f, 3.2f);
        d.tumbleAngle = kTwoPi * random.uniform(0.5f, 1.0f);
        const float h = random.uniform(70, 95);
        d.arcHeight[0] = h;
        d.arcHeight[1] = h * 0.3f;
        d.arcHeight[2] = h * 0.09f;

        const Quat spun = axisAngle(d.rollAxis, d.rollAngle) *
                          axisAngle(d.tumbleAxis, d.tumbleAngle) * d.finalOrientation;
        d.startCorrection = conjugate(spun) * start[i].orientation;
    }
    return p;
}

DiePose pose(const RollPlan &plan, int die, uint32_t elapsedMs) {
    const DieTrack &d = plan.dice[die];
    const float f = progress(d, elapsedMs);
    const float s = d.size;
    float x, y, z;
    if (f < kPhase[3]) {
        const int arc = f < kPhase[1] ? 0 : (f < kPhase[2] ? 1 : 2);
        const float t = (f - kPhase[arc]) / (kPhase[arc + 1] - kPhase[arc]);
        x = d.waypointX[arc] + (d.waypointX[arc + 1] - d.waypointX[arc]) * t;
        y = d.waypointY[arc] + (d.waypointY[arc + 1] - d.waypointY[arc]) * t;
        const float base = arc == 0 ? d.startZ + (s - d.startZ) * t : s;
        z = base + d.arcHeight[arc] * 4 * t * (1 - t);
    } else if (f < kPhase[4]) {
        const float t = (f - kPhase[3]) / (kPhase[4] - kPhase[3]);
        const float e = 1 - (1 - t) * (1 - t) * (1 - t);
        x = d.waypointX[3] + (d.waypointX[4] - d.waypointX[3]) * e;
        y = d.waypointY[3] + (d.waypointY[4] - d.waypointY[3]) * e;
        z = s;
    } else {
        x = d.waypointX[4];
        y = d.waypointY[4];
        z = s;
    }

    // Both spins run down to nothing at the end of the slide.
    const float g = f < kPhase[4] ? (1 - f / kPhase[4]) * (1 - f / kPhase[4]) : 0;
    const Quat correction = slerp(d.startCorrection, identityQuat(), smoothstep(0, 0.3f, f));
    Quat q = axisAngle(d.rollAxis, d.rollAngle * g) *
             axisAngle(d.tumbleAxis, d.tumbleAngle * g * sqrtf(g)) * d.finalOrientation *
             correction;
    if (f > kPhase[4]) {
        // Rocking on an edge after the landing, dying out.
        const float t = (f - kPhase[4]) / (1 - kPhase[4]);
        const float rock = 0.09f * sinf(t * kPi * 3) * (1 - t) * (1 - t);
        q = axisAngle(d.rollAxis, rock) * q;
        z += s * 0.9f * fabsf(rock);
    }
    const float size = d.startSize + (s - d.startSize) * smoothstep(0, 0.3f, f);
    return {{x, y, z}, q, size};
}

void separate(DiePose *dice, int count) {
    if (count < 2) {
        return;
    }
    Vec3 &a = dice[0].position;
    Vec3 &b = dice[1].position;
    // Rounded boxes: closer than this, they may overlap whatever their turn.
    const float apart = 1.25f * (dice[0].size + dice[1].size);
    const float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
    const float d = sqrtf(dx * dx + dy * dy + dz * dz);
    if (d >= apart) {
        return;
    }
    // Only sideways: the heights belong to the bounces.
    const float flat = sqrtf(dx * dx + dy * dy);
    const float ux = flat > 1e-3f ? dx / flat : 1, uy = flat > 1e-3f ? dy / flat : 0;
    const float needFlat = sqrtf(apart * apart - dz * dz < 0 ? 0 : apart * apart - dz * dz);
    const float push = (needFlat - flat) * 0.5f;
    if (push <= 0) {
        return;
    }
    a.x -= ux * push;
    a.y -= uy * push;
    b.x += ux * push;
    b.y += uy * push;
}

void poses(const RollPlan &plan, uint32_t elapsedMs, DiePose *out) {
    for (int i = 0; i < plan.count; ++i) {
        out[i] = pose(plan, i, elapsedMs);
    }
    separate(out, plan.count);
}

DiePose finalPose(const RollPlan &plan, int die) {
    const DieTrack &d = plan.dice[die];
    return {{d.waypointX[4], d.waypointY[4], d.size}, d.finalOrientation, d.size};
}

int bounces(const RollPlan &plan, Bounce *out, int capacity) {
    int n = 0;
    auto add = [&](const DieTrack &d, float phase, float strength) {
        if (n < capacity) {
            const float share = d.delay + phase * (1 - kSecondDieDelay);
            out[n++] = {static_cast<uint32_t>(share * kDurationMs + 0.5f), strength};
        }
    };
    for (int i = 0; i < plan.count; ++i) {
        const DieTrack &d = plan.dice[i];
        for (int b = 0; b < 3; ++b) {
            add(d, kPhase[b + 1], kBounceStrength[b]);
        }
        // Small knocks while it slides and tips over.
        add(d, 0.8f, 0.1f);
        add(d, 0.86f, 0.06f);
    }
    return n;
}

DiePose splashPose(int die, uint32_t sinceMs) {
    const float t = static_cast<float>(sinceMs) / 1000.0f;
    constexpr float s = 24.0f;
    if (die == 0) {
        return {{-20, -6 + 3 * sinf(t * 1.3f), 50 + 6 * sinf(t * 1.1f)},
                axisAngle({0.4f, 1, 0.25f}, t * 0.7f) * DiceMesh::faceUp(5, 0.4f), s};
    }
    return {{22, 38 + 3 * sinf(t * 1.2f + 1), 32 + 6 * sinf(t * 1.5f + 2)},
            axisAngle({1, 0.3f, 0.6f}, -t * 0.6f) * DiceMesh::faceUp(6, -0.3f), s};
}

DiePose departPose(const DiePose &start, uint32_t sinceMs) {
    const float t = clamp01(static_cast<float>(sinceMs) / kDepartMs);
    const float e = t * t; // speeding up
    // Out sideways, a little downwards.
    const Vec3 dir = normalized(Vec3{start.position.x < 0 ? -1.0f : 1.0f, 0.35f, 0});
    constexpr float kTravel = 190.0f;
    DiePose p = start;
    p.position = start.position + dir * (kTravel * e) + Vec3{0, 0, 30 * t};
    const Vec3 axis{-dir.y, dir.x, 0}; // rolling the way it flies
    p.orientation = axisAngle(axis, kTwoPi * 1.2f * e) * start.orientation;
    return p;
}

DiePose shakePose(const DiePose &base, int die, uint32_t sinceMs) {
    const float t = static_cast<float>(sinceMs) / 1000.0f;
    const float i = static_cast<float>(die);
    const float lift = 7 * smoothstep(0, 0.15f, t);
    DiePose p = base;
    p.position.x += 1.8f * sinf(t * 23 + i * 1.7f);
    p.position.y += 1.8f * cosf(t * 19 + i * 2.9f);
    p.position.z += lift + 2 * fabsf(sinf(t * 29 + i));
    p.orientation = axisAngle({sinf(t * 13 + i), cosf(t * 11 + 2 * i), 0.2f},
                              0.22f * sinf(t * 17 + i * 1.3f)) *
                    base.orientation;
    return p;
}

} // namespace RollPlanner
