# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Dice for the M5StickS3: one or two 3D dice on green felt, thrown with KEY1 or
by shaking the stick, with synthesized clacks. The dice tumble, bounce and
come to rest top face up at a random place and turn; one fixed light from the
upper left shades them per pixel. The screen sleeps after a minute without
activity and wakes, showing the last throw, when the stick is picked up.

## Language

All documentation and the project itself are kept in English: this file, code
comments, identifiers, strings shown on the display, commit messages and any
other text files. New text is written in English too, even when the
conversation with the user happens in another language.

## Commands

PlatformIO is not installed globally but by the official installer into a
venv. The binary lives at `~/.platformio/penv/bin/pio`; it is not on `PATH`,
so it has to be called by its full path.

```bash
~/.platformio/penv/bin/pio test -e native                         # host unit tests for the logic
~/.platformio/penv/bin/pio test -e native -f test_display_timeout # a single test suite
~/.platformio/penv/bin/pio run -e sticks3                         # build the firmware
~/.platformio/penv/bin/pio run -e sticks3 -t upload               # flash the board
~/.platformio/penv/bin/pio run -e sticks3 -t merged               # single image for M5Burner
~/.platformio/penv/bin/pio device monitor -e sticks3              # serial monitor, 115200
tools/preview/build.sh && /tmp/dice_preview /tmp/sheet.png        # the screens, rendered on the Mac
python3 tools/make_ui_assets.py                                    # after changing any text or the font
```

`tools/preview` builds the real `lib/` code on the Mac and saves a contact
sheet of frames (splash, a throw, the rest frames, the toast, the shaking),
plus `/tmp/dice_roll.wav` and `/tmp/dice_rattle.wav` (`afplay` them). Check
every change of the look there before flashing.

`tools/prototype/dice.html` is the reference the look was chosen from: the
same rasterizer in JavaScript, interactive, with sound. Change the look there
first if the change is a design question, then port it.

The xtensa-esp32s3 toolchain is installed into `~/.platformio/packages` once
per machine (about eight minutes) and is shared by all projects. The first
build of a new project downloads the latest M5Unified into `.pio/` and takes
about 20 seconds.

## Architecture

The split into `lib/` and `src/` is not cosmetic here, it is load-bearing:

- `lib/` is the logic and the drawing: plain C++ with no Arduino, no
  M5Unified and no hardware access of any kind.
  - `DiceMath`: vectors, quaternions, single precision only.
  - `DiceMesh`: the rounded cube (9 x 9 grid a face), faces, pips,
    `faceUp(value, yaw)`.
  - `DiceRaster`: the software rasterizer, see Rendering.
  - `RollPlanner`: a throw planned whole at its start (see its header), the
    splash and shaking poses, keeping two dice apart.
  - `Randomness`: the random source interface, `XorShift` for tests,
    `rollDie()` without modulo bias.
  - `DiceApp`: the game's state machine (splash, table, shaking, throw,
    toast, sleep); produces a `DiceView` per frame.
  - `DiceScreen`: composes frames from `DiceView`s and decides how much to
    redraw; `Compose` blends the text masks.
  - `ShakeDetector`, `PickupDetector`, `DisplayTimeout` (1 minute).
  - `DiceSound`: the clacks and the rattle, synthesized.
- `src/` is everything that knows about the board: `Renderer` owns the
  buffers and pushes the changed rectangle, `AudioOut` plays through
  `M5.Speaker`, `main.cpp` wires it all and takes the serial commands.
  `UiAssets.cpp` is generated.

The `native` environment builds only `lib/` (PlatformIO's `test_build_src`
defaults to `no`), so the logic is tested on the Mac without the board.
**Do not pull hardware dependencies into `lib/`: that breaks the tests and
the preview.**

### Time is passed in as a parameter

The logic in `lib/` does not call `millis()` itself; it receives the current
time as an argument. That way tests can substitute any moment without
waiting, and the logic has no `delay()`. At rest `loop()` runs at 50 Hz;
while dice move, drawing paces it.

