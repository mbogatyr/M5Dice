#include "PickupDetector.h"

#include <math.h>

bool PickupDetector::update(float ax, float ay, float az) {
    const float length = sqrtf(ax * ax + ay * ay + az * az);
    if (length < 0.3f) {
        return false; // a glitch, or falling: nothing to compare
    }
    const float x = ax / length, y = ay / length, z = az / length;
    if (!anchored_) {
        anchorX_ = x;
        anchorY_ = y;
        anchorZ_ = z;
        anchored_ = true;
        return false;
    }
    if (fabsf(length - 1.0f) > kJoltG) {
        return true;
    }
    static const float kTurnCos = cosf(kTurnDeg * 3.14159265f / 180.0f);
    return x * anchorX_ + y * anchorY_ + z * anchorZ_ < kTurnCos;
}
