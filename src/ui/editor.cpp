// Common Sense EQ - X11/Cairo editor, LV2 UI entry point, and preview tool
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <lv2/atom/atom.h>
#include <lv2/options/options.h>
#include <lv2/ui/ui.h>
#include <lv2/urid/urid.h>

#include "cse/param_info.hpp"
#include "ui/panel.hpp"
#include "ui/xdnd.hpp"

namespace cse {
namespace ui {

namespace {

double nowSeconds() {
  using clock = std::chrono::steady_clock;
  static const auto start = clock::now();
  return std::chrono::duration<double>(clock::now() - start).count();
}

float readScaleFactor(const LV2_Feature* const* features) {
  const LV2_URID_Map* map = nullptr;
  const LV2_Options_Option* options = nullptr;
  for (const LV2_Feature* const* f = features; f && *f; ++f) {
    if (!std::strcmp((*f)->URI, LV2_URID__map) && (*f)->data) {
      map = static_cast<const LV2_URID_Map*>(
          static_cast<const void*>((*f)->data));
    } else if (!std::strcmp((*f)->URI, LV2_OPTIONS__options) && (*f)->data) {
      options = static_cast<const LV2_Options_Option*>(
          static_cast<const void*>((*f)->data));
    }
  }
  if (!map || !options) {
    return 1.0f;
  }
  const LV2_URID key = map->map(map->handle, LV2_UI__scaleFactor);
  const LV2_URID tFloat = map->map(map->handle, LV2_ATOM__Float);
  const LV2_URID tDouble = map->map(map->handle, LV2_ATOM__Double);
  for (const LV2_Options_Option* o = options; o->key; ++o) {
    if (o->key != key || !o->value) {
      continue;
    }
    if (o->type == tFloat && o->size >= sizeof(float)) {
      return *static_cast<const float*>(o->value);
    }
    if (o->type == tDouble && o->size >= sizeof(double)) {
      return static_cast<float>(*static_cast<const double*>(o->value));
    }
  }
  return 1.0f;
}

int ignoreXError(Display*, XErrorEvent*) { return 0; }

bool isValidWindow(Display* display, Window window) {
  if (!window) {
    return false;
  }
  XErrorHandler old = XSetErrorHandler(ignoreXError);
  XWindowAttributes attributes;
  const Status status = XGetWindowAttributes(display, window, &attributes);
  XSync(display, False);
  XSetErrorHandler(old);
  return status != 0;
}

void chooseSize(Display* display, float scaleFactor, int& width, int& height) {
  const int screen = DefaultScreen(display);
  const double screenW = DisplayWidth(display, screen);
  const double screenH = DisplayHeight(display, screen);
  double sf = std::min(std::max(static_cast<double>(scaleFactor), 1.0), 2.2);
  double w = kWidth * sf;
  double h = kHeight * sf;
  const double fit = std::min(1.0, std::min(screenW * 0.92 / w,
                                            screenH * 0.86 / h));
  w *= fit;
  h *= fit;
  width = std::max(static_cast<int>(std::lround(w)), 560);
  height = std::max(static_cast<int>(std::lround(h)), 280);
}

struct Host {
  LV2UI_Write_Function write = nullptr;
  LV2UI_Controller controller = nullptr;
};

struct X11UI {
  Display* display = nullptr;
  Window parent = 0;
  Window window = 0;
  bool topLevel = false;
  Visual* visual = nullptr;
  int depth = 24;
  cairo_surface_t* windowSurface = nullptr;
  cairo_surface_t* imageSurface = nullptr;
  cairo_t* imageCr = nullptr;
  int surfaceW = 0;
  int surfaceH = 0;
  UiState state;
  Panel panel;
  Host host;
  XdndDropTarget xdnd;
  int windowW = 0;
  int windowH = 0;
  double scale = 1.0;
  double offsetX = 0.0;
  double offsetY = 0.0;
  const LV2UI_Resize* hostResize = nullptr;
  float scaleFactor = 1.0f;
  bool dirty = true;
  bool dragging = false;
  double lastDraw = 0.0;
  unsigned long lastClickTime = 0;
  int lastClickX = 0;
  int lastClickY = 0;
};

X11UI* get(LV2UI_Handle handle) { return static_cast<X11UI*>(handle); }

void sendValue(const X11UI* ui, uint32_t port, float value) {
  if (ui->host.write) {
    ui->host.write(ui->host.controller, port, sizeof(float), 0, &value);
  }
}

void updateScale(X11UI* ui) {
  if (ui->windowW <= 0 || ui->windowH <= 0) {
    return;
  }
  ui->scale = std::min(static_cast<double>(ui->windowW) / kWidth,
                       static_cast<double>(ui->windowH) / kHeight);
  ui->scale = std::max(ui->scale, 0.1);
  ui->offsetX = (ui->windowW - kWidth * ui->scale) * 0.5;
  ui->offsetY = (ui->windowH - kHeight * ui->scale) * 0.5;
}

void ensureSurfaces(X11UI* ui) {
  if (ui->windowW <= 0 || ui->windowH <= 0) {
    return;
  }
  if (!ui->imageSurface || ui->surfaceW != ui->windowW ||
      ui->surfaceH != ui->windowH) {
    if (ui->imageCr) {
      cairo_destroy(ui->imageCr);
    }
    if (ui->imageSurface) {
      cairo_surface_destroy(ui->imageSurface);
    }
    ui->imageSurface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, ui->windowW, ui->windowH);
    ui->imageCr = cairo_create(ui->imageSurface);
    ui->surfaceW = ui->windowW;
    ui->surfaceH = ui->windowH;
  }
}

void blit(X11UI* ui) {
  ensureSurfaces(ui);
  if (!ui->imageCr) {
    return;
  }
  cairo_set_source_rgb(ui->imageCr, 0.031, 0.043, 0.067);
  cairo_paint(ui->imageCr);
  cairo_save(ui->imageCr);
  cairo_translate(ui->imageCr, ui->offsetX, ui->offsetY);
  cairo_scale(ui->imageCr, ui->scale, ui->scale);
  ui->panel.draw(ui->imageCr, ui->state);
  cairo_restore(ui->imageCr);
  cairo_surface_flush(ui->imageSurface);

  if (!ui->windowSurface) {
    ui->windowSurface = cairo_xlib_surface_create(
        ui->display, ui->window, ui->visual, ui->windowW, ui->windowH);
  }
  cairo_t* cr = cairo_create(ui->windowSurface);
  cairo_set_source_surface(cr, ui->imageSurface, 0.0, 0.0);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_flush(ui->windowSurface);
  XFlush(ui->display);
  ui->dirty = false;
  ui->lastDraw = nowSeconds();
}

void toLogical(const X11UI* ui, int x, int y, double& lx, double& ly) {
  lx = (x - ui->offsetX) / ui->scale;
  ly = (y - ui->offsetY) / ui->scale;
}

void onButtonPress(X11UI* ui, XButtonEvent& ev) {
  if (ui->display && ui->window) {
    XSetInputFocus(ui->display, ui->window, RevertToParent, CurrentTime);
    XFlush(ui->display);
  }
  double x = 0.0;
  double y = 0.0;
  toLogical(ui, ev.x, ev.y, x, y);
  const bool fine = (ev.state & ShiftMask) != 0;

  if (ev.button == Button4 || ev.button == Button5) {
    const double delta = ev.button == Button4 ? 1.0 : -1.0;
    if (ui->panel.wheel(ui->state, x, y, delta, fine,
                        [ui](uint32_t port, float value) {
                          sendValue(ui, port, value);
                        })) {
      ui->dirty = true;
    }
    return;
  }

  if (ev.button == Button3) {
    if (ui->panel.rightClick(ui->state, x, y)) {
      ui->dirty = true;
    }
    return;
  }
  if (ev.button != Button1) {
    return;
  }

  const bool doubleClick =
      ui->lastClickTime != 0 && ev.time - ui->lastClickTime < 280 &&
      std::abs(ev.x - ui->lastClickX) < 6 && std::abs(ev.y - ui->lastClickY) < 6;
  ui->lastClickTime = ev.time;
  ui->lastClickX = ev.x;
  ui->lastClickY = ev.y;

  // Only swallow the second click when it actually performed a reset.  The
  // power button and the other chips are not resettable, so a quick second
  // click there must toggle them again instead of doing nothing.
  if (doubleClick &&
      ui->panel.doubleClick(ui->state, x, y,
                            [ui](uint32_t port, float value) {
                              sendValue(ui, port, value);
                            })) {
    ui->dirty = true;
    return;
  }

  if (ui->panel.pointerDown(ui->state, x, y, fine,
                            [ui](uint32_t port, float value) {
                              sendValue(ui, port, value);
                            })) {
    ui->dirty = true;
    ui->dragging = true;
  }
}

void onButtonRelease(X11UI* ui) {
  ui->panel.pointerUp(ui->state);
  ui->dragging = false;
}

void onMotion(X11UI* ui, XMotionEvent& ev) {
  double x = 0.0;
  double y = 0.0;
  toLogical(ui, ev.x, ev.y, x, y);
  ui->panel.setHover(x, y);
  ui->dirty = true;
  if (!ui->dragging) {
    return;
  }
  const bool fine = (ev.state & ShiftMask) != 0;
  if (ui->panel.pointerMove(ui->state, x, y, fine,
                            [ui](uint32_t port, float value) {
                              sendValue(ui, port, value);
                            })) {
    ui->dirty = true;
  }
}

void processEvents(X11UI* ui) {
  if (!ui->display) {
    return;
  }
  while (XPending(ui->display)) {
    XEvent ev;
    XNextEvent(ui->display, &ev);
    if (ui->xdnd.handle(ev)) {
      ui->dirty = true;
      continue;
    }
    switch (ev.type) {
      case Expose:
        ui->dirty = true;
        break;
      case ConfigureNotify:
        if (ev.xconfigure.width != ui->windowW ||
            ev.xconfigure.height != ui->windowH) {
          ui->windowW = ev.xconfigure.width;
          ui->windowH = ev.xconfigure.height;
          if (ui->windowSurface) {
            cairo_xlib_surface_set_size(ui->windowSurface, ui->windowW,
                                        ui->windowH);
          }
          updateScale(ui);
          ui->dirty = true;
        }
        break;
      case ButtonPress:
        onButtonPress(ui, ev.xbutton);
        break;
      case ButtonRelease:
        onButtonRelease(ui);
        break;
      case MotionNotify:
        onMotion(ui, ev.xmotion);
        break;
      case KeyPress: {
        char buf[8] = {};
        KeySym ks = 0;
        XLookupString(&ev.xkey, buf, sizeof(buf) - 1, &ks, nullptr);
        int key = 0;
        if (ks == XK_BackSpace) key = 8;
        else if (ks == XK_Return || ks == XK_KP_Enter) key = 13;
        else if (ks == XK_Escape) key = 27;
        if (ui->panel.key(ui->state, key, buf,
                          [ui](uint32_t port, float value) {
                            sendValue(ui, port, value);
                          })) {
          ui->dirty = true;
        }
        break;
      }
      case LeaveNotify:
        ui->panel.setHover(-10000.0, -10000.0);
        ui->dirty = true;
        break;
      default:
        break;
    }
  }
}

int idle(LV2UI_Handle handle) {
  X11UI* ui = get(handle);
  if (!ui) {
    return 1;
  }
  processEvents(ui);
  if (ui->dirty || nowSeconds() - ui->lastDraw > 0.05) {
    blit(ui);
  }
  return 0;
}

LV2UI_Handle uiInstantiate(const LV2UI_Descriptor*, const char*,
                           const char* bundlePath, LV2UI_Write_Function write,
                           LV2UI_Controller controller, LV2UI_Widget* widget,
                           const LV2_Feature* const* features) {
  (void)bundlePath;
  if (!write || !widget) {
    return nullptr;
  }
  X11UI* ui = new X11UI();
  initState(ui->state);
  ui->host.write = write;
  ui->host.controller = controller;

  const void* parentData = nullptr;
  for (const LV2_Feature* const* f = features; f && *f; ++f) {
    if (!std::strcmp((*f)->URI, LV2_UI__parent) && (*f)->data) {
      parentData = (*f)->data;
    } else if (!std::strcmp((*f)->URI, LV2_UI__resize) && (*f)->data) {
      ui->hostResize = static_cast<const LV2UI_Resize*>(
          static_cast<const void*>((*f)->data));
    }
  }

  ui->display = XOpenDisplay(nullptr);
  if (!ui->display) {
    delete ui;
    return nullptr;
  }
  ui->scaleFactor = readScaleFactor(features);

  Window parent = 0;
  if (parentData) {
    const uintptr_t raw = reinterpret_cast<uintptr_t>(parentData);
    if (raw != 0 && raw <= 0xffffffffu &&
        isValidWindow(ui->display, static_cast<Window>(raw))) {
      parent = static_cast<Window>(raw);
    } else {
      parent = *static_cast<const Window*>(parentData);
    }
  }
  if (!isValidWindow(ui->display, parent)) {
    parent = DefaultRootWindow(ui->display);
    ui->topLevel = true;
  }

  XWindowAttributes attributes;
  if (!XGetWindowAttributes(ui->display, parent, &attributes)) {
    XCloseDisplay(ui->display);
    delete ui;
    return nullptr;
  }
  ui->visual = attributes.visual;
  ui->depth = attributes.depth;

  int width = 0;
  int height = 0;
  chooseSize(ui->display, ui->scaleFactor, width, height);
  ui->windowW = width;
  ui->windowH = height;
  updateScale(ui);

  XSetWindowAttributes swa;
  std::memset(&swa, 0, sizeof(swa));
  swa.background_pixel = 0x080b12;
  swa.border_pixel = 0;
  swa.event_mask = ExposureMask | StructureNotifyMask | ButtonPressMask |
                   KeyPressMask |
                   ButtonReleaseMask | PointerMotionMask | LeaveWindowMask;
  swa.colormap = attributes.colormap
                     ? attributes.colormap
                     : DefaultColormap(ui->display, DefaultScreen(ui->display));
  ui->parent = parent;
  ui->window = XCreateWindow(ui->display, parent, 0, 0, width, height, 0,
                             attributes.depth, InputOutput, attributes.visual,
                             CWBackPixel | CWBorderPixel | CWEventMask |
                                 CWColormap,
                             &swa);
  if (!ui->window) {
    XCloseDisplay(ui->display);
    delete ui;
    return nullptr;
  }
  if (ui->topLevel) {
    ui->xdnd.init(ui->display, ui->window, [ui](const std::string& path) {
    if (ui->panel.importDroppedFile(ui->state, path,
                                    [ui](uint32_t port, float value) {
                                      sendValue(ui, port, value);
                                    })) {
      ui->dirty = true;
    }
  });
  XStoreName(ui->display, ui->window, "Common Sense EQ");
  }
  XMapRaised(ui->display, ui->window);
  XFlush(ui->display);

  ensureSurfaces(ui);
  blit(ui);
  *widget = reinterpret_cast<LV2UI_Widget>(static_cast<uintptr_t>(ui->window));

  if (ui->hostResize && ui->hostResize->ui_resize) {
    ui->hostResize->ui_resize(ui->hostResize->handle, width, height);
  }
  return ui;
}

void uiCleanup(LV2UI_Handle handle) {
  X11UI* ui = get(handle);
  if (!ui) {
    return;
  }
  if (ui->imageCr) cairo_destroy(ui->imageCr);
  if (ui->imageSurface) cairo_surface_destroy(ui->imageSurface);
  if (ui->windowSurface) cairo_surface_destroy(ui->windowSurface);
  if (ui->display) {
    if (ui->window) XDestroyWindow(ui->display, ui->window);
    XCloseDisplay(ui->display);
  }
  delete ui;
}

void uiPortEvent(LV2UI_Handle handle, uint32_t port, uint32_t size,
                 uint32_t format, const void* buffer) {
  X11UI* ui = get(handle);
  if (!ui || !buffer || size != sizeof(float) || format != 0) {
    return;
  }
  const float v = *static_cast<const float*>(buffer);
  if (!std::isfinite(v)) {
    return;
  }
  if (port >= PORT_SPECTRUM_1 && port < PORT_SPECTRUM_1 + kSpectrumCount) {
    ui->state.spectrum[port - PORT_SPECTRUM_1] = v;
    ui->dirty = true;
  } else if (port >= PORT_GR_1 && port < PORT_GR_1 + kGrCount) {
    ui->state.dyn[port - PORT_GR_1] = v;
    ui->dirty = true;
  } else if (port < PORT_COUNT) {
    const ParamInfo* info = infoFor(port);
    float value = v;
    if (info) {
      value = clamp(value, std::min(info->min, info->max),
                    std::max(info->min, info->max));
    }
    ui->state.values[port] = value;
    ui->dirty = true;
  }
}

int uiResize(LV2UI_Handle handle, int width, int height) {
  X11UI* ui = get(handle);
  if (!ui) {
    return 1;
  }
  width = std::max(width, 560);
  height = std::max(height, 280);
  ui->windowW = width;
  ui->windowH = height;
  updateScale(ui);
  if (ui->window) {
    XResizeWindow(ui->display, ui->window, width, height);
  }
  if (ui->windowSurface) {
    cairo_xlib_surface_set_size(ui->windowSurface, width, height);
  }
  ui->dirty = true;
  return 0;
}

const LV2UI_Idle_Interface kIdle = {idle};
const LV2UI_Resize kResize = {nullptr, uiResize};

const void* uiExtensionData(const char* uri) {
  if (!std::strcmp(uri, LV2_UI__idleInterface)) {
    return &kIdle;
  }
  if (!std::strcmp(uri, LV2_UI__resize)) {
    return &kResize;
  }
  return nullptr;
}

} // namespace

} // namespace ui
} // namespace cse

