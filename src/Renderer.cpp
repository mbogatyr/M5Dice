#include "Renderer.h"

#include <esp_heap_caps.h>
#include <esp_memory_utils.h>

#include "UiAssets.h"

namespace {

// The renderer reads and writes its buffers pixel by pixel: those go to
// internal RAM, which is several times faster than PSRAM through its cache.
// The copies that are only read in bulk go to PSRAM.
template <typename T>
T *allocate(size_t bytes, bool internal) {
    void *p = heap_caps_malloc(bytes, internal ? (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
                                               : MALLOC_CAP_SPIRAM);
    if (p == nullptr && internal) {
        p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    }
    return static_cast<T *>(p);
}

} // namespace

bool Renderer::CoreZero::begin() {
    done_ = xSemaphoreCreateBinary();
    return done_ != nullptr &&
           xTaskCreatePinnedToCore(&CoreZero::main, "raster", 8192, this, 1, &task_, 0) == pdPASS;
}

void Renderer::CoreZero::start(void (*job)(void *), void *arg) {
    job_ = job;
    arg_ = arg;
    xTaskNotifyGive(task_);
}

void Renderer::CoreZero::finish() { xSemaphoreTake(done_, portMAX_DELAY); }

void Renderer::CoreZero::main(void *self) {
    CoreZero &w = *static_cast<CoreZero *>(self);
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        w.job_(w.arg_);
        xSemaphoreGive(w.done_);
    }
}

bool Renderer::begin() {
    M5.Display.setRotation(0); // portrait: 135 wide, 240 tall
    M5.Display.fillScreen(TFT_BLACK);

    canvas_.setColorDepth(16);
    canvas_.setPsram(false);
    if (canvas_.createSprite(DiceScreen::kWidth, DiceScreen::kHeight) == nullptr) {
        canvas_.setPsram(true);
        canvas_.createSprite(DiceScreen::kWidth, DiceScreen::kHeight);
    }

    const size_t frameBytes = sizeof(uint16_t) * DiceScreen::kWidth * DiceScreen::kHeight;
    DiceScreen::Buffers b;
    b.frame = static_cast<uint16_t *>(canvas_.getBuffer());
    b.background = allocate<uint16_t>(frameBytes, true);
    background_ = b.background;
    b.sceneCopy = allocate<uint16_t>(frameBytes, false);
    b.tile = allocate<uint8_t>(DiceRaster::kTileBytes, false);
    if (b.frame == nullptr || b.background == nullptr || b.sceneCopy == nullptr ||
        b.tile == nullptr) {
        return false;
    }
    screen_.begin(b, &kUiAssets);
    screen_.raster().setClock([]() -> uint32_t { return micros(); });
    if (coreZero_.begin()) {
        screen_.raster().setParallel(&coreZero_);
    }
    return true;
}

void Renderer::draw(const DiceView &view) {
    const uint32_t t0 = micros();
    const Rect r = screen_.compose(view);
    const uint32_t t1 = micros();
    if (r.empty()) {
        return;
    }
    composeUs_ = t1 - t0;
    M5.Display.setClipRect(r.x0, r.y0, r.width(), r.height());
    canvas_.pushSprite(0, 0);
    M5.Display.clearClipRect();
    pushUs_ = micros() - t1;
    pushArea_ = r.width() * r.height();
}

void Renderer::printMemory(Print &out) {
    const auto where = [](const void *p) { return esp_ptr_external_ram(p) ? "ext" : "int"; };
    out.printf("mem frame %s %p felt %s %p screen %s %p\n", where(canvas_.getBuffer()),
               canvas_.getBuffer(), where(background_), background_, where(&screen_), &screen_);
}

void Renderer::writeSnapshot(Print &out) {
    out.printf("SNAP %d %d\n", canvas_.width(), canvas_.height());
    // A 16-bit LovyanGFX sprite already keeps its pixels byte-swapped for
    // SPI, i.e. high byte first, so the buffer goes out as it is.
    out.write(static_cast<const uint8_t *>(canvas_.getBuffer()),
              canvas_.width() * canvas_.height() * 2);
    out.flush();
}
