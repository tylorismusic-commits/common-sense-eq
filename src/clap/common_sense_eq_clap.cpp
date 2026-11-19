// Common Sense EQ - CLAP wrapper
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The CLAP build reuses the same DSP and the same X11/Cairo editor as the LV2
// build.  It also implements REAPER's cockos.reaper_embedui extension so the
// EQ curve can live in the TCP or MCP and be dragged like ReaEQ.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include <clap/all.h>
#include <lv2/ui/ui.h>

#include "clap/reaper_embed.hpp"
#include "cse/param_info.hpp"
#include "dsp/inline_curve.hpp"
#include "dsp/strip.hpp"
#include "ui/panel.hpp"

namespace cse {
namespace clapwrap {

static constexpr const char* kPluginId = "com.tylorismusic.commonsense";
static constexpr const char* kPluginName = "Common Sense EQ";
static constexpr const char* kUiUri =
    "https://github.com/tylorismusic-commits/common-sense-eq#ui";
static constexpr uint32_t kUiWidth = ui::kWidth;
static constexpr uint32_t kUiHeight = ui::kHeight;

static std::string gBundlePath;

static const ParamInfo* infoForId(uint32_t id) { return infoFor(id); }

static const char* groupName(int group) {
  if (group >= 0 && group < kGroupCount) {
    return kGroupNames[group];
  }
  return "";
}

static void formatValue(const ParamInfo& info, double value, char* out,
                        uint32_t size) {
  if (info.toggle) {
    std::snprintf(out, size, "%s", value > 0.5 ? "On" : "Off");
    return;
  }
  if (info.enumeration) {
    for (int i = 0; i < info.pointCount; ++i) {
      if (std::fabs(info.points[i].value - value) < 0.001) {
        std::snprintf(out, size, "%s", info.points[i].label);
        return;
      }
    }
    std::snprintf(out, size, "%.0f", value);
    return;
  }
  const char* symbol = info.symbol;
  if (std::strstr(symbol, "_freq")) {
    if (value >= 1000.0) {
      std::snprintf(out, size, "%.0f Hz", value);
    } else if (value >= 100.0) {
      std::snprintf(out, size, "%.0f Hz", value);
    } else {
      std::snprintf(out, size, "%.1f Hz", value);
    }
  } else if (std::strstr(symbol, "attack") ||
             std::strstr(symbol, "release")) {
    if (value >= 1000.0) {
      std::snprintf(out, size, "%.2f s", value * 0.001);
    } else if (value >= 100.0) {
      std::snprintf(out, size, "%.0f ms", value);
    } else {
      std::snprintf(out, size, "%.1f ms", value);
    }
  } else if (std::strstr(symbol, "_q")) {
    std::snprintf(out, size, "%.2f", value);
  } else if (std::strstr(symbol, "ratio")) {
    std::snprintf(out, size, "%.1f:1", value);
  } else if (std::strstr(symbol, "_gain") || std::strstr(symbol, "range") ||
             std::strstr(symbol, "threshold")) {
    std::snprintf(out, size, "%+.1f dB", value);
  } else if (std::strstr(symbol, "slope")) {
    std::snprintf(out, size, "%.0f dB/Oct", value);
  } else {
    std::snprintf(out, size, "%.2f", value);
  }
}

static double parseValue(const ParamInfo& info, const char* text) {
  if (!text) {
    return info.def;
  }
  while (*text == ' ' || *text == '\t') {
    ++text;
  }
  if (info.toggle) {
    if (!strcasecmp(text, "on") || !strcasecmp(text, "true") ||
        !strcasecmp(text, "yes")) {
      return 1.0;
    }
    if (!strcasecmp(text, "off") || !strcasecmp(text, "false") ||
        !strcasecmp(text, "no")) {
      return 0.0;
    }
  }
  if (info.enumeration) {
    for (int i = 0; i < info.pointCount; ++i) {
      if (!strcasecmp(text, info.points[i].label)) {
        return info.points[i].value;
      }
    }
  }
  char* end = nullptr;
  double parsed = std::strtod(text, &end);
  if (end == text) {
    return info.def;
  }
  while (*end == ' ') {
    ++end;
  }
  if (*end == 'k' || *end == 'K') {
    parsed *= 1000.0;
  } else if (*end == 'M') {
    parsed *= 1000000.0;
  }
  return std::min(std::max(parsed, static_cast<double>(info.min)),
                  static_cast<double>(info.max));
}

static void pushParamEvent(const clap_output_events_t* events, uint32_t id,
                           float value) {
  if (!events || !events->try_push) {
    return;
  }
  clap_event_param_value_t ev{};
  ev.header.size = sizeof(ev);
  ev.header.time = 0;
  ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
  ev.header.type = CLAP_EVENT_PARAM_VALUE;
  ev.header.flags = 0;
  ev.param_id = id;
  ev.cookie = nullptr;
  ev.note_id = -1;
  ev.port_index = -1;
  ev.channel = -1;
  ev.key = id;
  ev.value = value;
  events->try_push(events, &ev.header);
}

class ClapPlugin {
public:
  explicit ClapPlugin(const clap_host_t* h) : host(h) {
    for (uint32_t i = 0; i < PORT_COUNT; ++i) {
      values[i].store(0.0f);
      uiDirty[i].store(false);
      toUiDirty[i].store(false);
    }
    for (int i = 0; i < kParamCount; ++i) {
      values[kParams[i].index].store(kParams[i].def);
    }
    for (int b = 0; b < 8; ++b) {
      dynDb[b].store(0.0f);
    }
    for (uint32_t i = 0; i < kSpectrumCount; ++i) {
      spectrum[i].store(-100.0f);
    }
    plugin.desc = &descriptor();
    plugin.plugin_data = this;
    plugin.init = &init;
    plugin.destroy = &destroy;
    plugin.activate = &activate;
    plugin.deactivate = &deactivate;
    plugin.start_processing = &startProcessing;
    plugin.stop_processing = &stopProcessing;
    plugin.reset = &reset;
    plugin.process = &process;
    plugin.get_extension = &getExtension;
    plugin.on_main_thread = &onMainThread;
    readParams();
    publish();
  }

