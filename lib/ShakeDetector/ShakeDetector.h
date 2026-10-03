#pragma once

#include <stdint.h>

struct ShakeEvents {
    bool began;   // this reading confirmed a shake
    bool shaking; // a shake is going on (true from began up to ended)
    bool ended;   // this reading closed a shake
};

// Notices the stick being shaken like a dice cup, and when the shaking
// stops, which is when the dice are thrown.
//
// A shake is several jolts: readings whose length differs from 1 g by more
// than kKeepG, the first one by more than kStartG. It is confirmed after
// kJolts of them and ends after kQuietMs without one. A single knock or
// picking the stick up gives one or two jolts and is ignored.
//
// Like everything in lib/, it never touches hardware: the time and the
// acceleration in g go in (50 readings a second), events come out.
class ShakeDetector {
  public:
    static constexpr float kStartG = 1.0f;
    static constexpr float kKeepG = 0.6f;
    static constexpr int kJolts = 3;
    static constexpr uint32_t kQuietMs = 300;

    ShakeEvents update(uint32_t nowMs, float ax, float ay, float az);
    bool shaking() const { return state_ == State::Shaking; }

  private:
    enum class State : uint8_t { Idle, Maybe, Shaking };

    State state_ = State::Idle;
    int jolts_ = 0;
    uint32_t lastJoltMs_ = 0;
};
