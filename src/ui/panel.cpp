// Common Sense EQ - panel.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/panel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "dsp/curve.hpp"
#include "dsp/inline_curve.hpp"

namespace cse {
namespace ui {

namespace {

constexpr double kTopH = 54.0;
constexpr double kIconY = 58.0;
constexpr double kIconH = 36.0;
constexpr double kGraphX = 64.0;
constexpr double kGraphY = 98.0;
constexpr double kGraphW = 1008.0;
constexpr double kGraphH = 330.0;
constexpr double kBottomY = 430.0;
constexpr double kBottomH = 70.0;
constexpr double kFooterY = 502.0;
constexpr double kFooterH = 58.0;

constexpr double kGraphBottom = kGraphY + kGraphH;
constexpr double kGraphRight = kGraphX + kGraphW;

const Rgb kBgTop{0.071, 0.098, 0.145};
const Rgb kBgBottom{0.043, 0.063, 0.098};
const Rgb kPanel{0.090, 0.125, 0.180};
const Rgb kPanelHi{0.118, 0.157, 0.220};
const Rgb kLine{0.157, 0.204, 0.282};
const Rgb kGrid{0.145, 0.192, 0.271};
const Rgb kText{0.902, 0.925, 0.957};
const Rgb kMuted{0.545, 0.596, 0.671};
const Rgb kDim{0.361, 0.404, 0.478};
const Rgb kAccent{0.498, 0.827, 0.659};
const Rgb kAmber{0.878, 0.659, 0.353};

double fontScale = 1.0;

void setColor(cairo_t* cr, const Rgb& c, double a = 1.0) {
  cairo_set_source_rgba(cr, c.r, c.g, c.b, a);
}

void roundedRect(cairo_t* cr, double x, double y, double w, double h,
                 double r) {
  const double radius = std::min(r, std::min(w, h) * 0.5);
  cairo_new_path(cr);
  cairo_arc(cr, x + w - radius, y + radius, radius, -M_PI * 0.5, 0.0);
  cairo_arc(cr, x + w - radius, y + h - radius, radius, 0.0, M_PI * 0.5);
  cairo_arc(cr, x + radius, y + h - radius, radius, M_PI * 0.5, M_PI);
  cairo_arc(cr, x + radius, y + radius, radius, M_PI, M_PI * 1.5);
  cairo_close_path(cr);
}

enum TextAlign { kLeft = 0, kCenter = 1, kRight = 2 };

void textAt(cairo_t* cr, const char* text, double x, double y, double size,
            bool bold, const Rgb& color, TextAlign align = kLeft,
            double alpha = 1.0) {
  cairo_select_font_face(cr, "DejaVu Sans", CAIRO_FONT_SLANT_NORMAL,
                         bold ? CAIRO_FONT_WEIGHT_BOLD
                              : CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, size * fontScale);
  cairo_text_extents_t ext{};
  cairo_text_extents(cr, text, &ext);
  double px = x;
  if (align == kCenter) {
    px -= ext.width * 0.5 + ext.x_bearing;
  } else if (align == kRight) {
    px -= ext.width + ext.x_bearing;
  }
  setColor(cr, color, alpha);
  cairo_move_to(cr, px, y);
  cairo_show_text(cr, text);
  // show_text leaves the move_to subpath behind; a following arc would
  // connect to it with a stray line.
  cairo_new_path(cr);
}

void chip(cairo_t* cr, double x, double y, double w, double h, bool on,
          bool hover = false) {
  roundedRect(cr, x, y, w, h, 5.0);
  setColor(cr, on ? kPanelHi : kPanel, hover ? 1.0 : 0.92);
  cairo_fill_preserve(cr);
  setColor(cr, on ? kAccent : kLine, on ? 0.7 : 1.0);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);
}

std::string fmtFreq(float hz) {
  char buf[32];
  if (hz >= 1000.0f) {
    std::snprintf(buf, sizeof(buf), "%.0f Hz", hz);
  } else if (hz >= 100.0f) {
    std::snprintf(buf, sizeof(buf), "%.0f Hz", hz);
  } else {
    std::snprintf(buf, sizeof(buf), "%.1f Hz", hz);
  }
  return buf;
}

std::string fmtDb(float db) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%+.1f dB", db);
  return buf;
}

std::string fmtQ(float q) {
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%.2f", q);
  return buf;
}

std::string fmtMs(float ms) {
  char buf[24];
  if (ms >= 1000.0f) {
    std::snprintf(buf, sizeof(buf), "%.2f s", ms * 0.001f);
  } else if (ms >= 100.0f) {
    std::snprintf(buf, sizeof(buf), "%.0f ms", ms);
  } else {
    std::snprintf(buf, sizeof(buf), "%.1f ms", ms);
  }
  return buf;
}

std::string fmtRatio(float ratio) {
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%.1f:1", ratio);
  return buf;
}

const char* analysisName(int mode) { return mode == 0 ? "PRE" : "POST"; }

const char* processingName(int mode) {
  switch (mode) {
    case 1: return "MONO";
    case 2: return "LEFT";
    case 3: return "RIGHT";
    default: return "STEREO";
  }
}

double freqToX(double freq) {
  const double t = std::log(std::max(freq, 20.0) / 20.0) / std::log(1000.0);
  return kGraphX + clamp(static_cast<float>(t), 0.0f, 1.0f) * kGraphW;
}

double xToFreq(double x) {
  const double t = clamp(static_cast<float>((x - kGraphX) / kGraphW), 0.0f,
                         1.0f);
  return 20.0 * std::pow(1000.0, t);
}

double dbToY(double db) {
  const double t = clamp(static_cast<float>((30.0 - db) / 60.0), 0.0f, 1.0f);
  return kGraphY + t * kGraphH;
}

double yToDb(double y) {
  const double t = clamp(static_cast<float>((y - kGraphY) / kGraphH), 0.0f,
                         1.0f);
  return 30.0 - t * 60.0;
}

struct GridFreq {
  double f;
  const char* label;
  bool major;
};

const GridFreq kGridFreqs[] = {
    {20, "20", true},     {30, "30", false},    {40, "40", false},
    {50, "50", true},     {60, "60", false},    {80, "80", false},
    {100, "100", true},   {200, "200", true},   {300, "300", false},
    {400, "400", false},  {500, "500", true},   {600, "600", false},
    {800, "800", false},  {1000, "1k", true},   {2000, "2k", true},
    {3000, "3k", false},  {4000, "4k", false},  {5000, "5k", true},
    {6000, "6k", false},  {8000, "8k", false},  {10000, "10k", true},
    {20000, "20k", true},
};

void bandCurvePath(cairo_t* cr, const UiState& s, int band, bool live,
                   double step, bool closeToZero) {
  const float freq = s.values[bandParam(band, kSlotFreq)];
  const float gain = s.values[bandParam(band, kSlotGain)];
  float q = s.values[bandParam(band, kSlotQ)];
  const int slope =
      static_cast<int>(std::lround(s.values[bandParam(band, kSlotSlope)]));
  const int dyn =
      live ? static_cast<int>(std::lround(s.values[bandParam(band, kSlotDynOn)]))
           : 0;
  const bool qc = s.values[PORT_Q_COUPLE] > 0.5f;
  if (qc) {
    q = clamp(q * (1.0f + 0.10f * std::fabs(gain)), 0.1f, 25.0f);
  }
  const float d = dyn ? s.dyn[band] : 0.0f;
  bool first = true;
  for (double x = kGraphX; x <= kGraphRight; x += step) {
    const double f = xToFreq(x);
    const double db = bandResponseDb(shapeForBand(band), freq,
                                     static_cast<float>(gain + d), q, slope,
                                     static_cast<float>(f), 48000.0f);
    const double y = dbToY(db);
    if (first) {
      cairo_move_to(cr, x, y);
      first = false;
    } else {
      cairo_line_to(cr, x, y);
    }
  }
  if (closeToZero) {
    cairo_line_to(cr, kGraphRight, dbToY(0.0));
    cairo_line_to(cr, kGraphX, dbToY(0.0));
    cairo_close_path(cr);
  }
}

void totalCurvePath(cairo_t* cr, const UiState& s, bool live, bool fill) {
  Params p;
  p.enabled = true;
  for (int b = 0; b < 8; ++b) {
    p.bands[b].on = s.values[bandParam(b, kSlotOn)] > 0.5f;
    p.bands[b].freq = s.values[bandParam(b, kSlotFreq)];
    p.bands[b].gainDb = s.values[bandParam(b, kSlotGain)];
    p.bands[b].q = s.values[bandParam(b, kSlotQ)];
    p.bands[b].slope =
        static_cast<int>(std::lround(s.values[bandParam(b, kSlotSlope)]));
    p.bands[b].dynOn = s.values[bandParam(b, kSlotDynOn)] > 0.5f;
  }
  p.qCouple = s.values[PORT_Q_COUPLE] > 0.5f;
  const float* dyn = live ? s.dyn : nullptr;
  bool first = true;
  for (double x = kGraphX; x <= kGraphRight; x += 2.0) {
    const double f = xToFreq(x);
    const double db = totalResponseDb(p, dyn, static_cast<float>(f), 48000.0f);
    const double y = dbToY(db);
    if (first) {
      cairo_move_to(cr, x, y);
      first = false;
    } else {
      cairo_line_to(cr, x, y);
    }
  }
  if (fill) {
    cairo_line_to(cr, kGraphRight, dbToY(0.0));
    cairo_line_to(cr, kGraphX, dbToY(0.0));
    cairo_close_path(cr);
  }
}

void dynamicBandName(int band, char* out, size_t size) {
  static const char* names[8] = {"LOW CUT", "LOW SHELF", "BELL 1", "BELL 2",
                                 "BELL 3", "BELL 4", "HIGH SHELF", "HIGH CUT"};
  std::snprintf(out, size, "%s", names[clamp(band, 0, 7)]);
}

struct IconHit {
  int band = -1;
  bool badge = false;
};

IconHit iconAt(double x, double y) {
  if (y < kIconY || y > kIconY + kIconH) {
    return {};
  }
  for (int i = 0; i < 8; ++i) {
    const double cx = 90.0 + i * (940.0 / 7.0);
    if (x >= cx - 26 && x <= cx + 26) {
      const bool badge = bandHasGain(i) && x >= cx + 8 && x <= cx + 22 &&
                         y >= kIconY + 19 && y <= kIconY + 33;
      return {i, badge};
    }
  }
  return {};
}

struct CellHit {
  int band = -1;
  int row = -1;
};

CellHit cellAt(double x, double y) {
  if (y < kBottomY || y > kBottomY + kBottomH) {
    return {};
  }
  if (x < kGraphX || x > kGraphRight) {
    return {};
  }
  const int band = static_cast<int>((x - kGraphX) / (kGraphW / 8.0));
  const int row = static_cast<int>((y - kBottomY) / 23.0);
  return {clamp(band, 0, 7), clamp(row, 0, 2)};
}

constexpr double kDynX = 688.0;
constexpr double kDynY = 94.0;
constexpr double kDynW = 384.0;
constexpr double kDynH = 168.0;
constexpr double kDynRowGap = 34.0;
constexpr double kDynRowTop = 42.0;

int dynRowAt(double x, double y) {
  if (!(x >= kDynX && x <= kDynX + kDynW && y >= kDynY &&
        y <= kDynY + kDynH)) {
    return -1;
  }
  if (y < kDynY + 34.0) {
    return -2; // header
  }
  int row = static_cast<int>((y - (kDynY + kDynRowTop)) / kDynRowGap);
  row = clamp(row, 0, 2);
  if (x >= kDynX + 196.0) {
    if (row == 0) return 3; // Attack
    if (row == 1) return 4; // Release
    return 5;               // GR meter (not editable)
  }
  return row;
}

Rect dynPanelRect() { return {kDynX, kDynY, kDynW, kDynH}; }
Rect dynTrackRect(int row) {
  if (row < 3) {
    return {kDynX + 12.0, kDynY + kDynRowTop + row * kDynRowGap + 16.0,
            162.0, 8.0};
  }
  return {kDynX + 204.0,
          kDynY + kDynRowTop + (row - 3) * kDynRowGap + 16.0, 162.0, 8.0};
}
Rect dynCloseRect() { return {kDynX + 346.0, kDynY + 8.0, 22.0, 22.0}; }
Rect dynOnRect() { return {kDynX + 282.0, kDynY + 8.0, 48.0, 22.0}; }
Rect dynModeRect() { return {kDynX + 200.0, kDynY + 8.0, 74.0, 22.0}; }

double normalized(float value, const ParamInfo& info) {
  if (info.logarithmic) {
    const double lo = std::log(std::max(info.min, 0.0001f));
    const double hi = std::log(std::max(info.max, 0.0002f));
    return (std::log(std::max(value, 0.0001f)) - lo) / (hi - lo);
  }
  return (value - info.min) / (info.max - info.min);
}

float denormalized(double t, const ParamInfo& info) {
  t = std::min(std::max(t, 0.0), 1.0);
  if (info.logarithmic) {
    const double lo = std::log(std::max(info.min, 0.0001f));
    const double hi = std::log(std::max(info.max, 0.0002f));
    return static_cast<float>(std::exp(lo + t * (hi - lo)));
  }
  return static_cast<float>(info.min + t * (info.max - info.min));
}

} // namespace

