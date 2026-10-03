#include <unity.h>

#include <math.h>

#include <vector>

#include "DiceSound.h"

using namespace DiceSound;

void setUp(void) {}
void tearDown(void) {}

static int peakIn(const std::vector<int16_t> &s, uint32_t fromMs, uint32_t toMs) {
    int peak = 0;
    for (size_t i = fromMs * kSampleRate / 1000; i < toMs * kSampleRate / 1000 && i < s.size();
         ++i) {
        peak = abs(s[i]) > peak ? abs(s[i]) : peak;
    }
    return peak;
}

static const Bounce kBounces[] = {{300, 1.0f}, {900, 0.5f}, {1500, 0.25f}};

void test_roll_needs_room_for_the_whole_throw(void) {
    std::vector<int16_t> out(rollSamples(RollPlanner::kDurationMs) - 1);
    TEST_ASSERT_EQUAL(0, renderRoll(kBounces, 3, 1, out.data(), out.size()));
}

void test_roll_is_silent_until_the_first_bounce(void) {
    std::vector<int16_t> out(rollSamples(RollPlanner::kDurationMs));
    TEST_ASSERT_EQUAL(out.size(), renderRoll(kBounces, 3, 1, out.data(), out.size()));
    TEST_ASSERT_EQUAL(0, peakIn(out, 0, 299));
}

void test_each_bounce_is_heard_and_weaker_ones_are_quieter(void) {
    std::vector<int16_t> out(rollSamples(RollPlanner::kDurationMs));
    renderRoll(kBounces, 3, 1, out.data(), out.size());
    const int first = peakIn(out, 300, 360);
    const int second = peakIn(out, 900, 960);
    const int third = peakIn(out, 1500, 1560);
    TEST_ASSERT_TRUE(first > 20000);
    TEST_ASSERT_TRUE(second > 3000 && second < first);
    TEST_ASSERT_TRUE(third > 1000 && third < second);
}

void test_clacks_die_out_before_the_next_one(void) {
    std::vector<int16_t> out(rollSamples(RollPlanner::kDurationMs));
    renderRoll(kBounces, 3, 1, out.data(), out.size());
    TEST_ASSERT_TRUE(peakIn(out, 400, 890) < 50);
    TEST_ASSERT_TRUE(peakIn(out, 1600, 2100) < 50);
}

void test_nothing_clips(void) {
    std::vector<int16_t> out(rollSamples(RollPlanner::kDurationMs));
    const Bounce together[] = {{500, 1.0f}, {502, 1.0f}, {505, 1.0f}};
    renderRoll(together, 3, 9, out.data(), out.size());
    TEST_ASSERT_TRUE(peakIn(out, 0, 2100) <= 32767 * 0.95f + 1);
}

void test_rattle_loops_without_a_click(void) {
    std::vector<int16_t> out(kRattleSamples);
    TEST_ASSERT_EQUAL(kRattleSamples, renderRattle(4, out.data(), out.size()));
    // Quiet at both ends of the loop, busy in between.
    TEST_ASSERT_TRUE(abs(out.front()) < 50 && abs(out.back()) < 50);
    TEST_ASSERT_TRUE(peakIn(out, 100, 500) > 5000);
}

void test_same_seed_sounds_the_same(void) {
    std::vector<int16_t> a(rollSamples(RollPlanner::kDurationMs)), b(a.size());
    renderRoll(kBounces, 3, 77, a.data(), a.size());
    renderRoll(kBounces, 3, 77, b.data(), b.size());
    TEST_ASSERT_TRUE(a == b);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_roll_needs_room_for_the_whole_throw);
    RUN_TEST(test_roll_is_silent_until_the_first_bounce);
    RUN_TEST(test_each_bounce_is_heard_and_weaker_ones_are_quieter);
    RUN_TEST(test_clacks_die_out_before_the_next_one);
    RUN_TEST(test_nothing_clips);
    RUN_TEST(test_rattle_loops_without_a_click);
    RUN_TEST(test_same_seed_sounds_the_same);
    return UNITY_END();
}
