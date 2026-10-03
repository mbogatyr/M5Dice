# M5Burner listing

| | |
|---|---|
| Name | M5Dice |
| Category | Games |
| Device | StickS3 |
| Version | v1.0.0 |
| File | `dist/M5Dice-v1.0.0.bin`, a full image flashed at 0x0 |
| Cover | `dist/M5Dice-cover.png` (1440 x 810); a copy is `docs/cover.png`, shown in the README |
| Project link | https://github.com/mbogatyr/M5Dice |
| Visibility | Public |
| Uploaded | 2026-10-03, "Pending" review |

`dist/` is not in git. Rebuild it:

```bash
~/.platformio/penv/bin/pio run -e sticks3 -t merged
mkdir -p dist && cp .pio/build/sticks3/firmware-merged.bin dist/M5Dice-v1.0.0.bin
tools/preview/build.sh && /tmp/dice_preview --frames /tmp/frames 8
python3 tools/make_cover.py /tmp/frames
```

The image was checked on the board before uploading: written alone at 0x0
with esptool, it boots to the splash.

A listing is found by its name: a later upload named "M5Dice" adds a version,
and the server refuses it if the public data (the description below) differ.
While the listing waits for review there is no "New version" button.

## Description (as uploaded)

```markdown
**M5Dice** turns the M5StickS3 into real dice, in real 3D.

Press the blue button or just shake the stick: the dice fly up, tumble, bounce off the green felt and come to rest face up, every time at a different spot and angle. One fixed light shades them pixel by pixel, so their faces brighten and darken as they turn, with soft shadows, rounded edges and dimpled pips. Every bounce clacks through the speaker.

- Real-time 3D: a software renderer running on both cores of the ESP32-S3, 30 to 45 frames per second
- One die or two; the result is shown under the dice, like "1 + 6 = 7"
- Fair throws from the ESP32's hardware random number generator
- Shake to roll: the dice rattle while you shake and fly when you stop
- The screen sleeps after a minute without use and wakes up, showing the last throw, when you pick the stick up

### Controls

Hold the stick upright, screen towards you. It starts with one die.

| Input | Action |
|---|---|
| KEY1 (the blue button on the front) | Throw |
| Shake the stick | Rattle, then throw when you stop |
| KEY2 (the button on the edge) | One die / two dice |

Double-press the power button to switch the stick off.

Source code: https://github.com/mbogatyr/M5Dice
```

Version description (v1.0.0):

```markdown
First release.

- Real-time 3D dice with per-pixel lighting, soft shadows and dimpled pips, drawn on both cores at 30 to 45 fps.
- KEY1 or shaking throws; KEY2 switches between one and two dice. Starts with one die.
- Synthesized clacks on every bounce, and a rattle while you shake.
- The screen sleeps after a minute and wakes, showing the last throw, when you pick the stick up.
```
