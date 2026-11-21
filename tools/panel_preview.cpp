// Common Sense EQ - render the editor panel to a PNG without a window system.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Used by the macOS CI build to prove that panel.cpp compiles and draws with
// only cairo, no X11.  Not part of the plugin build.
#include <cmath>
#include <cstdio>
#include <string>

#include <cairo/cairo.h>

#include "ui/panel.hpp"

int main(int argc, char** argv) {
  using namespace cse;
  using namespace cse::ui;

  const std::string out = argc > 1 ? argv[1] : "docs/panel-preview.png";

  UiState state;
  initState(state);
  state.values[PORT_ANALYZER_MODE] = 2.0f;
  state.values[PORT_Q_COUPLE] = 1.0f;
  state.values[PORT_OUTPUT_GAIN] = 1.5f;

  // A musical curve plus a live-looking spectrum.
  const float gains[8] = {-1.5f, 2.0f, -2.5f, 3.0f, -1.0f, 1.5f, 2.5f, 0.0f};
  for (int b = 0; b < 8; ++b) {
    state.values[bandParam(b, kSlotGain)] = gains[b];
  }
  for (uint32_t i = 0; i < kSpectrumCount; ++i) {
    const float t = static_cast<float>(i) / (kSpectrumCount - 1);
    state.spectrum[i] = -58.0f + 26.0f * std::exp(-t * 2.6f) +
                        9.0f * std::exp(-(t - 0.55f) * (t - 0.55f) / 0.004f);
  }
  state.selectedBand = 3;

  cairo_surface_t* surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, kWidth, kHeight);
  cairo_t* cr = cairo_create(surface);
  Panel panel;
  panel.draw(cr, state);
  const cairo_status_t status = cairo_surface_write_to_png(surface, out.c_str());
  cairo_destroy(cr);
  cairo_surface_destroy(surface);

  std::printf("panel preview: %s (status %d)\n", out.c_str(),
              static_cast<int>(status));
  return status == CAIRO_STATUS_SUCCESS ? 0 : 1;
}
