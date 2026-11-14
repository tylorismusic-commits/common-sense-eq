// Common Sense EQ - CLAP host smoke test
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <vector>
#include <unistd.h>

#ifdef CSE_HAVE_XTEST
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <cairo/cairo-xlib.h>
#endif

#include <clap/clap.h>

#include "clap/reaper_embed.hpp"
#include "cse/param_info.hpp"
#include "dsp/inline_curve.hpp"

namespace {

int failures = 0;
void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

// ---------------------------------------------------------------------------
// Stub host
// ---------------------------------------------------------------------------
const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequestRestart(const clap_host_t*) {}
void hostRequestProcess(const clap_host_t*) {}
void hostRequestCallback(const clap_host_t*) {}

const clap_host_t gHost = {
    CLAP_VERSION,
    nullptr,
    "test host",
    "tylor",
    "https://example.invalid",
    "0.1",
    &hostGetExtension,
    &hostRequestRestart,
    &hostRequestProcess,
    &hostRequestCallback};

uint32_t noSize(const clap_input_events_t*) { return 0; }
const clap_event_header_t* noGet(const clap_input_events_t*, uint32_t) {
  return nullptr;
}
static int gOutEventCount = 0;
bool outPush(const clap_output_events_t*, const clap_event_header_t*) {
  ++gOutEventCount;
  return true;
}

const clap_input_events_t gInEvents = {nullptr, &noSize, &noGet};
const clap_output_events_t gOutEvents = {nullptr, &outPush};

// ---------------------------------------------------------------------------
// Fake REAPER bitmap
// ---------------------------------------------------------------------------
struct FakeBitmap : reaper::IBitmap {
  int w = 320;
  int h = 90;
  std::vector<unsigned int> pixels;
  FakeBitmap() : pixels(static_cast<size_t>(w) * h, 0u) {}
  unsigned int* getBits() override { return pixels.data(); }
  int getWidth() override { return w; }
  int getHeight() override { return h; }
  int getRowSpan() override { return w; }
  bool resize(int nw, int nh) override {
    w = nw;
    h = nh;
    pixels.assign(static_cast<size_t>(w) * h, 0u);
    return true;
  }
};

struct MemoryStream {
  std::vector<char> data;
  size_t readPos = 0;
};

int64_t memWrite(const clap_ostream_t* stream, const void* buffer,
                 uint64_t size) {
  auto* m = static_cast<MemoryStream*>(stream->ctx);
  const char* p = static_cast<const char*>(buffer);
  m->data.insert(m->data.end(), p, p + size);
  return static_cast<int64_t>(size);
}

int64_t memRead(const clap_istream_t* stream, void* buffer, uint64_t size) {
  auto* m = static_cast<MemoryStream*>(stream->ctx);
  const size_t left = m->data.size() - m->readPos;
  const size_t n = std::min<size_t>(left, static_cast<size_t>(size));
  std::memcpy(buffer, m->data.data() + m->readPos, n);
  m->readPos += n;
  return static_cast<int64_t>(n);
}

} // namespace