static const LV2UI_Descriptor kDescriptor = {
    "https://github.com/tylorismusic-commits/common-sense-eq#ui",
    &cse::ui::uiInstantiate,
    &cse::ui::uiCleanup,
    &cse::ui::uiPortEvent,
    &cse::ui::uiExtensionData,
};

extern "C" LV2_SYMBOL_EXPORT const LV2UI_Descriptor* lv2ui_descriptor(
    uint32_t index) {
  return index == 0 ? &kDescriptor : nullptr;
}

// ---------------------------------------------------------------------------
// Offline preview: build/cse_ui_preview <out.png> [scale]
// ---------------------------------------------------------------------------
#ifdef CSE_UI_PREVIEW
int main(int argc, char** argv) {
  using namespace cse;
  using namespace cse::ui;
  UiState state;
  initState(state);
  applyPreset(state, 4, nullptr); // Dynamic De-Ess
  state.selectedBand = 5;
  state.showDyn = true;
  // A plausible spectrum so the preview shows the analyzer.
  for (uint32_t i = 0; i < kSpectrumCount; ++i) {
    const float t = static_cast<float>(i) / (kSpectrumCount - 1);
    float db = -58.0f + 26.0f * std::exp(-t * 3.2f) +
               9.0f * std::exp(-(t - 0.34f) * (t - 0.34f) / 0.002f) +
               7.0f * std::exp(-(t - 0.72f) * (t - 0.72f) / 0.004f);
    db += 2.0f * std::sin(t * 47.0f);
    state.spectrum[i] = db;
  }
  state.dyn[5] = -2.4f;
  state.values[bandParam(5, kSlotGain)] = -3.2f;
  state.values[bandParam(4, kSlotGain)] = 1.8f;
  state.values[bandParam(3, kSlotGain)] = -1.2f;
  state.values[bandParam(2, kSlotGain)] = 2.4f;

  const double scale = (argc > 2) ? std::atof(argv[2]) : 1.0;
  const char* path = (argc > 1) ? argv[1] : "common-sense-eq.png";
  cairo_surface_t* surface = cairo_image_surface_create(
      CAIRO_FORMAT_ARGB32, static_cast<int>(kWidth * scale),
      static_cast<int>(kHeight * scale));
  cairo_t* cr = cairo_create(surface);
  cairo_scale(cr, scale, scale);
  Panel panel;
  panel.draw(cr, state);
  cairo_destroy(cr);
  cairo_surface_write_to_png(surface, path);
  cairo_surface_destroy(surface);
  std::printf("wrote %s\n", path);
  return 0;
}
#endif
