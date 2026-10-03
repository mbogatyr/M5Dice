#include <unity.h>

#include <math.h>

#include <initializer_list>

#include "RollPlanner.h"

using namespace RollPlanner;

void setUp(void) {}
void tearDown(void) {}

static float distance(Vec3 a, Vec3 b) { return length(a - b); }

// The angle between two orientations, in radians.
static float angleBetween(Quat a, Quat b) {
    const Quat d = conjugate(a) * b;
    const float w = fabsf(d.w) > 1 ? 1 : fabsf(d.w);
    return 2 * acosf(w);
}

static RollPlan throwFrom(const DiePose *start, uint8_t count, const uint8_t *values,
                          uint32_t seed) {
    XorShift random(seed);
    return plan(start, count, values, random);
}

void test_throw_starts_exactly_where_the_dice_are(void) {
    DiePose start[2];
    readyLayout(2, start);
    const uint8_t values[] = {6, 1};
    for (uint32_t seed = 1; seed <= 20; ++seed) {
        const RollPlan p = throwFrom(start, 2, values, seed);
        for (int i = 0; i < 2; ++i) {
            const DiePose at = pose(p, i, 0);
            TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0, distance(start[i].position, at.position));
            TEST_ASSERT_FLOAT_WITHIN(2e-3f, 0, angleBetween(start[i].orientation, at.orientation));
        }
    }
}

void test_dice_end_showing_their_values_on_top(void) {
    DiePose start[2];
    readyLayout(2, start);
    for (uint32_t seed = 1; seed <= 50; ++seed) {
        const uint8_t values[] = {static_cast<uint8_t>(1 + seed % 6),
                                  static_cast<uint8_t>(1 + (seed / 6) % 6)};
        const RollPlan p = throwFrom(start, 2, values, seed);
        for (int i = 0; i < 2; ++i) {
            const DiePose end = pose(p, i, kDurationMs);
            TEST_ASSERT_EQUAL(values[i], DiceMesh::topValue(end.orientation));
            TEST_ASSERT_FLOAT_WITHIN(2e-3f, 0,
                                     angleBetween(end.orientation, finalPose(p, i).orientation));
            TEST_ASSERT_FLOAT_WITHIN(1e-3f, end.size, end.position.z); // on the table
        }
    }
}

void test_dice_rest_inside_the_table_and_apart(void) {
    DiePose start[2];
    readyLayout(2, start);
    const uint8_t values[] = {3, 3};
    for (uint32_t seed = 1; seed <= 200; ++seed) {
        const RollPlan p = throwFrom(start, 2, values, seed);
        const Area a = area(dieSize(2));
        const DiePose d0 = finalPose(p, 0), d1 = finalPose(p, 1);
        for (const DiePose &d : {d0, d1}) {
            TEST_ASSERT_TRUE(fabsf(d.position.x) <= a.xRange + 1e-3f);
            TEST_ASSERT_TRUE(d.position.y >= a.yMin - 1e-3f && d.position.y <= a.yMax + 1e-3f);
        }
        TEST_ASSERT_TRUE(distance(d0.position, d1.position) >= 3.1f * dieSize(2) - 1e-3f);
    }
}

void test_the_motion_has_no_jumps(void) {
    DiePose start[1];
    readyLayout(1, start);
    const uint8_t values[] = {4};
    for (uint32_t seed = 1; seed <= 20; ++seed) {
        const RollPlan p = throwFrom(start, 1, values, seed);
        DiePose before = pose(p, 0, 0);
        for (uint32_t t = 5; t <= kDurationMs; t += 5) {
            const DiePose now = pose(p, 0, t);
            // Fast in the air, but never a teleport between 5 ms steps.
            TEST_ASSERT_TRUE(distance(before.position, now.position) < 4.0f);
            TEST_ASSERT_TRUE(angleBetween(before.orientation, now.orientation) < 0.35f);
            before = now;
        }
    }
}

