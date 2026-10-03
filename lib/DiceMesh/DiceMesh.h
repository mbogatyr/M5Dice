#pragma once

#include <stdint.h>

#include "DiceMath.h"

// The geometry of one die: a cube with rounded edges and corners, its faces
// and the layout of the pips.
//
// The die has a half size of 1: its faces lie at +-1 on each axis. Every face
// is a 9 x 9 grid of points on the cube; a point is pulled onto the
// rounded box by clamping it to the inner cube of half size 1 - kRounding
// and stepping kRounding along the direction from there. The grid is dense
// across the bends, so the highlights along them stay smooth; along an edge
// nothing changes, and the flat middle needs only a few cells. The normal of
// each point is that same direction, so it is exact.

// Where a die is: the center of the die in world coordinates (see
// DiceMath.h), its orientation and its half size in pixels. A die resting on
// the table has position.z == size.
struct DiePose {
    Vec3 position;
    Quat orientation;
    float size;
};

namespace DiceMesh {

constexpr int kGrid = 9;
constexpr int kFaces = 6;
constexpr int kVertices = kFaces * kGrid * kGrid;
constexpr int kTriangles = kFaces * (kGrid - 1) * (kGrid - 1) * 2;
constexpr float kRounding = 0.22f;

// A face as seen from outside: normal, and the directions of its u and v
// coordinates (both from -1 to 1 across the face). tangent x bitangent =
// normal, so a face's triangles wind counter-clockwise from outside.
struct Face {
    Vec3 normal;
    Vec3 tangent;
    Vec3 bitangent;
    uint8_t value;
};

// Opposite faces add up to seven.
extern const Face kFaceList[kFaces];

// Pip centers in face (u, v) coordinates; a face with value n has n pips.
struct Pip {
    float u, v;
};
const Pip *pips(uint8_t value);

struct Mesh {
    float position[kVertices * 3]; // on the rounded box, half size 1
    float normal[kVertices * 3];
    float uv[kVertices * 2];       // face coordinates of the grid point
    uint16_t triangle[kTriangles * 3];
    uint8_t face[kTriangles];      // index into kFaceList
};

void build(Mesh &mesh);

// Index of the face with this value, 1..6.
int faceOf(uint8_t value);

// The orientation that turns the face with this value towards the viewer
// (+z) and then spins the die by yaw radians around the vertical.
Quat faceUp(uint8_t value, float yaw);

// The value on the face pointing most towards the viewer.
uint8_t topValue(Quat q);

} // namespace DiceMesh
