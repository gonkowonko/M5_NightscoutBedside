#include "display.h"

#include <M5Unified.h>
#include <SD.h>

#include <algorithm>

namespace {

// ---- Layout (320x240 landscape, buttons at the bottom) ----------------------
constexpr int kW = 320;
constexpr int kH = 240;
constexpr int kPadX = 14;
constexpr int kPadTop = 10;
constexpr int kPadBottom = 12;
constexpr int kContentW = kW - 2 * kPadX;  // 292: every region is this wide

constexpr int kTopY = kPadTop;
constexpr int kTopH = 18;
constexpr int kStripH = 62;
constexpr int kStripY = kH - kPadBottom - kStripH;  // 166
constexpr int kDividerY = kStripY - 1;              // 165
constexpr int kClockY = kTopY + kTopH + 8;          // 36
constexpr int kClockH = kDividerY - 1 - kClockY;    // 128

constexpr int kBandH = 32;  // band sprite: 292 x 32 x 16 bit = 18.7 KB

// Bottom strip, relative to the strip's top.
constexpr int kBgCenterY = kStripH / 2;
constexpr int kDeltaCenterY = 22;
constexpr int kAgeCenterY = 45;
constexpr int kArrowGap = 12;
constexpr int kArrowBox = 34;
constexpr int kDoubleArrowGap = 20;  // DoubleUp/DoubleDown: second arrow offset

// ---- Colours (RGB888) -------------------------------------------------------
constexpr uint32_t kBg = 0x000000u;
constexpr uint32_t kClockColor = 0xF3F4F6u;
constexpr uint32_t kDim = 0x9CA3AFu;
constexpr uint32_t kDivider = 0x262626u;  // neutral grey
constexpr uint32_t kGrey = 0x6B7280u;
constexpr uint32_t kRed = 0xF87171u;
constexpr uint32_t kAmber = 0xFBBF24u;
constexpr uint32_t kGreen = 0x4ADE80u;

uint32_t rangeColor(BgRange r) {
  switch (r) {
    case BgRange::Urgent: return kRed;
    case BgRange::Warning: return kAmber;
    default: return kGreen;
  }
}

// ---- Fonts ------------------------------------------------------------------

// Lets M5GFX stream VLW glyphs straight from an SD card file, so the large
// clock font never has to fit in RAM.
struct FileData : public lgfx::DataWrapper {
  fs::File file;
  int read(uint8_t* buf, uint32_t len) override { return file.read(buf, len); }
  void skip(int32_t offset) override { file.seek(offset, SeekCur); }
  bool seek(uint32_t offset) override { return file.seek(offset); }
  void close() override {
    if (file) file.close();
  }
  int32_t tell() override { return file.position(); }
};

struct Face {
  const char* path;
  const lgfx::IFont* fallback;  // built-in font if the .vlw is missing
  float fallbackScale;
  lgfx::VLWfont vlw;
  FileData data;
  bool loaded = false;
  int digitH = 0;  // height of "0" above the baseline, for vertical centring

