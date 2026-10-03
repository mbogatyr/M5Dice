// Renders the firmware's own screens on the Mac and saves a contact sheet,
// to check the look without the board.
//
//   tools/preview/build.sh && /tmp/dice_preview /tmp/sheet.png [seed]
//   afplay /tmp/dice_roll.wav; afplay /tmp/dice_rattle.wav
//
// Frames, 2x size, four to a row: the splash, a throw of one die at several
// moments, the rest frames of one and two dice, the mode switch toast, the
// shaking. Every frame goes through DiceScreen, as on the board. It also
// prints the mean time of a moving frame on the Mac (the board is about ten
// times slower) for comparing changes, and writes the sounds of a throw of
// two dice and of the rattle as WAV files next to the sheet.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include <chrono>
#include <thread>
#include <vector>

#include "DiceApp.h"
#include "DiceScreen.h"
#include "DiceSound.h"
#include "UiAssets.h"

namespace {

constexpr int kW = DiceRaster::kWidth;
constexpr int kH = DiceRaster::kHeight;
constexpr int kScale = 2;
constexpr int kColumns = 4;
constexpr int kGap = 8;

void put32(std::vector<uint8_t> &v, uint32_t x) {
    for (int s = 24; s >= 0; s -= 8) {
        v.push_back(static_cast<uint8_t>(x >> s));
    }
}

void chunk(FILE *f, const char *kind, const std::vector<uint8_t> &data) {
    std::vector<uint8_t> out;
    put32(out, static_cast<uint32_t>(data.size()));
    std::vector<uint8_t> body(kind, kind + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    put32(out, static_cast<uint32_t>(crc32(0, body.data(), static_cast<uInt>(body.size()))));
    fwrite(out.data(), 1, out.size(), f);
}

bool writePng(const char *path, int w, int h, const std::vector<uint8_t> &rgb) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        return false;
    }
    static const uint8_t sig[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    fwrite(sig, 1, 8, f);
    std::vector<uint8_t> ihdr;
    put32(ihdr, static_cast<uint32_t>(w));
    put32(ihdr, static_cast<uint32_t>(h));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
    chunk(f, "IHDR", ihdr);
    std::vector<uint8_t> rows;
    for (int y = 0; y < h; ++y) {
        rows.push_back(0);
        rows.insert(rows.end(), rgb.begin() + y * w * 3, rgb.begin() + (y + 1) * w * 3);
    }
    uLongf size = compressBound(static_cast<uLong>(rows.size()));
    std::vector<uint8_t> packed(size);
    compress2(packed.data(), &size, rows.data(), static_cast<uLong>(rows.size()), 9);
    packed.resize(size);
    chunk(f, "IDAT", packed);
    chunk(f, "IEND", {});
    fclose(f);
    return true;
}

bool writeWav(const char *path, const std::vector<int16_t> &samples) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        return false;
    }
    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    const uint32_t bytes = static_cast<uint32_t>(samples.size() * 2);
    fwrite("RIFF", 1, 4, f);
    u32(36 + bytes);
    fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(1); // PCM
    u16(1); // mono
    u32(DiceSound::kSampleRate);
    u32(DiceSound::kSampleRate * 2);
    u16(2);
    u16(16);
    fwrite("data", 1, 4, f);
    u32(bytes);
    fwrite(samples.data(), 2, samples.size(), f); // the Mac is little-endian too
    fclose(f);
    return true;
}

struct Sheet {
    int count;
    int rows;
    int width, height;
    std::vector<uint8_t> rgb;

    explicit Sheet(int frames)
        : count(0), rows((frames + kColumns - 1) / kColumns),
          width(kColumns * kW * kScale + (kColumns + 1) * kGap),
          height(rows * kH * kScale + (rows + 1) * kGap),
          rgb(static_cast<size_t>(width) * height * 3, 40) {}

    void add(const uint16_t *frame) {
        const int ox = kGap + (count % kColumns) * (kW * kScale + kGap);
        const int oy = kGap + (count / kColumns) * (kH * kScale + kGap);
        for (int y = 0; y < kH * kScale; ++y) {
            for (int x = 0; x < kW * kScale; ++x) {
                const uint16_t s = frame[(y / kScale) * kW + x / kScale];
                const uint16_t c = static_cast<uint16_t>((s >> 8) | (s << 8));
                const int r = c >> 11, g = (c >> 5) & 0x3F, b = c & 0x1F;
                uint8_t *p = &rgb[(static_cast<size_t>(oy + y) * width + ox + x) * 3];
                p[0] = static_cast<uint8_t>((r << 3) | (r >> 2));
                p[1] = static_cast<uint8_t>((g << 2) | (g >> 4));
                p[2] = static_cast<uint8_t>((b << 3) | (b >> 2));
            }
        }
        ++count;
    }
};

// The second core, as a thread: the board's split, checked on the Mac.
class ThreadParallel : public DiceRaster::Parallel {
  public:
    void start(void (*job)(void *), void *arg) override { worker_ = std::thread(job, arg); }
    void finish() override { worker_.join(); }

  private:
    std::thread worker_;
};

// Feeds the app one tick of input and draws what it shows.
struct Rig {
    XorShift random;
    DiceApp app;
    DiceScreen screen;
    std::vector<uint16_t> frame, background, copy;
    std::vector<uint8_t> tile;
    double movingMs = 0;
    int movingFrames = 0;

