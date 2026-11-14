// Common Sense EQ - LV2 host smoke test (lilv)
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <lilv/lilv.h>
#include <lv2/core/lv2.h>

#include "cse/param_info.hpp"

namespace {

struct Surface {
  unsigned char* data;
  int width;
  int height;
  int stride;
};

using RenderFn = Surface* (*)(void*, unsigned, unsigned);
struct DisplayInterface {
  RenderFn render;
};

int failures = 0;
void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

} // namespace

int main(int argc, char** argv) {
  std::string bundlePath = argc > 1 ? argv[1] : "build/lv2/common-sense-eq.lv2";
  if (!bundlePath.empty() && bundlePath.back() != '/') {
    bundlePath += '/';
  }
  LilvWorld* world = lilv_world_new();
  LilvNode* path = lilv_new_file_uri(world, nullptr, bundlePath.c_str());
  lilv_world_load_bundle(world, path);
  lilv_node_free(path);

  const LilvPlugins* plugins = lilv_world_get_all_plugins(world);
  const LilvNode* uri = lilv_new_uri(
      world, "https://github.com/tylorismusic-commits/common-sense-eq");
  const LilvPlugin* plugin = lilv_plugins_get_by_uri(plugins, uri);
  check(plugin != nullptr, "plugin URI found in bundle");
  if (!plugin) {
    lilv_node_free(const_cast<LilvNode*>(uri));
    lilv_world_free(world);
    return 1;
  }

  LilvInstance* instance = lilv_plugin_instantiate(plugin, 48000.0, nullptr);
  check(instance != nullptr, "plugin instantiates");
  if (!instance) {
    lilv_node_free(const_cast<LilvNode*>(uri));
    lilv_world_free(world);
    return 1;
  }

  std::vector<float> inL(512, 0.0f), inR(512, 0.0f), outL(512, 0.0f),
      outR(512, 0.0f);
  float control[cse::PORT_COUNT] = {};
  for (int i = 0; i < cse::kParamCount; ++i) {
    control[cse::kParams[i].index] = cse::kParams[i].def;
  }
  for (uint32_t i = 0; i < cse::PORT_COUNT; ++i) {
    if (i == cse::PORT_AUDIO_IN_L) {
      lilv_instance_connect_port(instance, i, inL.data());
    } else if (i == cse::PORT_AUDIO_IN_R) {
      lilv_instance_connect_port(instance, i, inR.data());
    } else if (i == cse::PORT_AUDIO_OUT_L) {
      lilv_instance_connect_port(instance, i, outL.data());
    } else if (i == cse::PORT_AUDIO_OUT_R) {
      lilv_instance_connect_port(instance, i, outR.data());
    } else {
      lilv_instance_connect_port(instance, i, &control[i]);
    }
  }
  lilv_instance_activate(instance);
  bool finite = true;
  for (int block = 0; block < 8; ++block) {
    for (int i = 0; i < 512; ++i) {
      inL[i] = inR[i] = 0.5f * std::sin(0.02f * (block * 512 + i));
    }
    lilv_instance_run(instance, 512);
    for (int i = 0; i < 512; ++i) {
      if (!std::isfinite(outL[i]) || !std::isfinite(outR[i])) {
        finite = false;
      }
    }
  }
  check(finite, "LV2 output is finite");

  const LV2_Descriptor* desc = lilv_instance_get_descriptor(instance);
  auto* display = static_cast<const DisplayInterface*>(desc->extension_data(
      "http://harrisonconsoles.com/lv2/inlinedisplay#interface"));
  check(display != nullptr, "inline display extension is exported");
  if (display) {
    Surface* surface = display->render(lilv_instance_get_handle(instance), 240,
                                       64);
    check(surface != nullptr, "inline display renders");
    if (surface) {
      check(surface->width == 240 && surface->height == 64,
            "inline display size");
      check(surface->stride == 240 * 4, "inline display stride");
      unsigned checksum = 0;
      for (int i = 0; i < surface->width * surface->height; ++i) {
        checksum += reinterpret_cast<unsigned*>(surface->data)[i];
      }
      check(checksum != 0, "inline display has content");
    }
  }

  lilv_instance_deactivate(instance);
  lilv_instance_free(instance);
  lilv_node_free(const_cast<LilvNode*>(uri));
  lilv_world_free(world);

  if (failures == 0) {
    std::printf("LV2 host test passed\n");
    return 0;
  }
  return 1;
}
