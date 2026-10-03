#pragma once

#include <stddef.h>
#include <stdint.h>

#include "RollPlanner.h"

// The sounds of the dice, synthesized: no recordings in the firmware.
//
// A clack of a die on a hard table is three parts: a burst of noise through
// a band-pass filter (the knock), a short high ping (the plastic ringing)
// and a soft low thump (the table). The StickS3's speaker plays almost
// nothing below 200 Hz, so the thump sits at 300..420 Hz. Every part has a
// 2 ms attack and an exponential decay, so nothing clicks.
//
// renderRoll() writes the whole sound of a throw at once, one clack per
// bounce of the plan, normalized and softly limited; renderRattle() writes a
// short loop of dice knocking in a shaken cup.
//
// Mono 16-bit samples. Like everything in lib/, it never touches hardware.
namespace DiceSound {

constexpr uint32_t kSampleRate = 24000;
constexpr uint32_t kTailMs = 200; // after the last bounce
constexpr uint32_t kRattleMs = 600;

constexpr size_t rollSamples(uint32_t durationMs) {
    return static_cast<size_t>(durationMs + kTailMs) * kSampleRate / 1000;
}
constexpr size_t kRattleSamples = static_cast<size_t>(kRattleMs) * kSampleRate / 1000;

// out must hold rollSamples(RollPlanner::kDurationMs) samples; returns how
// many were written. seed varies the clacks. scratch, as many floats as out
// holds samples, saves allocating one each time.
size_t renderRoll(const Bounce *bounces, int count, uint32_t seed, int16_t *out, size_t capacity,
                  float *scratch = nullptr);

// out must hold kRattleSamples; loops without a seam.
size_t renderRattle(uint32_t seed, int16_t *out, size_t capacity);

} // namespace DiceSound