    explicit Rig(uint32_t seed)
        : random(seed), app(random), frame(kW * kH), background(kW * kH), copy(kW * kH),
          tile(DiceRaster::kTileBytes) {
        screen.begin({frame.data(), background.data(), copy.data(), tile.data()}, &kUiAssets);
    }

    void tick(uint32_t now, const DiceApp::Input &in = {}) {
        app.update(now, in);
        const DiceView v = app.view(now);
        const auto t0 = std::chrono::steady_clock::now();
        screen.compose(v);
        const auto t1 = std::chrono::steady_clock::now();
        if (v.moving) {
            movingMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
            ++movingFrames;
        }
    }

    // Steps through time at 30 fps so the screen sees every change.
    void run(uint32_t &now, uint32_t until) {
        while (now + 33 <= until) {
            now += 33;
            tick(now);
        }
        now = until;
        tick(now);
    }
};

} // namespace

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : "/tmp/dice_sheet.png";
    const uint32_t seed = argc > 2 ? static_cast<uint32_t>(atoi(argv[2])) : 7;

    Rig rig(seed);
    Sheet sheet(16);
    uint32_t now = 0;
    rig.app.begin(now);

    rig.run(now, 900);
    sheet.add(rig.frame.data()); // splash

    DiceApp::Input key1{};
    key1.key1 = true;
    rig.tick(now += 33, key1); // throw from the splash
    const uint32_t start = now;
    for (uint32_t at : {150u, 450u, 750u, 1100u, 1450u, 1750u}) {
        rig.run(now, start + DiceApp::kSoundLeadMs + at);
        sheet.add(rig.frame.data());
    }
    rig.run(now, start + 2600);
    sheet.add(rig.frame.data()); // one die at rest

    DiceApp::Input key2{};
    key2.key2 = true;
    rig.tick(now += 33, key2); // two dice
    rig.run(now, now + 500);
    sheet.add(rig.frame.data()); // the toast
    rig.run(now, now + 1500);
    sheet.add(rig.frame.data()); // two dice, ready

    rig.tick(now += 33, key1);
    rig.run(now, now + 2600);
    sheet.add(rig.frame.data()); // two dice after a throw

    rig.tick(now += 33, key2); // back to one
    rig.run(now, now + 1500);
    DiceApp::Input shake{};
    shake.shakeBegan = true;
    shake.shaking = true;
    rig.tick(now += 33, shake);
    rig.run(now, now + 300);
    sheet.add(rig.frame.data()); // shaking

    // The split between two workers must not change a single pixel.
    {
        DiceRaster single, split;
        single.begin();
        split.begin();
        ThreadParallel parallel;
        split.setParallel(&parallel);
        std::vector<uint16_t> bg(kW * kH), a(kW * kH), b(kW * kH);
        std::vector<uint8_t> ta(DiceRaster::kTileBytes), tb(DiceRaster::kTileBytes);
        DiceRaster::paintBackground(bg.data());
        DiePose dice[2] = {RollPlanner::splashPose(0, 700), RollPlanner::splashPose(1, 700)};
        const Rect full{0, 0, kW, kH};
        single.render(a.data(), bg.data(), dice, 2, full);
        split.render(b.data(), bg.data(), dice, 2, full);
        const bool same1 = a == b;
        single.renderSmooth(a.data(), bg.data(), dice, 2, full, ta.data());
        split.renderSmooth(b.data(), bg.data(), dice, 2, full, tb.data());
        printf("two workers draw the same frame: %s, %s\n", same1 ? "yes" : "NO",
               a == b ? "yes" : "NO");
    }

    if (!writePng(out, sheet.width, sheet.height, sheet.rgb)) {
        fprintf(stderr, "cannot write %s\n", out);
        return 1;
    }
    printf("%d frames -> %s; a moving frame takes %.2f ms on this Mac\n", sheet.count, out,
           rig.movingFrames ? rig.movingMs / rig.movingFrames : 0.0);

    // A throw of two dice twice over, with a pause, then the rattle three times.
    XorShift random(seed);
    DiePose ready[2];
    RollPlanner::readyLayout(2, ready);
    const uint8_t values[] = {3, 5};
    const RollPlan plan = RollPlanner::plan(ready, 2, values, random);
    Bounce bounces[RollPlanner::kMaxBounces];
    const int n = RollPlanner::bounces(plan, bounces, RollPlanner::kMaxBounces);
    std::vector<int16_t> roll(DiceSound::rollSamples(RollPlanner::kDurationMs));
    DiceSound::renderRoll(bounces, n, seed, roll.data(), roll.size());
    std::vector<int16_t> twice(roll);
    twice.resize(twice.size() + DiceSound::kSampleRate / 2, 0);
    twice.insert(twice.end(), roll.begin(), roll.end());
    std::vector<int16_t> rattle(DiceSound::kRattleSamples);
    DiceSound::renderRattle(seed, rattle.data(), rattle.size());
    std::vector<int16_t> rattles;
    for (int i = 0; i < 3; ++i) {
        rattles.insert(rattles.end(), rattle.begin(), rattle.end());
    }
    writeWav("/tmp/dice_roll.wav", twice);
    writeWav("/tmp/dice_rattle.wav", rattles);
    printf("sounds -> /tmp/dice_roll.wav, /tmp/dice_rattle.wav\n");
    return 0;
}