  Face(const char* p, const lgfx::IFont* f, float s) : path(p), fallback(f), fallbackScale(s) {}
};

Face gClockFace("/fonts/clock.vlw", &fonts::Font8, 1.0f);  // scale computed in begin
Face gBgFace("/fonts/bg.vlw", &fonts::Font6, 1.0f);
Face gDeltaFace("/fonts/delta.vlw", &fonts::FreeSansBold12pt7b, 1.0f);
Face gTextFace("/fonts/text15.vlw", &fonts::Font2, 1.0f);
Face gSmallFace("/fonts/text14.vlw", &fonts::Font2, 1.0f);
Face* const gFaces[] = {&gClockFace, &gBgFace, &gDeltaFace, &gTextFace, &gSmallFace};

uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

// Reads the "0" glyph's top offset from the VLW metrics table.
int vlwDigitHeight(FileData& d, int count) {
  uint8_t m[28];
  for (int i = 0; i < count; i++) {
    d.seek(24 + i * 28);
    if (d.read(m, sizeof(m)) != sizeof(m)) break;
    if (be32(m) == '0') return (int32_t)be32(m + 16);
  }
  return 0;
}

void loadFace(Face& f) {
  f.data.file = SD.open(f.path, FILE_READ);
  if (!f.data.file) {
    Serial.printf("display: %s missing, using built-in font\n", f.path);
    return;
  }
  if (!f.vlw.loadFont(&f.data)) {
    Serial.printf("display: %s is not a valid VLW font\n", f.path);
    f.data.close();
    return;
  }
  f.digitH = vlwDigitHeight(f.data, f.vlw.gCount);
  f.loaded = true;
}

LGFX_Sprite gBand(&M5.Display);

enum class Align { Left, Center, Right };

void useFace(LGFX_Sprite& s, const Face& f) {
  s.setFont(f.loaded ? static_cast<const lgfx::IFont*>(&f.vlw) : f.fallback);
  s.setTextSize(f.loaded ? 1.0f : f.fallbackScale);
}

// Built-in fonts are ASCII only: drop the degree sign, use a plain minus.
const char* asciiFor(const Face& f, const char* text, char* buf, size_t len) {
  if (f.loaded) return text;
  size_t o = 0;
  for (const char* p = text; *p && o + 1 < len; p++) {
    if ((uint8_t)p[0] == 0xC2 && (uint8_t)p[1] == 0xB0) { p++; continue; }  // °
    if ((uint8_t)p[0] == 0xE2 && (uint8_t)p[1] == 0x88 && (uint8_t)p[2] == 0x92) {  // −
      buf[o++] = '-';
      p += 2;
      continue;
    }
    buf[o++] = *p;
  }
  buf[o] = 0;
  return buf;
}

// Draws text vertically centred on the digits at centerY. Returns its width.
int drawText(LGFX_Sprite& s, const Face& f, const char* text, int x, int centerY, Align align,
             uint32_t color) {
  char buf[32];
  text = asciiFor(f, text, buf, sizeof(buf));
  useFace(s, f);
  s.setTextColor(color);  // no background: anti-aliasing blends with the sprite
  int y = centerY;
  if (f.loaded) {
    s.setTextDatum(align == Align::Left    ? textdatum_t::baseline_left
                   : align == Align::Right ? textdatum_t::baseline_right
                                           : textdatum_t::baseline_center);
    y += f.digitH / 2;
  } else {
    s.setTextDatum(align == Align::Left    ? textdatum_t::middle_left
                   : align == Align::Right ? textdatum_t::middle_right
                                           : textdatum_t::middle_center);
  }
  s.drawString(text, x, y);
  return s.textWidth(text);
}

// Renders one full-width region through the band sprite, top to bottom.
// `draw` receives the sprite and the y offset of the band within the region.
template <typename F>
void renderRegion(int y, int h, F&& draw) {
  for (int by = 0; by < h; by += kBandH) {
    int bh = std::min(kBandH, h - by);
    gBand.fillScreen(kBg);
    draw(gBand, -by);
    M5.Display.pushImage(kPadX, y + by, kContentW, bh,
                         static_cast<const lgfx::swap565_t*>(gBand.getBuffer()));
  }
}

// ---- Trend arrow ------------------------------------------------------------

float trendAngle(Trend t) {  // degrees, 0 = right, positive = clockwise (down)
  switch (t) {
    case Trend::DoubleUp:
    case Trend::SingleUp: return -90;
    case Trend::FortyFiveUp: return -45;
    case Trend::Flat: return 0;
    case Trend::FortyFiveDown: return 45;
    default: return 90;  // SingleDown, DoubleDown
  }
}

// A line with a chevron head, centred on (cx, cy).
void drawArrow(LGFX_Sprite& s, float cx, float cy, float angleDeg, uint32_t color) {
  constexpr float kLen = kArrowBox - 6;
  constexpr float kHead = kArrowBox * 0.4f;
  constexpr float kStroke = 2.5f;  // radius, i.e. 5 px wide
  float a = angleDeg * DEG_TO_RAD;
  float dx = cosf(a), dy = sinf(a);
  float hx = cx + dx * kLen / 2, hy = cy + dy * kLen / 2;
  float tx = cx - dx * kLen / 2, ty = cy - dy * kLen / 2;
  s.drawWideLine(tx, ty, hx, hy, kStroke, color);
  for (float side : {-1.0f, 1.0f}) {
    float b = a + PI + side * PI / 4;
    s.drawWideLine(hx, hy, hx + cosf(b) * kHead, hy + sinf(b) * kHead, kStroke, color);
  }
}

void drawTrend(LGFX_Sprite& s, Trend t, int x, int cy, uint32_t color) {
  if (t == Trend::None) return;
  float angle = trendAngle(t);
  if (t == Trend::DoubleUp || t == Trend::DoubleDown) {
    drawArrow(s, x + kArrowBox / 2, cy, angle, color);
    drawArrow(s, x + kArrowBox / 2 + kDoubleArrowGap, cy, angle, color);
  } else {
    drawArrow(s, x + kArrowBox / 2.0f, cy, angle, color);
  }
}

// ---- Regions ----------------------------------------------------------------

ScreenModel gShown;  // what is currently on the screen
bool gFullRedraw = true;
bool gDebugShown = false;
bool gProgressShown = false;

void drawTopLine(const ScreenModel& m) {
  renderRegion(kTopY, kTopH, [&](LGFX_Sprite& s, int oy) {
    int cy = oy + kTopH / 2;
    drawText(s, gTextFace, m.date, 0, cy, Align::Left, kDim);
    drawText(s, gTextFace, m.temp, kContentW, cy, Align::Right, kDim);
  });
}

void drawClock(const ScreenModel& m) {
  renderRegion(kClockY, kClockH, [&](LGFX_Sprite& s, int oy) {
    drawText(s, gClockFace, m.time, kContentW / 2, oy + kClockH / 2, Align::Center, kClockColor);
  });
}

void drawStrip(const ScreenModel& m) {
  renderRegion(kStripY, kStripH, [&](LGFX_Sprite& s, int oy) {
    int cy = oy + kBgCenterY;
    if (!m.hasBg) {
      drawText(s, gBgFace, "---", 0, cy, Align::Left, kGrey);
      return;
    }
    uint32_t color = m.stale ? kGrey : rangeColor(m.range);
    int w = drawText(s, gBgFace, m.bg, 0, cy, Align::Left, color);
    if (m.stale) s.fillRect(0, cy - 2, w, 4, kGrey);  // strike-through
    drawTrend(s, m.trend, w + kArrowGap, cy, color);

    if (m.delta[0]) {
      // Typographic minus for negative deltas (VLW font has U+2212).
      char delta[12];
      if (m.delta[0] == '-')
        snprintf(delta, sizeof(delta), "\xE2\x88\x92%s", m.delta + 1);
      else
        strlcpy(delta, m.delta, sizeof(delta));
      drawText(s, gDeltaFace, delta, kContentW, oy + kDeltaCenterY, Align::Right, color);
    }
    drawText(s, gSmallFace, m.age, kContentW, oy + kAgeCenterY, Align::Right,
             m.stale ? kRed : kDim);
  });
}

bool stripChanged(const ScreenModel& a, const ScreenModel& b) {
  return a.hasBg != b.hasBg || strcmp(a.bg, b.bg) || strcmp(a.delta, b.delta) ||
         a.trend != b.trend || a.range != b.range || a.stale != b.stale || strcmp(a.age, b.age);
}

// ---- Debug page -------------------------------------------------------------

constexpr int kDebugMaxLines = 14;
char gDebugLines[kDebugMaxLines][48];

}  // namespace

