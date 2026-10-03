#include "DiceScreen.h"

#include <string.h>

namespace {

constexpr uint16_t kIvory = rgb565(243, 234, 210);
constexpr uint16_t kInk = rgb565(43, 38, 32);
constexpr uint16_t kShade = rgb565(8, 22, 15); // behind the toast
constexpr int kLevels = 16;
constexpr int kPillHeight = 24;
constexpr int kPillTop = DiceScreen::kHeight - 36;

// The result fades in after the landing: 150 ms later, over 350 ms.
uint8_t pillLevel(uint32_t ageMs) {
    const float t = clamp01((static_cast<float>(ageMs) - 150) / 350);
    return static_cast<uint8_t>(smoothstep(0, 1, t) * kLevels + 0.5f);
}

// The toast fades in for 150 ms and out over its last 150 ms.
uint8_t toastLevel(uint32_t ageMs) {
    const float e = static_cast<float>(ageMs) / 1000;
    float a = e < 0.15f ? e / 0.15f : (e > 1.05f ? (1.2f - e) / 0.15f : 1);
    return static_cast<uint8_t>(clamp01(a) * kLevels + 0.5f);
}

uint8_t scale(int level, float opacity) {
    return static_cast<uint8_t>(opacity * 255 * level / kLevels + 0.5f);
}

} // namespace

void DiceScreen::begin(const Buffers &buffers, const UiAssets *assets) {
    buf_ = buffers;
    ui_ = assets;
    raster_.begin();
    DiceRaster::paintBackground(buf_.background);
    valid_ = false;
}

DiceScreen::UiState DiceScreen::uiState(const DiceView &v) {
    UiState s{};
    s.screen = v.screen;
    s.mode = v.mode;
    s.hint = v.showHint;
    s.result = v.showResult;
    s.count = v.count;
    if (v.showResult) {
        s.values[0] = v.result[0];
        s.values[1] = v.count == 2 ? v.result[1] : 0;
        s.pillLevel = pillLevel(v.resultAgeMs);
    }
    s.toastLevel = v.toast ? toastLevel(v.toastAgeMs) : 0;
    return s;
}

bool DiceScreen::sameUi(const UiState &a, const UiState &b) {
    return a.screen == b.screen && a.mode == b.mode && a.hint == b.hint &&
           a.result == b.result && a.values[0] == b.values[0] && a.values[1] == b.values[1] &&
           a.count == b.count && a.pillLevel == b.pillLevel && a.toastLevel == b.toastLevel;
}

Rect DiceScreen::diceBounds(const DiceView &v) const {
    Rect r{0, 0, 0, 0};
    for (int i = 0; i < v.count; ++i) {
        r = unite(r, raster_.bounds(v.dice[i]));
    }
    return r;
}

void DiceScreen::drawPill(const Canvas &c, const UiState &s) {
    // "5", or "3  +  4  =  7".
    int glyphs[12]; // at most "6  +  6  =  12"
    int n = 0;
    auto number = [&](int value) {
        if (value >= 10) {
            glyphs[n++] = value / 10;
        }
        glyphs[n++] = value % 10;
    };
    const int gap = -1; // two spaces
    if (s.count == 1) {
        number(s.values[0]);
    } else {
        number(s.values[0]);
        glyphs[n++] = gap;
        glyphs[n++] = 10; // +
        glyphs[n++] = gap;
        number(s.values[1]);
        glyphs[n++] = gap;
        glyphs[n++] = 11; // =
        glyphs[n++] = gap;
        number(s.values[0] + s.values[1]);
    }
    const int spaceWidth = 2 * ui_->pillSpace;
    int textWidth = 0;
    for (int i = 0; i < n; ++i) {
        textWidth += glyphs[i] == gap ? spaceWidth : ui_->pillGlyph[glyphs[i]].width;
    }

    const float a = static_cast<float>(s.pillLevel) / kLevels;
    const float width = textWidth + 28 > 40 ? static_cast<float>(textWidth + 28) : 40;
    const float top = kPillTop + (1 - a) * 6;
    fillCapsule(c, (kWidth - width) * 0.5f, top, width, kPillHeight, kIvory,
                scale(s.pillLevel, 0.93f));
    // The digits' middle on the pill's middle.
    const int baseline =
        static_cast<int>(top + kPillHeight * 0.5f + ui_->pillDigitHeight * 0.5f + 0.5f);
    int x = (kWidth - textWidth) / 2;
    for (int i = 0; i < n; ++i) {
        if (glyphs[i] == gap) {
            x += spaceWidth;
            continue;
        }
        const AlphaMask &g = ui_->pillGlyph[glyphs[i]];
        blendMask(c, g, x, baseline - g.baseline, kInk, scale(s.pillLevel, 1));
        x += g.width;
    }
}

