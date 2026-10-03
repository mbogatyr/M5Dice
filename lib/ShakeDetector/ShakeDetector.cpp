#include "ShakeDetector.h"

#include <math.h>

ShakeEvents ShakeDetector::update(uint32_t nowMs, float ax, float ay, float az) {
    const float jolt = fabsf(sqrtf(ax * ax + ay * ay + az * az) - 1.0f);
    ShakeEvents events{false, false, false};
    switch (state_) {
    case State::Idle:
        if (jolt > kStartG) {
            state_ = State::Maybe;
            jolts_ = 1;
            lastJoltMs_ = nowMs;
        }
        break;
    case State::Maybe:
        if (jolt > kKeepG) {
            lastJoltMs_ = nowMs;
            if (++jolts_ >= kJolts) {
                state_ = State::Shaking;
                events.began = true;
            }
        } else if (nowMs - lastJoltMs_ > kQuietMs) {
            state_ = State::Idle;
        }
        break;
    case State::Shaking:
        if (jolt > kKeepG) {
            lastJoltMs_ = nowMs;
        } else if (nowMs - lastJoltMs_ > kQuietMs) {
            state_ = State::Idle;
            events.ended = true;
        }
        break;
    }
    events.shaking = state_ == State::Shaking;
    return events;
}
