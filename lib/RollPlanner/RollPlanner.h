#pragma once

#include <stdint.h>

#include "DiceMath.h"
#include "DiceMesh.h"
#include "Randomness.h"

// How the dice move: the throw, the floating dice of the splash screen and
// the rattling while the stick is shaken.
//
// A throw is planned whole at its start. Each die gets its value and the
// place and turn it will end in first; the flight is then built to arrive
// there exactly:
//
// - it lifts off from where it lies, flies three ever lower arcs between
//   random points of the table (straight lines between bounces, parabolas in
//   height), then slides into place;
// - it spins around an axis across its final slide, as a die rolling that
//   way would, plus a second, random axis; both spins slow down to zero at
//   the end of the slide, so the die tips onto its final face;
// - the difference between where it started and where the spin begins is
//   faded out over the first 30 %, under the fastest spin;
// - after the slide it rocks a little and settles.
//
// Time comes in as milliseconds since the start of the throw.
struct DieTrack {
    float waypointX[5];
    float waypointY[5];
    Quat finalOrientation;
    Vec3 rollAxis;
    float rollAngle;
    Vec3 tumbleAxis;
    float tumbleAngle;
    float arcHeight[3];
    float startZ;
    float startSize;
    float size;
    Quat startCorrection;
    float delay; // share of the throw before this die moves
    uint8_t value;
};

struct RollPlan {
    uint8_t count;
    DieTrack dice[2];
};

// A die hitting the table: for the sound.
struct Bounce {
    uint32_t timeMs;
    float strength; // 0..1
};

namespace RollPlanner {

constexpr uint32_t kDurationMs = 1900;
constexpr float kSecondDieDelay = 0.07f; // the second die lands a bit later
constexpr int kMaxBounces = 10;          // per throw of two dice

// The part of the table the dice may come to rest in, in world coordinates
// (the middle of the screen is 0, 0). The mode squares sit above it and the
// result below it.
constexpr float kTableHalfWidth = 67.0f;
constexpr float kTableTop = -96.0f;
constexpr float kTableBottom = 82.0f;

// Half size of a die: one die is bigger than each of two.
float dieSize(uint8_t count);

// Where the center of a die of this size may rest so that it stays clear of
// the edges of the table area, even turned and seen in perspective.
struct Area {
    float xRange; // |x| <= xRange
    float yMin, yMax;
};
Area area(float size);

// start: the dice as they are now (count of them). values: what they will
// show, 1..6 each.
RollPlan plan(const DiePose *start, uint8_t count, const uint8_t *values, Randomness &random);

DiePose pose(const RollPlan &plan, int die, uint32_t elapsedMs);

// All dice at once, pushed apart sideways where they would pass through
// each other, as if they knocked together. Use this to draw a throw.
void poses(const RollPlan &plan, uint32_t elapsedMs, DiePose *out);

// Pushes two dice apart sideways until their centers are far enough for
// the boxes not to meet. Leaves dice that are apart untouched.
void separate(DiePose *dice, int count);

DiePose finalPose(const RollPlan &plan, int die);

// The bounces of all dice, unsorted; returns how many were written.
int bounces(const RollPlan &plan, Bounce *out, int capacity);

// A die lying on the table at (x, y), showing value, turned by yaw.
DiePose restingPose(float x, float y, uint8_t value, float yaw, float size);

// The layout shown before the first throw: a five, or a three and a four.
void readyLayout(uint8_t count, DiePose *out);

// A free spot for a new die at least 3.1 sizes from the others.
void placeApart(float size, const DiePose *others, int count, Randomness &random, float &x,
                float &y);

// The two dice floating and turning on the splash screen.
DiePose splashPose(int die, uint32_t sinceMs);

// The splash's second die leaving when the game starts with one die: it
// speeds up towards the nearer side edge, rising and tumbling, and is off the
// screen by kDepartMs.
constexpr uint32_t kDepartMs = 650;
DiePose departPose(const DiePose &start, uint32_t sinceMs);

// A die lifted and rattling while the stick is shaken, around base.
DiePose shakePose(const DiePose &base, int die, uint32_t sinceMs);

} // namespace RollPlanner
