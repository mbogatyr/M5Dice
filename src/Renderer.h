#pragma once

#include <M5Unified.h>

#include "DiceScreen.h"

// Puts DiceScreen's frames on the built-in StickS3 display (135x240,
// portrait).
//
// DiceScreen composes each frame in the sprite's buffer; only the rectangle
// it reports as changed is sent, so a die flying in one corner does not cost
// a whole-screen transfer. Composing directly in the sprite and sending it
// in one go also avoids the flicker of drawing on the screen itself.
class Renderer {
  public:
    // Call after M5.begin(). False if the buffers did not fit.
    bool begin();

    void draw(const DiceView &view);

    // Forgets the last frame. Needed after the display wakes up: its
    // contents are lost, and DiceScreen would otherwise think nothing needs
    // drawing.
    void invalidate() { screen_.invalidate(); }

    // The frame as it is, for the "s" serial command: "SNAP w h" and the
    // RGB565 pixels, high byte first.
    void writeSnapshot(Print &out);

    // Where the buffers ended up: "int" or "ext" (PSRAM) for frame and felt.
    void printMemory(Print &out);

    const DiceRaster::Profile &rasterProfile() { return screen_.raster().profile(); }
    uint32_t lastComposeUs() const { return composeUs_; }
    uint32_t lastPushUs() const { return pushUs_; }
    int lastPushArea() const { return pushArea_; }

  private:
    // The other core (0; loop() runs on 1) drawing half of the rows. It
    // blocks between frames, so core 0's idle task still runs and its
    // watchdog stays quiet.
    class CoreZero : public DiceRaster::Parallel {
      public:
        bool begin();
        void start(void (*job)(void *), void *arg) override;
        void finish() override;

      private:
        static void main(void *self);

        TaskHandle_t task_ = nullptr;
        SemaphoreHandle_t done_ = nullptr;
        void (*job_)(void *) = nullptr;
        void *arg_ = nullptr;
    };

    M5Canvas canvas_{&M5.Display};
    CoreZero coreZero_;
    const uint16_t *background_ = nullptr;
    DiceScreen screen_;
    uint32_t composeUs_ = 0;
    uint32_t pushUs_ = 0;
    int pushArea_ = 0;
};
