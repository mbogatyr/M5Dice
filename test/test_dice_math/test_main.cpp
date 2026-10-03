#include <unity.h>

#include "DiceMath.h"

void setUp(void) {}
void tearDown(void) {}

static void assertVec(Vec3 expected, Vec3 actual) {
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, expected.x, actual.x);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, expected.y, actual.y);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, expected.z, actual.z);
}

void test_quarter_turn_around_z_takes_x_to_y(void) {
    assertVec({0, 1, 0}, rotate(axisAngle({0, 0, 1}, kPi / 2), {1, 0, 0}));
}

void test_product_applies_the_right_rotation_first(void) {
    const Quat aboutX = axisAngle({1, 0, 0}, kPi / 2);
    const Quat aboutZ = axisAngle({0, 0, 1}, kPi / 2);
    // y -> z by the turn around x, and z stays put under the turn around z.
    assertVec({0, 0, 1}, rotate(aboutZ * aboutX, {0, 1, 0}));
}

void test_conjugate_undoes_a_rotation(void) {
    const Quat q = axisAngle({0.3f, -1, 0.5f}, 1.7f);
    assertVec({0.2f, 0.4f, -0.9f}, rotate(conjugate(q) * q, {0.2f, 0.4f, -0.9f}));
}

void test_zero_axis_gives_no_rotation(void) {
    const Quat q = axisAngle({0, 0, 0}, 1.0f);
    TEST_ASSERT_EQUAL_FLOAT(1, q.w);
    assertVec({1, 2, 3}, rotate(q, {1, 2, 3}));
}

void test_slerp_ends_at_both_quaternions(void) {
    const Quat a = axisAngle({0, 1, 0}, 0.4f);
    const Quat b = axisAngle({1, 0, 0}, 2.0f);
    assertVec(rotate(a, {1, 2, 3}), rotate(slerp(a, b, 0), {1, 2, 3}));
    assertVec(rotate(b, {1, 2, 3}), rotate(slerp(a, b, 1), {1, 2, 3}));
}

void test_slerp_halfway_is_half_the_angle(void) {
    const Quat half = slerp(identityQuat(), axisAngle({0, 0, 1}, kPi / 2), 0.5f);
    assertVec({cosf(kPi / 4), sinf(kPi / 4), 0}, rotate(half, {1, 0, 0}));
}

void test_normalized_zero_vector_stays_zero(void) {
    assertVec({0, 0, 0}, normalized(Vec3{0, 0, 0}));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_quarter_turn_around_z_takes_x_to_y);
    RUN_TEST(test_product_applies_the_right_rotation_first);
    RUN_TEST(test_conjugate_undoes_a_rotation);
    RUN_TEST(test_zero_axis_gives_no_rotation);
    RUN_TEST(test_slerp_ends_at_both_quaternions);
    RUN_TEST(test_slerp_halfway_is_half_the_angle);
    RUN_TEST(test_normalized_zero_vector_stays_zero);
    return UNITY_END();
}