`millis()` overflows after roughly 49 days. Compute intervals with unsigned
subtraction `now - since`, so the overflow goes unnoticed.

Randomness is passed in too: the board uses `esp_random()` (the hardware
RNG), tests a seeded `XorShift`.

### Rendering

`DiceRaster` draws into the sprite's RGB565 buffer (byte-swapped, as M5GFX
keeps it): a perspective camera 360 px above the table, back-face culling
(the dice are convex, so no depth buffer; two dice are painted far to near),
per-pixel shading from interpolated normals, pips as dimples, a shadow swept
along the light, ordered dithering into RGB565. `pow()` is replaced by
lookup tables.

`DiceScreen` decides how much to redraw:

- dice moving: one sample per pixel, only the union of where the dice were
  and are, and only that rectangle is pushed;
- dice come to rest: the whole screen once, the dice at 2 x 2 samples (a
  PSRAM tile), kept in `sceneCopy`;
- only the text changes (the result fading in, the toast): `sceneCopy`
  restored, the text drawn again.

The text is blended, so it must never be drawn twice over the same pixels:
`Canvas` carries a clip rectangle for that.

`Renderer::invalidate()` makes the next frame a full one; `main.cpp` calls it
when the display wakes up, because the panel's contents are lost during
sleep.

The sprite and the felt live in internal RAM (per-pixel access through the
PSRAM cache is several times slower); `sceneCopy`, the 2 x tile and the sound
buffers live in PSRAM.

### Text

No font reaches the firmware. `tools/make_ui_assets.py` renders the captions
and digits with Inter (`assets/fonts`, SIL OFL) into 8-bit alpha masks in
`src/UiAssets.cpp`; `Compose` blends them in any color. Captions are spaced
capitals made with real spaces, as in the prototype.

### Sound

`DiceSound` renders a whole throw into one buffer at its start, one clack per
bounce of the plan, so the sound follows the dice exactly. The speaker's DMA
ring delays sound by about 40 ms, so a throw's dice start moving
`DiceApp::kSoundLeadMs` later. The speaker is switched off 3 s after the last
sound, which also turns the amp off.

## Board specifics

M5StickS3 is an ESP32-S3-PICO-1-N8R8 with 8 MB of flash, 8 MB of octal PSRAM
and an ST7789P3 135x240 display.

- PlatformIO has **no** `m5stack-sticks3` board id. The project uses
  `esp32-s3-devkitc-1` plus `board_build.arduino.memory_type = qio_opi` and
  the `default_8MB.csv` partitions. Do not "fix" this to a non-existent id.
- USB is native, with no CH9102 bridge, so on macOS the port is called
  `/dev/cu.usbmodem*`, not `/dev/cu.usbserial*`. Serial output needs the
  `-DARDUINO_USB_CDC_ON_BOOT=1` flag, which is already set, and
  `Serial.begin()` in `setup()`. `upload_port` is pinned to that name,
  otherwise PlatformIO may pick the Bluetooth port when the board is absent.
- Buttons: KEY1 on G11 (`M5.BtnA`), KEY2 on G12 (`M5.BtnB`). Grove (G9/G10)
  and HAT2 (G1–G8, G43, G44) are free.
- `platformio.ini` drops `-std=gnu++11` (the board would build as C++11) and
  `-Os` (per-pixel code runs about twice as slow as with `-O2`).
- The IMU is a BMI270; M5Unified already turns its axes to the board's: X
  across the short side, Y along the long side, Z out of the screen.
  `M5.Imu.getAccel()` reads the sensor itself.
- The speaker (ES8311 DAC, amp switched by the PMIC) shares the I2S clock
  lines with the microphone, so the mic is disabled in `setup()`.

### Publishing to M5Burner

