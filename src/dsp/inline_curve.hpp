// Common Sense EQ - inline_curve.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Dependency-free ARGB rasteriser for the mixer-strip / TCP / MCP views.
// The editor uses Cairo, but the inline views cannot assume that Cairo is
// loaded in the host's audio process, so this file draws the same model with
// plain pixels.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "curve.hpp"
#include "params.hpp"
#include "util.hpp"

namespace cse {

struct InlineSnapshot {
  Params params;
  float dynDb[8] = {};
  float spectrum[32] = {};
  bool hasSpectrum = false;
};

// Straight-alpha ARGB helpers.  The final buffer is opaque, which keeps the
// Harrison inline-display surface and Reaper's LICE bitmap happy.
inline constexpr uint32_t argb(int r, int g, int b) {
  return 0xff000000u | (static_cast<uint32_t>(r) << 16) |
         (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
}

inline uint32_t mix(uint32_t dst, uint32_t rgb, float a) {
  a = std::min(std::max(a, 0.0f), 1.0f);
  const float ia = 1.0f - a;
  const int dr = (dst >> 16) & 0xff;
  const int dg = (dst >> 8) & 0xff;
  const int db = dst & 0xff;
  const int sr = (rgb >> 16) & 0xff;
  const int sg = (rgb >> 8) & 0xff;
  const int sb = rgb & 0xff;
  return argb(static_cast<int>(sr * a + dr * ia),
              static_cast<int>(sg * a + dg * ia),
              static_cast<int>(sb * a + db * ia));
}

struct PlotRect {
  float left;
  float top;
  float right;
  float bottom;
};

inline PlotRect plotRect(int width, int height, bool roomForGrid) {
  const float inset = roomForGrid ? 5.0f : 3.0f;
  return {inset, inset, static_cast<float>(width) - inset,
          static_cast<float>(height) - inset};
}

inline float logFreq(float x, const PlotRect& r) {
  const float t = clamp((x - r.left) / std::max(1.0f, r.right - r.left), 0.0f,
                        1.0f);
  return 20.0f * std::pow(1000.0f, t);
}

inline float freqX(float freq, const PlotRect& r) {
  const float t = std::log(std::max(freq, 20.0f) / 20.0f) / std::log(1000.0f);
  return r.left + clamp(t, 0.0f, 1.0f) * (r.right - r.left);
}

inline float dbY(float db, const PlotRect& r) {
  const float t = clamp((30.0f - db) / 60.0f, 0.0f, 1.0f);
  return r.top + t * (r.bottom - r.top);
}

inline float yDb(float y, const PlotRect& r) {
  const float t = clamp((y - r.top) / std::max(1.0f, r.bottom - r.top), 0.0f,
                        1.0f);
  return 30.0f - t * 60.0f;
}

inline uint32_t bandColor(int band) {
  static constexpr uint32_t colors[8] = {
      argb(216, 201, 74), argb(224, 138, 60), argb(229, 207, 106),
      argb(85, 192, 122), argb(79, 185, 168), argb(106, 169, 224),
      argb(165, 138, 216), argb(217, 138, 138)};
  return colors[clamp(band, 0, 7)];
}

inline void put(uint32_t* pixels, int width, int height, int rowSpan, int x,
                int y, uint32_t color, float alpha) {
  if (x < 0 || y < 0 || x >= width || y >= height) {
    return;
  }
  uint32_t* p = pixels + static_cast<size_t>(y) * rowSpan + x;
  *p = mix(*p, color, alpha);
}

inline void fillRect(uint32_t* pixels, int width, int height, int rowSpan,
                     int x0, int y0, int x1, int y1, uint32_t color,
                     float alpha) {
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      put(pixels, width, height, rowSpan, x, y, color, alpha);
    }
  }
}

inline void line(uint32_t* pixels, int width, int height, int rowSpan,
                 float x0, float y0, float x1, float y1, uint32_t color,
                 float alpha) {
  const float dx = x1 - x0;
  const float dy = y1 - y0;
  const int steps = static_cast<int>(std::ceil(std::max(std::fabs(dx),
                                                        std::fabs(dy))));
  if (steps <= 0) {
    put(pixels, width, height, rowSpan, static_cast<int>(std::lround(x0)),
        static_cast<int>(std::lround(y0)), color, alpha);
    return;
  }
  for (int i = 0; i <= steps; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(steps);
    const float x = x0 + dx * t;
    const float y = y0 + dy * t;
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    const float fx = x - ix;
    const float fy = y - iy;
    put(pixels, width, height, rowSpan, ix, iy, color, alpha * (1.0f - fx) * (1.0f - fy));
    put(pixels, width, height, rowSpan, ix + 1, iy, color, alpha * fx * (1.0f - fy));
    put(pixels, width, height, rowSpan, ix, iy + 1, color, alpha * (1.0f - fx) * fy);
    put(pixels, width, height, rowSpan, ix + 1, iy + 1, color, alpha * fx * fy);
  }
}

