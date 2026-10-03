#pragma once

#include <stdint.h>

#include "DiceMesh.h"
#include "DisplayTimeout.h"
#include "Randomness.h"
#include "RollPlanner.h"

enum class Screen : uint8_t { Splash, Table, Asleep };

// Everything the screen needs to draw one frame.
struct DiceView {
    Screen screen;
    uint8_t mode;  // 1 or 2 dice
    uint8_t count; // dice in dice[]
    DiePose dice[2];
    bool moving;           // the dice change every frame
    uint32_t sceneVersion; // changes whenever dice come to rest elsewhere
    bool showResult;
    uint8_t result[2];
    uint32_t resultAgeMs; // since the dice landed
    bool showHint;        // "press or shake" on the table
    bool toast;           // the mode switch notice
    uint32_t toastAgeMs;
};

// The game: what the keys and the shaking do, and what is on the screen.
//
// - It starts with one die. The splash shows two floating dice until the
//   first input; KEY1 or a shake throws the first of them from the air
//   (both in the two dice mode) while the second flies off the screen; KEY2
//   lays the dice on the table.
// - On the table KEY1 throws. Shaking lifts the dice and rattles them; the
//   throw starts when the shaking stops. KEY2 switches between one and two
//   dice, with a short notice.
// - Nothing is taken during a throw.
// - After DisplayTimeout of inactivity the screen sleeps. A key, a shake or
//   picking the stick up only wakes it, to the same dice and result.
//
// Time is passed in; randomness too, so tests can replay a game.
class DiceApp {
  public:
    // Edge events of this tick, except shaking, which lasts.
    struct Input {
        bool key1;
        bool key2;
        bool shakeBegan;
        bool shaking;
        bool shakeEnded;
        bool pickedUp;
    };

    static constexpr uint32_t kToastMs = 1200;
    // The speaker's DMA ring delays sound by about 40 ms; the dice wait for
    // it, so the clacks fall on the bounces.
    static constexpr uint32_t kSoundLeadMs = 40;

    explicit DiceApp(Randomness &random) : random_(random) {}

    void begin(uint32_t nowMs);
    void update(uint32_t nowMs, const Input &in);
    DiceView view(uint32_t nowMs) const;

    bool awake() const { return screen_ != Screen::Asleep; }
    bool shaking() const { return phase_ == Phase::Shaking; }
    bool rolling() const { return phase_ == Phase::Rolling; }
    uint8_t mode() const { return mode_; }

    // True once after a throw started; plan() then holds it.
    bool takeRollStarted();
    // Starts the throw's clock again from now: call after starting its sound,
    // so time spent there never eats into the motion.
    void restartRollClock(uint32_t nowMs) {
        if (phase_ == Phase::Rolling) {
            rollAtMs_ = nowMs + kSoundLeadMs;
        }
    }
    const RollPlan &plan() const { return plan_; }

  private:
    enum class Phase : uint8_t { Resting, Shaking, Rolling };

    void currentPoses(uint32_t nowMs, DiePose *out) const;
    void startDeparture(uint32_t nowMs);
    void addDeparting(uint32_t nowMs, DiceView &v) const;
    void startRoll(uint32_t nowMs);
    void switchMode(uint32_t nowMs);
    void layReady();
    void wake(uint32_t nowMs);

    Randomness &random_;
    DisplayTimeout timeout_;

    Screen screen_ = Screen::Splash;
    Phase phase_ = Phase::Resting;
    uint8_t mode_ = 1;
    uint8_t count_ = 1;
    DiePose dice_[2] = {};  // resting dice, or the base of the shaking
    uint8_t result_[2] = {};
    bool hasResult_ = false;
    uint32_t sceneVersion_ = 0;

    uint32_t splashAtMs_ = 0;
    uint32_t shakeAtMs_ = 0;
    uint32_t rollAtMs_ = 0; // when the dice start moving
    uint32_t landedAtMs_ = 0;
    uint32_t toastAtMs_ = 0;
    bool toastOn_ = false;

    // The splash's second die on its way out, in the one die mode.
    bool departing_ = false;
    uint32_t departAtMs_ = 0;
    DiePose departFrom_ = {};

    // A shake that woke the screen must not also throw when it ends.
    bool ignoreShake_ = false;
    bool rollStarted_ = false;
    RollPlan plan_ = {};
};