void displayBegin() {
  M5.Display.setRotation(1);
  M5.Display.fillScreen(kBg);

  for (Face* f : gFaces) loadFace(*f);

  gBand.setColorDepth(16);
  if (!gBand.createSprite(kContentW, kBandH)) Serial.println("display: band sprite alloc failed");

  if (!gClockFace.loaded) {
    // Scale the built-in digit font up to fill the clock width.
    gBand.setFont(gClockFace.fallback);
    gBand.setTextSize(1);
    float sx = (float)kContentW / gBand.textWidth("00:00");
    float sy = (float)kClockH / gBand.fontHeight();
    gClockFace.fallbackScale = std::min(sx, sy);
  }
}

int displayFontsLoaded() {
  int n = 0;
  for (Face* f : gFaces) n += f->loaded;
  return n;
}

void displayMessage(const char* title, const char* detail) {
  auto& d = M5.Display;
  d.fillScreen(kBg);
  d.setTextDatum(textdatum_t::middle_center);
  d.setFont(&fonts::FreeSansBold12pt7b);
  d.setTextSize(1);
  d.setTextColor(kClockColor, kBg);
  d.drawString(title, kW / 2, kH / 2 - 16);
  d.setFont(&fonts::Font2);
  d.setTextColor(kDim, kBg);
  d.drawString(detail, kW / 2, kH / 2 + 16);
  gFullRedraw = true;
}