// Draws one curve evaluated at every pixel column.  Returns false if empty.
inline void drawCurve(uint32_t* pixels, int width, int height, int rowSpan,
                      const InlineSnapshot& snap, const PlotRect& r,
                      bool live) {
  const float sampleRate = 48000.0f;
  const int x0 = static_cast<int>(std::ceil(r.left));
  const int x1 = static_cast<int>(std::floor(r.right));
  float prevX = 0.0f;
  float prevY = 0.0f;
  bool have = false;
  const uint32_t sumColor = argb(226, 236, 240);
  for (int x = x0; x <= x1; ++x) {
    const float f = logFreq(static_cast<float>(x) + 0.5f, r);
    const float db = totalResponseDb(snap.params, live ? snap.dynDb : nullptr,
                                     f, sampleRate);
    const float y = dbY(clamp(db, -30.0f, 30.0f), r);
    if (have) {
      line(pixels, width, height, rowSpan, prevX, prevY, static_cast<float>(x),
           y, sumColor, 0.95f);
    }
    prevX = static_cast<float>(x);
    prevY = y;
    have = true;
  }
}

class InlineDisplay {
public:
  struct Surface {
    unsigned char* data = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;
  };

  Surface* render(const InlineSnapshot& snap, uint32_t width,
                  uint32_t height) {
    if (width < 16 || height < 12) {
      return nullptr;
    }
    width = std::min(width, 2048u);
    height = std::min(height, 512u);
    const size_t count = static_cast<size_t>(width) * height;
    if (pixels_.size() != count) {
      pixels_.assign(count, 0xff0e1420u);
    }
    surface_.data = reinterpret_cast<unsigned char*>(pixels_.data());
    surface_.width = static_cast<int>(width);
    surface_.height = static_cast<int>(height);
    surface_.stride = static_cast<int>(width * 4);
    draw(snap, static_cast<int>(width), static_cast<int>(height));
    return &surface_;
  }

  static void draw(const InlineSnapshot& snap, int width, int height,
                   uint32_t* pixels, int rowSpan) {
    drawInto(snap, width, height, pixels, rowSpan);
  }

