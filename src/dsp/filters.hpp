// Common Sense EQ - filters.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>
#include <complex>

#include "smoother.hpp"
#include "util.hpp"

namespace cse {

// Topology-preserving state-variable filter (Zavalishin).  Stable under
// modulation, which is what the dynamic bands need.
struct SVFCoeffs {
  float g = 0.0f;
  float k = 1.0f;
  float a1 = 0.0f;
  float a2 = 0.0f;
  float a3 = 0.0f;

  void set(float cutoff, float q, float sampleRate) noexcept {
    cutoff = clamp(cutoff, 5.0f, sampleRate * 0.49f);
    g = std::tan(kPi * cutoff / sampleRate);
    k = 1.0f / std::max(q, 0.05f);
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
  }
};

struct SVFState {
  float ic1 = 0.0f;
  float ic2 = 0.0f;

  void reset() noexcept { ic1 = ic2 = 0.0f; }

  inline void process(float x, const SVFCoeffs& c,
                      float& lp, float& bp, float& hp) noexcept {
    const float v3 = x - ic2;
    const float v1 = c.a1 * ic1 + c.a2 * v3;
    const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    if (std::fabs(ic1) < 1.0e-20f) {
      ic1 = 0.0f;
    }
    if (std::fabs(ic2) < 1.0e-20f) {
      ic2 = 0.0f;
    }
    lp = v2;
    bp = v1;
    hp = x - c.k * v1 - v2;
  }
};

// RBJ biquad.  Coefficients are shared between channels; state is per channel.
// At exactly 0 dB the shelving coefficients collapse to a true identity, so a
// flat band adds neither gain nor phase.
struct BiquadCoeffs {
  float b0 = 1.0f;
  float b1 = 0.0f;
  float b2 = 0.0f;
  float a1 = 0.0f;
  float a2 = 0.0f;

  void identity() noexcept {
    b0 = 1.0f;
    b1 = b2 = a1 = a2 = 0.0f;
  }

  void setLowShelf(float f0, float gainDb, float q,
                   float sampleRate) noexcept {
    if (std::fabs(gainDb) < 1.0e-5f) {
      identity();
      return;
    }
    const float a = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * kPi * clamp(f0, 10.0f, sampleRate * 0.49f) /
                     sampleRate;
    const float cosw = std::cos(w0);
    const float sinw = std::sin(w0);
    const float alpha = sinw / (2.0f * std::max(q, 0.05f));
    const float sqrtA = std::sqrt(a);
    const float a0 = (a + 1.0f) + (a - 1.0f) * cosw + 2.0f * sqrtA * alpha;
    b0 = a * ((a + 1.0f) - (a - 1.0f) * cosw + 2.0f * sqrtA * alpha) / a0;
    b1 = 2.0f * a * ((a - 1.0f) - (a + 1.0f) * cosw) / a0;
    b2 = a * ((a + 1.0f) - (a - 1.0f) * cosw - 2.0f * sqrtA * alpha) / a0;
    a1 = -2.0f * ((a - 1.0f) + (a + 1.0f) * cosw) / a0;
    a2 = ((a + 1.0f) + (a - 1.0f) * cosw - 2.0f * sqrtA * alpha) / a0;
  }

  void setHighShelf(float f0, float gainDb, float q,
                    float sampleRate) noexcept {
    if (std::fabs(gainDb) < 1.0e-5f) {
      identity();
      return;
    }
    const float a = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * kPi * clamp(f0, 10.0f, sampleRate * 0.49f) /
                     sampleRate;
    const float cosw = std::cos(w0);
    const float sinw = std::sin(w0);
    const float alpha = sinw / (2.0f * std::max(q, 0.05f));
    const float sqrtA = std::sqrt(a);
    const float a0 = (a + 1.0f) - (a - 1.0f) * cosw + 2.0f * sqrtA * alpha;
    b0 = a * ((a + 1.0f) + (a - 1.0f) * cosw + 2.0f * sqrtA * alpha) / a0;
    b1 = -2.0f * a * ((a - 1.0f) + (a + 1.0f) * cosw) / a0;
    b2 = a * ((a + 1.0f) + (a - 1.0f) * cosw - 2.0f * sqrtA * alpha) / a0;
    a1 = 2.0f * ((a - 1.0f) - (a + 1.0f) * cosw) / a0;
    a2 = ((a + 1.0f) - (a - 1.0f) * cosw - 2.0f * sqrtA * alpha) / a0;
  }

