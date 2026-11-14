// Common Sense EQ - render the inline mixer-strip view to PNG
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include <cairo/cairo.h>

#include "cse/param_info.hpp"
#include "dsp/inline_curve.hpp"

int main(int argc, char** argv) {
  using namespace cse;
  Params p;
  for (int b = 0; b < 8; ++b) {
    p.bands[b].freq = kBandDefaultFreq[b];
    p.bands[b].q = kBandDefaultQ[b];
    p.bands[b].slope = kBandDefaultSlope[b];
  }
  // A musical setting with one dynamic band.
  p.bands[1].gainDb = 2.5f;
  p.bands[1].q = 0.9f;
  p.bands[3].gainDb = -2.0f;
  p.bands[4].gainDb = 1.5f;
  p.bands[5].freq = 3200.0f;
  p.bands[5].gainDb = 3.0f;
  p.bands[5].q = 1.1f;
  p.bands[5].dynOn = true;
  p.bands[6].freq = 7000.0f;
  p.bands[6].gainDb = 2.0f;
  p.bands[6].q = 0.7f;

  InlineSnapshot snap;
  snap.params = p;
  snap.hasSpectrum = true;
  for (uint32_t i = 0; i < kSpectrumCount; ++i) {
    const float t = static_cast<float>(i) / (kSpectrumCount - 1);
    snap.spectrum[i] = -56.0f + 24.0f * std::exp(-t * 3.0f) +
                       10.0f * std::exp(-(t - 0.42f) * (t - 0.42f) / 0.002f) +
                       8.0f * std::exp(-(t - 0.78f) * (t - 0.78f) / 0.003f);
  }
  snap.dynDb[5] = -3.2f;

  InlineDisplay display;
  const struct {
    uint32_t w;
    uint32_t h;
    const char* path;
    const char* label;
  } sizes[] = {
      {520, 150, "docs/inline-large.png", "large"},
      {260, 68, "docs/inline-small.png", "small"},
  };
  for (const auto& size : sizes) {
    InlineDisplay::Surface* surf = display.render(snap, size.w, size.h);
    if (!surf) {
      std::printf("render failed\n");
      return 1;
    }
    cairo_surface_t* image = cairo_image_surface_create_for_data(
        surf->data, CAIRO_FORMAT_ARGB32, surf->width, surf->height,
        surf->stride);
    const cairo_status_t status = cairo_surface_write_to_png(image, size.path);
    cairo_surface_destroy(image);
    std::printf("%s: %s (%dx%d) status=%d\n", size.label, size.path,
                surf->width, surf->height, static_cast<int>(status));
  }
  return 0;
}