  static void drawInto(const InlineSnapshot& snap, int width, int height,
                       uint32_t* pixels, int rowSpan) {
    const uint32_t bg = argb(14, 20, 32);
    const uint32_t border = argb(62, 78, 104);
    const uint32_t grid = argb(38, 52, 76);
    const uint32_t zero = argb(92, 112, 144);
    const bool roomForGrid = height >= 34 && width >= 90;
    const PlotRect r = plotRect(width, height, roomForGrid);

    fillRect(pixels, width, height, rowSpan, 0, 0, width, height, bg, 1.0f);
    fillRect(pixels, width, height, rowSpan, 0, 0, width, 1, border, 0.9f);
    fillRect(pixels, width, height, rowSpan, 0, height - 1, width, height,
             border, 0.9f);
    fillRect(pixels, width, height, rowSpan, 0, 0, 1, height, border, 0.9f);
    fillRect(pixels, width, height, rowSpan, width - 1, 0, width, height,
             border, 0.9f);

    if (roomForGrid) {
      static constexpr float kFreqs[] = {30, 50, 100, 200, 500, 1000,
                                         2000, 5000, 10000};
      for (float f : kFreqs) {
        const int x = static_cast<int>(std::lround(freqX(f, r)));
        fillRect(pixels, width, height, rowSpan, x, static_cast<int>(r.top), x + 1,
                 static_cast<int>(r.bottom), grid, 0.55f);
      }
      for (float db : {-24.0f, -12.0f, 12.0f, 24.0f}) {
        const int y = static_cast<int>(std::lround(dbY(db, r)));
        fillRect(pixels, width, height, rowSpan, static_cast<int>(r.left), y,
                 static_cast<int>(r.right), y + 1, grid, 0.45f);
      }
    }

    const int zeroY = static_cast<int>(std::lround(dbY(0.0f, r)));
    fillRect(pixels, width, height, rowSpan, static_cast<int>(r.left), zeroY,
             static_cast<int>(r.right), zeroY + 1, zero, 0.9f);

    if (!snap.params.enabled) {
      line(pixels, width, height, rowSpan, r.left, zeroY, r.right, zeroY,
           argb(120, 132, 150), 0.8f);
      return;
    }

    if (snap.hasSpectrum && roomForGrid) {
      float prevX = 0.0f;
      float prevY = 0.0f;
      for (int i = 0; i < 32; ++i) {
        const float t = static_cast<float>(i) / 31.0f;
        const float f = 20.0f * std::pow(1000.0f, t);
        const float x = freqX(f, r);
        const float level = clamp((snap.spectrum[i] + 60.0f) / 60.0f, 0.0f, 1.0f);
        const float y = r.bottom - level * (r.bottom - r.top);
        if (i > 0) {
          line(pixels, width, height, rowSpan, prevX, prevY, x, y,
               argb(128, 148, 172), 0.35f);
        }
        prevX = x;
        prevY = y;
      }
    }

    // Filled body under the summed curve, from the curve to 0 dB.
    const int x0 = static_cast<int>(std::ceil(r.left));
    const int x1 = static_cast<int>(std::floor(r.right));
    const uint32_t fillColor = argb(95, 190, 150);
    for (int x = x0; x <= x1; ++x) {
      const float f = logFreq(static_cast<float>(x) + 0.5f, r);
      const float db = totalResponseDb(snap.params, snap.dynDb, f, 48000.0f);
      const int y = static_cast<int>(std::lround(dbY(clamp(db, -30.0f, 30.0f), r)));
      const int ya = std::min(y, zeroY);
      const int yb = std::max(y, zeroY);
      const float dist = std::fabs(static_cast<float>(y - zeroY));
      const float a = 0.10f + 0.10f * std::min(1.0f, dist / 40.0f);
      fillRect(pixels, width, height, rowSpan, x, ya, x + 1, yb, fillColor, a);
    }

    // Individual band curves in their Logic-like colours.
    if (width >= 110) {
      for (int b = 0; b < 8; ++b) {
        if (!snap.params.bands[b].on) {
          continue;
        }
        const uint32_t color = bandColor(b);
        float prevX = 0.0f;
        float prevY = 0.0f;
        bool have = false;
        for (int x = x0; x <= x1; ++x) {
          const float f = logFreq(static_cast<float>(x) + 0.5f, r);
          const BandParams& bp = snap.params.bands[b];
          const float db = bandResponseDb(
              shapeForBand(b), bp.freq, bp.gainDb + snap.dynDb[b],
              effectiveQ(snap.params, b), bp.slope, f, 48000.0f);
          const float y = dbY(clamp(db, -30.0f, 30.0f), r);
          if (have) {
            line(pixels, width, height, rowSpan, prevX, prevY,
                 static_cast<float>(x), y, color, 0.42f);
          }
          prevX = static_cast<float>(x);
          prevY = y;
          have = true;
        }
      }
    }

    // Static (set) curve when dynamics are moving the applied curve.
    bool anyDynamic = false;
    for (int b = 0; b < 8; ++b) {
      if (snap.params.bands[b].dynOn &&
          std::fabs(snap.dynDb[b]) > 0.05f) {
        anyDynamic = true;
      }
    }
    if (anyDynamic) {
      drawCurve(pixels, width, height, rowSpan, snap, r, false);
    }
    drawCurve(pixels, width, height, rowSpan, snap, r, true);

    // Band handles.
    if (width >= 90 && height >= 26) {
      for (int b = 0; b < 8; ++b) {
        const BandParams& bp = snap.params.bands[b];
        if (!bp.on) {
          continue;
        }
        const float x = freqX(bp.freq, r);
        const float y = dbY(clamp(bp.gainDb + snap.dynDb[b], -30.0f, 30.0f), r);
        const int rad = (b == 0 || b == 7) ? 1 : 2;
        for (int yy = -rad; yy <= rad; ++yy) {
          for (int xx = -rad; xx <= rad; ++xx) {
            put(pixels, width, height, rowSpan,
                static_cast<int>(std::lround(x)) + xx,
                static_cast<int>(std::lround(y)) + yy, bandColor(b), 0.95f);
          }
        }
        put(pixels, width, height, rowSpan,
            static_cast<int>(std::lround(x)),
            static_cast<int>(std::lround(y)), argb(245, 250, 252), 0.9f);
      }
    }
  }

  // Geometry used by Reaper's embedded mouse handling.
  static int hitBand(const InlineSnapshot& snap, int width, int height,
                     float mx, float my) {
    const PlotRect r = plotRect(width, height, height >= 34 && width >= 90);
    int best = -1;
    float bestDist = 14.0f * 14.0f;
    for (int b = 0; b < 8; ++b) {
      const BandParams& bp = snap.params.bands[b];
      if (!bp.on) {
        continue;
      }
      const float x = freqX(bp.freq, r);
      const float y = dbY(clamp(bp.gainDb + snap.dynDb[b], -30.0f, 30.0f), r);
      const float dx = mx - x;
      const float dy = my - y;
      const float d = dx * dx + dy * dy;
      if (d < bestDist) {
        bestDist = d;
        best = b;
      }
    }
    return best;
  }

  static float dbAtPoint(int width, int height, float my) {
    return yDb(my, plotRect(width, height, height >= 34 && width >= 90));
  }

  static float freqAtPoint(int width, int height, float mx) {
    return logFreq(mx, plotRect(width, height, height >= 34 && width >= 90));
  }

private:
  void draw(const InlineSnapshot& snap, int width, int height) {
    drawInto(snap, width, height, pixels_.data(), width);
  }

  std::vector<uint32_t> pixels_;
  Surface surface_;
};

} // namespace cse