  // First-order sections, expressed as biquads with b2 = a2 = 0.  Used for
  // the 6, 18, and 30 dB/oct cut slopes.
  void setFirstOrderLowPass(float f0, float sampleRate) noexcept {
    const float cutoff = clamp(f0, 5.0f, sampleRate * 0.49f);
    const float g = std::tan(kPi * cutoff / sampleRate);
    const float G = g / (1.0f + g);
    b0 = G;
    b1 = G;
    b2 = 0.0f;
    a1 = 2.0f * G - 1.0f;
    a2 = 0.0f;
  }

  void setFirstOrderHighPass(float f0, float sampleRate) noexcept {
    const float cutoff = clamp(f0, 5.0f, sampleRate * 0.49f);
    const float g = std::tan(kPi * cutoff / sampleRate);
    const float G = g / (1.0f + g);
    b0 = 1.0f - G;
    b1 = -(1.0f - G);
    b2 = 0.0f;
    a1 = 2.0f * G - 1.0f;
    a2 = 0.0f;
  }

  void setHighPass(float f0, float q, float sampleRate) noexcept {
    const float w0 = 2.0f * kPi * clamp(f0, 5.0f, sampleRate * 0.49f) /
                     sampleRate;
    const float cosw = std::cos(w0);
    const float alpha = std::sin(w0) / (2.0f * std::max(q, 0.05f));
    const float a0 = 1.0f + alpha;
    b0 = (1.0f + cosw) * 0.5f / a0;
    b1 = -(1.0f + cosw) / a0;
    b2 = b0;
    a1 = -2.0f * cosw / a0;
    a2 = (1.0f - alpha) / a0;
  }

  void setLowPass(float f0, float q, float sampleRate) noexcept {
    const float w0 = 2.0f * kPi * clamp(f0, 5.0f, sampleRate * 0.49f) /
                     sampleRate;
    const float cosw = std::cos(w0);
    const float alpha = std::sin(w0) / (2.0f * std::max(q, 0.05f));
    const float a0 = 1.0f + alpha;
    b0 = (1.0f - cosw) * 0.5f / a0;
    b1 = (1.0f - cosw) / a0;
    b2 = b0;
    a1 = -2.0f * cosw / a0;
    a2 = (1.0f - alpha) / a0;
  }

  float magnitudeDb(float freq, float sampleRate) const noexcept {
    const float w = 2.0f * kPi * std::max(freq, 1.0f) / sampleRate;
    const std::complex<float> z1(std::cos(w), -std::sin(w));
    const std::complex<float> z2 = z1 * z1;
    const std::complex<float> num = b0 + b1 * z1 + b2 * z2;
    const std::complex<float> den = 1.0f + a1 * z1 + a2 * z2;
    return gainToDb(std::abs(num) / std::max(std::abs(den), 1.0e-12f));
  }
};

struct BiquadState {
  float x1 = 0.0f;
  float x2 = 0.0f;
  float y1 = 0.0f;
  float y2 = 0.0f;

  void reset() noexcept { x1 = x2 = y1 = y2 = 0.0f; }

  inline float process(float x, const BiquadCoeffs& c) noexcept {
    const float y = c.b0 * x + c.b1 * x1 + c.b2 * x2 - c.a1 * y1 - c.a2 * y2;
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    if (std::fabs(y1) < 1.0e-20f) {
      y1 = 0.0f;
    }
    if (std::fabs(y2) < 1.0e-20f) {
      y2 = 0.0f;
    }
    return y;
  }
};

} // namespace cse