int main(int argc, char** argv) {
  const char* path = argc > 1 ? argv[1] : "build/clap/CommonSenseEQ.clap";
  const char* id = argc > 2 ? argv[2] : "com.tylorismusic.commonsense";
  const bool guiMode = argc > 3 && !std::strcmp(argv[3], "--gui");

  void* lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  check(lib != nullptr, "CLAP loads");
  if (!lib) {
    std::printf("%s\n", dlerror());
    return 1;
  }
  auto* entry =
      static_cast<const clap_plugin_entry_t*>(dlsym(lib, "clap_entry"));
  check(entry != nullptr, "clap_entry exported");
  if (!entry) {
    dlclose(lib);
    return 1;
  }
  check(entry->init(path), "entry init");
  auto* factory = static_cast<const clap_plugin_factory_t*>(
      entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  check(factory != nullptr, "plugin factory");
  check(factory->get_plugin_count(factory) == 1, "one plugin");
  const clap_plugin_descriptor_t* desc = factory->get_plugin_descriptor(factory, 0);
  check(desc && !std::strcmp(desc->id, id), "plugin id");

  const clap_plugin_t* plugin = factory->create_plugin(factory, &gHost, id);
  check(plugin != nullptr, "plugin created");
  if (!plugin) {
    entry->deinit();
    dlclose(lib);
    return 1;
  }

  check(plugin->init(plugin), "plugin init");
  auto* params = static_cast<const clap_plugin_params_t*>(
      plugin->get_extension(plugin, CLAP_EXT_PARAMS));
  check(params != nullptr, "params extension");
  if (params) {
    check(params->count(plugin) == static_cast<uint32_t>(cse::kParamCount),
          "parameter count");
    clap_param_info_t info{};
    check(params->get_info(plugin, 0, &info), "parameter info");
    char text[64];
    check(params->value_to_text(plugin, info.id, info.default_value, text,
                                sizeof(text)),
          "value to text");
  }

  auto* embed = static_cast<const reaper::EmbedUI*>(
      plugin->get_extension(plugin, reaper::kExtensionId));
  check(embed != nullptr, "REAPER embedded UI extension");
  if (embed) {
    check(embed->inline_editor(plugin, reaper::WM_IS_SUPPORTED, nullptr,
                               nullptr) == 1,
          "embedded UI supported");
    reaper::SizeHints hints{};
    check(embed->inline_editor(plugin, reaper::WM_GETMINMAXINFO, nullptr,
                               &hints) == 1,
          "embedded UI size hints");
    FakeBitmap bitmap;
    reaper::DrawInfo draw{};
    draw.context = 1;
    draw.dpi = 256;
    draw.width = bitmap.w;
    draw.height = bitmap.h;
    check(embed->inline_editor(plugin, reaper::WM_PAINT, &bitmap, &draw) == 1,
          "embedded UI paints");
    unsigned checksum = 0;
    for (unsigned v : bitmap.pixels) {
      checksum += v;
    }
    check(checksum != 0, "embedded UI bitmap has content");

    // Drag the 270 Hz bell in the embedded view.
    double beforeFreq = 0.0;
    double afterFreq = 0.0;
    if (params) {
      params->get_value(plugin, cse::bandParam(3, cse::kSlotFreq),
                        &beforeFreq);
    }
    // Default Bell 2 is 270 Hz at 0 dB.
    const auto rect = cse::plotRect(bitmap.w, bitmap.h, true);
    const float nx = cse::freqX(270.0f, rect);
    const float ny = cse::dbY(0.0f, rect);
    draw.width = bitmap.w;
    draw.height = bitmap.h;
    draw.mouse_x = static_cast<int>(nx);
    draw.mouse_y = static_cast<int>(ny);
    embed->inline_editor(plugin, reaper::WM_LBUTTONDOWN, nullptr, &draw);
    draw.mouse_x = static_cast<int>(nx) + 80;
    draw.mouse_y = static_cast<int>(ny) - 12;
    embed->inline_editor(plugin, reaper::WM_MOUSEMOVE, nullptr, &draw);
    embed->inline_editor(plugin, reaper::WM_LBUTTONUP, nullptr, &draw);
    if (params) {
      params->get_value(plugin, cse::bandParam(3, cse::kSlotFreq),
                        &afterFreq);
      check(afterFreq > beforeFreq * 1.2,
            "embedded drag changes frequency");
    }
  }

  check(plugin->activate(plugin, 48000.0, 1, 512), "activate");
  check(plugin->start_processing(plugin), "start processing");

  std::vector<float> inL(512, 0.1f), inR(512, 0.1f), outL(512, 0.0f),
      outR(512, 0.0f);
  float* inChans[2] = {inL.data(), inR.data()};
  float* outChans[2] = {outL.data(), outR.data()};
  clap_audio_buffer_t inBuf{};
  inBuf.data32 = inChans;
  inBuf.channel_count = 2;
  clap_audio_buffer_t outBuf{};
  outBuf.data32 = outChans;
  outBuf.channel_count = 2;

  clap_process_t process{};
  process.frames_count = 512;
  process.audio_inputs = &inBuf;
  process.audio_inputs_count = 1;
  process.audio_outputs = &outBuf;
  process.audio_outputs_count = 1;
  process.in_events = &gInEvents;
  process.out_events = &gOutEvents;
  for (int block = 0; block < 16; ++block) {
    const clap_process_status status = plugin->process(plugin, &process);
    check(status == CLAP_PROCESS_CONTINUE, "process continues");
  }
  if (guiMode) {
#ifdef CSE_HAVE_XTEST
    Display* display = XOpenDisplay(nullptr);
    check(display != nullptr, "GUI test opens X display");
    if (display) {
      Window parent = XCreateSimpleWindow(display, DefaultRootWindow(display),
                                          0, 0, 1120, 560, 0, 0, 0x080b12);
      XMapWindow(display, parent);
      XSync(display, False);
      auto* gui = static_cast<const clap_plugin_gui_t*>(
          plugin->get_extension(plugin, CLAP_EXT_GUI));
      check(gui != nullptr, "GUI extension");
      if (gui) {
        check(gui->is_api_supported(plugin, CLAP_WINDOW_API_X11, false),
              "X11 GUI supported");
        check(gui->create(plugin, CLAP_WINDOW_API_X11, false), "GUI create");
        clap_window_t window{};
        window.api = CLAP_WINDOW_API_X11;
        window.x11 = parent;
        check(gui->set_parent(plugin, &window), "GUI set parent");
        check(gui->show(plugin), "GUI show");
        usleep(300000);

        // Feed a 1 kHz tone so the analyzer and the dynamic display have
        // something to draw, then capture the embedded editor.
        for (int block = 0; block < 48; ++block) {
          for (int i = 0; i < 512; ++i) {
            const double t = (block * 512 + i) / 48000.0;
            inL[i] = inR[i] = static_cast<float>(0.5 * std::sin(2.0 * M_PI * 1000.0 * t));
          }
          plugin->process(plugin, &process);
        }
        usleep(500000);
        Window root = 0;
        Window parentWindow = 0;
        Window* children = nullptr;
        unsigned int childCount = 0;
        Window uiWindow = parent;
        if (XQueryTree(display, parent, &root, &parentWindow, &children,
                       &childCount) &&
            childCount > 0) {
          uiWindow = children[childCount - 1];
        }
        if (children) {
          XFree(children);
        }
        XWindowAttributes childAttributes{};
        if (XGetWindowAttributes(display, uiWindow, &childAttributes) &&
            childAttributes.width > 0) {
          cairo_surface_t* source = cairo_xlib_surface_create(
              display, uiWindow, DefaultVisual(display, 0),
              childAttributes.width, childAttributes.height);
          cairo_surface_t* image = cairo_image_surface_create(
              CAIRO_FORMAT_ARGB32, childAttributes.width,
              childAttributes.height);
          cairo_t* cr = cairo_create(image);
          cairo_set_source_surface(cr, source, 0.0, 0.0);
          cairo_paint(cr);
          cairo_destroy(cr);
          cairo_surface_write_to_png(image, "/tmp/cse-clap-ui.png");
          cairo_surface_destroy(image);
          cairo_surface_destroy(source);
        }

        gOutEventCount = 0;
        XTestFakeMotionEvent(display, -1, 32, 27, CurrentTime);
        XTestFakeButtonEvent(display, Button1, True, CurrentTime);
        XTestFakeButtonEvent(display, Button1, False, CurrentTime);
        XFlush(display);
        usleep(150000);
        plugin->process(plugin, &process);
        check(gOutEventCount > 0, "GUI changes reach host param events");
        gui->hide(plugin);
        gui->destroy(plugin);
      }
      XDestroyWindow(display, parent);
      XCloseDisplay(display);
    }
#endif
  }

  bool finite = true;
  for (float v : outL) {
    if (!std::isfinite(v)) {
      finite = false;
    }
  }
  check(finite, "CLAP output is finite");

  auto* state = static_cast<const clap_plugin_state_t*>(
      plugin->get_extension(plugin, CLAP_EXT_STATE));
  check(state != nullptr, "state extension");
  if (state) {
    MemoryStream mem;
    clap_ostream_t os{&mem, &memWrite};
    check(state->save(plugin, &os), "state save");
    check(mem.data.size() > 8, "state has content");
    MemoryStream inMem;
    inMem.data = mem.data;
    clap_istream_t is{&inMem, &memRead};
    check(state->load(plugin, &is), "state load");
  }

  plugin->stop_processing(plugin);
  plugin->deactivate(plugin);
  plugin->destroy(plugin);
  entry->deinit();
  dlclose(lib);

  if (failures == 0) {
    std::printf("CLAP host test passed\n");
    return 0;
  }
  return 1;
}
