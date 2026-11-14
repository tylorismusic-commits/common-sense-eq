// Common Sense EQ - X11 UI embedding smoke test
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <unistd.h>

#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>

#include <lv2/core/lv2.h>
#include <lv2/ui/ui.h>

#include "cse/param_info.hpp"

using LV2UIDescriptorFn = const LV2UI_Descriptor* (*)(uint32_t);

static int g_writes = 0;
static uint32_t g_lastPort = 0;
static float g_lastValue = 0.0f;

static void writeFn(LV2UI_Controller, uint32_t port, uint32_t size,
                    uint32_t, const void* buffer) {
  if (size != sizeof(float) || !buffer) {
    return;
  }
  ++g_writes;
  g_lastPort = port;
  g_lastValue = *static_cast<const float*>(buffer);
}

static void click(Display* d, int x, int y) {
  XTestFakeMotionEvent(d, -1, x, y, CurrentTime);
  XTestFakeButtonEvent(d, Button1, True, CurrentTime);
  XTestFakeButtonEvent(d, Button1, False, CurrentTime);
  XFlush(d);
}

static void drag(Display* d, int x0, int y0, int x1, int y1) {
  XTestFakeMotionEvent(d, -1, x0, y0, CurrentTime);
  XTestFakeButtonEvent(d, Button1, True, CurrentTime);
  for (int i = 1; i <= 8; ++i) {
    XTestFakeMotionEvent(d, -1, x0 + (x1 - x0) * i / 8,
                         y0 + (y1 - y0) * i / 8, CurrentTime);
  }
  XTestFakeButtonEvent(d, Button1, False, CurrentTime);
  XFlush(d);
}

int main(int argc, char** argv) {
  const char* uiPath = argc > 1 ? argv[1] : nullptr;
  const char* outPath = argc > 2 ? argv[2] : "/tmp/cse-ui-live.png";
  if (!uiPath) {
    std::printf("usage: %s <CommonSenseEQUI.so> [out.png]\n", argv[0]);
    return 2;
  }

  Display* display = XOpenDisplay(nullptr);
  if (!display) {
    std::printf("FAIL: cannot open X display\n");
    return 1;
  }
  const int width = 1200;
  const int height = 640;
  Window parent = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0,
                                      width, height, 0, 0, 0x080b12);
  XMapWindow(display, parent);
  XSync(display, False);

  void* dl = dlopen(uiPath, RTLD_NOW);
  if (!dl) {
    std::printf("FAIL: dlopen: %s\n", dlerror());
    return 1;
  }
  auto descriptorFn =
      reinterpret_cast<LV2UIDescriptorFn>(dlsym(dl, "lv2ui_descriptor"));
  if (!descriptorFn) {
    std::printf("FAIL: lv2ui_descriptor missing\n");
    return 1;
  }
  const LV2UI_Descriptor* descriptor = descriptorFn(0);
  if (!descriptor) {
    return 1;
  }

  LV2_Feature parentFeature{LV2_UI__parent, &parent};
  const LV2_Feature* features[] = {&parentFeature, nullptr};
  std::string path(uiPath);
  std::string bundle = path.substr(0, path.find_last_of('/') + 1);
  LV2UI_Widget widget = nullptr;
  LV2UI_Handle handle = descriptor->instantiate(
      descriptor,
      "https://github.com/tylorismusic-commits/common-sense-eq#ui",
      bundle.c_str(), &writeFn, nullptr, &widget, features);
  if (!handle || !widget) {
    std::printf("FAIL: instantiate\n");
    return 1;
  }

  const LV2UI_Idle_Interface* idle = static_cast<const LV2UI_Idle_Interface*>(
      descriptor->extension_data(LV2_UI__idleInterface));
  if (!idle) {
    std::printf("FAIL: idle interface\n");
    return 1;
  }

  // Feed plausible telemetry so the analyzer and dynamic shading show up.
  float config[cse::PORT_COUNT] = {};
  for (int i = 0; i < cse::kParamCount; ++i) {
    config[cse::kParams[i].index] = cse::kParams[i].def;
  }
  for (int i = 0; i < cse::kParamCount; ++i) {
    descriptor->port_event(handle, cse::kParams[i].index, sizeof(float), 0,
                           &config[cse::kParams[i].index]);
  }
  float gain4 = 3.5f;
  float freq4 = 270.0f;
  float gain5 = -2.5f;
  float dynOn = 1.0f;
  descriptor->port_event(handle, cse::bandParam(3, cse::kSlotGain),
                         sizeof(float), 0, &gain4);
  descriptor->port_event(handle, cse::bandParam(3, cse::kSlotFreq),
                         sizeof(float), 0, &freq4);
  descriptor->port_event(handle, cse::bandParam(4, cse::kSlotGain),
                         sizeof(float), 0, &gain5);
  descriptor->port_event(handle, cse::bandParam(4, cse::kSlotDynOn),
                         sizeof(float), 0, &dynOn);
  for (uint32_t i = 0; i < cse::kSpectrumCount; ++i) {
    const float t = static_cast<float>(i) / (cse::kSpectrumCount - 1);
    float db = -58.0f + 24.0f * std::exp(-t * 2.8f) +
               8.0f * std::exp(-(t - 0.4f) * (t - 0.4f) / 0.003f);
    descriptor->port_event(handle, cse::PORT_SPECTRUM_1 + i, sizeof(float), 0,
                           &db);
  }
  float gr = -2.2f;
  descriptor->port_event(handle, cse::PORT_GR_1 + 4, sizeof(float), 0, &gr);

  for (int i = 0; i < 8; ++i) {
    idle->idle(handle);
    usleep(12000);
  }
  XSync(display, False);

  const int before = g_writes;
  click(display, 370, 27);           // next preset
  click(display, 944, 27);           // open dynamics panel
  click(display, 812, 325);          // threshold row (selected band 3)
  drag(display, 444, 263, 520, 215); // drag the 270 Hz bell node
  for (int i = 0; i < 12; ++i) {
    idle->idle(handle);
    usleep(12000);
  }
  XSync(display, False);
  const int writes = g_writes - before;
  std::printf("interaction writes=%d last port=%u value=%.3f\n", writes,
              g_lastPort, g_lastValue);
  if (writes < 4) {
    std::printf("FAIL: interaction did not write parameters\n");
    return 1;
  }

  // Capture the live window.
  Window uiWindow = static_cast<Window>(reinterpret_cast<uintptr_t>(widget));
  XWindowAttributes attributes;
  XGetWindowAttributes(display, uiWindow, &attributes);
  cairo_surface_t* xlib = cairo_xlib_surface_create(
      display, uiWindow, DefaultVisual(display, 0), attributes.width,
      attributes.height);
  cairo_surface_t* image = cairo_image_surface_create(
      CAIRO_FORMAT_ARGB32, attributes.width, attributes.height);
  cairo_t* cr = cairo_create(image);
  cairo_set_source_surface(cr, xlib, 0.0, 0.0);
  cairo_paint(cr);
  cairo_destroy(cr);
  const cairo_status_t status = cairo_surface_write_to_png(image, outPath);
  std::printf("captured %s status=%d\n", outPath, static_cast<int>(status));

  cairo_surface_destroy(image);
  cairo_surface_destroy(xlib);
  descriptor->cleanup(handle);
  dlclose(dl);
  XDestroyWindow(display, parent);
  XCloseDisplay(display);
  return status == CAIRO_STATUS_SUCCESS ? 0 : 1;
}