void test_dice_stay_on_or_above_the_table(void) {
    DiePose start[2];
    readyLayout(2, start);
    const uint8_t values[] = {2, 5};
    for (uint32_t seed = 1; seed <= 20; ++seed) {
        const RollPlan p = throwFrom(start, 2, values, seed);
        for (uint32_t t = 0; t <= kDurationMs; t += 10) {
            for (int i = 0; i < 2; ++i) {
                const DiePose d = pose(p, i, t);
                TEST_ASSERT_TRUE(d.position.z >= d.size - 1e-3f);
            }
        }
    }
}

void test_flying_dice_never_overlap(void) {
    DiePose start[2] = {splashPose(0, 0), splashPose(1, 0)};
    const uint8_t values[] = {1, 6};
    for (uint32_t seed = 1; seed <= 50; ++seed) {
        const RollPlan p = throwFrom(start, 2, values, seed);
        for (uint32_t t = 0; t <= kDurationMs; t += 10) {
            DiePose d[2];
            poses(p, t, d);
            TEST_ASSERT_TRUE(distance(d[0].position, d[1].position) >=
                             1.25f * (d[0].size + d[1].size) - 1e-2f);
        }
    }
}

void test_second_die_lands_a_little_later(void) {
    DiePose start[2];
    readyLayout(2, start);
    const uint8_t values[] = {1, 2};
    const RollPlan p = throwFrom(start, 2, values, 3);
    Bounce b[kMaxBounces];
    const int n = bounces(p, b, kMaxBounces);
    TEST_ASSERT_EQUAL(10, n);
    TEST_ASSERT_TRUE(b[5].timeMs > b[0].timeMs);
    for (int i = 0; i < n; ++i) {
        TEST_ASSERT_TRUE(b[i].timeMs < kDurationMs);
        TEST_ASSERT_TRUE(b[i].strength > 0 && b[i].strength <= 1);
    }
}

void test_one_die_is_bigger_than_each_of_two(void) {
    TEST_ASSERT_TRUE(dieSize(1) > dieSize(2));
}

void test_a_new_die_is_placed_clear_of_the_others(void) {
    XorShift random(8);
    DiePose one[1];
    readyLayout(1, one);
    for (int i = 0; i < 100; ++i) {
        float x, y;
        placeApart(dieSize(2), one, 1, random, x, y);
        const float dx = x - one[0].position.x, dy = y - one[0].position.y;
        TEST_ASSERT_TRUE(sqrtf(dx * dx + dy * dy) >= 3.1f * dieSize(2));
    }
}

void test_the_departing_die_ends_off_the_screen(void) {
    for (int die = 0; die < 2; ++die) {
        const DiePose start = splashPose(die, 1234);
        const DiePose end = departPose(start, kDepartMs);
        // Its nearest point, seen in perspective, is past the side edge.
        const float persp = 360.0f / (360.0f - end.position.z - 1.6f * end.size);
        const float nearestX = fabsf(end.position.x * 360.0f / (360.0f - end.position.z)) -
                               1.6f * end.size * persp;
        TEST_ASSERT_TRUE(nearestX > 67.5f);
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0, distance(start.position, departPose(start, 0).position));
    }
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_throw_starts_exactly_where_the_dice_are);
    RUN_TEST(test_dice_end_showing_their_values_on_top);
    RUN_TEST(test_dice_rest_inside_the_table_and_apart);
    RUN_TEST(test_the_motion_has_no_jumps);
    RUN_TEST(test_dice_stay_on_or_above_the_table);
    RUN_TEST(test_flying_dice_never_overlap);
    RUN_TEST(test_second_die_lands_a_little_later);
    RUN_TEST(test_one_die_is_bigger_than_each_of_two);
    RUN_TEST(test_a_new_die_is_placed_clear_of_the_others);
    RUN_TEST(test_the_departing_die_ends_off_the_screen);
    return UNITY_END();
}
