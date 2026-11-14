// Common Sense EQ - strip.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

#include "analyzer.hpp"
#include "eq_band.hpp"
#include "params.hpp"
#include "smoother.hpp"

namespace cse {

// Per-band dynamic detector.  A band-pass at the band frequency feeds a
// stereo-linked peak envelope; the envelope drives a signed gain offset that
// is added to the band's static gain.
struct DynUnit {
  void prepare(float sampleRate) noexcept {
    sampleRate_ = sampleRate;
    delta_.setTime(0.5f, sampleRate);
    delta_.reset(0.0f);
    for (auto& s : state_) {
      s.reset();
    }
    env_ = 0.0f;
  }

  void configure(float freq, float q, const BandParams& p,
                 float sampleRate) noexcept {
    sampleRate_ = sampleRate;
    if (!prepared_ || std::fabs(freq - freq_) > 0.01f ||
        std::fabs(q - q_) > 0.001f) {
      freq_ = freq;
      q_ = q;
      coeffs_.set(freq, q, sampleRate);
      prepared_ = true;
    }
    thresholdDb_ = p.dynThresholdDb;
    ratio_ = std::max(p.dynRatio, 1.0f);
    rangeDb_ = std::max(p.dynRangeDb, 0.0f);
    mode_ = p.dynMode;
    enabled_ = p.dynOn;
    attackCoef_ = std::exp(-1.0f / std::max(0.05f, p.dynAttackMs) /
                           (sampleRate * 0.001f));
    releaseCoef_ = std::exp(-1.0f / std::max(1.0f, p.dynReleaseMs) /
                            (sampleRate * 0.001f));
    if (!enabled_) {
      env_ = 0.0f;
      delta_.setTarget(0.0f);
    }
  }

  inline float process(float left, float right) noexcept {
    if (!enabled_) {
      delta_.setTarget(0.0f);
      delta_.next();
      return 0.0f;
    }
    float lp, bp, hp;
    state_[0].process(left, coeffs_, lp, bp, hp);
    const float bpL = bp * coeffs_.k;
    state_[1].process(right, coeffs_, lp, bp, hp);
    const float bpR = bp * coeffs_.k;
    const float power = std::max(bpL * bpL, bpR * bpR);
    const float coeff = power > env_ ? attackCoef_ : releaseCoef_;
    env_ = power + coeff * (env_ - power);
    if (env_ < 1.0e-14f) {
      env_ = 0.0f;
    }

    const float levelDb = gainToDb(std::sqrt(env_));
    const float over = levelDb - thresholdDb_;
    float amount = 0.0f;
    if (over > 0.0f) {
      amount = over * (1.0f - 1.0f / ratio_);
      amount = clamp(amount, 0.0f, rangeDb_);
    }
    const float target = (mode_ == 0) ? -amount : amount;
    delta_.setTarget(target);
    return delta_.next();
  }

  void reset() noexcept {
    for (auto& s : state_) {
      s.reset();
    }
    env_ = 0.0f;
    delta_.reset(0.0f);
  }

  float gainDb() const noexcept { return delta_.current(); }
  bool enabled() const noexcept { return enabled_; }

private:
  float sampleRate_ = 48000.0f;
  SVFCoeffs coeffs_;
  SVFState state_[2];
  float freq_ = 1000.0f;
  float q_ = 1.0f;
  float env_ = 0.0f;
  float thresholdDb_ = -24.0f;
  float ratio_ = 4.0f;
  float rangeDb_ = 6.0f;
  int mode_ = 0;
  float attackCoef_ = 0.0f;
  float releaseCoef_ = 0.0f;
  bool enabled_ = false;
  bool prepared_ = false;
  Smoothed delta_;
};

class Strip {
public:
  void prepare(float sampleRate) noexcept {
    sampleRate_ = sampleRate;
    outGain_.setTime(15.0f, sampleRate);
    for (int i = 0; i < 8; ++i) {
      const BandShape shape = shapeForBand(i);
      bands_[i].prepare(sampleRate, shape, kDefaultFreq[i], 0.0f, kDefaultQ[i],
                        kDefaultSlope[i]);
      dyn_[i].prepare(sampleRate);
      dynDb_[i] = 0.0f;
      wasOn_[i] = true;
    }
    outGain_.reset(0.0f);
    analyzer_.prepare(sampleRate);
    reset();
  }

  void setTargets(const Params& p) noexcept {
    p_ = p;
    for (int i = 0; i < 8; ++i) {
      const BandParams& b = p.bands[i];
      if (b.on && !wasOn_[i]) {
        bands_[i].reset();
        dyn_[i].reset();
      }
      wasOn_[i] = b.on;
      bands_[i].setTargets(b.freq, b.gainDb, effectiveQ(p, i), b.slope);
      dyn_[i].configure(b.freq, std::max(0.6f, std::min(effectiveQ(p, i), 4.0f)),
                        b, sampleRate_);
    }
    outGain_.setTarget(p.enabled ? p.outputGainDb : 0.0f);
  }

