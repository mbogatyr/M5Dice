#include <unity.h>

#include "ShakeDetector.h"

void setUp(void) {}
void tearDown(void) {}

// Readings every 20 ms (50 Hz), as the firmware takes them.
struct Feed {
    ShakeDetector detector;
    uint32_t now = 0;
    int began = 0, ended = 0;

    ShakeEvents step(float g) {
        now += 20;
        const ShakeEvents e = detector.update(now, 0, 0, g);
        began += e.began;
        ended += e.ended;
        return e;
    }
    void still(int readings) {
        for (int i = 0; i < readings; ++i) {
            step(1.0f);
        }
    }
    // A shake: readings alternating between 2.5 g and nearly free fall.
    void shake(int readings) {
        for (int i = 0; i < readings; ++i) {
            step(i % 2 ? 0.1f : 2.5f);
        }
    }
};

void test_lying_still_is_no_shake(void) {
    Feed f;
    f.still(200);
    TEST_ASSERT_EQUAL(0, f.began);
    TEST_ASSERT_FALSE(f.detector.shaking());
}

void test_a_single_knock_is_no_shake(void) {
    Feed f;
    f.still(10);
    f.step(3.0f);
    f.still(50);
    TEST_ASSERT_EQUAL(0, f.began);
    TEST_ASSERT_EQUAL(0, f.ended);
}

void test_shaking_begins_after_a_few_jolts(void) {
    Feed f;
    f.still(10);
    f.shake(2);
    TEST_ASSERT_EQUAL(0, f.began);
    f.shake(1);
    TEST_ASSERT_EQUAL(1, f.began);
    TEST_ASSERT_TRUE(f.detector.shaking());
}

void test_long_shaking_begins_once(void) {
    Feed f;
    f.shake(150);
    TEST_ASSERT_EQUAL(1, f.began);
    TEST_ASSERT_EQUAL(0, f.ended);
}

void test_shaking_ends_after_a_quiet_moment(void) {
    Feed f;
    f.shake(30);
    f.still(15); // 300 ms: not yet
    TEST_ASSERT_EQUAL(0, f.ended);
    f.still(1);
    TEST_ASSERT_EQUAL(1, f.ended);
    TEST_ASSERT_FALSE(f.detector.shaking());
    f.still(100);
    TEST_ASSERT_EQUAL(1, f.ended);
}

void test_short_pauses_do_not_end_a_shake(void) {
    Feed f;
    f.shake(20);
    f.still(10); // 200 ms
    f.shake(20);
    f.still(30);
    TEST_ASSERT_EQUAL(1, f.began);
    TEST_ASSERT_EQUAL(1, f.ended);
}

void test_shaking_flag_covers_the_began_reading_but_not_the_ended_one(void) {
    Feed f;
    f.step(2.5f);
    f.step(0.1f);
    const ShakeEvents b = f.step(2.5f);
    TEST_ASSERT_TRUE(b.began && b.shaking);
    ShakeEvents e{};
    for (int i = 0; i < 20 && !e.ended; ++i) {
        e = f.step(1.0f);
    }
    TEST_ASSERT_TRUE(e.ended);
    TEST_ASSERT_FALSE(e.shaking);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_lying_still_is_no_shake);
    RUN_TEST(test_a_single_knock_is_no_shake);
    RUN_TEST(test_shaking_begins_after_a_few_jolts);
    RUN_TEST(test_long_shaking_begins_once);
    RUN_TEST(test_shaking_ends_after_a_quiet_moment);
    RUN_TEST(test_short_pauses_do_not_end_a_shake);
    RUN_TEST(test_shaking_flag_covers_the_began_reading_but_not_the_ended_one);
    return UNITY_END();
}
