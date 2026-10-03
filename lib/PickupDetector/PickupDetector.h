#pragma once

// Notices the stick being picked up while the screen is off: gravity turning
// by more than kTurnDeg against where it pointed when the screen went off,
// or a jolt. A stick lying still, at any angle, wakes nothing.
//
// Like everything in lib/, it never touches hardware: acceleration in g goes
// in, a yes or no comes out.
class PickupDetector {
  public:
    static constexpr float kTurnDeg = 8.0f;
    static constexpr float kJoltG = 0.25f;

    // The next reading becomes the resting pose. Call when the screen goes
    // off.
    void reset() { anchored_ = false; }

    bool update(float ax, float ay, float az);

  private:
    float anchorX_ = 0, anchorY_ = 0, anchorZ_ = 1;
    bool anchored_ = false;
};