  void reset() noexcept {
    for (auto& b : bands_) {
      b.reset();
    }
    for (auto& d : dyn_) {
      d.reset();
    }
    for (int i = 0; i < 8; ++i) {
      dynDb_[i] = 0.0f;
    }
    analyzer_.reset();
    outGain_.snap();
  }

  void process(float* left, float* right, uint32_t frames) noexcept {
    if (!left || !right) {
      return;
    }
    if (!p_.enabled) {
      for (uint32_t n = 0; n < frames; ++n) {
        const float l = sanitize(left[n]);
        const float r = sanitize(right[n]);
        analyzer_.push(l, r);
      }
      for (int i = 0; i < 8; ++i) {
        dynDb_[i] = 0.0f;
      }
      return;
    }

    // Analyzer PRE taps the untouched input.
    if (p_.analyzerMode == 0) {
      for (uint32_t n = 0; n < frames; ++n) {
        analyzer_.push(left[n], right[n]);
      }
    }

    const int mode = p_.processing;
    if (mode == 1) {
      // Mono: collapse to one channel, process, and feed both outputs.
      for (uint32_t n = 0; n < frames; ++n) {
        const float m = 0.5f * (sanitize(left[n]) + sanitize(right[n]));
        left[n] = m;
        right[n] = m;
      }
      applyBands(left, right, frames, true);
      for (uint32_t n = 0; n < frames; ++n) {
        const float y = left[n] * dbToGain(outGain_.next());
        left[n] = y;
        right[n] = y;
      }
    } else if (mode == 2 || mode == 3) {
      // Left or right only; the other channel passes through.
      float* active = (mode == 2) ? left : right;
      float* other = (mode == 2) ? right : left;
      for (uint32_t n = 0; n < frames; ++n) {
        active[n] = sanitize(active[n]);
        other[n] = sanitize(other[n]);
      }
      applyBands(active, active, frames, false);
      for (uint32_t n = 0; n < frames; ++n) {
        const float g = dbToGain(outGain_.next());
        active[n] *= g;
        other[n] *= g;
      }
    } else {
      for (uint32_t n = 0; n < frames; ++n) {
        left[n] = sanitize(left[n]);
        right[n] = sanitize(right[n]);
      }
      applyBands(left, right, frames, true);
      for (uint32_t n = 0; n < frames; ++n) {
        const float g = dbToGain(outGain_.next());
        left[n] *= g;
        right[n] *= g;
      }
    }

    if (p_.analyzerMode == 1) {
      for (uint32_t n = 0; n < frames; ++n) {
        analyzer_.push(left[n], right[n]);
      }
    }
  }

  float dynDb(int band) const noexcept {
    return (band >= 0 && band < 8) ? dynDb_[band] : 0.0f;
  }

  const float* spectrum() const noexcept { return analyzer_.spectrum(); }
  uint32_t spectrumBins() const noexcept { return Analyzer::kBins; }
  bool spectrumPrimed() const noexcept { return analyzer_.primed(); }

private:
  static float sanitize(float x) noexcept {
    return std::isfinite(x) ? x : 0.0f;
  }

  // `left`/`right` are modified in place.  `linked` selects whether the
  // detector sees both channels or just the active one.
  void applyBands(float* left, float* right, uint32_t frames,
                  bool linked) noexcept {
    if (frames == 0) {
      return;
    }
    for (int b = 0; b < 8; ++b) {
      const BandParams& bp = p_.bands[b];
      if (!bp.on) {
        bands_[b].setTargets(bp.freq, bp.gainDb, effectiveQ(p_, b), bp.slope);
        bands_[b].setDynamicDb(0.0f);
        continue;
      }
      float lastDyn = 0.0f;
      for (uint32_t n = 0; n < frames; ++n) {
        const float dl = left[n];
        const float dr = linked ? right[n] : left[n];
        const float dyn = dyn_[b].process(dl, dr);
        lastDyn = dyn;
        bands_[b].setDynamicDb(dyn);
        bands_[b].tick();
        if ((n & EqBand::kUpdateMask) == 0) {
          bands_[b].update();
        }
        left[n] = bands_[b].process(0, left[n]);
        if (linked) {
          right[n] = bands_[b].process(1, right[n]);
        }
      }
      dynDb_[b] = lastDyn;
    }
  }

  float sampleRate_ = 48000.0f;
  Params p_;
  EqBand bands_[8];
  DynUnit dyn_[8];
  Analyzer analyzer_;
  Smoothed outGain_;
  float dynDb_[8] = {};
  bool wasOn_[8] = {};
};

} // namespace cse