M5Burner writes the uploaded file starting at address 0x0, so it needs a full
image. A bare `firmware.bin` is meant for address 0x10000: written at 0x0, it
overwrites the bootloader. `pio run -e sticks3 -t merged` (the extra script
`tools/merged_image.py`) merges the bootloader, the partition table,
`boot_app0` and the application into `.pio/build/sticks3/firmware-merged.bin`
with esptool `merge_bin`. The script takes the addresses and flash parameters
(dio, 80m, 8MB) from PlatformIO's regular upload settings, so the image matches
what `upload` writes.

How this is known. Checked in M5SpectrumAnalyzer on 2026-09-27: of the six
StickS3 firmwares on burner.m5stack.com, five, including the official
UIFlow2.0, are full images. Each has the bootloader at 0x0 (header
`e9 03 02 3f`), the partition table at 0x8000 (`aa 50`) and the application at
0x10000. One firmware was uploaded as a bare application. The merged image was
tested on the board: flashed on its own, at address 0x0, with esptool.

The upload form is at burner.m5stack.com/developer/firmware/upload. It asks for:
- a name, a category and the supported devices (StickS3);
- a firmware description and a version description, both in Markdown;
- the version number and a link to the project;
- the `.bin` file;
- visibility: Public requires moderation;
- a cover image: a screenshot of the screen works.

### The side button is handled by the PMIC, not the firmware

| Action | Result |
|---|---|
| Single press | Power on / reset |
| Double press | Power off |
| Long hold | Download mode (the internal green LED blinks) |

So the firmware does not need its own power-off button. If powering off from
software is ever needed (for example on idle), `M5.Power.powerOff()` used to
wake the StickS3 right away by timer —
[M5Unified#235](https://github.com/m5stack/M5Unified/issues/235), fixed in
0.2.23. This has not been checked on a board with the fixed version.

`M5.Power` does not set `_wakeupPin` for the StickS3, so there is no
ready-made wake-up from deep sleep by button; it would have to be configured
manually with `esp_sleep_enable_ext0_wakeup`.

### If flashing fails

`A fatal error occurred: Failed to connect to ESP32-S3: No serial data received.`

The board shows up as `USB JTAG_serial debug unit` (VID 0x303A, PID 0x1001):
that is the built-in USB-Serial-JTAG, not a CDC port (a consequence of
`ARDUINO_USB_MODE=1`). Auto-reset into download mode through it does not
always work, and neither `--before usb_reset` nor `--before no_reset` helps.
The only fix is manual: hold the side button until the green LED blinks.

### If the board is stuck in the bootloader

The firmware does not start, and the port shows `boot:0x0 (DOWNLOAD(USB/UART0))`
and `waiting for download`. This happened when a pyserial script opened and
closed the port to check on it: macOS toggles DTR/RTS when doing so, and
USB-Serial-JTAG takes that as a command to enter the bootloader. The way out is
a single short press of the side button. So checking the firmware by opening
the port from a script is best avoided; `pio device monitor` has not been
checked for this effect.

## Tests

Unit tests cover the logic in `lib/`, with one directory
`test/test_<module>/test_main.cpp` per module. Rendering is checked by eye,
on the Mac with `tools/preview` and on the board with the `s` screenshot: do
not try to write tests for `DiceRaster` or `DiceScreen`; they would prove
nothing useful.

`main` in the tests returns the number of failures from `UNITY_END()`, and
PlatformIO reports a non-zero exit code as a signal number. A line like
`Program received signal SIGALRM` with failing tests is a reporting artifact,
not a separate problem; it disappears once the tests pass.

## Self-testing on the board

`tools/serial_cmd.py` talks to the firmware over USB Serial without resetting
it (see `open_port()`): `s` screenshot, `k1`/`k2` keys, `shake [ms]`, `perf`,
`acc`, `st`, `vol <n>`; its docstring lists them.

```bash
~/.platformio/penv/bin/python tools/serial_cmd.py snap /tmp/screen.png
~/.platformio/penv/bin/python tools/serial_cmd.py send k1 --wait 3
~/.platformio/penv/bin/python tools/serial_cmd.py send perf --wait 5
```

Serial lines are skipped when nobody reads them (`Serial.availableForWrite()`),
so a full USB buffer never stalls the loop.
