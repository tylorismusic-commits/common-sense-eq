// Common Sense EQ - LV2 wrapper
// SPDX-License-Identifier: GPL-3.0-or-later
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

#include <lv2/core/lv2.h>

#include "cse/param_info.hpp"
#include "dsp/inline_curve.hpp"
#include "dsp/params.hpp"
#include "dsp/strip.hpp"

namespace cse {

inline constexpr const char* kInlineInterfaceURI =
    "http://harrisonconsoles.com/lv2/inlinedisplay#interface";
inline constexpr const char* kInlineQueueDrawURI =
    "http://harrisonconsoles.com/lv2/inlinedisplay#queue_draw";

struct InlineDisplaySurface {
  unsigned char* data = nullptr;
  int width = 0;
  int height = 0;
  int stride = 0;
};

struct InlineDisplayQueueDraw {
  void* handle = nullptr;
  void (*queue_draw)(void* handle) = nullptr;
};

struct InlineDisplayInterface {
  InlineDisplaySurface* (*render)(void* instance, uint32_t width,
                                  uint32_t height) = nullptr;
};

class Instance {
public:
  float* ports[PORT_COUNT] = {};
  float sampleRate = 48000.0f;
  Strip strip;
  Params params;
  InlineDisplay display;
  InlineDisplaySurface surface;
  const InlineDisplayQueueDraw* queueDraw = nullptr;
  uint32_t blocks = 0;

  // Seqlock-protected snapshot for the GUI thread renderer.
  std::atomic<uint32_t> seq{0};
  InlineSnapshot snapshot;

  void publish() noexcept {
    const uint32_t s = seq.load(std::memory_order_relaxed);
    seq.store(s + 1, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_release);
    snapshot.params = params;
    for (uint32_t b = 0; b < 8; ++b) {
      snapshot.dynDb[b] = strip.dynDb(static_cast<int>(b));
    }
    const float* spec = strip.spectrum();
    std::memcpy(snapshot.spectrum, spec, sizeof(snapshot.spectrum));
    snapshot.hasSpectrum = strip.spectrumPrimed();
    seq.store(s + 2, std::memory_order_release);
  }

  bool copySnapshot(InlineSnapshot& out) noexcept {
    for (int attempt = 0; attempt < 4; ++attempt) {
      const uint32_t s1 = seq.load(std::memory_order_acquire);
      if (s1 & 1u) {
        continue;
      }
      out = snapshot;
      std::atomic_thread_fence(std::memory_order_acquire);
      const uint32_t s2 = seq.load(std::memory_order_acquire);
      if (s1 == s2) {
        return true;
      }
    }
    return false;
  }
};

static float clampToInfo(uint32_t index, float value,
                         float fallback) noexcept {
  const ParamInfo* info = infoFor(index);
  if (!info) {
    return fallback;
  }
  if (!std::isfinite(value)) {
    return fallback;
  }
  return clamp(value, std::min(info->min, info->max),
               std::max(info->min, info->max));
}

static float readPort(Instance* self, uint32_t index) noexcept {
  const ParamInfo* info = infoFor(index);
  const float fallback = info ? info->def : 0.0f;
  float value = fallback;
  if (index < PORT_COUNT && self->ports[index]) {
    value = self->ports[index][0];
  }
  return clampToInfo(index, value, fallback);
}

static void readParams(Instance* self) noexcept {
  Params& p = self->params;
  p.enabled = readPort(self, PORT_ENABLE) > 0.5f;
  p.outputGainDb = readPort(self, PORT_OUTPUT_GAIN);
  p.analyzerMode = static_cast<int>(std::lround(readPort(self, PORT_ANALYZER_MODE)));
  p.qCouple = readPort(self, PORT_Q_COUPLE) > 0.5f;
  p.processing = static_cast<int>(std::lround(readPort(self, PORT_PROCESSING)));
  for (int b = 0; b < 8; ++b) {
    BandParams& bp = p.bands[b];
    bp.on = readPort(self, bandParam(b, kSlotOn)) > 0.5f;
    bp.freq = readPort(self, bandParam(b, kSlotFreq));
    bp.gainDb = readPort(self, bandParam(b, kSlotGain));
    bp.q = readPort(self, bandParam(b, kSlotQ));
    bp.slope = static_cast<int>(std::lround(readPort(self, bandParam(b, kSlotSlope))));
    bp.dynOn = readPort(self, bandParam(b, kSlotDynOn)) > 0.5f;
    bp.dynThresholdDb = readPort(self, bandParam(b, kSlotDynThreshold));
    bp.dynRatio = readPort(self, bandParam(b, kSlotDynRatio));
    bp.dynAttackMs = readPort(self, bandParam(b, kSlotDynAttack));
    bp.dynReleaseMs = readPort(self, bandParam(b, kSlotDynRelease));
    bp.dynRangeDb = readPort(self, bandParam(b, kSlotDynRange));
    bp.dynMode = static_cast<int>(std::lround(readPort(self, bandParam(b, kSlotDynMode))));
  }
}

static void writeTelemetry(Instance* self) noexcept {
  for (uint32_t b = 0; b < kSpectrumCount; ++b) {
    if (self->ports[PORT_SPECTRUM_1 + b]) {
      self->ports[PORT_SPECTRUM_1 + b][0] = self->strip.spectrum()[b];
    }
  }
  for (uint32_t b = 0; b < kGrCount; ++b) {
    if (self->ports[PORT_GR_1 + b]) {
      self->ports[PORT_GR_1 + b][0] =
          self->strip.dynDb(static_cast<int>(b));
    }
  }
}

} // namespace cse

