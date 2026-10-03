#include "DiceApp.h"

void DiceApp::begin(uint32_t nowMs) {
    screen_ = Screen::Splash;
    phase_ = Phase::Resting;
    mode_ = 1;
    count_ = 1;
    splashAtMs_ = nowMs;
    timeout_.begin(nowMs);
}

bool DiceApp::takeRollStarted() {
    const bool started = rollStarted_;
    rollStarted_ = false;
    return started;
}

void DiceApp::currentPoses(uint32_t nowMs, DiePose *out) const {
    for (int i = 0; i < count_; ++i) {
        out[i] = screen_ == Screen::Splash ? RollPlanner::splashPose(i, nowMs - splashAtMs_)
                                           : dice_[i];
    }
}

void DiceApp::startDeparture(uint32_t nowMs) {
    if (screen_ == Screen::Splash && count_ == 1 && !departing_) {
        departing_ = true;
        departAtMs_ = nowMs;
        departFrom_ = RollPlanner::splashPose(1, nowMs - splashAtMs_);
    }
}

void DiceApp::addDeparting(uint32_t nowMs, DiceView &v) const {
    if (departing_ && nowMs - departAtMs_ < RollPlanner::kDepartMs && v.count == 1) {
        v.dice[1] = RollPlanner::departPose(departFrom_, nowMs - departAtMs_);
        v.count = 2;
        RollPlanner::separate(v.dice, 2);
    }
}

void DiceApp::startRoll(uint32_t nowMs) {
    startDeparture(nowMs);
    DiePose start[2];
    if (phase_ == Phase::Shaking) {
        for (int i = 0; i < count_; ++i) {
            start[i] = RollPlanner::shakePose(dice_[i], i, nowMs - shakeAtMs_);
        }
    } else {
        currentPoses(nowMs, start);
    }
    uint8_t values[2];
    for (int i = 0; i < count_; ++i) {
        values[i] = rollDie(random_);
    }
    plan_ = RollPlanner::plan(start, count_, values, random_);
    rollAtMs_ = nowMs + kSoundLeadMs;
    phase_ = Phase::Rolling;
    screen_ = Screen::Table;
    hasResult_ = false;
    toastOn_ = false;
    rollStarted_ = true;
}

void DiceApp::layReady() {
    count_ = mode_;
    RollPlanner::readyLayout(count_, dice_);
    hasResult_ = false;
    ++sceneVersion_;
}

void DiceApp::switchMode(uint32_t nowMs) {
    mode_ = static_cast<uint8_t>(3 - mode_);
    const float s = RollPlanner::dieSize(mode_);
    const RollPlanner::Area a = RollPlanner::area(s);
    // The first die stays where it is, resized; a second one joins it.
    DiePose first = dice_[0];
    float x = first.position.x, y = first.position.y;
    x = x < -a.xRange ? -a.xRange : (x > a.xRange ? a.xRange : x);
    y = y < a.yMin ? a.yMin : (y > a.yMax ? a.yMax : y);
    first.position = {x, y, s};
    first.size = s;
    dice_[0] = first;
    if (mode_ == 2) {
        RollPlanner::placeApart(s, dice_, 1, random_, x, y);
        dice_[1] = RollPlanner::restingPose(x, y, rollDie(random_), random_.uniform(0, kTwoPi), s);
    }
    count_ = mode_;
    hasResult_ = false;
    toastOn_ = true;
    toastAtMs_ = nowMs;
    ++sceneVersion_;
}

void DiceApp::wake(uint32_t nowMs) {
    if (screen_ == Screen::Asleep && count_ != mode_) {
        layReady();
    }
    screen_ = Screen::Table;
    phase_ = Phase::Resting;
    toastOn_ = false;
    timeout_.begin(nowMs);
    ++sceneVersion_;
}