  ~ClapPlugin() { destroyUi(); }

  static ClapPlugin* self(const clap_plugin_t* p) {
    return static_cast<ClapPlugin*>(p->plugin_data);
  }

  static const clap_plugin_descriptor_t& descriptor() {
    static const char* features[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, "eq",
                                     "stereo", nullptr};
    static const clap_plugin_descriptor_t desc = {
        CLAP_VERSION,
        kPluginId,
        kPluginName,
        "Tylor",
        "https://github.com/tylorismusic-commits/common-sense-eq",
        "",
        "https://github.com/tylorismusic-commits/common-sense-eq/issues",
        "0.1.2",
        "Eight-band channel EQ with dynamic bands, an analyzer, and inline "
        "mixer-strip displays.",
        features};
    return desc;
  }

  // -------------------------------------------------------------------------
  // Parameters
  // -------------------------------------------------------------------------
  Params readParams() {
    Params p;
    p.enabled = values[PORT_ENABLE].load() > 0.5f;
    p.outputGainDb = values[PORT_OUTPUT_GAIN].load();
    p.analyzerMode =
        static_cast<int>(std::lround(values[PORT_ANALYZER_MODE].load()));
    p.qCouple = values[PORT_Q_COUPLE].load() > 0.5f;
    p.processing =
        static_cast<int>(std::lround(values[PORT_PROCESSING].load()));
    for (int b = 0; b < 8; ++b) {
      BandParams& bp = p.bands[b];
      bp.on = values[bandParam(b, kSlotOn)].load() > 0.5f;
      bp.freq = values[bandParam(b, kSlotFreq)].load();
      bp.gainDb = values[bandParam(b, kSlotGain)].load();
      bp.q = values[bandParam(b, kSlotQ)].load();
      bp.slope =
          static_cast<int>(std::lround(values[bandParam(b, kSlotSlope)].load()));
      bp.dynOn = values[bandParam(b, kSlotDynOn)].load() > 0.5f;
      bp.dynThresholdDb = values[bandParam(b, kSlotDynThreshold)].load();
      bp.dynRatio = values[bandParam(b, kSlotDynRatio)].load();
      bp.dynAttackMs = values[bandParam(b, kSlotDynAttack)].load();
      bp.dynReleaseMs = values[bandParam(b, kSlotDynRelease)].load();
      bp.dynRangeDb = values[bandParam(b, kSlotDynRange)].load();
      bp.dynMode =
          static_cast<int>(std::lround(values[bandParam(b, kSlotDynMode)].load()));
    }
    params = p;
    return p;
  }

  void setParam(uint32_t id, float value) {
    const ParamInfo* info = infoForId(id);
    if (!info) {
      return;
    }
    value = clamp(value, std::min(info->min, info->max),
                  std::max(info->min, info->max));
    if (info->toggle) {
      value = value > 0.5f ? 1.0f : 0.0f;
    } else if (info->enumeration) {
      value = std::round(value);
    }
    values[id].store(value, std::memory_order_relaxed);
    uiDirty[id].store(true, std::memory_order_release);
    toUiDirty[id].store(true, std::memory_order_release);
    if (host && host->request_process) {
      host->request_process(host);
    }
  }

  // -------------------------------------------------------------------------
  // Display snapshot
  // -------------------------------------------------------------------------
  void publish() {
    const uint32_t s = seq.load(std::memory_order_relaxed);
    seq.store(s + 1, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_release);
    snapshot.params = params;
    for (int b = 0; b < 8; ++b) {
      const float d = strip.dynDb(b);
      dynDb[b].store(d, std::memory_order_relaxed);
      snapshot.dynDb[b] = d;
    }
    const float* spec = strip.spectrum();
    for (uint32_t i = 0; i < kSpectrumCount; ++i) {
      spectrum[i].store(spec[i], std::memory_order_relaxed);
      snapshot.spectrum[i] = spec[i];
    }
    snapshot.hasSpectrum = strip.spectrumPrimed();
    seq.store(s + 2, std::memory_order_release);
  }

