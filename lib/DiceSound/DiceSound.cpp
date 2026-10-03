#include "DiceSound.h"

#include <math.h>

#include <vector>

#include "Randomness.h"

#pragma GCC diagnostic warning "-Wdouble-promotion"

namespace DiceSound {

namespace {

constexpr float kRate = static_cast<float>(kSampleRate);
constexpr int kClackSamples = static_cast<int>(kSampleRate * 80 / 1000);

// Decay to 1/10000 (-80 dB) over ms, as a per-sample factor.
float decayOver(float ms) { return expf(-9.21f / (ms * 0.001f * kRate)); }

void addClack(float *mix, size_t length, size_t at, float strength, XorShift &random) {
    // Band-pass biquad (RBJ, 0 dB peak) for the knock.
    const float f0 = random.uniform(1800, 4000);
    const float w0 = kTwoPi * f0 / kRate;
    const float alpha = sinf(w0) / (2 * 2.5f);
    const float a0 = 1 + alpha;
    const float b0 = alpha / a0, b2 = -alpha / a0;
    const float a1 = -2 * cosf(w0) / a0, a2 = (1 - alpha) / a0;
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    // The ping as a rotating phasor: no sine per sample.
    const float pingStep = kTwoPi * random.uniform(2600, 4000) / kRate;
    const float rotC = cosf(pingStep), rotS = sinf(pingStep);
    float pingX = 1, pingY = 0;
    const float thumpStep = random.uniform(300, 420) / kRate; // cycles per sample
    const float knockDecay = decayOver(45), pingDecay = decayOver(70), thumpDecay = decayOver(50);
    float knock = 1, ping = 1, thump = 1;
    const int attack = static_cast<int>(kRate * 0.002f);

    for (int i = 0; i < kClackSamples && at + i < length; ++i) {
        const float noise = random.uniform(-1, 1);
        const float band = b0 * noise + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = noise;
        y2 = y1;
        y1 = band;
        const float phase = thumpStep * i - floorf(thumpStep * i);
        const float triangle = 4 * fabsf(phase - 0.5f) - 1;
        const float rise = i < attack ? static_cast<float>(i) / attack : 1;
        mix[at + i] += strength * rise *
                       (0.9f * knock * band + 0.22f * ping * pingY + 0.35f * thump * triangle);
        const float nextX = pingX * rotC - pingY * rotS;
        pingY = pingX * rotS + pingY * rotC;
        pingX = nextX;
        knock *= knockDecay;
        ping *= pingDecay;
        thump *= thumpDecay;
    }
}

// Scales the loudest point to peak and writes 16-bit samples, rounding off
// anything that still sticks out.
void finish(const float *mix, size_t length, float peak, int16_t *out) {
    float loudest = 1e-6f;
    for (size_t i = 0; i < length; ++i) {
        loudest = fabsf(mix[i]) > loudest ? fabsf(mix[i]) : loudest;
    }
    const float gain = peak / loudest;
    for (size_t i = 0; i < length; ++i) {
        float v = mix[i] * gain;
        v = v > 0.95f ? 0.95f : (v < -0.95f ? -0.95f : v);
        out[i] = static_cast<int16_t>(v * 32767);
    }
}

} // namespace

size_t renderRoll(const Bounce *bounces, int count, uint32_t seed, int16_t *out,
                  size_t capacity, float *scratch) {
    const size_t length = rollSamples(RollPlanner::kDurationMs);
    if (capacity < length) {
        return 0;
    }
    std::vector<float> own;
    if (scratch == nullptr) {
        own.resize(length);
        scratch = own.data();
    }
    for (size_t i = 0; i < length; ++i) {
        scratch[i] = 0;
    }
    XorShift random(seed);
    for (int i = 0; i < count; ++i) {
        const size_t at = static_cast<size_t>(bounces[i].timeMs) * kSampleRate / 1000;
        addClack(scratch, length, at, bounces[i].strength, random);
    }
    finish(scratch, length, 0.85f, out);
    return length;
}

size_t renderRattle(uint32_t seed, int16_t *out, size_t capacity) {
    if (capacity < kRattleSamples) {
        return 0;
    }
    std::vector<float> mix(kRattleSamples, 0.0f);
    XorShift random(seed);
    // Knocks every 45..70 ms; the last one dies out before the loop wraps.
    const size_t last = kRattleSamples - kClackSamples;
    for (size_t at = kSampleRate * 10 / 1000; at < last;
         at += static_cast<size_t>(random.uniform(0.045f, 0.07f) * kRate)) {
        addClack(mix.data(), kRattleSamples, at, random.uniform(0.45f, 1.0f), random);
    }
    finish(mix.data(), mix.size(), 0.5f, out);
    return kRattleSamples;
}

} // namespace DiceSound