using namespace cse;

extern "C" LV2_Handle lv2_instantiate(const LV2_Descriptor*, double rate,
                                      const char*, const LV2_Feature* const* features) {
  Instance* self = new (std::nothrow) Instance();
  if (!self) {
    return nullptr;
  }
  for (const LV2_Feature* const* f = features; f && *f; ++f) {
    if (!std::strcmp((*f)->URI, kInlineQueueDrawURI) && (*f)->data) {
      self->queueDraw = static_cast<const InlineDisplayQueueDraw*>(
          static_cast<const void*>((*f)->data));
    }
  }
  self->sampleRate = std::max(8000.0f, static_cast<float>(rate));
  self->strip.prepare(self->sampleRate);
  readParams(self);
  self->strip.setTargets(self->params);
  self->publish();
  return self;
}

extern "C" void lv2_connect_port(LV2_Handle handle, uint32_t port,
                                 void* data) {
  Instance* self = static_cast<Instance*>(handle);
  if (self && port < PORT_COUNT) {
    self->ports[port] = static_cast<float*>(data);
  }
}

extern "C" void lv2_activate(LV2_Handle handle) {
  Instance* self = static_cast<Instance*>(handle);
  if (!self) {
    return;
  }
  readParams(self);
  self->strip.setTargets(self->params);
  self->strip.reset();
  self->publish();
}

extern "C" void lv2_run(LV2_Handle handle, uint32_t frames) {
  Instance* self = static_cast<Instance*>(handle);
  if (!self) {
    return;
  }
  float* inL = self->ports[PORT_AUDIO_IN_L];
  float* inR = self->ports[PORT_AUDIO_IN_R];
  float* outL = self->ports[PORT_AUDIO_OUT_L];
  float* outR = self->ports[PORT_AUDIO_OUT_R];
  if (!outL || !outR) {
    return;
  }
  if (!inL) {
    inL = outL;
  }
  if (!inR) {
    inR = outR;
  }
  if (inL != outL) {
    std::memcpy(outL, inL, frames * sizeof(float));
  }
  if (inR != outR) {
    std::memcpy(outR, inR, frames * sizeof(float));
  }

  readParams(self);
  self->strip.setTargets(self->params);
  self->strip.process(outL, outR, frames);
  writeTelemetry(self);
  self->publish();

  if (self->queueDraw && self->queueDraw->queue_draw &&
      ((self->blocks++ & 3u) == 0u)) {
    self->queueDraw->queue_draw(self->queueDraw->handle);
  }
}

extern "C" void lv2_deactivate(LV2_Handle handle) {
  Instance* self = static_cast<Instance*>(handle);
  if (self) {
    self->strip.reset();
    self->publish();
  }
}

extern "C" void lv2_cleanup(LV2_Handle handle) {
  delete static_cast<Instance*>(handle);
}

static InlineDisplaySurface* renderInline(void* instance, uint32_t width,
                                          uint32_t height) {
  Instance* self = static_cast<Instance*>(instance);
  if (!self) {
    return nullptr;
  }
  InlineSnapshot snap;
  if (!self->copySnapshot(snap)) {
    return nullptr;
  }
  InlineDisplay::Surface* surface = self->display.render(snap, width, height);
  if (!surface) {
    return nullptr;
  }
  self->surface.data = surface->data;
  self->surface.width = surface->width;
  self->surface.height = surface->height;
  self->surface.stride = surface->stride;
  return &self->surface;
}

static const InlineDisplayInterface kDisplayInterface = {&renderInline};

extern "C" const void* lv2_extension_data(const char* uri) {
  if (!std::strcmp(uri, kInlineInterfaceURI)) {
    return &kDisplayInterface;
  }
  return nullptr;
}

static const LV2_Descriptor kDescriptor = {
    "https://github.com/tylorismusic-commits/common-sense-eq",
    &lv2_instantiate,
    &lv2_connect_port,
    &lv2_activate,
    &lv2_run,
    &lv2_deactivate,
    &lv2_cleanup,
    &lv2_extension_data,
};

extern "C" LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(
    uint32_t index) {
  return index == 0 ? &kDescriptor : nullptr;
}