Rgb bandRgb(int band) {
  static const Rgb colors[8] = {
      {0.847, 0.788, 0.290}, {0.878, 0.541, 0.235},
      {0.898, 0.812, 0.416}, {0.333, 0.753, 0.478},
      {0.310, 0.725, 0.659}, {0.416, 0.663, 0.878},
      {0.647, 0.541, 0.847}, {0.851, 0.541, 0.541}};
  return colors[clamp(band, 0, 7)];
}

const char* bandGlyphName(int band) {
  switch (band) {
    case 0: return "cut";
    case 1: return "shelf";
    case 6: return "shelf";
    case 7: return "cut";
    default: return "bell";
  }
}

void initState(UiState& state) {
  for (int i = 0; i < kParamCount; ++i) {
    state.values[kParams[i].index] = kParams[i].def;
  }
  state.selectedBand = 3;
  state.showDyn = false;
  state.presetIndex = 0;
  state.presetDirty = false;
  state.abSlot = 0;
  state.abInit[0] = state.abInit[1] = false;
  for (uint32_t i = 0; i < kSpectrumCount; ++i) {
    state.spectrum[i] = -100.0f;
  }
  for (uint32_t i = 0; i < kGrCount; ++i) {
    state.dyn[i] = 0.0f;
  }
}

void applyPreset(UiState& state, int index,
                 const std::function<void(uint32_t, float)>& set) {
  if (index < 0 || index >= kPresetCount) {
    return;
  }
  for (int i = 0; i < kParamCount; ++i) {
    const ParamInfo& info = kParams[i];
    if (info.group == 0) {
      continue; // keep global settings while stepping through presets
    }
    if (state.values[info.index] != info.def) {
      state.values[info.index] = info.def;
      if (set) {
        set(info.index, info.def);
      }
    }
  }
  const PresetSpec& preset = kPresets[index];
  for (int i = 0; i < preset.count; ++i) {
    const PresetValue& value = preset.values[i];
    state.values[value.port] = value.value;
    if (set) {
      set(value.port, value.value);
    }
  }
  state.presetIndex = index;
  state.presetDirty = false;
}

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------
float Panel::value(const UiState& state, uint32_t port) const {
  return state.values[port];
}

void Panel::setValue(UiState& state, uint32_t port, float v,
                     const Setter& set) const {
  const ParamInfo* info = infoFor(port);
  if (!info) {
    return;
  }
  v = clamp(v, std::min(info->min, info->max), std::max(info->min, info->max));
  if (info->toggle) {
    v = v > 0.5f ? 1.0f : 0.0f;
  } else if (info->enumeration) {
    v = std::round(v);
  }
  if (std::fabs(state.values[port] - v) < 1.0e-7f) {
    return;
  }
  state.values[port] = v;
  state.presetDirty = true;
  if (set) {
    set(port, v);
  }
}

void Panel::selectBand(UiState& state, int band) {
  state.selectedBand = clamp(band, 0, 7);
}

