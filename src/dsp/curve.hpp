// Common Sense EQ - curve.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>
#include <complex>

#include "filters.hpp"
#include "params.hpp"

namespace cse {

// Magnitude of one band, in dB, evaluated analytically.  This is the same
// response the audio path implements: TPT SVF bells, RBJ shelves and cuts.
inline float bandResponseDb(BandShape shape, float f0, float gainDb, float q,
                            int slope, float freq, float sampleRate) noexcept {
  if (freq < 1.0f || f0 < 1.0f) {
    return 0.0f;
  }

  if (shape == kBell) {
    // TPT bell: H = 1 + (A - 1) * k * BP(s)
    const float a = dbToGain(gainDb);
    const float r = freq / f0;
    const float k = 1.0f / std::max(q, 0.05f);
    const std::complex<float> den(1.0f - r * r, k * r);
    const std::complex<float> bp(0.0f, r);
    const std::complex<float> h = 1.0f + (a - 1.0f) * k * (bp / den);
    return gainToDb(std::abs(h));
  }

  if (shape == kLowShelf || shape == kHighShelf) {
    BiquadCoeffs c;
    if (shape == kLowShelf) {
      c.setLowShelf(f0, gainDb, q, sampleRate);
    } else {
      c.setHighShelf(f0, gainDb, q, sampleRate);
    }
    return c.magnitudeDb(freq, sampleRate);
  }

  // Mirror the audio path's cut cascade: first-order sections for 6/18/30
  // and up to two second-order sections for 12/18/24/30.
  const bool lowCut = shape == kLowCut;
  const bool useFirst = slope == 6 || slope == 18 || slope == 30;
  const int secondOrder =
      (slope == 12) ? 1
      : (slope == 18) ? 1
      : (slope == 24) ? 2
      : (slope == 30) ? 2
                       : 0;
  float db = 0.0f;
  BiquadCoeffs c;
  if (useFirst) {
    if (lowCut) {
      c.setFirstOrderHighPass(f0, sampleRate);
    } else {
      c.setFirstOrderLowPass(f0, sampleRate);
    }
    db += c.magnitudeDb(freq, sampleRate);
  }
  for (int section = 0; section < secondOrder; ++section) {
    if (lowCut) {
      c.setHighPass(f0, 0.70710678f, sampleRate);
    } else {
      c.setLowPass(f0, 0.70710678f, sampleRate);
    }
    db += c.magnitudeDb(freq, sampleRate);
  }
  return db;
}

inline float bandResponseDb(const Params& p, int band, float freq,
                            float sampleRate) noexcept {
  const BandParams& b = p.bands[band];
  if (!b.on) {
    return 0.0f;
  }
  return bandResponseDb(shapeForBand(band), b.freq, b.gainDb, effectiveQ(p, band),
                        b.slope, freq, sampleRate);
}

// Static response of all enabled bands.  Dynamic gain is added separately by
// the caller so the UI can draw both the set curve and the applied curve.
inline float totalResponseDb(const Params& p, float freq,
                             float sampleRate) noexcept {
  float db = 0.0f;
  for (int i = 0; i < 8; ++i) {
    db += bandResponseDb(p, i, freq, sampleRate);
  }
  return db;
}

inline float totalResponseDb(const Params& p, const float* dynDb, float freq,
                             float sampleRate) noexcept {
  float db = 0.0f;
  for (int i = 0; i < 8; ++i) {
    const BandParams& b = p.bands[i];
    if (!b.on) {
      continue;
    }
    const float gain = b.gainDb + (dynDb ? dynDb[i] : 0.0f);
    db += bandResponseDb(shapeForBand(i), b.freq, gain, effectiveQ(p, i),
                         b.slope, freq, sampleRate);
  }
  return db;
}

} // namespace cse