  bool copySnapshot(InlineSnapshot& out) const {
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

  // Snapshot with parameters read straight from the atomics, so an embedded
  // drag repaints immediately rather than one block behind.
  void buildLiveSnapshot(InlineSnapshot& out) const {
    InlineSnapshot base;
    copySnapshot(base);
    base.params = [&] {
      Params p;
      p.enabled = values[PORT_ENABLE].load() > 0.5f;
      p.outputGainDb = values[PORT_OUTPUT_GAIN].load();
      p.qCouple = values[PORT_Q_COUPLE].load() > 0.5f;
      p.analyzerMode =
          static_cast<int>(std::lround(values[PORT_ANALYZER_MODE].load()));
      p.processing =
          static_cast<int>(std::lround(values[PORT_PROCESSING].load()));
      for (int b = 0; b < 8; ++b) {
        BandParams& bp = p.bands[b];
        bp.on = values[bandParam(b, kSlotOn)].load() > 0.5f;
        bp.freq = values[bandParam(b, kSlotFreq)].load();
        bp.gainDb = values[bandParam(b, kSlotGain)].load();
        bp.q = values[bandParam(b, kSlotQ)].load();
        bp.slope = static_cast<int>(
            std::lround(values[bandParam(b, kSlotSlope)].load()));
        bp.dynOn = values[bandParam(b, kSlotDynOn)].load() > 0.5f;
      }
      return p;
    }();
    out = base;
  }

  // -------------------------------------------------------------------------
  // CLAP lifecycle
  // -------------------------------------------------------------------------
  static bool CLAP_ABI init(const clap_plugin_t* p) {
    ClapPlugin* s = self(p);
    return s && s->host;
  }

  static void CLAP_ABI destroy(const clap_plugin_t* p) { delete self(p); }

  static bool CLAP_ABI activate(const clap_plugin_t* p, double sampleRate,
                                uint32_t, uint32_t) {
    ClapPlugin* s = self(p);
    if (!s) {
      return false;
    }
    s->sampleRate = std::max(8000.0, sampleRate);
    s->strip.prepare(static_cast<float>(s->sampleRate));
    s->strip.setTargets(s->readParams());
    s->strip.reset();
    s->publish();
    return true;
  }

  static void CLAP_ABI deactivate(const clap_plugin_t* p) {
    ClapPlugin* s = self(p);
    if (s) {
      s->strip.reset();
    }
  }

  static bool CLAP_ABI startProcessing(const clap_plugin_t*) { return true; }
  static void CLAP_ABI stopProcessing(const clap_plugin_t*) {}

  static void CLAP_ABI reset(const clap_plugin_t* p) {
    ClapPlugin* s = self(p);
    if (s) {
      s->strip.reset();
    }
  }

  static void CLAP_ABI onMainThread(const clap_plugin_t*) {}

  static void applyEvent(ClapPlugin* s, const clap_event_header_t* h) {
    if (!h || h->space_id != CLAP_CORE_EVENT_SPACE_ID) {
      return;
    }
    if (h->type != CLAP_EVENT_PARAM_VALUE) {
      return;
    }
    const auto* ev = reinterpret_cast<const clap_event_param_value_t*>(h);
    if (ev->param_id >= PORT_COUNT) {
      return;
    }
    s->values[ev->param_id].store(static_cast<float>(ev->value));
    s->toUiDirty[ev->param_id].store(true, std::memory_order_release);
  }

  static clap_process_status CLAP_ABI process(const clap_plugin_t* p,
                                              const clap_process_t* process) {
    ClapPlugin* s = self(p);
    if (!s || !process) {
      return CLAP_PROCESS_ERROR;
    }
    const uint32_t n = process->frames_count;

    const clap_input_events_t* inEvents = process->in_events;
    if (inEvents) {
      const uint32_t count = inEvents->size(inEvents);
      for (uint32_t i = 0; i < count; ++i) {
        applyEvent(s, inEvents->get(inEvents, i));
      }
    }

    // Tell the host about moves made in the UI so they are recorded and saved.
    if (process->out_events) {
      for (uint32_t i = 0; i < PORT_COUNT; ++i) {
        if (s->uiDirty[i].exchange(false, std::memory_order_acq_rel)) {
          pushParamEvent(process->out_events, i,
                         s->values[i].load(std::memory_order_relaxed));
        }
      }
    }

    float* outL = nullptr;
    float* outR = nullptr;
    if (process->audio_outputs_count > 0) {
      const clap_audio_buffer_t& out = process->audio_outputs[0];
      if (out.data32 && out.channel_count > 0) {
        outL = out.data32[0];
        outR = out.channel_count > 1 ? out.data32[1] : out.data32[0];
      }
    }
    const clap_audio_buffer_t* in = nullptr;
    if (process->audio_inputs_count > 0) {
      in = &process->audio_inputs[0];
    }
    if (outL && outR) {
      if (in && in->data32 && in->channel_count > 0) {
        const float* inL = in->data32[0];
        const float* inR = in->channel_count > 1 ? in->data32[1] : inL;
        if (inL != outL) {
          std::memcpy(outL, inL, n * sizeof(float));
        }
        if (inR != outR) {
          std::memcpy(outR, inR, n * sizeof(float));
        }
      } else {
        std::memset(outL, 0, n * sizeof(float));
        std::memset(outR, 0, n * sizeof(float));
      }
      s->strip.setTargets(s->readParams());
      s->strip.process(outL, outR, n);
      s->publish();
    }
    (void)process->steady_time;
    return CLAP_PROCESS_CONTINUE;
  }

  // -------------------------------------------------------------------------
  // State
  // -------------------------------------------------------------------------
  static bool CLAP_ABI stateSave(const clap_plugin_t* p,
                                 const clap_ostream_t* stream) {
    ClapPlugin* s = self(p);
    if (!s || !stream) {
      return false;
    }
    std::string out = "CSE1\n";
    char line[64];
    for (int i = 0; i < kParamCount; ++i) {
      const uint32_t id = kParams[i].index;
      std::snprintf(line, sizeof(line), "%u %.8g\n", id,
                    static_cast<double>(s->values[id].load()));
      out += line;
    }
    const char* data = out.data();
    uint64_t left = out.size();
    while (left > 0) {
      const int64_t written = stream->write(stream, data, left);
      if (written <= 0) {
        return false;
      }
      data += written;
      left -= static_cast<uint64_t>(written);
    }
    return true;
  }

  static bool CLAP_ABI stateLoad(const clap_plugin_t* p,
                                 const clap_istream_t* stream) {
    ClapPlugin* s = self(p);
    if (!s || !stream) {
      return false;
    }
    char buffer[8192];
    uint64_t total = 0;
    while (total < sizeof(buffer) - 1) {
      const int64_t got =
          stream->read(stream, buffer + total, sizeof(buffer) - 1 - total);
      if (got < 0) {
        return false;
      }
      if (got == 0) {
        break;
      }
      total += static_cast<uint64_t>(got);
    }
    if (total == 0) {
      return false;
    }
    buffer[total] = '\0';
    const char* cursor = std::strstr(buffer, "CSE1\n");
    if (!cursor) {
      return false;
    }
    cursor += 5;
    while (*cursor) {
      char* end = nullptr;
      const unsigned long id = std::strtoul(cursor, &end, 10);
      if (end == cursor || id >= PORT_COUNT) {
        break;
      }
      cursor = end;
      const double value = std::strtod(cursor, &end);
      if (end == cursor) {
        break;
      }
      if (infoForId(static_cast<uint32_t>(id))) {
        s->values[id].store(static_cast<float>(value));
        s->toUiDirty[id].store(true, std::memory_order_release);
      }
      cursor = end;
      while (*cursor == '\n' || *cursor == '\r' || *cursor == ' ') {
        ++cursor;
      }
    }
    s->readParams();
    s->strip.setTargets(s->params);
    s->publish();
    return true;
  }

  static const clap_plugin_state_t& stateExt() {
    static const clap_plugin_state_t ext = {&stateSave, &stateLoad};
    return ext;
  }

  // -------------------------------------------------------------------------
  // Audio ports extension (required for the host to route audio)
  // -------------------------------------------------------------------------
  static uint32_t CLAP_ABI audioPortCount(const clap_plugin_t*, bool) {
    return 1;
  }

  static bool CLAP_ABI audioPortInfo(const clap_plugin_t*, uint32_t index,
                                     bool isInput,
                                     clap_audio_port_info_t* info) {
    if (!info || index != 0) {
      return false;
    }
    std::memset(info, 0, sizeof(*info));
    info->id = isInput ? 0 : 1;
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = isInput ? 1 : CLAP_INVALID_ID;
    std::snprintf(info->name, sizeof(info->name), "%s",
                  isInput ? "Input" : "Output");
    return true;
  }

  static const clap_plugin_audio_ports_t& audioPortsExt() {
    static const clap_plugin_audio_ports_t ext = {&audioPortCount,
                                                   &audioPortInfo};
    return ext;
  }

  // -------------------------------------------------------------------------
  // Params extension
  // -------------------------------------------------------------------------
  static uint32_t CLAP_ABI paramCount(const clap_plugin_t*) {
    return static_cast<uint32_t>(kParamCount);
  }

  static bool CLAP_ABI paramInfo(const clap_plugin_t*, uint32_t index,
                                 clap_param_info_t* info) {
    if (!info || index >= static_cast<uint32_t>(kParamCount)) {
      return false;
    }
    const ParamInfo& p = kParams[index];
    std::memset(info, 0, sizeof(*info));
    info->id = p.index;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    if (p.toggle) {
      info->flags |= CLAP_PARAM_IS_STEPPED;
    }
    if (p.enumeration) {
      info->flags |= CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
    }
    info->cookie = nullptr;
    std::snprintf(info->name, sizeof(info->name), "%s", p.name);
    std::snprintf(info->module, sizeof(info->module), "%s",
                  p.group == 0 ? "Global" : groupName(p.group));
    info->min_value = p.min;
    info->max_value = p.max;
    info->default_value = p.def;
    return true;
  }

  static bool CLAP_ABI paramValue(const clap_plugin_t* p, clap_id id,
                                  double* value) {
    ClapPlugin* s = self(p);
    if (!s || !value || id >= PORT_COUNT || !infoForId(id)) {
      return false;
    }
    *value = s->values[id].load();
    return true;
  }

  static bool CLAP_ABI paramTextToValue(const clap_plugin_t*, clap_id id,
                                        const char* text, double* value) {
    const ParamInfo* info = infoForId(id);
    if (!info || !value) {
      return false;
    }
    *value = parseValue(*info, text);
    return true;
  }

  static bool CLAP_ABI paramValueToText(const clap_plugin_t*, clap_id id,
                                        double value, char* text,
                                        uint32_t size) {
    const ParamInfo* info = infoForId(id);
    if (!info || !text || size == 0) {
      return false;
    }
    formatValue(*info, value, text, size);
    return true;
  }

  static void CLAP_ABI paramFlush(const clap_plugin_t* p,
                                  const clap_input_events_t* in,
                                  const clap_output_events_t* out) {
    ClapPlugin* s = self(p);
    if (!s || !in) {
      return;
    }
    const uint32_t count = in->size(in);
    for (uint32_t i = 0; i < count; ++i) {
      applyEvent(s, in->get(in, i));
    }
    (void)out;
  }

  static const clap_plugin_params_t& paramsExt() {
    static const clap_plugin_params_t ext = {
        &paramCount, &paramInfo, &paramValue, &paramValueToText,
        &paramTextToValue, &paramFlush};
    return ext;
  }

  // -------------------------------------------------------------------------
  // Editor (LV2 X11 UI module)
  // -------------------------------------------------------------------------
  static void uiWrite(LV2UI_Controller controller, uint32_t port, uint32_t size,
                      uint32_t, const void* buffer) {
    ClapPlugin* s = static_cast<ClapPlugin*>(controller);
    if (!s || !buffer || size != sizeof(float) || port >= PORT_COUNT) {
      return;
    }
    s->setParam(port, *static_cast<const float*>(buffer));
  }

  void uiThreadLoop() {
    while (uiRunning.load()) {
      {
        std::lock_guard<std::mutex> lock(uiMutex);
        if (ui) {
          for (uint32_t i = 0; i < PORT_COUNT; ++i) {
            if (toUiDirty[i].exchange(false, std::memory_order_acq_rel)) {
              const float value = values[i].load();
              uiDesc->port_event(ui, i, sizeof(float), 0, &value);
            }
          }
          {
            float spec[kSpectrumCount];
            float gr[kGrCount];
            for (uint32_t i = 0; i < kSpectrumCount; ++i) {
              spec[i] = spectrum[i].load(std::memory_order_relaxed);
            }
            for (uint32_t i = 0; i < kGrCount; ++i) {
              gr[i] = dynDb[i].load(std::memory_order_relaxed);
            }
            for (uint32_t i = 0; i < kSpectrumCount; ++i) {
              uiDesc->port_event(ui, PORT_SPECTRUM_1 + i, sizeof(float), 0,
                                 &spec[i]);
            }
            for (uint32_t i = 0; i < kGrCount; ++i) {
              uiDesc->port_event(ui, PORT_GR_1 + i, sizeof(float), 0, &gr[i]);
            }
          }
          if (uiIdle) {
            uiIdle->idle(ui);
          }
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
  }

  void stopUiThread() {
    if (uiThread.joinable()) {
      uiRunning.store(false);
      uiThread.join();
    }
  }

  bool createUi(uintptr_t parent) {
    std::lock_guard<std::mutex> lock(uiMutex);
    if (ui) {
      return true;
    }
    if (!uiLib) {
      std::string dir = gBundlePath;
      if (!dir.empty() && dir.back() != '/') {
        dir += '/';
      }
      const std::string path = dir + "CommonSenseEQUI.so";
      uiLib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
      if (!uiLib) {
        std::fprintf(stderr, "Common Sense EQ: could not load %s: %s\n",
                     path.c_str(), dlerror());
        return false;
      }
      auto descriptorFn = reinterpret_cast<const LV2UI_Descriptor* (*)(
          uint32_t)>(dlsym(uiLib, "lv2ui_descriptor"));
      if (!descriptorFn) {
        dlclose(uiLib);
        uiLib = nullptr;
        return false;
      }
      uiDesc = descriptorFn(0);
      if (!uiDesc) {
        dlclose(uiLib);
        uiLib = nullptr;
        return false;
      }
    }

    uintptr_t parentWindow = parent;
    LV2_Feature parentFeature{LV2_UI__parent, &parentWindow};
    const LV2_Feature* features[] = {&parentFeature, nullptr};
    LV2UI_Widget widget = nullptr;
    std::string dir = gBundlePath;
    if (!dir.empty() && dir.back() != '/') {
      dir += '/';
    }
    ui = uiDesc->instantiate(uiDesc, kUiUri, dir.c_str(), &uiWrite, this,
                             &widget, features);
    if (!ui) {
      return false;
    }
    uiIdle = static_cast<const LV2UI_Idle_Interface*>(
        uiDesc->extension_data(LV2_UI__idleInterface));
    uiResize = static_cast<const LV2UI_Resize*>(
        uiDesc->extension_data(LV2_UI__resize));

    // Sync the editor with the current parameter values.
    for (uint32_t i = 0; i < PORT_COUNT; ++i) {
      if (infoForId(i)) {
        const float value = values[i].load();
        uiDesc->port_event(ui, i, sizeof(float), 0, &value);
      }
    }
    if (uiResize && uiResize->ui_resize) {
      uiResize->ui_resize(ui, static_cast<int>(uiWidth),
                          static_cast<int>(uiHeight));
    }
    return true;
  }

  void destroyUi() {
    stopUiThread();
    std::lock_guard<std::mutex> lock(uiMutex);
    if (ui) {
      uiDesc->cleanup(ui);
      ui = nullptr;
    }
    uiIdle = nullptr;
    uiResize = nullptr;
    uiDesc = nullptr;
    if (uiLib) {
      dlclose(uiLib);
      uiLib = nullptr;
    }
  }

  void startUiThread() {
    if (uiThread.joinable()) {
      return;
    }
    uiRunning.store(true);
    uiThread = std::thread(&ClapPlugin::uiThreadLoop, this);
  }

  static bool CLAP_ABI guiIsApiSupported(const clap_plugin_t*, const char* api,
                                         bool) {
    return api && !std::strcmp(api, CLAP_WINDOW_API_X11);
  }

  static bool CLAP_ABI guiGetPreferredApi(const clap_plugin_t*, const char** api,
                                          bool* floating) {
    if (api) *api = CLAP_WINDOW_API_X11;
    if (floating) *floating = false;
    return true;
  }

  static bool CLAP_ABI guiCreate(const clap_plugin_t* p, const char*, bool floating) {
    ClapPlugin* s = self(p);
    if (!s) {
      return false;
    }
    s->floating = floating;
    return floating ? s->createUi(0) : true;
  }

  static void CLAP_ABI guiDestroy(const clap_plugin_t* p) {
    if (ClapPlugin* s = self(p)) {
      s->destroyUi();
    }
  }

  static bool CLAP_ABI guiSetScale(const clap_plugin_t*, double) {
    return false;
  }

  static bool CLAP_ABI guiGetSize(const clap_plugin_t* p, uint32_t* width,
                                  uint32_t* height) {
    ClapPlugin* s = self(p);
    if (!s || !width || !height) {
      return false;
    }
    *width = s->uiWidth;
    *height = s->uiHeight;
    return true;
  }

  static bool CLAP_ABI guiCanResize(const clap_plugin_t*) { return true; }

  static bool CLAP_ABI guiGetResizeHints(const clap_plugin_t*,
                                         clap_gui_resize_hints_t* hints) {
    if (!hints) {
      return false;
    }
    hints->can_resize_horizontally = true;
    hints->can_resize_vertically = true;
    hints->preserve_aspect_ratio = false;
    hints->aspect_ratio_width = kUiWidth;
    hints->aspect_ratio_height = kUiHeight;
    return true;
  }

  static bool CLAP_ABI guiAdjustSize(const clap_plugin_t*, uint32_t* width,
                                     uint32_t* height) {
    if (!width || !height) {
      return false;
    }
    *width = std::max(*width, 560u);
    *height = std::max(*height, 280u);
    return true;
  }

  static bool CLAP_ABI guiSetSize(const clap_plugin_t* p, uint32_t width,
                                  uint32_t height) {
    ClapPlugin* s = self(p);
    if (!s || width == 0 || height == 0) {
      return false;
    }
    s->uiWidth = width;
    s->uiHeight = height;
    std::lock_guard<std::mutex> lock(s->uiMutex);
    if (s->ui && s->uiResize && s->uiResize->ui_resize) {
      s->uiResize->ui_resize(s->ui, static_cast<int>(width),
                             static_cast<int>(height));
    }
    return true;
  }

  static bool CLAP_ABI guiSetParent(const clap_plugin_t* p,
                                    const clap_window_t* window) {
    ClapPlugin* s = self(p);
    if (!s || !window || !window->api ||
        std::strcmp(window->api, CLAP_WINDOW_API_X11)) {
      return false;
    }
    return s->createUi(static_cast<uintptr_t>(window->x11));
  }

  static bool CLAP_ABI guiSetTransient(const clap_plugin_t*, const clap_window_t*) {
    return true;
  }

  static void CLAP_ABI guiSuggestTitle(const clap_plugin_t*, const char*) {}

  static bool CLAP_ABI guiShow(const clap_plugin_t* p) {
    ClapPlugin* s = self(p);
    if (!s) {
      return false;
    }
    if (!s->ui && !s->createUi(0)) {
      return false;
    }
    s->startUiThread();
    return true;
  }

  static bool CLAP_ABI guiHide(const clap_plugin_t* p) {
    ClapPlugin* s = self(p);
    if (!s) {
      return false;
    }
    s->stopUiThread();
    return true;
  }

  static const clap_plugin_gui_t& guiExt() {
    static const clap_plugin_gui_t ext = {
        &guiIsApiSupported, &guiGetPreferredApi, &guiCreate, &guiDestroy,
        &guiSetScale,        &guiGetSize,       &guiCanResize,
        &guiGetResizeHints,  &guiAdjustSize,    &guiSetSize,
        &guiSetParent,       &guiSetTransient,  &guiSuggestTitle,
        &guiShow,            &guiHide};
    return ext;
  }

  // -------------------------------------------------------------------------
  // REAPER embedded UI
  // -------------------------------------------------------------------------
  static reaper::INT_PTR CLAP_ABI inlineEditor(const clap_plugin_t* p, int msg,
                                               void* p1, void* p2) {
    ClapPlugin* s = self(p);
    if (!s) {
      return 0;
    }
    if (msg == reaper::WM_IS_SUPPORTED) {
      return 1;
    }
    if (msg == reaper::WM_NOBORDER) {
      return 1;
    }
    if (msg == reaper::WM_CREATE || msg == reaper::WM_DESTROY) {
      return 0;
    }
    if (msg == reaper::WM_GETMINMAXINFO) {
      auto* hints = static_cast<reaper::SizeHints*>(p2);
      if (!hints) {
        return 0;
      }
      hints->preferred_aspect = 3 * 65536;
      hints->minimum_aspect = 2 * 65536;
      hints->min_width = 150;
      hints->min_height = 46;
      hints->max_width = 1800;
      hints->max_height = 460;
      return 1;
    }
    if (msg == reaper::WM_PAINT) {
      auto* bitmap = static_cast<reaper::IBitmap*>(p1);
      auto* info = static_cast<reaper::DrawInfo*>(p2);
      if (!bitmap || !info) {
        return 0;
      }
      const int w = bitmap->getWidth();
      const int h = bitmap->getHeight();
      if (w <= 0 || h <= 0) {
        return 0;
      }
      InlineSnapshot snap;
      s->buildLiveSnapshot(snap);
      std::vector<uint32_t> temp(static_cast<size_t>(w) * h, 0xff0e1420u);
      InlineDisplay::drawInto(snap, w, h, temp.data(), w);
      unsigned int* bits = bitmap->getBits();
      if (!bits) {
        return 0;
      }
      const int rowSpan = bitmap->getRowSpan();
      const bool flipped = bitmap->isFlipped();
      for (int y = 0; y < h; ++y) {
        const int src = flipped ? (h - 1 - y) : y;
        std::memcpy(bits + static_cast<size_t>(y) * rowSpan,
                    temp.data() + static_cast<size_t>(src) * w,
                    static_cast<size_t>(w) * sizeof(uint32_t));
      }
      return 1;
    }
    if (msg == reaper::WM_SETCURSOR) {
      return 0;
    }
    if (msg >= reaper::WM_MOUSEMOVE && msg <= reaper::WM_MOUSEWHEEL) {
      auto* info = static_cast<reaper::DrawInfo*>(p2);
      if (!info) {
        return 0;
      }
      const int w = info->width;
      const int h = info->height;
      InlineSnapshot snap;
      s->buildLiveSnapshot(snap);
      const float mx = static_cast<float>(info->mouse_x);
      const float my = static_cast<float>(info->mouse_y);

      if (msg == reaper::WM_LBUTTONDOWN) {
        const int band = InlineDisplay::hitBand(snap, w, h, mx, my);
        if (band >= 0) {
          s->embedBand = band;
          s->embedStartFreq = s->values[bandParam(band, kSlotFreq)].load();
          s->embedStartGain = s->values[bandParam(band, kSlotGain)].load();
          s->selected = band;
          return reaper::RETNOTIFY_INVALIDATE;
        }
        return 0;
      }
      if (msg == reaper::WM_LBUTTONUP) {
        s->embedBand = -1;
        return 0;
      }
      if (msg == reaper::WM_LBUTTONDBLCLK) {
        const int band = InlineDisplay::hitBand(snap, w, h, mx, my);
        if (band >= 0) {
          if (bandHasGain(band)) {
            s->setParam(bandParam(band, kSlotGain), 0.0f);
          } else {
            s->setParam(bandParam(band, kSlotFreq), kDefaultFreq[band]);
          }
          s->selected = band;
          return reaper::RETNOTIFY_INVALIDATE;
        }
        return 0;
      }
      if (msg == reaper::WM_RBUTTONUP) {
        const int band = InlineDisplay::hitBand(snap, w, h, mx, my);
        if (band >= 0) {
          const float now = s->values[bandParam(band, kSlotOn)].load();
          s->setParam(bandParam(band, kSlotOn), now > 0.5f ? 0.0f : 1.0f);
          s->selected = band;
          return reaper::RETNOTIFY_INVALIDATE;
        }
        return 0;
      }
      if (msg == reaper::WM_MOUSEWHEEL) {
        int band = InlineDisplay::hitBand(snap, w, h, mx, my);
        if (band < 0) {
          band = s->selected;
        }
        if (band >= 0 && bandHasGain(band)) {
          const double steps = info->mousewheel_amt / 120.0;
          const float q = s->values[bandParam(band, kSlotQ)].load();
          s->setParam(bandParam(band, kSlotQ),
                      static_cast<float>(q * std::exp(steps * 0.12)));
          s->selected = band;
          return reaper::RETNOTIFY_INVALIDATE;
        }
        return 0;
      }
      if (msg == reaper::WM_MOUSEMOVE && s->embedBand >= 0) {
        const int band = s->embedBand;
        const float freq = InlineDisplay::freqAtPoint(w, h, mx);
        s->setParam(bandParam(band, kSlotFreq), clamp(freq, 20.0f, 20000.0f));
        if (bandHasGain(band)) {
          const float db = InlineDisplay::dbAtPoint(w, h, my);
          s->setParam(bandParam(band, kSlotGain), clamp(db, -30.0f, 30.0f));
        }
        return reaper::RETNOTIFY_INVALIDATE;
      }
    }
    return 0;
  }

  static const reaper::EmbedUI& embedExt() {
    static const reaper::EmbedUI ext = {&inlineEditor};
    return ext;
  }

  static const void* CLAP_ABI getExtension(const clap_plugin_t* p,
                                           const char* id) {
    ClapPlugin* s = self(p);
    if (!s || !id) {
      return nullptr;
    }
    if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) {
      return &audioPortsExt();
    }
    if (!std::strcmp(id, CLAP_EXT_PARAMS)) {
      return &paramsExt();
    }
    if (!std::strcmp(id, CLAP_EXT_STATE)) {
      return &stateExt();
    }
    if (!std::strcmp(id, CLAP_EXT_GUI)) {
      return &guiExt();
    }
    if (!std::strcmp(id, reaper::kExtensionId)) {
      return &embedExt();
    }
    return nullptr;
  }

  // -------------------------------------------------------------------------
  clap_plugin_t plugin{};
  const clap_host_t* host = nullptr;
  std::atomic<float> values[PORT_COUNT];
  std::atomic<bool> uiDirty[PORT_COUNT];
  std::atomic<bool> toUiDirty[PORT_COUNT];
  std::atomic<float> dynDb[kGrCount];
  std::atomic<float> spectrum[kSpectrumCount];

  double sampleRate = 48000.0;
  Params params;
  Strip strip;

  std::atomic<uint32_t> seq{0};
  InlineSnapshot snapshot;

  void* uiLib = nullptr;
  const LV2UI_Descriptor* uiDesc = nullptr;
  LV2UI_Handle ui = nullptr;
  const LV2UI_Idle_Interface* uiIdle = nullptr;
  const LV2UI_Resize* uiResize = nullptr;
  std::thread uiThread;
  std::atomic<bool> uiRunning{false};
  std::mutex uiMutex;
  uint32_t uiWidth = kUiWidth;
  uint32_t uiHeight = kUiHeight;
  bool floating = false;

  int embedBand = -1;
  float embedStartFreq = 0.0f;
  float embedStartGain = 0.0f;
  int selected = 3;
};

// ---------------------------------------------------------------------------
// Factory / entry
// ---------------------------------------------------------------------------
static uint32_t CLAP_ABI factoryPluginCount(const clap_plugin_factory_t*) {
  return 1;
}

static const clap_plugin_descriptor_t* CLAP_ABI factoryPluginDescriptor(
    const clap_plugin_factory_t*, uint32_t index) {
  return index == 0 ? &ClapPlugin::descriptor() : nullptr;
}

static const clap_plugin_t* CLAP_ABI factoryCreatePlugin(
    const clap_plugin_factory_t*, const clap_host_t* host, const char* pluginId) {
  if (!host || !pluginId || std::strcmp(pluginId, kPluginId)) {
    return nullptr;
  }
  ClapPlugin* instance = new (std::nothrow) ClapPlugin(host);
  return instance ? &instance->plugin : nullptr;
}

static const clap_plugin_factory_t kFactory = {
    &factoryPluginCount, &factoryPluginDescriptor, &factoryCreatePlugin};

static bool CLAP_ABI entryInit(const char* pluginPath) {
  if (!pluginPath) {
    return false;
  }
  std::string path(pluginPath);
  const std::string suffix = ".clap";
  if (path.size() >= suffix.size() &&
      path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0) {
    path = path.substr(0, path.size() - suffix.size()) + ".resources";
  } else {
    const size_t slash = path.find_last_of('/');
    if (slash != std::string::npos) {
      path = path.substr(0, slash);
    }
  }
  gBundlePath = path;
  return true;
}

static void CLAP_ABI entryDeinit() { gBundlePath.clear(); }

static const void* CLAP_ABI entryGetFactory(const char* factoryId) {
  if (factoryId && !std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID)) {
    return &kFactory;
  }
  return nullptr;
}

} // namespace clapwrap
} // namespace cse

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION, &cse::clapwrap::entryInit, &cse::clapwrap::entryDeinit,
    &cse::clapwrap::entryGetFactory};