void Panel::toggleBand(UiState& state, int band, const Setter& set) {
  const float now = state.values[bandParam(band, kSlotOn)];
  setValue(state, bandParam(band, kSlotOn), now > 0.5f ? 0.0f : 1.0f, set);
}

void Panel::toggleDynamics(UiState& state, int band, const Setter& set) {
  if (!bandHasGain(band)) {
    return;
  }
  selectBand(state, band);
  const float now = state.values[bandParam(band, kSlotDynOn)];
  setValue(state, bandParam(band, kSlotDynOn), now > 0.5f ? 0.0f : 1.0f, set);
  state.showDyn = true;
}

void Panel::cyclePreset(UiState& state, int direction, const Setter& set) {
  int index = state.presetIndex;
  if (index < 0) {
    index = direction >= 0 ? 0 : kPresetCount - 1;
  } else {
    index = (index + direction + kPresetCount) % kPresetCount;
  }
  applyPreset(state, index, set);
}

void Panel::cycleAB(UiState& state, const Setter& set) {
  const int other = 1 - state.abSlot;
  for (int i = 0; i < kParamCount; ++i) {
    state.abValues[state.abSlot][kParams[i].index] = state.values[kParams[i].index];
  }
  state.abInit[state.abSlot] = true;
  state.abSlot = other;
  if (!state.abInit[other]) {
    for (int i = 0; i < kParamCount; ++i) {
      state.abValues[other][kParams[i].index] = state.values[kParams[i].index];
    }
    state.abInit[other] = true;
  } else {
    for (int i = 0; i < kParamCount; ++i) {
      const uint32_t port = kParams[i].index;
      setValue(state, port, state.abValues[other][port], set);
    }
  }
  state.presetDirty = true;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
void Panel::draw(cairo_t* cr, UiState& state) {
  fontScale = 1.0;
  cairo_save(cr);
  cairo_new_path(cr);

  cairo_pattern_t* bg = cairo_pattern_create_linear(0, 0, 0, kHeight);
  cairo_pattern_add_color_stop_rgb(bg, 0.0, kBgTop.r, kBgTop.g, kBgTop.b);
  cairo_pattern_add_color_stop_rgb(bg, 1.0, kBgBottom.r, kBgBottom.g, kBgBottom.b);
  cairo_set_source(cr, bg);
  cairo_paint(cr);
  cairo_pattern_destroy(bg);

  drawTopBar(cr, state);
  drawIcons(cr, state);
  drawGraph(cr, state);
  drawBottom(cr, state);
  drawFooter(cr, state);
  if (state.showDyn && bandHasGain(state.selectedBand)) {
    drawDynPanel(cr, state);
  }

  cairo_restore(cr);
}

void Panel::drawBandIcon(cairo_t* cr, int band, double cx, double cy, bool on,
                         bool selected, bool dynOn) {
  const Rgb c = bandRgb(band);
  const double alpha = on ? 1.0 : 0.28;
  cairo_save(cr);
  if (selected) {
    roundedRect(cr, cx - 23, kIconY + 2, 46, 32, 6);
    setColor(cr, c, 0.10);
    cairo_fill(cr);
    setColor(cr, c, 0.35);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
  }
  setColor(cr, c, alpha);
  cairo_set_line_width(cr, 1.8);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  const char* glyph = bandGlyphName(band);
  if (!std::strcmp(glyph, "bell")) {
    cairo_arc(cr, cx, cy, 7.0, 0.0, M_PI * 2.0);
    cairo_stroke(cr);
    cairo_arc(cr, cx, cy, 1.8, 0.0, M_PI * 2.0);
    cairo_fill(cr);
  } else if (!std::strcmp(glyph, "cut")) {
    cairo_arc(cr, cx, cy, 7.0, 0.0, M_PI);
    cairo_stroke(cr);
    cairo_move_to(cr, cx - 7.0, cy);
    cairo_line_to(cr, cx + 7.0, cy);
    cairo_stroke(cr);
    for (int i = -1; i <= 1; ++i) {
      cairo_move_to(cr, cx + i * 3.5, cy + 7.0);
      cairo_line_to(cr, cx + i * 3.5, cy + 9.0);
    }
    cairo_stroke(cr);
  } else {
    const bool low = band == 1;
    cairo_move_to(cr, cx - 9.0, low ? cy + 7.0 : cy - 7.0);
    cairo_curve_to(cr, cx - 2.0, low ? cy + 7.0 : cy - 7.0, cx + 1.0,
                   low ? cy - 4.0 : cy + 4.0, cx + 9.0,
                   low ? cy - 7.0 : cy + 7.0);
    cairo_stroke(cr);
    cairo_arc(cr, cx + 9.0, low ? cy - 7.0 : cy + 7.0, 1.8, 0.0, M_PI * 2.0);
    cairo_fill(cr);
  }
  if (!on) {
    setColor(cr, kDim, 0.8);
    cairo_set_line_width(cr, 1.2);
    cairo_move_to(cr, cx - 9.0, cy + 9.0);
    cairo_line_to(cr, cx + 9.0, cy - 9.0);
    cairo_stroke(cr);
  }
  cairo_restore(cr);

  if (bandHasGain(band)) {
    roundedRect(cr, cx + 9.0, kIconY + 20.0, 14.0, 13.0, 3.0);
    setColor(cr, dynOn ? kAmber : kPanel, dynOn ? 0.95 : 0.85);
    cairo_fill_preserve(cr);
    setColor(cr, dynOn ? kAmber : kLine, 1.0);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
    textAt(cr, "D", cx + 16.0, kIconY + 30.0, 8.0, true,
           dynOn ? Rgb{0.09, 0.11, 0.14} : kMuted, kCenter);
  }
}

void Panel::drawTopBar(cairo_t* cr, UiState& state) {
  cairo_save(cr);
  cairo_rectangle(cr, 0, 0, kWidth, kTopH);
  cairo_clip(cr);
  cairo_pattern_t* bar = cairo_pattern_create_linear(0, 0, 0, kTopH);
  cairo_pattern_add_color_stop_rgb(bar, 0.0, 0.086, 0.114, 0.157);
  cairo_pattern_add_color_stop_rgb(bar, 1.0, 0.059, 0.082, 0.118);
  cairo_set_source(cr, bar);
  cairo_paint(cr);
  cairo_pattern_destroy(bar);
  setColor(cr, kLine, 0.7);
  cairo_set_line_width(cr, 1.0);
  cairo_move_to(cr, 0, kTopH - 0.5);
  cairo_line_to(cr, kWidth, kTopH - 0.5);
  cairo_stroke(cr);

  // Power.
  const bool on = state.values[PORT_ENABLE] > 0.5f;
  cairo_arc(cr, 32.0, 27.0, 14.0, 0.0, M_PI * 2.0);
  setColor(cr, on ? kAccent : kDim, 0.12);
  cairo_fill_preserve(cr);
  setColor(cr, on ? kAccent : kDim, 0.9);
  cairo_set_line_width(cr, 1.2);
  cairo_stroke(cr);
  setColor(cr, on ? kAccent : kMuted, 1.0);
  cairo_set_line_width(cr, 1.6);
  cairo_arc(cr, 32.0, 27.5, 6.2, -M_PI * 0.75, M_PI * 0.75);
  cairo_stroke(cr);
  cairo_move_to(cr, 32.0, 20.0);
  cairo_line_to(cr, 32.0, 26.0);
  cairo_stroke(cr);

  // Preset chip.
  chip(cr, 62.0, 12.0, 320.0, 30.0, false);
  textAt(cr, "<", 74.0, 32.0, 13.0, true, kMuted, kCenter);
  textAt(cr, ">", 370.0, 32.0, 13.0, true, kMuted, kCenter);
  const char* presetName =
      (state.presetIndex >= 0 && state.presetIndex < kPresetCount)
          ? kPresets[state.presetIndex].name
          : "Init";
  textAt(cr, presetName, 92.0, 32.0, 11.5, true,
         state.presetDirty ? kAmber : kText);
  if (state.presetDirty) {
    textAt(cr, "*", 344.0, 31.0, 13.0, true, kAmber);
  }

  // A/B.
  chip(cr, 392.0, 12.0, 46.0, 30.0, true);
  const char label[2] = {static_cast<char>('A' + state.abSlot), '\0'};
  textAt(cr, label, 415.0, 32.0, 12.0, true, kAccent, kCenter);

  chip(cr, 446.0, 12.0, 58.0, 30.0, false);
  textAt(cr, "INIT", 475.0, 31.0, 9.5, true, kMuted, kCenter);

  textAt(cr, "COMMON SENSE EQ", 706.0, 30.0, 10.0, true, kDim, kCenter);

  const bool dynOpen = state.showDyn;
  chip(cr, 900.0, 12.0, 88.0, 30.0, dynOpen);
  textAt(cr, "DYNAMICS", 944.0, 31.0, 9.0, true,
         dynOpen ? kAccent : kMuted, kCenter);

  cairo_restore(cr);
}

void Panel::drawIcons(cairo_t* cr, UiState& state) {
  cairo_save(cr);
  cairo_rectangle(cr, 0, kIconY - 2, kWidth, kIconH + 4);
  cairo_clip(cr);
  for (int i = 0; i < 8; ++i) {
    const double cx = 90.0 + i * (940.0 / 7.0);
    const double cy = kIconY + 14.0;
    drawBandIcon(cr, i, cx, cy,
                 state.values[bandParam(i, kSlotOn)] > 0.5f,
                 state.selectedBand == i,
                 state.values[bandParam(i, kSlotDynOn)] > 0.5f);
  }
  cairo_restore(cr);
}

void Panel::drawNode(cairo_t* cr, int band, double x, double y,
                     bool selected) {
  const Rgb c = bandRgb(band);
  const double r = selected ? 6.0 : 4.5;
  cairo_arc(cr, x, y, r + 2.5, 0.0, M_PI * 2.0);
  setColor(cr, c, 0.16);
  cairo_fill(cr);
  cairo_arc(cr, x, y, r, 0.0, M_PI * 2.0);
  setColor(cr, c, 1.0);
  cairo_fill(cr);
  cairo_arc(cr, x, y, r, 0.0, M_PI * 2.0);
  setColor(cr, selected ? Rgb{1, 1, 1} : Rgb{0.86, 0.89, 0.93},
           selected ? 1.0 : 0.7);
  cairo_set_line_width(cr, selected ? 1.6 : 1.0);
  cairo_stroke(cr);
  if (selected) {
    cairo_arc(cr, x, y, 1.8, 0.0, M_PI * 2.0);
    setColor(cr, Rgb{0.06, 0.08, 0.11}, 0.85);
    cairo_fill(cr);
  }
}

void Panel::drawGraph(cairo_t* cr, UiState& state) {
  cairo_save(cr);
  cairo_rectangle(cr, kGraphX, kGraphY, kGraphW, kGraphH);
  cairo_clip(cr);

  cairo_pattern_t* bg = cairo_pattern_create_linear(0, kGraphY, 0, kGraphBottom);
  cairo_pattern_add_color_stop_rgb(bg, 0.0, 0.075, 0.106, 0.161);
  cairo_pattern_add_color_stop_rgb(bg, 1.0, 0.047, 0.067, 0.102);
  cairo_set_source(cr, bg);
  cairo_paint(cr);
  cairo_pattern_destroy(bg);

  // Grid.
  cairo_set_line_width(cr, 1.0);
  for (const GridFreq& g : kGridFreqs) {
    const double x = std::floor(freqToX(g.f)) + 0.5;
    setColor(cr, kGrid, g.major ? 0.95 : 0.55);
    cairo_move_to(cr, x, kGraphY);
    cairo_line_to(cr, x, kGraphBottom);
    cairo_stroke(cr);
  }
  for (int db = -30; db <= 30; db += 5) {
    const double y = std::floor(dbToY(db)) + 0.5;
    setColor(cr, kGrid, db == 0 ? 0.0 : (db % 10 == 0 ? 0.7 : 0.4));
    if (db == 0) {
      setColor(cr, Rgb{0.42, 0.49, 0.60}, 0.85);
    }
    cairo_set_line_width(cr, db == 0 ? 1.2 : 1.0);
    cairo_move_to(cr, kGraphX, y);
    cairo_line_to(cr, kGraphRight, y);
    cairo_stroke(cr);
  }

  // Analyzer.
  bool haveSpectrum = false;
  for (uint32_t i = 0; i < kSpectrumCount; ++i) {
    if (state.spectrum[i] > -99.0f) {
      haveSpectrum = true;
      break;
    }
  }
  if (haveSpectrum) {
    cairo_new_path(cr);
    bool first = true;
    for (uint32_t i = 0; i < kSpectrumCount; ++i) {
      const double t = static_cast<double>(i) / (kSpectrumCount - 1);
      const double f = 20.0 * std::pow(1000.0, t);
      const double levelDb = state.spectrum[i];
      const double axis = clamp(levelDb + 30.0, -30.0, 30.0);
      const double x = freqToX(f);
      const double y = dbToY(axis);
      if (first) {
        cairo_move_to(cr, x, kGraphBottom);
        cairo_line_to(cr, x, y);
        first = false;
      } else {
        cairo_line_to(cr, x, y);
      }
    }
    cairo_line_to(cr, kGraphRight, kGraphBottom);
    cairo_close_path(cr);
    cairo_pattern_t* spec = cairo_pattern_create_linear(0, kGraphY, 0,
                                                        kGraphBottom);
    cairo_pattern_add_color_stop_rgba(spec, 0.0, 0.66, 0.74, 0.86, 0.46);
    cairo_pattern_add_color_stop_rgba(spec, 1.0, 0.55, 0.63, 0.74, 0.07);
    cairo_set_source(cr, spec);
    cairo_fill(cr);
    cairo_pattern_destroy(spec);

    // Spectrum outline.
    cairo_new_path(cr);
    for (uint32_t i = 0; i < kSpectrumCount; ++i) {
      const double t = static_cast<double>(i) / (kSpectrumCount - 1);
      const double f = 20.0 * std::pow(1000.0, t);
      const double axis = clamp(state.spectrum[i] + 30.0, -30.0, 30.0);
      const double x = freqToX(f);
      const double y = dbToY(axis);
      if (i == 0) {
        cairo_move_to(cr, x, y);
      } else {
        cairo_line_to(cr, x, y);
      }
    }
    setColor(cr, Rgb{0.72, 0.80, 0.92}, 0.85);
    cairo_set_line_width(cr, 1.3);
    cairo_stroke(cr);
  }

  // Per-band fills.
  for (int b = 0; b < 8; ++b) {
    if (state.values[bandParam(b, kSlotOn)] <= 0.5f) {
      continue;
    }
    bandCurvePath(cr, state, b, true, 3.0, true);
    const Rgb c = bandRgb(b);
    setColor(cr, c, state.selectedBand == b ? 0.17 : 0.075);
    cairo_fill(cr);
  }

  // Dynamic reduction shading between the set curve and the applied curve.
  bool anyDyn = false;
  for (int b = 0; b < 8; ++b) {
    if (state.values[bandParam(b, kSlotDynOn)] > 0.5f &&
        std::fabs(state.dyn[b]) > 0.03f) {
      anyDyn = true;
    }
  }
  if (anyDyn) {
    Params pp;
    for (int b = 0; b < 8; ++b) {
      pp.bands[b].on = state.values[bandParam(b, kSlotOn)] > 0.5f;
      pp.bands[b].freq = state.values[bandParam(b, kSlotFreq)];
      pp.bands[b].gainDb = state.values[bandParam(b, kSlotGain)];
      pp.bands[b].q = state.values[bandParam(b, kSlotQ)];
      pp.bands[b].slope =
          static_cast<int>(std::lround(state.values[bandParam(b, kSlotSlope)]));
      pp.bands[b].dynOn = state.values[bandParam(b, kSlotDynOn)] > 0.5f;
    }
    pp.qCouple = state.values[PORT_Q_COUPLE] > 0.5f;
    cairo_new_path(cr);
    bool first = true;
    for (double x = kGraphX; x <= kGraphRight; x += 2.0) {
      const double db = totalResponseDb(pp, state.dyn,
                                        static_cast<float>(xToFreq(x)),
                                        48000.0f);
      if (first) {
        cairo_move_to(cr, x, dbToY(db));
        first = false;
      } else {
        cairo_line_to(cr, x, dbToY(db));
      }
    }
    for (double x = kGraphRight; x >= kGraphX; x -= 2.0) {
      const double db = totalResponseDb(pp, nullptr,
                                        static_cast<float>(xToFreq(x)),
                                        48000.0f);
      cairo_line_to(cr, x, dbToY(db));
    }
    cairo_close_path(cr);
    setColor(cr, kAmber, 0.16);
    cairo_fill(cr);
  }

  // Individual band curves.
  for (int b = 0; b < 8; ++b) {
    if (state.values[bandParam(b, kSlotOn)] <= 0.5f) {
      continue;
    }
    bandCurvePath(cr, state, b, true, 2.0, false);
    setColor(cr, bandRgb(b), state.selectedBand == b ? 0.95 : 0.55);
    cairo_set_line_width(cr, state.selectedBand == b ? 1.9 : 1.2);
    cairo_stroke(cr);
  }

  // Static curve when dynamics move the applied curve.
  if (anyDyn) {
    totalCurvePath(cr, state, false, false);
    setColor(cr, kMuted, 0.55);
    cairo_set_line_width(cr, 1.0);
    const double dashes[] = {4.0, 4.0};
    cairo_set_dash(cr, dashes, 2, 0.0);
    cairo_stroke(cr);
    cairo_set_dash(cr, nullptr, 0, 0.0);
  }

  // Summed curve with a soft glow.
  totalCurvePath(cr, state, true, false);
  setColor(cr, Rgb{0.90, 0.93, 0.97}, 0.10);
  cairo_set_line_width(cr, 6.0);
  cairo_stroke_preserve(cr);
  setColor(cr, Rgb{0.94, 0.96, 0.99}, 1.0);
  cairo_set_line_width(cr, 2.1);
  cairo_stroke(cr);

  // Nodes.
  for (int b = 0; b < 8; ++b) {
    if (state.values[bandParam(b, kSlotOn)] <= 0.5f) {
      continue;
    }
    const double f = state.values[bandParam(b, kSlotFreq)];
    const bool cut = bandIsCut(b);
    const double gain = cut ? 0.0 : state.values[bandParam(b, kSlotGain)];
    const double x = freqToX(f);
    const double y = dbToY(gain + state.dyn[b]);
    drawNode(cr, b, x, y, state.selectedBand == b);
  }

  // Threshold line for the selected dynamic band.
  const int sel = state.selectedBand;
  if (bandHasGain(sel) && state.values[bandParam(sel, kSlotDynOn)] > 0.5f) {
    const double thr = state.values[bandParam(sel, kSlotDynThreshold)];
    const double y = dbToY(clamp(thr + 30.0, -30.0, 30.0));
    setColor(cr, kAmber, 0.55);
    cairo_set_line_width(cr, 1.0);
    const double dashes[] = {6.0, 5.0};
    cairo_set_dash(cr, dashes, 2, 0.0);
    cairo_move_to(cr, kGraphX, y);
    cairo_line_to(cr, kGraphRight, y);
    cairo_stroke(cr);
    cairo_set_dash(cr, nullptr, 0, 0.0);
    char label[32];
    std::snprintf(label, sizeof(label), "THR %+.0f", thr);
    setColor(cr, kAmber, 0.8);
    textAt(cr, label, kGraphX + 8.0, y - 5.0, 8.0, true, kAmber);
  }

  // Selected tooltip: only while dragging or hovering that band.
  const bool showTip =
      sel >= 0 && (drag_ == Drag::Node ||
                   nearestBand(state, hoverX_, hoverY_) == sel);
  if (showTip) {
    const double f = state.values[bandParam(sel, kSlotFreq)];
    const bool cut = bandIsCut(sel);
    const double gain = cut ? 0.0 : state.values[bandParam(sel, kSlotGain)];
    const double x = clamp(freqToX(f), kGraphX + 70.0, kGraphRight - 70.0);
    const double y = clamp(dbToY(gain + state.dyn[sel]) - 30.0, kGraphY + 10.0,
                           kGraphBottom - 40.0);
    char line[96];
    if (bandHasGain(sel)) {
      if (state.values[bandParam(sel, kSlotDynOn)] > 0.5f) {
        std::snprintf(line, sizeof(line), "%s  %+.1f dB  Q %.2f   DYN %+.1f",
                      fmtFreq(state.values[bandParam(sel, kSlotFreq)]).c_str(),
                      gain, state.values[bandParam(sel, kSlotQ)],
                      state.dyn[sel]);
      } else {
        std::snprintf(line, sizeof(line), "%s  %+.1f dB  Q %.2f",
                      fmtFreq(state.values[bandParam(sel, kSlotFreq)]).c_str(),
                      gain, state.values[bandParam(sel, kSlotQ)]);
      }
    } else {
      std::snprintf(line, sizeof(line), "%s  %d dB/Oct",
                    fmtFreq(state.values[bandParam(sel, kSlotFreq)]).c_str(),
                    static_cast<int>(std::lround(
                        state.values[bandParam(sel, kSlotSlope)])));
    }
    roundedRect(cr, x - 76.0, y, 152.0, 20.0, 4.0);
    setColor(cr, Rgb{0.055, 0.075, 0.105}, 0.92);
    cairo_fill_preserve(cr);
    setColor(cr, bandRgb(sel), 0.55);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
    textAt(cr, line, x, y + 14.0, 8.5, true, kText, kCenter);
  }

  // Frequency labels along the bottom inside the plot.
  for (const GridFreq& g : kGridFreqs) {
    if (!g.major) {
      continue;
    }
    setColor(cr, kMuted, 0.9);
    textAt(cr, g.label, freqToX(g.f), kGraphBottom - 5.0, 8.0, false, kMuted,
           kCenter);
  }

  cairo_restore(cr);

  // dB scale outside the plot.
  for (int db = -30; db <= 30; db += 5) {
    const double y = dbToY(db);
    const std::string label = (db > 0 ? "+" : "") + std::to_string(db);
    textAt(cr, label.c_str(), kGraphX - 8.0, y + 3.0, 7.5, false,
           db == 0 ? kText : kMuted, kRight);
    textAt(cr, label.c_str(), kGraphRight + 8.0, y + 3.0, 7.5, false,
           db == 0 ? kText : kMuted, kLeft);
  }

  // Frame.
  cairo_rectangle(cr, kGraphX + 0.5, kGraphY + 0.5, kGraphW - 1.0,
                  kGraphH - 1.0);
  setColor(cr, kLine, 0.9);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  // "Analyzer" mode tag.
  char mode[32];
  std::snprintf(mode, sizeof(mode), "ANALYZER %s",
                analysisName(static_cast<int>(
                    std::lround(state.values[PORT_ANALYZER_MODE]))));
  textAt(cr, mode, kGraphX + 10.0, kGraphY + 14.0, 7.5, true, kDim);
}

void Panel::drawBottom(cairo_t* cr, UiState& state) {
  cairo_save(cr);
  cairo_rectangle(cr, 0, kBottomY, kWidth, kBottomH);
  cairo_clip(cr);
  setColor(cr, Rgb{0.055, 0.078, 0.114}, 1.0);
  cairo_paint(cr);
  const double colW = kGraphW / 8.0;
  for (int b = 0; b < 8; ++b) {
    const double x = kGraphX + b * colW;
    if (state.selectedBand == b) {
      setColor(cr, bandRgb(b), 0.09);
      cairo_rectangle(cr, x, kBottomY, colW, kBottomH);
      cairo_fill(cr);
      setColor(cr, bandRgb(b), 0.7);
      cairo_rectangle(cr, x, kBottomY, colW, 1.5);
      cairo_fill(cr);
    }
    if (b > 0) {
      setColor(cr, kLine, 0.55);
      cairo_set_line_width(cr, 1.0);
      cairo_move_to(cr, x + 0.5, kBottomY + 6.0);
      cairo_line_to(cr, x + 0.5, kBottomY + kBottomH - 6.0);
      cairo_stroke(cr);
    }

    const double cx = x + colW * 0.5;
    const bool on = state.values[bandParam(b, kSlotOn)] > 0.5f;
    const Rgb c = bandRgb(b);
    const double alpha = on ? 1.0 : 0.35;
    const bool cut = bandIsCut(b);

    textAt(cr, fmtFreq(state.values[bandParam(b, kSlotFreq)]).c_str(), cx,
           kBottomY + 16.0, 9.0, true, c, kCenter, alpha);
    if (cut) {
      char slope[32];
      std::snprintf(slope, sizeof(slope), "%d dB/Oct",
                    static_cast<int>(std::lround(
                        state.values[bandParam(b, kSlotSlope)])));
      textAt(cr, slope, cx, kBottomY + 39.0, 9.0, false, c, kCenter, alpha);
      textAt(cr, "--", cx, kBottomY + 61.0, 9.0, false, kMuted, kCenter,
             alpha * 0.7);
    } else {
      textAt(cr, fmtDb(state.values[bandParam(b, kSlotGain)]).c_str(), cx,
             kBottomY + 39.0, 9.0, false, c, kCenter, alpha);
      textAt(cr, fmtQ(state.values[bandParam(b, kSlotQ)]).c_str(), cx,
             kBottomY + 61.0, 9.0, false, c, kCenter, alpha);
    }
  }
  // Live dynamic-reduction bars at the right edge of each band column.
  for (int b = 0; b < 8; ++b) {
    if (!bandHasGain(b) ||
        state.values[bandParam(b, kSlotOn)] <= 0.5f ||
        state.values[bandParam(b, kSlotDynOn)] <= 0.5f) {
      continue;
    }
    const double x = kGraphX + b * colW;
    const double barX = x + colW - 6.0;
    const double top = kBottomY + 7.0;
    const double h = kBottomH - 14.0;
    setColor(cr, Rgb{0.04, 0.055, 0.075}, 1.0);
    roundedRect(cr, barX, top, 3.0, h, 1.5);
    cairo_fill(cr);
    const double amount =
        std::min(1.0, std::fabs(state.dyn[b]) / 24.0);
    if (amount > 0.001) {
      setColor(cr, kAmber, 0.9);
      roundedRect(cr, barX, top + h * (1.0 - amount), 3.0, h * amount, 1.5);
      cairo_fill(cr);
    }
  }
  setColor(cr, kLine, 0.7);
  cairo_set_line_width(cr, 1.0);
  cairo_move_to(cr, 0, kBottomY + 0.5);
  cairo_line_to(cr, kWidth, kBottomY + 0.5);
  cairo_stroke(cr);
  cairo_restore(cr);
}

void Panel::drawFooter(cairo_t* cr, UiState& state) {
  cairo_save(cr);
  cairo_rectangle(cr, 0, kFooterY, kWidth, kFooterH);
  cairo_clip(cr);
  setColor(cr, Rgb{0.047, 0.067, 0.098}, 1.0);
  cairo_paint(cr);
  setColor(cr, kLine, 0.6);
  cairo_set_line_width(cr, 1.0);
  cairo_move_to(cr, 0, kFooterY + 0.5);
  cairo_line_to(cr, kWidth, kFooterY + 0.5);
  cairo_stroke(cr);

  const double y = kFooterY + 14.0;
  const double h = 30.0;

  chip(cr, 64.0, y, 132.0, h, false);
  textAt(cr, "Analyzer", 76.0, y + 20.0, 9.0, false, kMuted);
  const int am = static_cast<int>(
      std::lround(state.values[PORT_ANALYZER_MODE]));
  textAt(cr, analysisName(am), 184.0, y + 20.0, 9.0, true, kAccent, kRight);

  const bool qc = state.values[PORT_Q_COUPLE] > 0.5f;
  chip(cr, 206.0, y, 118.0, h, qc);
  cairo_arc(cr, 220.0, y + h * 0.5, 3.2, 0.0, M_PI * 2.0);
  setColor(cr, qc ? kAccent : kDim, 1.0);
  cairo_fill(cr);
  textAt(cr, "Q-Couple", 232.0, y + 20.0, 9.0, false,
         qc ? kText : kMuted);

  const int pm = static_cast<int>(
      std::lround(state.values[PORT_PROCESSING]));
  chip(cr, 334.0, y, 150.0, h, false);
  textAt(cr, "Processing", 346.0, y + 20.0, 9.0, false, kMuted);
  textAt(cr, processingName(pm), 474.0, y + 20.0, 9.0, true, kText, kRight);

  chip(cr, 494.0, y, 116.0, h, state.showDyn);
  textAt(cr, "Dynamics", 552.0, y + 20.0, 9.0, true,
         state.showDyn ? kAccent : kMuted, kCenter);

  const bool on = state.values[PORT_ENABLE] > 0.5f;
  textAt(cr, on ? "ACTIVE" : "BYPASSED", 64.0, kFooterY + 52.0, 7.5, true,
         on ? kAccent : kDim);

  // Output gain.
  const double gx = 940.0;
  chip(cr, gx, y - 2.0, 132.0, 34.0, false);
  textAt(cr, "Gain", gx + 12.0, y + 20.0, 9.0, false, kMuted);
  textAt(cr, fmtDb(state.values[PORT_OUTPUT_GAIN]).c_str(), gx + 120.0,
         y + 20.0, 10.0, true, kText, kRight);
  cairo_restore(cr);
}

void Panel::drawDynPanel(cairo_t* cr, UiState& state) {
  const int band = state.selectedBand;
  const Rect r = dynPanelRect();
  cairo_save(cr);
  roundedRect(cr, r.x + 3.0, r.y + 4.0, r.w, r.h, 8.0);
  setColor(cr, Rgb{0, 0, 0}, 0.3);
  cairo_fill(cr);
  roundedRect(cr, r.x, r.y, r.w, r.h, 8.0);
  setColor(cr, Rgb{0.068, 0.094, 0.134}, 0.985);
  cairo_fill_preserve(cr);
  setColor(cr, kAmber, 0.55);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  char title[64];
  char bandName[24];
  dynamicBandName(band, bandName, sizeof(bandName));
  std::snprintf(title, sizeof(title), "DYNAMICS  .  %s", bandName);
  textAt(cr, title, r.x + 14.0, r.y + 23.0, 9.0, true, kAmber);

  const bool dynOn = state.values[bandParam(band, kSlotDynOn)] > 0.5f;
  const bool up = state.values[bandParam(band, kSlotDynMode)] > 0.5f;
  chip(cr, dynModeRect().x, dynModeRect().y, dynModeRect().w,
       dynModeRect().h, false);
  textAt(cr, up ? "UP" : "DOWN", dynModeRect().x + dynModeRect().w * 0.5,
         dynModeRect().y + 15.0, 8.5, true, up ? kAmber : kMuted, kCenter);
  chip(cr, dynOnRect().x, dynOnRect().y, dynOnRect().w, dynOnRect().h, dynOn);
  textAt(cr, dynOn ? "ON" : "OFF", dynOnRect().x + dynOnRect().w * 0.5,
         dynOnRect().y + 15.0, 8.5, true, dynOn ? kAccent : kMuted, kCenter);
  textAt(cr, "x", dynCloseRect().x + 11.0, dynCloseRect().y + 16.0, 12.0,
         false, kMuted, kCenter);

  static const char* labels[5] = {"Threshold", "Ratio", "Range", "Attack",
                                  "Release"};
  static const BandSlot slots[5] = {kSlotDynThreshold, kSlotDynRatio,
                                    kSlotDynRange, kSlotDynAttack,
                                    kSlotDynRelease};
  for (int i = 0; i < 5; ++i) {
    const uint32_t port = bandParam(band, slots[i]);
    const ParamInfo* info = infoFor(port);
    const Rect track = dynTrackRect(i);
    const bool right = i >= 3;
    const double textX = right ? kDynX + 204.0 : kDynX + 12.0;
    const double valueX = right ? kDynX + 358.0 : kDynX + 166.0;
    const double labelY = track.y - 7.0;
    textAt(cr, labels[i], textX, labelY, 8.5, false, dynOn ? kMuted : kDim);
    std::string valueText;
    switch (slots[i]) {
      case kSlotDynThreshold:
      case kSlotDynRange:
        valueText = fmtDb(state.values[port]);
        break;
      case kSlotDynRatio:
        valueText = fmtRatio(state.values[port]);
        break;
      default:
        valueText = fmtMs(state.values[port]);
        break;
    }
    textAt(cr, valueText.c_str(), valueX, labelY, 8.0, true,
           dynOn ? kText : kDim, kRight);

    setColor(cr, Rgb{0.030, 0.041, 0.058}, 1.0);
    roundedRect(cr, track.x, track.y, track.w, track.h, 4.0);
    cairo_fill(cr);
    const double t = normalized(state.values[port], *info);
    setColor(cr, kAmber, dynOn ? 0.75 : 0.16);
    roundedRect(cr, track.x, track.y, std::max(4.0, track.w * t), track.h,
                4.0);
    cairo_fill(cr);
    cairo_arc(cr, track.x + track.w * t, track.y + track.h * 0.5, 4.5, 0.0,
              M_PI * 2.0);
    setColor(cr, kAmber, dynOn ? 1.0 : 0.32);
    cairo_fill(cr);
  }

  // Live reduction meter in the right-hand third row.
  {
    const Rect track = dynTrackRect(5);
    const double dyn = state.dyn[band];
    const double range = std::max(
        0.1f, state.values[bandParam(band, kSlotDynRange)]);
    const double t = std::min(1.0, std::fabs(dyn) / range);
    textAt(cr, "Reduction", kDynX + 204.0, track.y - 7.0, 8.5, false,
           dynOn ? kMuted : kDim);
    textAt(cr, fmtDb(static_cast<float>(dyn)).c_str(), kDynX + 358.0,
           track.y - 7.0, 8.0, true, dynOn ? kAmber : kDim, kRight);
    setColor(cr, Rgb{0.030, 0.041, 0.058}, 1.0);
    roundedRect(cr, track.x, track.y, track.w, track.h, 4.0);
    cairo_fill(cr);
    if (t > 0.001) {
      setColor(cr, kAmber, dynOn ? 0.9 : 0.25);
      roundedRect(cr, track.x, track.y, std::max(3.0, track.w * t), track.h,
                  4.0);
      cairo_fill(cr);
    }
    for (int i = 1; i < 4; ++i) {
      const double x = track.x + track.w * i / 4.0;
      setColor(cr, Rgb{0.03, 0.04, 0.06}, 0.8);
      cairo_rectangle(cr, x, track.y, 1.0, track.h);
      cairo_fill(cr);
    }
  }

  textAt(cr, "drag a value, or drag the handles in the graph", r.x + 14.0,
         r.y + r.h - 9.0, 7.0, false, kDim);
  cairo_restore(cr);
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------
int Panel::nearestBand(const UiState& state, double x, double y) const {
  int best = -1;
  double bestDist = 18.0 * 18.0;
  for (int b = 0; b < 8; ++b) {
    if (state.values[bandParam(b, kSlotOn)] <= 0.5f) {
      continue;
    }
    const double f = state.values[bandParam(b, kSlotFreq)];
    const bool cut = bandIsCut(b);
    const double gain = cut ? 0.0 : state.values[bandParam(b, kSlotGain)];
    const double dx = x - freqToX(f);
    const double dy = y - dbToY(gain + state.dyn[b]);
    const double dist = dx * dx + dy * dy;
    if (dist < bestDist) {
      bestDist = dist;
      best = b;
    }
  }
  return best;
}

bool Panel::pointerDown(UiState& state, double x, double y, bool fine,
                        const Setter& set) {
  (void)fine;
  // Top bar.
  if (y <= kTopH) {
    if (x >= 12.0 && x <= 56.0 && y >= 12.0 && y <= 42.0) {
      setValue(state, PORT_ENABLE,
               state.values[PORT_ENABLE] > 0.5f ? 0.0f : 1.0f, set);
      return true;
    }
    if (x >= 62 && x <= 84 && y >= 12 && y <= 42) {
      cyclePreset(state, -1, set);
      return true;
    }
    if (x >= 358 && x <= 382 && y >= 12 && y <= 42) {
      cyclePreset(state, 1, set);
      return true;
    }
    if (x >= 88 && x <= 358 && y >= 12 && y <= 42) {
      return false;
    }
    if (x >= 392 && x <= 438 && y >= 12 && y <= 42) {
      cycleAB(state, set);
      return true;
    }
    if (x >= 446 && x <= 504 && y >= 12 && y <= 42) {
      for (int i = 0; i < kParamCount; ++i) {
        setValue(state, kParams[i].index, kParams[i].def, set);
      }
      state.presetIndex = 0;
      state.presetDirty = false;
      return true;
    }
    if (x >= 900 && x <= 988 && y >= 12 && y <= 42) {
      if (!bandHasGain(state.selectedBand)) {
        state.selectedBand = 3;
      }
      state.showDyn = !state.showDyn;
      return true;
    }
    return false;
  }

  // Dyn panel gets first refusal.
  if (state.showDyn && bandHasGain(state.selectedBand)) {
    const Rect panel = dynPanelRect();
    if (panel.in(x, y)) {
      if (dynCloseRect().in(x, y)) {
        state.showDyn = false;
        return true;
      }
      if (dynOnRect().in(x, y)) {
        toggleDynamics(state, state.selectedBand, set);
        return true;
      }
      if (dynModeRect().in(x, y)) {
        const float mode =
            state.values[bandParam(state.selectedBand, kSlotDynMode)];
        setValue(state, bandParam(state.selectedBand, kSlotDynMode),
                 mode > 0.5f ? 0.0f : 1.0f, set);
        return true;
      }
      const int row = dynRowAt(x, y);
      if (row == 5) {
        return true;
      }
      if (row >= 0) {
        static const BandSlot slots[5] = {kSlotDynThreshold, kSlotDynRatio,
                                          kSlotDynRange, kSlotDynAttack,
                                          kSlotDynRelease};
        drag_ = Drag::DynRow;
        dragRow_ = row;
        dragPort_ = bandParam(state.selectedBand, slots[row]);
        const ParamInfo* info = infoFor(dragPort_);
        const Rect track = dynTrackRect(row);
        const double t = clamp(static_cast<float>((x - track.x) / track.w),
                               0.0f, 1.0f);
        setValue(state, dragPort_, denormalized(t, *info), set);
        return true;
      }
      return true;
    }
  }

  // Band icons.
  const IconHit icon = iconAt(x, y);
  if (icon.band >= 0) {
    selectBand(state, icon.band);
    if (icon.badge) {
      toggleDynamics(state, icon.band, set);
    } else {
      toggleBand(state, icon.band, set);
    }
    return true;
  }

  // Graph.
  if (x >= kGraphX && x <= kGraphRight && y >= kGraphY && y <= kGraphBottom) {
    const int band = nearestBand(state, x, y);
    if (band >= 0) {
      selectBand(state, band);
      drag_ = Drag::Node;
      dragBand_ = band;
      dragStartY_ = y;
      dragStartX_ = x;
      dragStartValue_ = state.values[bandParam(band, kSlotFreq)];
      dragStartValue2_ = state.values[bandParam(band, kSlotGain)];
      return true;
    }
    return false;
  }

  // Bottom strip.
  const CellHit cell = cellAt(x, y);
  if (cell.band >= 0) {
    selectBand(state, cell.band);
    dragBand_ = cell.band;
    dragRow_ = cell.row;
    dragStartY_ = y;
    if (cell.row == 0) {
      drag_ = Drag::CellFreq;
      dragStartValue_ = state.values[bandParam(cell.band, kSlotFreq)];
    } else if (cell.row == 1) {
      if (bandIsCut(cell.band)) {
        const float slope = state.values[bandParam(cell.band, kSlotSlope)];
        setValue(state, bandParam(cell.band, kSlotSlope),
                 slope > 18.0f ? 12.0f : 24.0f, set);
        drag_ = Drag::Idle;
      } else {
        drag_ = Drag::CellGain;
        dragStartValue_ = state.values[bandParam(cell.band, kSlotGain)];
      }
    } else {
      drag_ = Drag::CellQ;
      dragStartValue_ = state.values[bandParam(cell.band, kSlotQ)];
    }
    return true;
  }

  // Footer.
  if (y >= kFooterY) {
    if (x >= 64 && x <= 196 && y >= kFooterY + 14 && y <= kFooterY + 44) {
      const int mode = static_cast<int>(
          std::lround(state.values[PORT_ANALYZER_MODE]));
      setValue(state, PORT_ANALYZER_MODE, mode == 0 ? 1.0f : 0.0f, set);
      return true;
    }
    if (x >= 206 && x <= 324 && y >= kFooterY + 14 && y <= kFooterY + 44) {
      setValue(state, PORT_Q_COUPLE,
               state.values[PORT_Q_COUPLE] > 0.5f ? 0.0f : 1.0f, set);
      return true;
    }
    if (x >= 334 && x <= 484 && y >= kFooterY + 14 && y <= kFooterY + 44) {
      const int mode = static_cast<int>(
          std::lround(state.values[PORT_PROCESSING]));
      setValue(state, PORT_PROCESSING, static_cast<float>((mode + 1) % 4), set);
      return true;
    }
    if (x >= 494 && x <= 610 && y >= kFooterY + 14 && y <= kFooterY + 44) {
      if (!bandHasGain(state.selectedBand)) {
        state.selectedBand = 3;
      }
      state.showDyn = !state.showDyn;
      return true;
    }
    if (x >= 940 && x <= 1072 && y >= kFooterY + 12 && y <= kFooterY + 46) {
      drag_ = Drag::OutputGain;
      dragStartY_ = y;
      dragStartValue_ = state.values[PORT_OUTPUT_GAIN];
      return true;
    }
  }
  return false;
}

bool Panel::pointerMove(UiState& state, double x, double y, bool fine,
                        const Setter& set) {
  if (drag_ == Drag::Idle) {
    return false;
  }
  const double sens = fine ? 0.25 : 1.0;
  if (drag_ == Drag::Node && dragBand_ >= 0) {
    const double nx = fine ? dragStartX_ + (x - dragStartX_) * 0.25 : x;
    const double ny = fine ? dragStartY_ + (y - dragStartY_) * 0.25 : y;
    const float freq = static_cast<float>(xToFreq(nx));
    setValue(state, bandParam(dragBand_, kSlotFreq), freq, set);
    if (!bandIsCut(dragBand_)) {
      const float gain = static_cast<float>(yToDb(ny));
      setValue(state, bandParam(dragBand_, kSlotGain), gain, set);
    }
    return true;
  }
  if (drag_ == Drag::CellFreq && dragBand_ >= 0) {
    const double dy = y - dragStartY_;
    const double factor = std::exp(-dy * 0.010 * sens);
    setValue(state, bandParam(dragBand_, kSlotFreq),
             static_cast<float>(dragStartValue_ * factor), set);
    return true;
  }
  if (drag_ == Drag::CellGain && dragBand_ >= 0) {
    const double dy = y - dragStartY_;
    setValue(state, bandParam(dragBand_, kSlotGain),
             static_cast<float>(dragStartValue_ - dy * 0.15 * sens), set);
    return true;
  }
  if (drag_ == Drag::CellQ && dragBand_ >= 0) {
    const double dy = y - dragStartY_;
    const double factor = std::exp(-dy * 0.012 * sens);
    setValue(state, bandParam(dragBand_, kSlotQ),
             static_cast<float>(dragStartValue_ * factor), set);
    return true;
  }
  if (drag_ == Drag::OutputGain) {
    const double dy = y - dragStartY_;
    setValue(state, PORT_OUTPUT_GAIN,
             static_cast<float>(dragStartValue_ - dy * 0.10 * sens), set);
    return true;
  }
  if (drag_ == Drag::DynRow) {
    const ParamInfo* info = infoFor(dragPort_);
    const Rect track = dynTrackRect(dragRow_);
    const double t = clamp(static_cast<float>((x - track.x) / track.w), 0.0f,
                           1.0f);
    setValue(state, dragPort_, denormalized(t, *info), set);
    return true;
  }
  return false;
}

void Panel::pointerUp(UiState& state) {
  (void)state;
  drag_ = Drag::Idle;
  dragBand_ = -1;
  dragRow_ = 0;
}

bool Panel::wheel(UiState& state, double x, double y, double delta, bool fine,
                  const Setter& set) {
  if (state.showDyn && bandHasGain(state.selectedBand)) {
    const Rect panel = dynPanelRect();
    if (panel.in(x, y)) {
      const int row = dynRowAt(x, y);
      if (row == 5) {
        return true;
      }
      if (row >= 0) {
        static const BandSlot slots[5] = {kSlotDynThreshold, kSlotDynRatio,
                                          kSlotDynRange, kSlotDynAttack,
                                          kSlotDynRelease};
        const uint32_t port = bandParam(state.selectedBand, slots[row]);
        const ParamInfo* info = infoFor(port);
        const double t = normalized(state.values[port], *info);
        setValue(state, port,
                 denormalized(t + delta * (fine ? 0.005 : 0.02), *info), set);
        return true;
      }
      return false;
    }
  }

  const double sens = fine ? 0.25 : 1.0;
  const CellHit cell = cellAt(x, y);
  if (cell.band >= 0) {
    selectBand(state, cell.band);
    if (cell.row == 0) {
      const float f = state.values[bandParam(cell.band, kSlotFreq)];
      setValue(state, bandParam(cell.band, kSlotFreq),
               static_cast<float>(f * std::exp(delta * 0.04 * sens)), set);
      return true;
    }
    if (cell.row == 1) {
      if (bandIsCut(cell.band)) {
        return false;
      }
      const float g = state.values[bandParam(cell.band, kSlotGain)];
      setValue(state, bandParam(cell.band, kSlotGain),
               static_cast<float>(g + delta * 0.5 * sens), set);
      return true;
    }
    const float q = state.values[bandParam(cell.band, kSlotQ)];
    setValue(state, bandParam(cell.band, kSlotQ),
             static_cast<float>(q * std::exp(delta * 0.08 * sens)), set);
    return true;
  }

  if (x < kGraphX || x > kGraphRight || y < kGraphY || y > kGraphY + kGraphH) {
    return false;
  }
  const int band = nearestBand(state, x, y);
  const int target = band >= 0 ? band : state.selectedBand;
  if (!bandHasGain(target)) {
    return false;
  }
  const float q = state.values[bandParam(target, kSlotQ)];
  setValue(state, bandParam(target, kSlotQ),
           static_cast<float>(q * std::exp(delta * 0.08 * sens)), set);
  selectBand(state, target);
  return true;
}

bool Panel::doubleClick(UiState& state, double x, double y,
                        const Setter& set) {
  const int band = nearestBand(state, x, y);
  if (band >= 0 && x >= kGraphX && y >= kGraphY && y <= kGraphBottom) {
    if (bandHasGain(band)) {
      setValue(state, bandParam(band, kSlotGain), 0.0f, set);
    } else {
      setValue(state, bandParam(band, kSlotFreq), kDefaultFreq[band], set);
    }
    return true;
  }
  const CellHit cell = cellAt(x, y);
  if (cell.band >= 0) {
    const ParamInfo* info = nullptr;
    uint32_t port = 0;
    if (cell.row == 0) {
      port = bandParam(cell.band, kSlotFreq);
    } else if (cell.row == 1) {
      port = bandParam(cell.band, bandIsCut(cell.band) ? kSlotSlope
                                                       : kSlotGain);
    } else {
      port = bandParam(cell.band, kSlotQ);
    }
    info = infoFor(port);
    if (info) {
      setValue(state, port, info->def, set);
      return true;
    }
  }
  return false;
}

bool Panel::rightClick(UiState& state, double x, double y) {
  const int band = nearestBand(state, x, y);
  if (band >= 0) {
    selectBand(state, band);
    if (bandHasGain(band)) {
      state.showDyn = true;
    }
    return true;
  }
  return false;
}

} // namespace ui
} // namespace cse
