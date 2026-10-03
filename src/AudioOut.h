#pragma once

#include <M5Unified.h>

#include "RollPlanner.h"

// Plays the dice on the StickS3's speaker (ES8311 DAC and the amp behind the
// PMIC): the rattle while the stick is shaken, the clacks of a throw.
//
// The sounds are synthesized by DiceSound into PSRAM buffers. The speaker is
// switched off a few seconds after the last sound, which also turns the amp
// off.
class AudioOut {
  public:
    // M5TalkingTom measured M5Unified's StickS3 defaults (magnification 1)
    // about 20 dB below what the speaker can do; 4 is the loudest setting
    // that does not clip. The volume then keeps the clacks pleasant.
    static constexpr uint8_t kMagnification = 4;
    static constexpr uint8_t kVolume = 160;
    static constexpr uint32_t kIdleOffMs = 3000;

    // Call once after M5.begin(). False if the buffers did not fit.
    bool begin();

    // Renders and starts the sound of a throw; takes some milliseconds.
    void playRoll(const RollPlan &plan, uint32_t seed);
    void startRattle();
    void stopRattle();

    // Call every loop: switches the speaker off after a quiet while.
    void service(uint32_t nowMs);

    void setVolume(uint8_t volume) { volume_ = volume; }
    uint8_t volume() const { return volume_; }
    bool muted() const { return volume_ == 0; }

  private:
    void play(const int16_t *samples, size_t count, uint32_t repeat);

    int16_t *roll_ = nullptr;
    float *scratch_ = nullptr;
    int16_t *rattle_ = nullptr;
    size_t rollCapacity_ = 0;
    bool on_ = false;
    bool rattling_ = false;
    uint32_t quietSinceMs_ = 0;
    uint8_t volume_ = kVolume;
};