void DiceScreen::drawUi(const DiceView &v, const UiState &s, Rect clip) {
    const Canvas c{buf_.frame, kWidth, kHeight, clip.x0, clip.y0, clip.x1, clip.y1};
    const UiAssets &ui = *ui_;
    if (v.screen == Screen::Splash) {
        blendMask(c, ui.title, centered(ui.title), 40 - ui.title.baseline, kIvory, 255);
        fillRect(c, kWidth / 2 - 14, 50, 28, 1, kIvory, 255);
        blendMask(c, ui.pressOrShake, centered(ui.pressOrShake),
                  228 - ui.pressOrShake.baseline, kIvory, 153);
        return;
    }

    for (int i = 0; i < 2; ++i) {
        blendMask(c, i < v.mode ? ui.modeOn : ui.modeOff, 8 + i * 12, 8, kIvory,
                  i < v.mode ? 255 : 115);
    }
    if (s.result && s.pillLevel > 0) {
        drawPill(c, s);
    }
    if (s.hint) {
        blendMask(c, ui.pressOrShake, centered(ui.pressOrShake),
                  kHeight - 18 - ui.pressOrShake.baseline, kIvory, 153);
    }
    if (s.toastLevel > 0) {
        tint(c, 0, kHeight, kShade, scale(s.toastLevel, 0.6f));
        const uint8_t a = scale(s.toastLevel, 1);
        const AlphaMask &digit = ui.toastDigit[v.mode == 1 ? 0 : 1];
        blendMask(c, digit, centered(digit), 138 - digit.baseline, kIvory, a);
        fillRect(c, kWidth / 2 - 14, 148, 28, 1, kIvory, a);
        const AlphaMask &words = v.mode == 1 ? ui.oneDie : ui.twoDice;
        blendMask(c, words, centered(words), 166 - words.baseline, kIvory, a);
    }
}

Rect DiceScreen::compose(const DiceView &v) {
    const Rect full{0, 0, kWidth, kHeight};
    if (v.screen == Screen::Asleep) {
        valid_ = false;
        return {0, 0, 0, 0};
    }
    const UiState s = uiState(v);
    const bool fresh = !valid_ || v.screen != lastScreen_;

    Rect push{0, 0, 0, 0};
    if (v.moving) {
        const Rect dice = diceBounds(v);
        const bool whole = fresh || !lastMoving_ || !sameUi(s, lastUi_);
        const Rect region = whole ? full : unite(dice, lastDice_);
        raster_.render(buf_.frame, buf_.background, v.dice, v.count, region);
        drawUi(v, s, region);
        push = region;
        lastDice_ = dice;
    } else if (fresh || lastMoving_ || v.sceneVersion != lastVersion_) {
        raster_.render(buf_.frame, buf_.background, v.dice, 0, full); // the felt
        raster_.renderSmooth(buf_.frame, buf_.background, v.dice, v.count, diceBounds(v),
                             buf_.tile);
        memcpy(buf_.sceneCopy, buf_.frame, sizeof(uint16_t) * kWidth * kHeight);
        drawUi(v, s, full);
        push = full;
    } else if (!sameUi(s, lastUi_)) {
        // Only the text changed. When only the result moved, the band under
        // it is enough.
        UiState pillOnly = lastUi_;
        pillOnly.pillLevel = s.pillLevel;
        const Rect band{0, kPillTop - 2, kWidth, kHeight};
        const Rect r = sameUi(s, pillOnly) ? band : full;
        for (int y = r.y0; y < r.y1; ++y) {
            memcpy(buf_.frame + y * kWidth, buf_.sceneCopy + y * kWidth,
                   sizeof(uint16_t) * kWidth);
        }
        drawUi(v, s, r);
        push = r;
    }

    valid_ = true;
    lastMoving_ = v.moving;
    lastScreen_ = v.screen;
    lastVersion_ = v.sceneVersion;
    lastUi_ = s;
    return push;
}