void displayProgress(const char* title, const char* detail, int percent) {
  auto& d = M5.Display;
  static int lastPercent = -2;
  if (!gProgressShown || gFullRedraw || percent < lastPercent) {
    d.fillScreen(kBg);
    gProgressShown = true;
    gFullRedraw = false;
    gDebugShown = true;  // the main screen repaints fully afterwards
    lastPercent = -2;
  }
  d.setTextDatum(textdatum_t::middle_center);
  d.setTextPadding(kContentW);
  d.setFont(&fonts::FreeSansBold12pt7b);
  d.setTextSize(1);
  d.setTextColor(kClockColor, kBg);
  d.drawString(title, kW / 2, kH / 2 - 36);
  d.setFont(&fonts::Font2);
  d.setTextColor(kDim, kBg);
  d.drawString(detail, kW / 2, kH / 2 + 34);
  d.setTextPadding(0);
  if (percent >= 0 && percent != lastPercent) {
    constexpr int kBarH = 8;
    int y = kH / 2 - kBarH / 2;
    d.fillRoundRect(kPadX, y, kContentW, kBarH, kBarH / 2, kDivider);
    int w = kContentW * std::min(percent, 100) / 100;
    if (w > 0) d.fillRoundRect(kPadX, y, std::max(w, kBarH), kBarH, kBarH / 2, kGreen);
  }
  lastPercent = percent;
}

void displayInvalidate() { gFullRedraw = true; }

void displayUpdate(const ScreenModel& m) {
  if (!gBand.getBuffer()) return;
  bool full = gFullRedraw || gDebugShown;
  if (full) {
    M5.Display.fillScreen(kBg);
    M5.Display.drawFastHLine(kPadX, kDividerY, kContentW, kDivider);
  }
  if (full || strcmp(m.date, gShown.date) || strcmp(m.temp, gShown.temp)) drawTopLine(m);
  if (full || strcmp(m.time, gShown.time)) drawClock(m);
  if (full || stripChanged(m, gShown)) drawStrip(m);
  gShown = m;
  gFullRedraw = false;
  gDebugShown = false;
  gProgressShown = false;
}

void displayDebug(const char* const* lines, int count) {
  auto& d = M5.Display;
  count = std::min(count, kDebugMaxLines);
  bool full = !gDebugShown || gFullRedraw;
  if (full) {
    d.fillScreen(kBg);
    memset(gDebugLines, 0, sizeof(gDebugLines));
  }
  d.setFont(&fonts::Font2);
  d.setTextSize(1);
  d.setTextDatum(textdatum_t::top_left);
  d.setTextPadding(kW - 2 * 8);  // overwrite the previous text on the line
  for (int i = 0; i < count; i++) {
    if (!full && strcmp(lines[i], gDebugLines[i]) == 0) continue;
    strlcpy(gDebugLines[i], lines[i], sizeof(gDebugLines[i]));
    d.setTextColor(i == 0 ? kClockColor : kDim, kBg);
    d.drawString(lines[i], 8, 6 + i * 16);
  }
  d.setTextPadding(0);
  gDebugShown = true;
  gFullRedraw = false;
  gProgressShown = false;
}
