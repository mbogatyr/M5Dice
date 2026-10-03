#include <unity.h>

#include <math.h>

#include "PickupDetector.h"

void setUp(void) {}
void tearDown(void) {}

// Gravity tilted by deg away from the screen normal.
static bool tilted(PickupDetector &d, float deg) {
    const float r = deg * 3.14159265f / 180.0f;
    return d.update(sinf(r), 0, cosf(r));
}

void test_the_first_reading_only_anchors(void) {
    PickupDetector d;
    TEST_ASSERT_FALSE(tilted(d, 40));
}

void test_lying_still_at_an_angle_wakes_nothing(void) {
    PickupDetector d;
    tilted(d, 30);
    for (int i = 0; i < 100; ++i) {
        TEST_ASSERT_FALSE(tilted(d, 30.5f));
    }
}

void test_turning_the_stick_is_a_pickup(void) {
    PickupDetector d;
    tilted(d, 0);
    TEST_ASSERT_FALSE(tilted(d, 7));
    TEST_ASSERT_TRUE(tilted(d, 9));
}

void test_a_jolt_is_a_pickup(void) {
    PickupDetector d;
    d.update(0, 0, 1);
    TEST_ASSERT_TRUE(d.update(0, 0, 1.3f));
}

void test_reset_takes_the_next_pose_as_rest(void) {
    PickupDetector d;
    tilted(d, 0);
    d.reset();
    TEST_ASSERT_FALSE(tilted(d, 60));
    TEST_ASSERT_FALSE(tilted(d, 62));
}

void test_a_glitch_reading_is_ignored(void) {
    PickupDetector d;
    tilted(d, 0);
    TEST_ASSERT_FALSE(d.update(0, 0, 0.1f));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_the_first_reading_only_anchors);
    RUN_TEST(test_lying_still_at_an_angle_wakes_nothing);
    RUN_TEST(test_turning_the_stick_is_a_pickup);
    RUN_TEST(test_a_jolt_is_a_pickup);
    RUN_TEST(test_reset_takes_the_next_pose_as_rest);
    RUN_TEST(test_a_glitch_reading_is_ignored);
    return UNITY_END();
}
