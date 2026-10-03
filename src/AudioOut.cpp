#include "AudioOut.h"

#include <esp_heap_caps.h>
#include <esp_random.h>

#include "DiceSound.h"

bool AudioOut::begin() {
    auto cfg = M5.Speaker.config();
    // M5Unified resamples every sound to this rate; well above the 24 kHz of
    // the clacks, so the resampling leaves no images in hearing range.
    cfg.sample_rate = 48000;
    cfg.magnification = kMagnification;
    M5.Speaker.config(cfg);

    rollCapacity_ = DiceSound::rollSamples(RollPlanner::kDurationMs);
    roll_ = static_cast<int16_t *>(
        heap_caps_malloc(rollCapacity_ * sizeof(int16_t), MALLOC_CAP_SPIRAM));
    scratch_ = static_cast<float *>(
        heap_caps_malloc(rollCapacity_ * sizeof(float), MALLOC_CAP_SPIRAM));
    rattle_ = static_cast<int16_t *>(
        heap_caps_malloc(DiceSound::kRattleSamples * sizeof(int16_t), MALLOC_CAP_SPIRAM));
    if (roll_ == nullptr || scratch_ == nullptr || rattle_ == nullptr) {
        return false;
    }
    DiceSound::renderRattle(esp_random(), rattle_, DiceSound::kRattleSamples);
    return true;
}

void AudioOut::play(const int16_t *samples, size_t count, uint32_t repeat) {
    if (muted()) {
        return;
    }
    if (!on_) {
        on_ = M5.Speaker.begin();
    }
    M5.Speaker.setVolume(volume_);
    // The samples are not copied: the buffers stay untouched while playing.
    M5.Speaker.playRaw(samples, count, DiceSound::kSampleRate, false, repeat, 0, true);
}

void AudioOut::playRoll(const RollPlan &plan, uint32_t seed) {
    rattling_ = false;
    if (muted()) {
        return;
    }
    Bounce bounces[RollPlanner::kMaxBounces];
    const int n = RollPlanner::bounces(plan, bounces, RollPlanner::kMaxBounces);
    const size_t length = DiceSound::renderRoll(bounces, n, seed, roll_, rollCapacity_, scratch_);
    play(roll_, length, 1);
}

void AudioOut::startRattle() {
    if (rattling_) {
        return;
    }
    rattling_ = true;
    play(rattle_, DiceSound::kRattleSamples, ~0u); // until stopped
}

void AudioOut::stopRattle() {
    if (!rattling_) {
        return;
    }
    rattling_ = false;
    if (on_) {
        M5.Speaker.stop();
    }
}

void AudioOut::service(uint32_t nowMs) {
    if (!on_) {
        return;
    }
    if (rattling_ || M5.Speaker.isPlaying()) {
        quietSinceMs_ = nowMs;
        return;
    }
    if (nowMs - quietSinceMs_ >= kIdleOffMs) {
        M5.Speaker.end();
        on_ = false;
    }
}