void DiceApp::update(uint32_t nowMs, const Input &in) {
    if (screen_ == Screen::Asleep) {
        if (in.key1 || in.key2 || in.shakeBegan || in.pickedUp) {
            wake(nowMs);
            ignoreShake_ = in.shaking || in.shakeBegan;
        }
        return;
    }

    bool shakeBegan = in.shakeBegan;
    bool shakeEnded = in.shakeEnded;
    if (ignoreShake_) {
        if (in.shakeEnded || !in.shaking) {
            ignoreShake_ = false;
        }
        shakeBegan = shakeEnded = false;
    }

    switch (phase_) {
    case Phase::Rolling:
        if (nowMs - rollAtMs_ < 0x80000000u && nowMs - rollAtMs_ >= RollPlanner::kDurationMs) {
            for (int i = 0; i < count_; ++i) {
                dice_[i] = RollPlanner::finalPose(plan_, i);
                result_[i] = plan_.dice[i].value;
            }
            // The result reads top to bottom, like the dice on the screen.
            if (count_ == 2 && dice_[1].position.y < dice_[0].position.y) {
                const uint8_t first = result_[0];
                result_[0] = result_[1];
                result_[1] = first;
            }
            hasResult_ = true;
            landedAtMs_ = nowMs;
            phase_ = Phase::Resting;
            ++sceneVersion_;
        }
        break;
    case Phase::Shaking:
        if (shakeEnded || in.key1) {
            startRoll(nowMs);
        }
        break;
    case Phase::Resting:
        if (in.key1) {
            startRoll(nowMs);
        } else if (shakeBegan) {
            startDeparture(nowMs);
            currentPoses(nowMs, dice_);
            phase_ = Phase::Shaking;
            shakeAtMs_ = nowMs;
        } else if (in.key2) {
            if (screen_ == Screen::Splash) {
                screen_ = Screen::Table;
                layReady();
            } else {
                switchMode(nowMs);
            }
        }
        break;
    }

    if (toastOn_ && nowMs - toastAtMs_ >= kToastMs) {
        toastOn_ = false;
    }

    const bool activity =
        in.key1 || in.key2 || in.shakeBegan || in.shaking || phase_ != Phase::Resting;
    if (!timeout_.shouldBeOn(nowMs, activity)) {
        if (screen_ == Screen::Splash) {
            layReady(); // wakes up on the table
        }
        screen_ = Screen::Asleep;
        toastOn_ = false;
    }
}

DiceView DiceApp::view(uint32_t nowMs) const {
    DiceView v{};
    v.screen = screen_;
    v.mode = mode_;
    v.count = count_;
    v.sceneVersion = sceneVersion_;
    switch (phase_) {
    case Phase::Rolling: {
        const uint32_t since = nowMs - rollAtMs_;
        const uint32_t elapsed = since < 0x80000000u ? since : 0; // before the lead ends
        RollPlanner::poses(plan_, elapsed, v.dice);
        v.moving = true;
        break;
    }
    case Phase::Shaking:
        for (int i = 0; i < count_; ++i) {
            v.dice[i] = RollPlanner::shakePose(dice_[i], i, nowMs - shakeAtMs_);
        }
        v.moving = true;
        break;
    case Phase::Resting:
        if (screen_ == Screen::Splash) {
            // The splash always shows a pair, whatever the mode; a throw
            // from it takes the first of them.
            v.count = 2;
            for (int i = 0; i < 2; ++i) {
                v.dice[i] = RollPlanner::splashPose(i, nowMs - splashAtMs_);
            }
            v.moving = true;
        } else {
            currentPoses(nowMs, v.dice);
        }
        break;
    }
    if (phase_ != Phase::Resting) {
        addDeparting(nowMs, v);
    }
    const bool resting = screen_ == Screen::Table && phase_ == Phase::Resting;
    v.showResult = resting && hasResult_;
    v.result[0] = result_[0];
    v.result[1] = result_[1];
    v.resultAgeMs = nowMs - landedAtMs_;
    v.showHint = resting && !hasResult_;
    v.toast = toastOn_ && nowMs - toastAtMs_ < kToastMs;
    v.toastAgeMs = nowMs - toastAtMs_;
    return v;
}
