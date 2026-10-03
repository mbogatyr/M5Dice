#include "DiceMesh.h"

namespace DiceMesh {

namespace {

// Grid positions along a face. The points past +-0.78 sit on the bend at 15,
// 30 and 45 degrees: 0.78 + 0.22 * tan(angle). Between them the face is flat
// and needs only one split, against the affine texture warp of a tilted face.
constexpr float kSteps[kGrid] = {-1.0f, -0.907f, -0.839f, -0.78f, 0.0f,
                                 0.78f, 0.839f,  0.907f,  1.0f};

const Pip kPips[] = {
    {0, 0},                                                    // 1
    {-0.5f, -0.5f}, {0.5f, 0.5f},                              // 2
    {-0.5f, -0.5f}, {0, 0},         {0.5f, 0.5f},              // 3
    {-0.5f, -0.5f}, {0.5f, -0.5f},  {-0.5f, 0.5f}, {0.5f, 0.5f}, // 4
    {-0.5f, -0.5f}, {0.5f, -0.5f},  {0, 0},        {-0.5f, 0.5f}, {0.5f, 0.5f}, // 5
    {-0.5f, -0.5f}, {0.5f, -0.5f},  {-0.5f, 0},    {0.5f, 0},
    {-0.5f, 0.5f},  {0.5f, 0.5f},                              // 6
};

// Where the pips of each value start in kPips: 0, 1, 3, 6, 10, 15.
constexpr int kPipStart[7] = {0, 0, 1, 3, 6, 10, 15};

float clampInner(float x) {
    constexpr float kInner = 1.0f - kRounding;
    return x < -kInner ? -kInner : (x > kInner ? kInner : x);
}

} // namespace

const Face kFaceList[kFaces] = {
    {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}, 1},   {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}, 6},
    {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, 3},   {{-1, 0, 0}, {0, -1, 0}, {0, 0, 1}, 4},
    {{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}, 2},  {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}, 5},
};

const Pip *pips(uint8_t value) { return kPips + kPipStart[value]; }

void build(Mesh &mesh) {
    int vertex = 0;
    int triangle = 0;
    for (int f = 0; f < kFaces; ++f) {
        const Face &face = kFaceList[f];
        const int base = vertex;
        for (int i = 0; i < kGrid; ++i) {
            for (int j = 0; j < kGrid; ++j) {
                const float u = kSteps[i];
                const float v = kSteps[j];
                const Vec3 onCube = face.normal + face.tangent * u + face.bitangent * v;
                const Vec3 inner{clampInner(onCube.x), clampInner(onCube.y),
                                 clampInner(onCube.z)};
                const Vec3 n = normalized(onCube - inner);
                const Vec3 p = inner + n * kRounding;
                mesh.position[vertex * 3] = p.x;
                mesh.position[vertex * 3 + 1] = p.y;
                mesh.position[vertex * 3 + 2] = p.z;
                mesh.normal[vertex * 3] = n.x;
                mesh.normal[vertex * 3 + 1] = n.y;
                mesh.normal[vertex * 3 + 2] = n.z;
                mesh.uv[vertex * 2] = u;
                mesh.uv[vertex * 2 + 1] = v;
                ++vertex;
            }
        }
        for (int i = 0; i < kGrid - 1; ++i) {
            for (int j = 0; j < kGrid - 1; ++j) {
                // (i, j), (i+1, j), (i+1, j+1), (i, j+1): counter-clockwise
                // in (u, v), hence from outside.
                const uint16_t a = static_cast<uint16_t>(base + i * kGrid + j);
                const uint16_t b = static_cast<uint16_t>(a + kGrid);
                const uint16_t c = static_cast<uint16_t>(b + 1);
                const uint16_t d = static_cast<uint16_t>(a + 1);
                const uint16_t quad[6] = {a, b, c, a, c, d};
                for (int k = 0; k < 6; ++k) {
                    mesh.triangle[triangle * 3 + k] = quad[k];
                }
                mesh.face[triangle] = static_cast<uint8_t>(f);
                mesh.face[triangle + 1] = static_cast<uint8_t>(f);
                triangle += 2;
            }
        }
    }
}

int faceOf(uint8_t value) {
    for (int f = 0; f < kFaces; ++f) {
        if (kFaceList[f].value == value) {
            return f;
        }
    }
    return 0;
}

Quat faceUp(uint8_t value, float yaw) {
    const Vec3 n = kFaceList[faceOf(value)].normal;
    Quat align;
    if (n.z > 0.5f) {
        align = identityQuat();
    } else if (n.z < -0.5f) {
        align = axisAngle({1, 0, 0}, kPi);
    } else {
        // A quarter turn around n x z carries n onto z.
        align = axisAngle({n.y, -n.x, 0}, kPi / 2);
    }
    return axisAngle({0, 0, 1}, yaw) * align;
}

uint8_t topValue(Quat q) {
    const Mat3 r = toMat3(q);
    float best = -2;
    uint8_t value = 0;
    for (const Face &face : kFaceList) {
        const float up = (r * face.normal).z;
        if (up > best) {
            best = up;
            value = face.value;
        }
    }
    return value;
}

} // namespace DiceMesh
