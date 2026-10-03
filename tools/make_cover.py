#!/usr/bin/env python3
"""Makes the M5Burner cover, dist/M5Dice-cover.png (1440 x 810).

Two StickS3s on the felt, a throw in the air and its result, showing real
frames of the firmware
(rendered by tools/preview from the firmware's own lib/ code), next to the
title and what the firmware does.

    tools/preview/build.sh && /tmp/dice_preview --frames /tmp/frames 8
    python3 tools/make_cover.py /tmp/frames

Needs Pillow and numpy.
"""

import pathlib
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = pathlib.Path(__file__).resolve().parent.parent
FONT = ROOT / "assets/fonts/Inter[opsz,wght].ttf"
OUTPUT = ROOT / "dist/M5Dice-cover.png"

W, H = 1440, 810
IVORY = (243, 234, 210)
SCREEN_SCALE = 2  # 135 x 240 -> 270 x 480, nearest neighbour: the real pixels


def font(size, weight):
    f = ImageFont.truetype(str(FONT), size)
    f.set_variation_by_axes([min(32, max(14, size)), weight])
    return f


def felt():
    """The firmware's felt, at cover size: lit from the upper left."""
    y, x = np.mgrid[0:H, 0:W].astype(np.float32)
    dx = (x - W * 0.30) / W
    dy = (y - H * 0.30) / H
    r2 = dx * dx * 1.2 + dy * dy * 1.6
    vignette = np.clip(1 - 1.0 * r2, 0.28, 1)
    rng = np.random.default_rng(7)
    grain = rng.random((H, W), dtype=np.float32) - 0.5
    fibers = np.repeat(rng.random((H, W // 2 + 1), dtype=np.float32) - 0.5, 2, axis=1)[:, :W]
    f = vignette * (1 + grain * 0.10 + fibers * 0.06)
    rgb = np.stack([0.12 * f, 0.40 * f, 0.26 * f], axis=-1)
    return Image.fromarray(np.clip(rgb * 255, 0, 255).astype(np.uint8), "RGB")


def stick(frame):
    """A StickS3 around a frame: graphite body, the blue KEY1 under the screen."""
    screen = frame.resize((135 * SCREEN_SCALE, 240 * SCREEN_SCALE), Image.NEAREST)
    pad_x, pad_top, pad_bottom = 22, 26, 78
    bw, bh = screen.width + 2 * pad_x, screen.height + pad_top + pad_bottom
    body = Image.new("RGBA", (bw + 8, bh), (0, 0, 0, 0))
    d = ImageDraw.Draw(body)
    d.rounded_rectangle((0, 0, bw - 1, bh - 1), radius=44, fill=(36, 37, 41, 255))
    d.rounded_rectangle((3, 3, bw - 4, bh - 4), radius=41, outline=(70, 71, 76, 255), width=2)
    # KEY2 on the right edge
    d.rounded_rectangle((bw - 4, 150, bw + 6, 230), radius=4, fill=(90, 91, 96, 255))
    # the glass, then the picture
    d.rounded_rectangle((pad_x - 6, pad_top - 6, pad_x + screen.width + 5,
                         pad_top + screen.height + 5), radius=8, fill=(8, 8, 9, 255))
    body.paste(screen, (pad_x, pad_top))
    # KEY1, the blue button
    kw, kh = 84, 24
    kx, ky = (bw - kw) // 2, pad_top + screen.height + (pad_bottom - kh) // 2
    d.rounded_rectangle((kx, ky, kx + kw, ky + kh), radius=12, fill=(47, 111, 214, 255))
    d.rounded_rectangle((kx + 3, ky + 2, kx + kw - 4, ky + 9), radius=5,
                        fill=(96, 152, 240, 255))
    return body


def place(canvas, device, center, angle):
    rotated = device.rotate(angle, resample=Image.BICUBIC, expand=True)
    # soft shadow towards the lower right, away from the light
    alpha = rotated.split()[3]
    shadow = Image.new("RGBA", rotated.size, (0, 0, 0, 0))
    shadow.putalpha(alpha.point(lambda a: a * 0.55))
    shadow = shadow.filter(ImageFilter.GaussianBlur(18))
    x = int(center[0] - rotated.width / 2)
    y = int(center[1] - rotated.height / 2)
    canvas.alpha_composite(shadow, (x + 26, y + 30))
    canvas.alpha_composite(rotated, (x, y))


def main():
    frames = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/tmp/frames")
    canvas = felt().convert("RGBA")

    flight = stick(Image.open(frames / "flight2_0300.png"))
    result = stick(Image.open(frames / "result2.png"))
    place(canvas, result, (1215, 420), -7)
    place(canvas, flight, (880, 400), 5)

    d = ImageDraw.Draw(canvas)
    x = 84
    d.text((x - 4, 118), "M5Dice", font=font(118, 650), fill=IVORY)
    d.text((x, 262), "Real 3D dice for the M5StickS3", font=font(36, 400),
           fill=IVORY + (235,))
    d.rectangle((x, 330, x + 64, 332), fill=IVORY)
    features = [
        ("Roll with the button", "or just shake the stick"),
        ("Tumbling, bouncing 3D", "lit pixel by pixel, at 30+ fps"),
        ("One die or two", "fair hardware randomness"),
        ("Clacks like the real thing", "sleeps and wakes by itself"),
    ]
    y = 372
    bold, light = font(29, 600), font(25, 400)
    for title, line in features:
        d.rounded_rectangle((x, y + 9, x + 14, y + 23), radius=3, fill=IVORY)
        d.text((x + 32, y), title, font=bold, fill=IVORY)
        d.text((x + 32, y + 38), line, font=light, fill=IVORY + (190,))
        y += 92

    OUTPUT.parent.mkdir(exist_ok=True)
    canvas.convert("RGB").save(OUTPUT)
    print("wrote", OUTPUT.relative_to(ROOT))


if __name__ == "__main__":
    main()
