#include <unity.h>

#include <math.h>

#include <initializer_list>

#include "DiceMesh.h"

using namespace DiceMesh;

static Mesh mesh;

void setUp(void) {}
void tearDown(void) {}

void test_opposite_faces_add_up_to_seven(void) {
    for (const Face &a : kFaceList) {
        for (const Face &b : kFaceList) {
            if (dot(a.normal, b.normal) < -0.5f) {
                TEST_ASSERT_EQUAL(7, a.value + b.value);
            }
        }
    }
}

void test_faces_wind_counter_clockwise_from_outside(void) {
    for (const Face &f : kFaceList) {
        const Vec3 n = cross(f.tangent, f.bitangent);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1, dot(n, f.normal));
    }
}

void test_normals_are_unit_and_point_outwards(void) {
    for (int i = 0; i < kVertices; ++i) {
        const Vec3 n{mesh.normal[i * 3], mesh.normal[i * 3 + 1], mesh.normal[i * 3 + 2]};
        const Vec3 p{mesh.position[i * 3], mesh.position[i * 3 + 1], mesh.position[i * 3 + 2]};
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1, length(n));
        TEST_ASSERT_TRUE(dot(n, p) > 0);
    }
}

void test_points_lie_on_the_rounded_box(void) {
    // Every point is kRounding away from the inner cube.
    const float inner = 1 - kRounding;
    for (int i = 0; i < kVertices; ++i) {
        float d2 = 0;
        for (int k = 0; k < 3; ++k) {
            const float excess = fabsf(mesh.position[i * 3 + k]) - inner;
            d2 += excess > 0 ? excess * excess : 0;
        }
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, kRounding, sqrtf(d2));
    }
}

void test_neighbouring_faces_meet_without_gaps(void) {
    // Every point on the outer ring of a face has a twin on the next face.
    int matched = 0;
    for (int i = 0; i < kVertices; ++i) {
        const float u = mesh.uv[i * 2], v = mesh.uv[i * 2 + 1];
        if (fabsf(u) < 0.999f && fabsf(v) < 0.999f) {
            continue;
        }
        for (int j = 0; j < kVertices; ++j) {
            if (j / (kGrid * kGrid) == i / (kGrid * kGrid)) {
                continue;
            }
            float d2 = 0;
            for (int k = 0; k < 3; ++k) {
                const float d = mesh.position[i * 3 + k] - mesh.position[j * 3 + k];
                d2 += d * d;
            }
            if (d2 < 1e-10f) {
                ++matched;
                break;
            }
        }
    }
    TEST_ASSERT_EQUAL(kFaces * (kGrid - 1) * 4, matched);
}

void test_face_up_shows_the_value_on_top(void) {
    for (uint8_t value = 1; value <= 6; ++value) {
        for (float yaw : {0.0f, 0.7f, -2.5f}) {
            TEST_ASSERT_EQUAL(value, topValue(faceUp(value, yaw)));
            const Vec3 n = rotate(faceUp(value, yaw), kFaceList[faceOf(value)].normal);
            TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1, n.z);
        }
    }
}

void test_pips_count_matches_the_value(void) {
    for (uint8_t value = 1; value <= 6; ++value) {
        const Pip *p = pips(value);
        for (int i = 0; i < value; ++i) {
            TEST_ASSERT_TRUE(fabsf(p[i].u) <= 0.5f && fabsf(p[i].v) <= 0.5f);
        }
    }
    TEST_ASSERT_EQUAL_FLOAT(0, pips(1)[0].u);
}

int main(int, char **) {
    build(mesh);
    UNITY_BEGIN();
    RUN_TEST(test_opposite_faces_add_up_to_seven);
    RUN_TEST(test_faces_wind_counter_clockwise_from_outside);
    RUN_TEST(test_normals_are_unit_and_point_outwards);
    RUN_TEST(test_points_lie_on_the_rounded_box);
    RUN_TEST(test_neighbouring_faces_meet_without_gaps);
    RUN_TEST(test_face_up_shows_the_value_on_top);
    RUN_TEST(test_pips_count_matches_the_value);
    return UNITY_END();
}
