#pragma once

#include <stdint.h>

// A source of random 32-bit numbers. On the board it is the ESP32's hardware
// random number generator; in tests a seeded XorShift, so every run is the
// same.
class Randomness {
  public:
    virtual ~Randomness() = default;
    virtual uint32_t next() = 0;

    // Uniform in [0, 1), from the top 24 bits.
    float uniform() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }
    float uniform(float from, float to) { return from + uniform() * (to - from); }
};

class XorShift : public Randomness {
  public:
    explicit XorShift(uint32_t seed) : state_(seed != 0 ? seed : 0x9E3779B9u) {}

    uint32_t next() override {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }

  private:
    uint32_t state_;
};

// A fair throw, 1..6. Plain "% 6" would favor 1..4 a tiny bit, since 2^32 is
// not a multiple of 6; numbers from the incomplete last block of six are
// thrown away instead.
inline uint8_t rollDie(Randomness &random) {
    constexpr uint32_t kLimit = 4294967292u; // 6 * 715827882
    uint32_t v;
    do {
        v = random.next();
    } while (v >= kLimit);
    return static_cast<uint8_t>(1 + v % 6);
}
