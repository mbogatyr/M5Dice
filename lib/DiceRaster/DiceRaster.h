#pragma once

#include <stddef.h>
#include <stdint.h>

#include "DiceMath.h"
#include "DiceMesh.h"

// A rectangle of screen pixels, [x0, x1) x [y0, y1).
struct Rect {
    int x0, y0, x1, y1;

    bool empty() const { return x1 <= x0 || y1 <= y0; }
    int width() const { return x1 - x0; }
    int height() const { return y1 - y0; }
};

Rect unite(Rect a, Rect b);
Rect intersect(Rect a, Rect b);

// Draws dice lying on green felt into an RGB565 frame (byte-swapped, the
// order an M5GFX sprite keeps), the way tools/prototype/dice.html does:
//
// - a perspective camera straight above the table, kCameraDistance pixels
//   high, so a die away from the middle shows a little of its sides;
// - one fixed light from the upper left: the light does not turn with a die,
//   so its faces change brightness as it tumbles;
// - per-pixel shading of a rounded box: diffuse, two specular lobes, the
//   reflection of a soft box light and of the felt, with Fresnel;
// - pips as concave dimples with antialiased rims;
// - a soft shadow swept along the light, plus contact darkening when a die
//   touches the table.
//
// The dice are convex, so back-face culling alone resolves a die's own
// faces, and two dice are painted far to near: no depth buffer is needed.
//
// It never touches hardware: buffers come from the caller.
class DiceRaster {
  public:
    static constexpr int kWidth = 135;
    static constexpr int kHeight = 240;
    static constexpr float kCameraDistance = 360.0f;
    static constexpr int kMaxDice = 2;

    // Bytes the 2x tile for renderSmooth() needs at most (a full screen).
    static constexpr size_t kTileBytes = size_t(kWidth) * 2 * kHeight * 2 * 3;

    // Builds the mesh and the lookup tables. Call once.
    void begin();

    // The felt: kWidth * kHeight byte-swapped RGB565 pixels, dithered.
    static void paintBackground(uint16_t *background);

    // The screen pixels a die and its shadow can touch, clipped to the
    // screen.
    Rect bounds(const DiePose &die) const;

    // Repaints clip: felt, shadows, dice. One sample per pixel, for frames
    // in motion.
    void render(uint16_t *frame, const uint16_t *background, const DiePose *dice,
                int count, Rect clip);

    // Optional microsecond clock for profiling; the time of the last
    // render() goes into profile().
    struct Profile {
        uint32_t totalUs;
        uint32_t workerUs[2];
        uint32_t stageUs[4]; // worker 0: fill, shadows, die setup, die pixels
        uint32_t scanned, drawn, triangles; // worker 0
        uint32_t shadedPixels, shadowPixels; // lit one by one, darkened by shadows
    };
    void setClock(uint32_t (*clock)()) { clock_ = clock; }
    const Profile &profile() const { return profile_; }

    // Lets render() and renderSmooth() split their rows with a second core:
    // start() runs job(arg) there, finish() waits for it. Null runs
    // everything on the calling core.
    class Parallel {
      public:
        virtual ~Parallel() = default;
        virtual void start(void (*job)(void *), void *arg) = 0;
        virtual void finish() = 0;
    };
    void setParallel(Parallel *parallel) { parallel_ = parallel; }

    // The same at four samples per pixel, for a die at rest: draws clip at
    // twice the size into tile (RGB888, kTileBytes) and averages it down.
    void renderSmooth(uint16_t *frame, const uint16_t *background,
                      const DiePose *dice, int count, Rect clip, uint8_t *tile);

  private:
    // A part of the screen being drawn, at 1 or 2 samples per pixel side.
    struct Target {
        int scale;
        int originX, originY; // screen pixel of the top left corner
        int width, height;    // in samples
        uint16_t *frame;      // scale 1: the whole frame, kWidth wide
        uint8_t *rgb;         // scale 2: the tile, width * 3 bytes a row
        int worker;           // which scratch, 0 or 1
        int rowFrom, rowTo;   // the rows this worker draws, [rowFrom, rowTo)
    };

    // Per-vertex scratch of the die being drawn, one set per worker.
    struct Scratch {
        float sx[DiceMesh::kVertices];
        float sy[DiceMesh::kVertices];
        float nx[DiceMesh::kVertices];
        float ny[DiceMesh::kVertices];
        float nz[DiceMesh::kVertices];
        uint8_t lit[DiceMesh::kVertices][3]; // vertex colors, sRGB
        uint32_t litAt[DiceMesh::kVertices]; // which die (stamp) lit belongs to
        uint32_t stamp;
        uint32_t shaded, shadow; // pixels, for the profile
        uint32_t us;             // time spent on the last frame
        uint32_t stageUs[4];     // fill, shadows, die setup, die pixels
        uint32_t scanned, drawn, triangles;
    };

    struct Job {
        DiceRaster *raster;
        Target target;
        const uint16_t *background;
        const DiePose *dice;
        int count;
    };
    static void runJob(void *job);
    void drawRows(const Target &t, const uint16_t *background, const DiePose *dice, int count);
    // Splits a target's rows between this core and the other one, if any.
    void drawSplit(Target t, const uint16_t *background, const DiePose *dice, int count);
    // Where to cut the rows so both workers get about the same work.
    int splitRow(const Target &t, const DiePose *dice, int count) const;

    struct FaceFrame {
        Vec3 normal, tangent, bitangent;
        uint8_t value;
    };

    // Per die: the view direction and what follows from it.
    struct Shading {
        Vec3 view, halfway;
        float viewDotLight;
    };

    // Lights a point with unit normal n and color base; linear light out.
    Vec3 lightLinear(const Shading &sh, float nx, float ny, float nz, Vec3 base,
                     float occlusion) const;
    // Tone maps linear light into sRGB bytes.
    void toBytes(Vec3 c, uint8_t out[3]) const;

    void fillBackground(const Target &t, const uint16_t *background) const;
    void drawShadows(const Target &t, const DiePose &die) const;
    void shadowPass(const Target &t, float cx, float cy, float sweepX, float sweepY,
                    float yaw, float half, float rounding, float softness,
                    float opacity) const;
    void drawDie(const Target &t, const DiePose &die);

    void darken(const Target &t, int x, int y, int alpha) const;
    void put(const Target &t, int x, int y, uint8_t r, uint8_t g, uint8_t b) const;

    uint32_t now() const { return clock_ ? clock_() : 0; }

    uint32_t (*clock_)() = nullptr;
    Parallel *parallel_ = nullptr;
    Profile profile_ = {};

    DiceMesh::Mesh mesh_;
    uint8_t flatTriangle_[DiceMesh::kTriangles]; // all three corners on the flat middle
    uint16_t pipMasks_[7];
    mutable Scratch scratch_[2];

    uint8_t toSrgb_[2048];     // linear 0..2 (tone mapped) -> sRGB byte
    uint8_t linearToSrgb_[4096]; // linear 0..1 -> sRGB byte, for averaging
    uint16_t srgbToLinear_[256]; // sRGB byte -> linear * 65535
    uint16_t shadowKeep_[256]; // shadow alpha -> sRGB multiplier * 256
    float specular_[1024];     // both specular lobes against n.h
};
