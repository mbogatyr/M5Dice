#include <M5Unified.h>
#include <esp_random.h>

#include "AudioOut.h"
#include "DiceMath.h"
#include "DiceApp.h"
#include "PickupDetector.h"
#include "Renderer.h"
#include "ShakeDetector.h"

namespace {

constexpr uint8_t kBrightness = 110;
constexpr uint32_t kRestingLoopMs = 20; // 50 Hz: keys and the accelerometer

// The ESP32's hardware random number generator: true randomness for the
// throws (radio noise, or the SAR ADC's while the radio is off).
class HardwareRandom : public Randomness {
  public:
    uint32_t next() override { return esp_random(); }
};

HardwareRandom hardwareRandom;
DiceApp app(hardwareRandom);
Renderer renderer;
AudioOut audio;
ShakeDetector shakeDetector;
PickupDetector pickupDetector;

bool displayAwake = true;

// Test hooks over Serial: line commands, see CLAUDE.md.
bool perfOn = false;
bool accOn = false;
uint8_t pendingKeys = 0;     // bit 0: KEY1, bit 1: KEY2
uint32_t fakeShakeUntil = 0; // a shake played from the serial "shake" command
bool fakeShaking = false;
bool fakeShakeBegun = false;
char command[32];
size_t commandLen = 0;
struct {
    uint32_t sinceMs = 0;
    uint32_t frames = 0;
    uint32_t maxComposeUs = 0;
    uint32_t maxPushUs = 0;
} stats;

void setDisplayAwake(bool awake) {
    if (awake == displayAwake) {
        return;
    }
    displayAwake = awake;
    if (awake) {
        M5.Display.wakeup();
        M5.Display.setBrightness(kBrightness);
        renderer.invalidate();
    } else {
        // The backlight is the main power consumer, so it is turned off
        // separately from putting the panel itself to sleep.
        M5.Display.setBrightness(0);
        M5.Display.sleep();
        pickupDetector.reset();
    }
}

void printStatus() {
    const DiceView v = app.view(millis());
    Serial.printf("st screen %d mode %u awake %d rolling %d shaking %d result %u %u vol %u "
                  "heap %u psram %u\n",
                  static_cast<int>(v.screen), v.mode, app.awake() ? 1 : 0, app.rolling() ? 1 : 0,
                  app.shaking() ? 1 : 0, v.showResult ? v.result[0] : 0,
                  v.showResult && v.count == 2 ? v.result[1] : 0, audio.volume(),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

// Cycles per operation for a few kinds of work, to know what the renderer
// can afford.
void bench() {
    volatile float sink = 0;
    float a = 1.0001f, b = 0.9999f, acc = 0;
    uint32_t c0 = ESP.getCycleCount();
    for (int i = 0; i < 100000; ++i) {
        acc = acc * a + b; // a dependent chain
    }
    uint32_t c1 = ESP.getCycleCount();
    sink = acc;
    float x0 = 0, x1 = 0, x2 = 0, x3 = 0;
    for (int i = 0; i < 100000; ++i) {
        x0 += a; x1 += b; x2 += a; x3 += b; // four independent chains
    }
    uint32_t c2 = ESP.getCycleCount();
    sink = x0 + x1 + x2 + x3;
    float m = 0;
    for (int i = 0; i < 100000; ++i) {
        const float v = static_cast<float>(i & 255) * 0.01f;
        m = v > m ? v : m; // compare and select
    }
    uint32_t c3 = ESP.getCycleCount();
    sink = m;
    int n = 0;
    for (int i = 0; i < 100000; ++i) {
        n += static_cast<int>(static_cast<float>(i) * 0.5f); // float to int
    }
    uint32_t c4 = ESP.getCycleCount();
    sink = static_cast<float>(n);
    float r = 0;
    for (int i = 1; i <= 100000; ++i) {
        r += fastRsqrt(static_cast<float>(i));
    }
    uint32_t c5 = ESP.getCycleCount();
    sink = r;
    (void)sink;
    Serial.printf("bench cpu %u MHz: chain %.1f indep4 %.1f select %.1f toint %.1f rsqrt %.1f "
                  "cycles/iter\n",
                  static_cast<unsigned>(getCpuFrequencyMhz()), (c1 - c0) / 1e5f,
                  (c2 - c1) / 1e5f, (c3 - c2) / 1e5f, (c4 - c3) / 1e5f, (c5 - c4) / 1e5f);
}

void runCommand(const char *line, uint32_t now) {
    if (strcmp(line, "s") == 0) {
        renderer.writeSnapshot(Serial);
    } else if (strcmp(line, "k1") == 0) {
        pendingKeys |= 1;
    } else if (strcmp(line, "k2") == 0) {
        pendingKeys |= 2;
    } else if (strncmp(line, "shake", 5) == 0) {
        const int ms = line[5] == ' ' ? atoi(line + 6) : 600;
        fakeShakeUntil = now + static_cast<uint32_t>(ms);
        fakeShaking = true;
        fakeShakeBegun = false;
    } else if (strcmp(line, "perf") == 0) {
        perfOn = !perfOn;
    } else if (strcmp(line, "acc") == 0) {
        accOn = !accOn;
    } else if (strcmp(line, "st") == 0) {
        printStatus();
    } else if (strcmp(line, "mem") == 0) {
        renderer.printMemory(Serial);
    } else if (strcmp(line, "bench") == 0) {
        bench();
    } else if (strncmp(line, "vol ", 4) == 0) {
        audio.setVolume(static_cast<uint8_t>(atoi(line + 4)));
        Serial.printf("OK vol %u\n", audio.volume());
    } else if (line[0] != '\0') {
        Serial.printf("ERR unknown '%s'\n", line);
    }
}

void pollSerial(uint32_t now) {
    while (Serial.available() > 0) {
        const int c = Serial.read();
        if (c == '\n' || c == '\r') {
            command[commandLen] = '\0';
            runCommand(command, now);
            commandLen = 0;
        } else if (commandLen + 1 < sizeof command) {
            command[commandLen++] = static_cast<char>(c);
        }
    }
}

// Lines for the test tools; skipped when nobody reads them, so a full USB
// buffer never stalls the loop.
bool canPrint() { return Serial.availableForWrite() >= 128; }

DiceApp::Input readInput(uint32_t now, float ax, float ay, float az) {
    DiceApp::Input in{};
    in.key1 = M5.BtnA.wasPressed() || (pendingKeys & 1);
    in.key2 = M5.BtnB.wasPressed() || (pendingKeys & 2);
    pendingKeys = 0;

    ShakeEvents shake = shakeDetector.update(now, ax, ay, az);
    if (fakeShaking) {
        if (static_cast<int32_t>(now - fakeShakeUntil) >= 0) {
            fakeShaking = false;
            shake.ended = true;
            shake.shaking = false;
        } else {
            shake.began = !fakeShakeBegun;
            fakeShakeBegun = true;
            shake.shaking = true;
        }
    }
    in.shakeBegan = shake.began;
    in.shaking = shake.shaking;
    in.shakeEnded = shake.ended;
    in.pickedUp = !app.awake() && pickupDetector.update(ax, ay, az);
    return in;
}

void report(uint32_t now) {
    if (perfOn && now - stats.sinceMs >= 1000) {
        if (canPrint()) {
            const DiceRaster::Profile &rp = renderer.rasterProfile();
            Serial.printf("w0 triangles %u scanned %u drawn %u\n", static_cast<unsigned>(rp.triangles),
                          static_cast<unsigned>(rp.scanned), static_cast<unsigned>(rp.drawn));
            Serial.printf("stages fill %u shadows %u setup %u pixels %u\n",
                          static_cast<unsigned>(rp.stageUs[0]), static_cast<unsigned>(rp.stageUs[1]),
                          static_cast<unsigned>(rp.stageUs[2]), static_cast<unsigned>(rp.stageUs[3]));
            Serial.printf("raster total %u (%u %u) px %u shadowpx %u\n",
                          static_cast<unsigned>(rp.totalUs),
                          static_cast<unsigned>(rp.workerUs[0]),
                          static_cast<unsigned>(rp.workerUs[1]),
                          static_cast<unsigned>(rp.shadedPixels),
                          static_cast<unsigned>(rp.shadowPixels));
            Serial.printf("perf fps %u compose %u push %u (max %u %u) area %d\n",
                          static_cast<unsigned>(stats.frames * 1000 / (now - stats.sinceMs)),
                          static_cast<unsigned>(renderer.lastComposeUs()),
                          static_cast<unsigned>(renderer.lastPushUs()),
                          static_cast<unsigned>(stats.maxComposeUs),
                          static_cast<unsigned>(stats.maxPushUs), renderer.lastPushArea());
        }
        stats.sinceMs = now;
        stats.frames = 0;
        stats.maxComposeUs = 0;
        stats.maxPushUs = 0;
    }
}

} // namespace

void setup() {
    auto cfg = M5.config();
    cfg.internal_spk = true;
    cfg.internal_mic = false; // it shares the I2S clock lines with the speaker
    M5.begin(cfg);
    Serial.begin(115200);

    M5.Display.setBrightness(kBrightness);
    if (!renderer.begin() || !audio.begin()) {
        Serial.println("ERR out of memory");
    }
    app.begin(millis());
}

void loop() {
    M5.update();
    const uint32_t now = millis();
    pollSerial(now);

    float ax = 0, ay = 0, az = 1;
    M5.Imu.getAccel(&ax, &ay, &az);
    if (accOn && canPrint()) {
        Serial.printf("acc %.2f %.2f %.2f\n", ax, ay, az);
    }

    const DiceApp::Input in = readInput(now, ax, ay, az);
    app.update(now, in);

    if (app.takeRollStarted()) {
        const uint32_t t0 = micros();
        audio.playRoll(app.plan(), esp_random());
        app.restartRollClock(millis());
        if (canPrint()) {
            Serial.printf("EV roll audio %u us\n", static_cast<unsigned>(micros() - t0));
        }
    } else if (app.shaking()) {
        audio.startRattle();
    } else {
        audio.stopRattle();
    }
    audio.service(now);

    setDisplayAwake(app.awake());
    if (!displayAwake) {
        delay(kRestingLoopMs);
        return;
    }

    const DiceView view = app.view(millis());
    renderer.draw(view);
    if (view.moving) {
        ++stats.frames;
        stats.maxComposeUs = max(stats.maxComposeUs, renderer.lastComposeUs());
        stats.maxPushUs = max(stats.maxPushUs, renderer.lastPushUs());
    }
    report(now);

    // While the dice move, drawing paces the loop; at rest it waits.
    if (!view.moving && !view.toast) {
        delay(kRestingLoopMs);
    }
}
