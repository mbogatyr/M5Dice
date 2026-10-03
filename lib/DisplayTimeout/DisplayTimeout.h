#pragma once

#include <stdint.h>

// Decides when to turn the display off after a period of inactivity.
//
// It does not deal with powering off: the StickS3 side button turns the
// board off on a double press by itself, in the PMIC, without the firmware.
//
// Like everything in lib/, it does not touch the hardware: the time and
// whether anything happened go in, a decision comes out.
class DisplayTimeout {
  public:
    static constexpr uint32_t kIdleMs = 60000; // 1 minute

    explicit DisplayTimeout(uint32_t idleMs = kIdleMs);

    // Sets the point the idle time is counted from. Call once at startup.
    void begin(uint32_t nowMs);

    // activity: whether anything happened in this tick: a key, a shake, the
    // dice moving.
    bool shouldBeOn(uint32_t nowMs, bool activity);

  private:
    uint32_t idleMs_;
    uint32_t lastActivityMs_ = 0;
};
