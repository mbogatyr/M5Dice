#include <unity.h>

#include "Randomness.h"

void setUp(void) {}
void tearDown(void) {}

// Feeds a fixed list of numbers.
class Script : public Randomness {
  public:
    Script(const uint32_t *values, int count) : values_(values), count_(count) {}
    uint32_t next() override { return values_[index_++ % count_]; }
    int used() const { return index_; }

  private:
    const uint32_t *values_;
    int count_;
    int index_ = 0;
};

void test_die_values_stay_within_one_to_six(void) {
    XorShift random(1);
    for (int i = 0; i < 10000; ++i) {
        const uint8_t v = rollDie(random);
        TEST_ASSERT_TRUE(v >= 1 && v <= 6);
    }
}

void test_numbers_from_the_incomplete_last_six_are_thrown_away(void) {
    const uint32_t values[] = {0xFFFFFFFFu, 4294967292u, 7};
    Script script(values, 3);
    TEST_ASSERT_EQUAL(2, rollDie(script)); // 7 % 6 + 1
    TEST_ASSERT_EQUAL(3, script.used());
}

void test_die_is_fair(void) {
    // 600000 throws: a chi-square over 5 degrees of freedom above 20.5
    // happens by chance once in a thousand.
    XorShift random(12345);
    int counts[7] = {};
    const int throws = 600000;
    for (int i = 0; i < throws; ++i) {
        ++counts[rollDie(random)];
    }
    const double expected = throws / 6.0;
    double chi2 = 0;
    for (int v = 1; v <= 6; ++v) {
        const double d = counts[v] - expected;
        chi2 += d * d / expected;
    }
    TEST_ASSERT_TRUE(chi2 < 20.5);
}

void test_uniform_stays_in_range(void) {
    XorShift random(99);
    for (int i = 0; i < 10000; ++i) {
        const float u = random.uniform(-2, 3);
        TEST_ASSERT_TRUE(u >= -2 && u < 3);
    }
}

void test_same_seed_gives_same_numbers(void) {
    XorShift a(5), b(5);
    for (int i = 0; i < 100; ++i) {
        TEST_ASSERT_EQUAL_UINT32(a.next(), b.next());
    }
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_die_values_stay_within_one_to_six);
    RUN_TEST(test_numbers_from_the_incomplete_last_six_are_thrown_away);
    RUN_TEST(test_die_is_fair);
    RUN_TEST(test_uniform_stays_in_range);
    RUN_TEST(test_same_seed_gives_same_numbers);
    return UNITY_END();
}
