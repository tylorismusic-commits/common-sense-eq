// Common Sense EQ - DSP tests
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

#include "dsp/analyzer.hpp"
#include "dsp/curve.hpp"
#include "dsp/inline_curve.hpp"
#include "dsp/params.hpp"
#include "dsp/strip.hpp"

namespace {

using cse::Params;

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

cse::Params flatParams() {
  cse::Params p;
  for (int b = 0; b < 8; ++b) {
    p.bands[b].on = false;
  }
  return p;
}

std::vector<float> impulseResponse(cse::Strip& strip, const cse::Params& p,
                                   int length) {
  strip.prepare(48000.0f);
  strip.setTargets(p);
  strip.reset();
  std::vector<float> left(length, 0.0f);
  std::vector<float> right(length, 0.0f);
  left[0] = 1.0f;
  right[0] = 1.0f;
  const int block = 128;
  for (int i = 0; i < length; i += block) {
    const int n = std::min(block, length - i);
    strip.process(left.data() + i, right.data() + i, n);
  }
  return left;
}

double dftDb(const std::vector<float>& x, double freq, double fs) {
  const double w = 2.0 * M_PI * freq / fs;
  std::complex<double> acc(0.0, 0.0);
  for (size_t i = 0; i < x.size(); ++i) {
    acc += static_cast<double>(x[i]) *
           std::exp(std::complex<double>(0.0, -w * static_cast<double>(i)));
  }
  return 20.0 * std::log10(std::max(std::abs(acc), 1.0e-12));
}

} // namespace

