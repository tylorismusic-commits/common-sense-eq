// Common Sense EQ - panel.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cairo.h>

#include <functional>

#include "cse/param_info.hpp"
#include "dsp/params.hpp"

namespace cse {
namespace ui {

constexpr int kWidth = 1120;
constexpr int kHeight = 560;

struct Rect {
  double x = 0.0;
  double y = 0.0;
  double w = 0.0;
  double h = 0.0;
  bool in(double px, double py) const {
    return px >= x && py >= y && px <= x + w && py <= y + h;
  }
};

struct Rgb {
  double r = 1.0;
  double g = 1.0;
  double b = 1.0;
};

Rgb bandRgb(int band);
const char* bandGlyphName(int band);

struct UiState {
  float values[PORT_COUNT] = {};
  float spectrum[kSpectrumCount] = {};
  float dyn[kGrCount] = {};
  int selectedBand = 3;
  bool showDyn = false;
  int presetIndex = 0;
  bool presetDirty = false;
  int abSlot = 0;
  bool abInit[2] = {false, false};
  float abValues[2][PORT_COUNT] = {};
};

void initState(UiState& state);
void applyPreset(UiState& state, int index,
                 const std::function<void(uint32_t, float)>& set);

class Panel {
public:
  using Setter = std::function<void(uint32_t, float)>;

  void draw(cairo_t* cr, UiState& state);

  bool pointerDown(UiState& state, double x, double y, bool fine,
                   const Setter& set);
  bool pointerMove(UiState& state, double x, double y, bool fine,
                   const Setter& set);
  void pointerUp(UiState& state);
  bool wheel(UiState& state, double x, double y, double delta, bool fine,
             const Setter& set);
  bool doubleClick(UiState& state, double x, double y, const Setter& set);
  bool rightClick(UiState& state, double x, double y);

  bool dragging() const { return drag_ != Drag::Idle; }
  void setHover(double x, double y) {
    hoverX_ = x;
    hoverY_ = y;
  }

private:
  enum class Drag {
    Idle,
    Node,
    CellFreq,
    CellGain,
    CellQ,
    DynRow,
    OutputGain
  };

  struct Layout;

  void setValue(UiState& state, uint32_t port, float value,
                const Setter& set) const;
  void drawTopBar(cairo_t* cr, UiState& state);
  void drawIcons(cairo_t* cr, UiState& state);
  void drawGraph(cairo_t* cr, UiState& state);
  void drawBottom(cairo_t* cr, UiState& state);
  void drawFooter(cairo_t* cr, UiState& state);
  void drawDynPanel(cairo_t* cr, UiState& state);
  void drawNode(cairo_t* cr, int band, double x, double y, bool selected);
  void drawBandIcon(cairo_t* cr, int band, double cx, double cy, bool on,
                    bool selected, bool dynOn);

  float value(const UiState& state, uint32_t port) const;
  int nearestBand(const UiState& state, double x, double y) const;
  void selectBand(UiState& state, int band);
  void toggleBand(UiState& state, int band, const Setter& set);
  void toggleDynamics(UiState& state, int band, const Setter& set);
  void cyclePreset(UiState& state, int direction, const Setter& set);
  void cycleAB(UiState& state, const Setter& set);

  Drag drag_ = Drag::Idle;
  int dragBand_ = -1;
  int dragRow_ = 0;
  double dragStartY_ = 0.0;
  double dragStartX_ = 0.0;
  double dragStartValue_ = 0.0;
  double dragStartValue2_ = 0.0;
  uint32_t dragPort_ = 0;
  double hoverX_ = -10000.0;
  double hoverY_ = -10000.0;
  bool hoverDyn_ = false;
};

} // namespace ui
} // namespace cse
