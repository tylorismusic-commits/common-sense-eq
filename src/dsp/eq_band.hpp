// Common Sense EQ - eq_band.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>

#include "filters.hpp"
#include "params.hpp"
#include "smoother.hpp"
#include "util.hpp"

namespace cse {

// One EQ band.  Bells use a TPT SVF so the dynamic gain can move every
// sample without recomputing the filter; shelves and cuts use RBJ biquads
// whose coefficients are refreshed every 16 samples while the smoothers run
// per sample.
class EqBand {
public:
  static constexpr uint32_t kUpdateMask = 15;

  void prepare(float sampleRate, BandShape shape, float freq, float gainDb,
               float q, int slope) noexcept {
    sampleRate_ = sampleRate;
    shape_ = shape;
    freq_.setTime(20.0f, sampleRate);
    gain_.setTime(20.0f, sampleRate);
    dyn_.setTime(1.0f, sampleRate);
    q_.setTime(20.0f, sampleRate);
    freq_.reset(clamp(freq, 20.0f, 20000.0f));
    gain_.reset(gainDb);
    dyn_.reset(0.0f);
    q_.reset(clamp(q, 0.1f, 25.0f));
    slope_ = slope;
    for (auto& s : bellState_) {
      s.reset();
    }
    for (auto& s : bq1State_) {
      s.reset();
    }
    for (auto& s : bq2State_) {
      s.reset();
    }
    for (auto& s : bq3State_) {
      s.reset();
    }
    update();
  }

  void setTargets(float freq, float gainDb, float q, int slope) noexcept {
    freq_.setTarget(clamp(freq, 20.0f, 20000.0f));
    gain_.setTarget(gainDb);
    q_.setTarget(clamp(q, 0.1f, 25.0f));
    slope_ = slope;
  }

  // Extra gain from the dynamic detector, in dB (negative attenuates).
  void setDynamicDb(float db) noexcept { dyn_.setTarget(db); }
  void snapDynamic() noexcept { dyn_.snap(); }

  void tick() noexcept {
    freq_.next();
    gain_.next();
    dyn_.next();
    q_.next();
  }

  void update() noexcept {
    const float f = freq_.current();
    const float g = gain_.current() + dyn_.current();
    const float q = std::max(q_.current(), 0.05f);
    if (shape_ == kBell) {
      coeffs_.set(f, q, sampleRate_);
      a_ = dbToGain(g);
    } else if (shape_ == kLowShelf) {
      shelf_.setLowShelf(f, g, q, sampleRate_);
    } else if (shape_ == kHighShelf) {
      shelf_.setHighShelf(f, g, q, sampleRate_);
    } else {
      const bool lowCut = shape_ == kLowCut;
      const bool useFirst = slope_ == 6 || slope_ == 18 || slope_ == 30;
      const int secondOrder =
          (slope_ == 12) ? 1
          : (slope_ == 18) ? 1
          : (slope_ == 24) ? 2
          : (slope_ == 30) ? 2
                           : 0;
      if (useFirst) {
        if (lowCut) {
          cut1_.setFirstOrderHighPass(f, sampleRate_);
        } else {
          cut1_.setFirstOrderLowPass(f, sampleRate_);
        }
      } else {
        cut1_.identity();
      }
      if (secondOrder >= 1) {
        if (lowCut) {
          cut2_.setHighPass(f, 0.70710678f, sampleRate_);
        } else {
          cut2_.setLowPass(f, 0.70710678f, sampleRate_);
        }
      } else {
        cut2_.identity();
      }
      if (secondOrder >= 2) {
        if (lowCut) {
          cut3_.setHighPass(f, 0.70710678f, sampleRate_);
        } else {
          cut3_.setLowPass(f, 0.70710678f, sampleRate_);
        }
      } else {
        cut3_.identity();
      }
    }
  }

  inline float process(int channel, float x) noexcept {
    if (shape_ == kBell) {
      float lp, bp, hp;
      bellState_[channel].process(x, coeffs_, lp, bp, hp);
      return x + (a_ - 1.0f) * coeffs_.k * bp;
    }
    if (shape_ == kLowShelf || shape_ == kHighShelf) {
      return bq1State_[channel].process(x, shelf_);
    }
    float y = bq1State_[channel].process(x, cut1_);
    y = bq2State_[channel].process(y, cut2_);
    y = bq3State_[channel].process(y, cut3_);
    return y;
  }

  void reset() noexcept {
    for (auto& s : bellState_) {
      s.reset();
    }
    for (auto& s : bq1State_) {
      s.reset();
    }
    for (auto& s : bq2State_) {
      s.reset();
    }
    for (auto& s : bq3State_) {
      s.reset();
    }
    freq_.snap();
    gain_.snap();
    dyn_.snap();
    q_.snap();
    update();
  }

  float frequency() const noexcept { return freq_.current(); }
  float gainDb() const noexcept { return gain_.current(); }
  float dynamicDb() const noexcept { return dyn_.current(); }
  float q() const noexcept { return q_.current(); }
  BandShape shape() const noexcept { return shape_; }

  // Current effective response, matching the audio path.
  float magnitudeDb(float freq) const noexcept {
    if (shape_ == kBell) {
      const float f0 = freq_.current();
      if (f0 < 1.0f || freq < 1.0f) {
        return 0.0f;
      }
      const float r = freq / f0;
      const float k = 1.0f / std::max(q_.current(), 0.05f);
      const std::complex<float> den(1.0f - r * r, k * r);
      const std::complex<float> bp(0.0f, r);
      const std::complex<float> h = 1.0f + (a_ - 1.0f) * k * (bp / den);
      return gainToDb(std::abs(h));
    }
    if (shape_ == kLowShelf || shape_ == kHighShelf) {
      return shelf_.magnitudeDb(freq, sampleRate_);
    }
    return cut1_.magnitudeDb(freq, sampleRate_) +
           cut2_.magnitudeDb(freq, sampleRate_) +
           cut3_.magnitudeDb(freq, sampleRate_);
  }

private:
  float sampleRate_ = 48000.0f;
  BandShape shape_ = kBell;
  int slope_ = 24;
  SVFCoeffs coeffs_;
  BiquadCoeffs shelf_;
  BiquadCoeffs cut1_;
  BiquadCoeffs cut2_;
  BiquadCoeffs cut3_;
  SVFState bellState_[2];
  BiquadState bq1State_[2];
  BiquadState bq2State_[2];
  BiquadState bq3State_[2];
  Smoothed freq_;
  Smoothed gain_;
  Smoothed dyn_;
  Smoothed q_;
  float a_ = 1.0f;
};

} // namespace cse