int main() {
  // Flat: every band off, unity gain, output must equal input.
  {
    cse::Strip strip;
    cse::Params p = flatParams();
    std::vector<float> l(512), r(512);
    for (int i = 0; i < 512; ++i) {
      l[i] = 0.25f * std::sin(i * 0.03f);
      r[i] = -0.15f * std::cos(i * 0.021f);
    }
    std::vector<float> inL = l;
    std::vector<float> inR = r;
    strip.prepare(48000.0f);
    strip.setTargets(p);
    strip.reset();
    strip.process(l.data(), r.data(), 512);
    float err = 0.0f;
    for (int i = 0; i < 512; ++i) {
      err = std::max(err, std::fabs(l[i] - inL[i]));
      err = std::max(err, std::fabs(r[i] - inR[i]));
    }
    check(err < 1.0e-6f, "flat chain is transparent");
  }

  // Bell response matches the analytic curve.
  {
    cse::Strip strip;
    cse::Params p = flatParams();
    const int band = 4;
    p.bands[band].on = true;
    p.bands[band].freq = 1000.0f;
    p.bands[band].gainDb = 6.0f;
    p.bands[band].q = 1.0f;
    const auto ir = impulseResponse(strip, p, 8192);
    for (double f : {100.0, 500.0, 1000.0, 2000.0, 5000.0}) {
      const double measured = dftDb(ir, f, 48000.0);
      const double expected = cse::bandResponseDb(
          cse::kBell, 1000.0f, 6.0f, 1.0f, 24, static_cast<float>(f),
          48000.0f);
      check(std::fabs(measured - expected) < 0.6,
            "bell magnitude matches analytic response");
    }
  }

  // Low cut reaches the expected slope.
  {
    cse::Strip strip;
    cse::Params p = flatParams();
    p.bands[0].on = true;
    p.bands[0].freq = 100.0f;
    p.bands[0].slope = 24;
    const auto ir = impulseResponse(strip, p, 8192);
    const double at50 = dftDb(ir, 25.0, 48000.0);
    check(at50 < -20.0, "24 dB/oct low cut attenuates below cutoff");
    const double at1k = dftDb(ir, 1000.0, 48000.0);
    check(std::fabs(at1k) < 0.25, "low cut passes the top end");
  }

  // Every cut slope option has the expected relative steepness.
  {
    double previous = 0.0;
    bool monotonic = true;
    for (int slope : {6, 12, 18, 24, 30}) {
      cse::Strip strip;
      cse::Params p = flatParams();
      p.bands[0].on = true;
      p.bands[0].freq = 100.0f;
      p.bands[0].slope = slope;
      const auto ir = impulseResponse(strip, p, 8192);
      const double at25 = dftDb(ir, 25.0, 48000.0);
      const double at1k = dftDb(ir, 1000.0, 48000.0);
      std::printf("low cut %d dB/oct: 25 Hz %.1f dB, 1 kHz %.1f dB\n", slope,
                  at25, at1k);
      check(at1k > -0.5, "cut passes the top end at every slope");
      if (slope > 6 && at25 > previous) {
        monotonic = false;
      }
      previous = at25;
    }
    check(monotonic, "steeper cut slopes attenuate more");
  }

  // Dynamics reduce the band gain on a loud tone and release on silence.
  {
    cse::Strip strip;
    cse::Params p = flatParams();
    const int band = 5;
    p.bands[band].on = true;
    p.bands[band].freq = 1500.0f;
    p.bands[band].gainDb = 6.0f;
    p.bands[band].q = 1.2f;
    p.bands[band].dynOn = true;
    p.bands[band].dynThresholdDb = -30.0f;
    p.bands[band].dynRatio = 6.0f;
    p.bands[band].dynAttackMs = 1.0f;
    p.bands[band].dynReleaseMs = 100.0f;
    p.bands[band].dynRangeDb = 9.0f;
    strip.prepare(48000.0f);
    strip.setTargets(p);
    strip.reset();
    std::vector<float> l(512), r(512);
    for (int block = 0; block < 80; ++block) {
      for (int i = 0; i < 512; ++i) {
        l[i] = r[i] = 0.8f * std::sin(2.0 * M_PI * 1500.0 * (block * 512 + i) /
                                      48000.0);
      }
      strip.process(l.data(), r.data(), 512);
    }
    check(strip.dynDb(band) < -1.0f, "dynamic band reduces on a loud tone");
    for (int block = 0; block < 80; ++block) {
      std::fill(l.begin(), l.end(), 0.0f);
      std::fill(r.begin(), r.end(), 0.0f);
      strip.process(l.data(), r.data(), 512);
    }
    check(std::fabs(strip.dynDb(band)) < 0.5f,
          "dynamic band releases on silence");
  }

  // Dynamic gain actually changes the audio, not just the meter.
  {
    auto runLevel = [](bool dynamic) {
      cse::Strip strip;
      cse::Params p = flatParams();
      const int band = 4;
      p.bands[band].on = true;
      p.bands[band].freq = 1500.0f;
      p.bands[band].gainDb = 6.0f;
      p.bands[band].q = 1.2f;
      p.bands[band].dynOn = dynamic;
      p.bands[band].dynThresholdDb = -30.0f;
      p.bands[band].dynRatio = 6.0f;
      p.bands[band].dynAttackMs = 1.0f;
      p.bands[band].dynReleaseMs = 100.0f;
      p.bands[band].dynRangeDb = 9.0f;
      strip.prepare(48000.0f);
      strip.setTargets(p);
      strip.reset();
      std::vector<float> l(512), r(512);
      double sum = 0.0;
      for (int block = 0; block < 120; ++block) {
        for (int i = 0; i < 512; ++i) {
          l[i] = r[i] = static_cast<float>(
              0.5 * std::sin(2.0 * M_PI * 1500.0 *
                             (block * 512 + i) / 48000.0));
        }
        strip.process(l.data(), r.data(), 512);
        if (block > 60) {
          for (int i = 0; i < 512; ++i) {
            sum += l[i] * l[i];
          }
        }
      }
      return 10.0 * std::log10(sum / (59.0 * 512.0) + 1.0e-12);
    };
    const double staticLevel = runLevel(false);
    const double dynamicLevel = runLevel(true);
    std::printf("dynamic audio: static %.2f dB, dynamic %.2f dB\n",
                staticLevel, dynamicLevel);
    check(staticLevel - dynamicLevel > 1.0,
          "dynamic band reduces the audio level");
  }

  // Extreme settings stay finite.
  {
    cse::Strip strip;
    cse::Params p;
    for (int b = 0; b < 8; ++b) {
      p.bands[b].on = true;
      p.bands[b].gainDb = (b % 2) ? 30.0f : -30.0f;
      p.bands[b].freq = 25.0f * std::pow(20.0f, static_cast<float>(b));
      p.bands[b].q = (b % 2) ? 24.0f : 0.12f;
      p.bands[b].dynOn = true;
      p.bands[b].dynRangeDb = 24.0f;
    }
    strip.prepare(192000.0f);
    strip.setTargets(p);
    strip.reset();
    std::vector<float> l(256), r(256);
    bool finite = true;
    for (int block = 0; block < 400; ++block) {
      for (int i = 0; i < 256; ++i) {
        l[i] = r[i] = 0.7f * std::sin(0.001f * (block * 256 + i));
      }
      strip.process(l.data(), r.data(), 256);
      for (int i = 0; i < 256; ++i) {
        if (!std::isfinite(l[i]) || !std::isfinite(r[i])) {
          finite = false;
        }
      }
    }
    check(finite, "extreme settings stay finite");
  }

  // Analyzer publishes a spectrum.
  {
    cse::Analyzer a;
    a.prepare(48000.0f);
    for (int i = 0; i < 8192; ++i) {
      a.push(0.5f * std::sin(2.0 * M_PI * 1000.0 * i / 48000.0),
             0.5f * std::sin(2.0 * M_PI * 1000.0 * i / 48000.0));
    }
    check(a.primed(), "analyzer primes");
    const float* s = a.spectrum();
    int peak = 0;
    for (int i = 1; i < 32; ++i) {
      if (s[i] > s[peak]) {
        peak = i;
      }
    }
    const double peakHz = 20.0 * std::pow(1000.0, peak / 31.0);
    check(peakHz > 500.0 && peakHz < 2000.0, "analyzer finds a 1 kHz tone");
  }

  // Inline renderer: respects the row span and draws inside its bounds.
  {
    cse::Params p = flatParams();
    p.bands[3].on = true;
    p.bands[3].freq = 800.0f;
    p.bands[3].gainDb = 6.0f;
    cse::InlineSnapshot snap;
    snap.params = p;
    constexpr int w = 180;
    constexpr int h = 60;
    constexpr int span = w + 8;
    std::vector<uint32_t> pixels(static_cast<size_t>(span) * h, 0xdeadbeefu);
    cse::InlineDisplay::drawInto(snap, w, h, pixels.data(), span);
    bool paddingUntouched = true;
    for (int y = 0; y < h; ++y) {
      for (int x = w; x < span; ++x) {
        if (pixels[static_cast<size_t>(y) * span + x] != 0xdeadbeefu) {
          paddingUntouched = false;
        }
      }
    }
    check(paddingUntouched, "inline renderer respects row span");
    unsigned checksum = 0;
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        checksum += pixels[static_cast<size_t>(y) * span + x];
      }
    }
    check(checksum != 0, "inline renderer writes pixels");
    cse::InlineDisplay display;
    check(display.render(snap, 8, 8) == nullptr,
          "inline renderer rejects tiny surfaces");
  }

  if (failures == 0) {
    std::printf("all DSP tests passed\n");
    return 0;
  }
  std::printf("%d DSP test(s) failed\n", failures);
  return 1;
}
