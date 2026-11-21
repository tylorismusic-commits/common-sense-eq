// Common Sense EQ - params.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>

#include "util.hpp"

namespace cse {

enum BandShape : int {
  kLowCut = 0,
  kLowShelf = 1,
  kBell = 2,
  kHighShelf = 3,
  kHighCut = 4,
};

inline BandShape shapeForBand(int band) noexcept {
  static constexpr BandShape shapes[8] = {kLowCut, kLowShelf, kBell, kBell,
                                          kBell,   kBell,     kHighShelf,
                                          kHighCut};
  return shapes[clamp(band, 0, 7)];
}

inline bool bandHasGain(int band) noexcept {
  const BandShape s = shapeForBand(band);
  return s == kLowShelf || s == kBell || s == kHighShelf;
}

inline bool bandHasQ(int band) noexcept {
  const BandShape s = shapeForBand(band);
  return s == kLowShelf || s == kBell || s == kHighShelf;
}

inline bool bandIsCut(int band) noexcept {
  const BandShape s = shapeForBand(band);
  return s == kLowCut || s == kHighCut;
}

inline constexpr float kDefaultFreq[8] = {20.0f, 75.0f, 100.0f, 270.0f,
                                           750.0f, 2500.0f, 4600.0f, 20000.0f};
inline constexpr float kDefaultQ[8] = {1.0f, 1.0f, 1.0f, 1.0f,
                                       1.0f, 1.0f, 1.0f, 1.0f};
inline constexpr int kDefaultSlope[8] = {24, 24, 24, 24, 24, 24, 24, 24};

struct BandParams {
  bool on = true;
  float freq = 1000.0f;
  float gainDb = 0.0f;
  float q = 0.70710678f;
  int slope = 24;
  bool dynOn = false;
  float dynThresholdDb = -24.0f;
  float dynRatio = 4.0f;
  float dynAttackMs = 10.0f;
  float dynReleaseMs = 150.0f;
  float dynRangeDb = 6.0f;
  int dynMode = 0; // 0 = down, 1 = up
};

struct Params {
  bool enabled = true;
  float outputGainDb = 0.0f;
  int analyzerMode = 1; // 0 = pre, 1 = post
  bool qCouple = false;
  int processing = 0; // 0 stereo, 1 mono, 2 left, 3 right
  BandParams bands[8];
};

// Q-Couple widens the bell/shelf as gain increases, like the Logic original.
inline float effectiveQ(const Params& p, int band) noexcept {
  const float q = clamp(p.bands[band].q, 0.1f, 25.0f);
  if (!p.qCouple || !bandHasGain(band)) {
    return q;
  }
  const float gain = std::fabs(p.bands[band].gainDb);
  return clamp(q * (1.0f + 0.10f * gain), 0.1f, 25.0f);
}

} // namespace cse
