// Common Sense EQ - analyzer.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>

#include "util.hpp"

namespace cse {

// Small in-place radix-2 FFT analyzer.  Fixed size, no allocation in the
// audio thread.  Publishes 32 log-spaced bands from 20 Hz to 20 kHz.
class Analyzer {
public:
  static constexpr uint32_t kFftSize = 2048;
  static constexpr uint32_t kHop = 512;
  static constexpr uint32_t kBins = 32;

  void prepare(float sampleRate) noexcept {
    sampleRate_ = sampleRate;
    for (uint32_t i = 0; i < kFftSize; ++i) {
      window_[i] = 0.5f - 0.5f * std::cos(2.0f * kPi * i /
                                          static_cast<float>(kFftSize - 1));
    }
    // Log-spaced display edges.
    const float lo = 20.0f;
    const float hi = std::min(20000.0f, sampleRate * 0.45f);
    for (uint32_t b = 0; b <= kBins; ++b) {
      const float t = static_cast<float>(b) / static_cast<float>(kBins);
      const float f = lo * std::pow(hi / lo, t);
      edges_[b] = f;
    }
    reset();
  }

  void reset() noexcept {
    std::memset(buffer_, 0, sizeof(buffer_));
    index_ = 0;
    for (uint32_t b = 0; b < kBins; ++b) {
      spectrum_[b] = -100.0f;
    }
    primed_ = false;
  }

  inline void push(float left, float right) noexcept {
    buffer_[index_++] = 0.5f * (left + right);
    if (index_ >= kFftSize) {
      compute();
      std::memmove(buffer_, buffer_ + kHop,
                   (kFftSize - kHop) * sizeof(float));
      index_ = kFftSize - kHop;
    }
  }

  const float* spectrum() const noexcept { return spectrum_; }
  bool primed() const noexcept { return primed_; }

private:
  void compute() noexcept {
    for (uint32_t i = 0; i < kFftSize; ++i) {
      re_[i] = buffer_[i] * window_[i];
      im_[i] = 0.0f;
    }
    fft();

    const float binHz = sampleRate_ / static_cast<float>(kFftSize);
    float fresh[kBins];
    for (uint32_t b = 0; b < kBins; ++b) {
      const int first = static_cast<int>(std::floor(edges_[b] / binHz));
      const int last = static_cast<int>(std::ceil(edges_[b + 1] / binHz));
      const int lo = clamp(first, 1, static_cast<int>(kFftSize / 2) - 1);
      const int hi = clamp(last, lo + 1, static_cast<int>(kFftSize / 2));
      float peak = 0.0f;
      for (int k = lo; k < hi; ++k) {
        const float mag = re_[k] * re_[k] + im_[k] * im_[k];
        peak = std::max(peak, mag);
      }
      const float amp = 4.0f * std::sqrt(peak) / static_cast<float>(kFftSize);
      fresh[b] = clamp(gainToDb(amp), -100.0f, 6.0f);
    }

    for (uint32_t b = 0; b < kBins; ++b) {
      // Fast attack, slow release, so transients read clearly.
      const float coeff = fresh[b] > spectrum_[b] ? 0.65f : 0.07f;
      spectrum_[b] += coeff * (fresh[b] - spectrum_[b]);
    }
    primed_ = true;
  }

  void fft() noexcept {
    for (uint32_t i = 1, j = 0; i < kFftSize; ++i) {
      uint32_t bit = kFftSize >> 1;
      for (; j & bit; bit >>= 1) {
        j ^= bit;
      }
      j ^= bit;
      if (i < j) {
        std::swap(re_[i], re_[j]);
        std::swap(im_[i], im_[j]);
      }
    }
    for (uint32_t len = 2; len <= kFftSize; len <<= 1) {
      const float ang = -2.0f * kPi / static_cast<float>(len);
      const float wlenRe = std::cos(ang);
      const float wlenIm = std::sin(ang);
      for (uint32_t i = 0; i < kFftSize; i += len) {
        float wr = 1.0f;
        float wi = 0.0f;
        for (uint32_t k = 0; k < len / 2; ++k) {
          const float ur = re_[i + k];
          const float ui = im_[i + k];
          const float vr = re_[i + k + len / 2] * wr -
                           im_[i + k + len / 2] * wi;
          const float vi = re_[i + k + len / 2] * wi +
                           im_[i + k + len / 2] * wr;
          re_[i + k] = ur + vr;
          im_[i + k] = ui + vi;
          re_[i + k + len / 2] = ur - vr;
          im_[i + k + len / 2] = ui - vi;
          const float nextWr = wr * wlenRe - wi * wlenIm;
          wi = wr * wlenIm + wi * wlenRe;
          wr = nextWr;
        }
      }
    }
  }

  float sampleRate_ = 48000.0f;
  float window_[kFftSize] = {};
  float buffer_[kFftSize] = {};
  float re_[kFftSize] = {};
  float im_[kFftSize] = {};
  float edges_[kBins + 1] = {};
  float spectrum_[kBins] = {};
  uint32_t index_ = 0;
  bool primed_ = false;
};

} // namespace cse
