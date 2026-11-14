// Minimal copy of the REAPER embedded-UI ABI used by the CLAP build.
// Source: reaper-sdk/sdk/reaper_plugin_fx_embed.h (Cockos).
// Only the declarations needed to paint and handle the TCP/MCP view are here.
#pragma once

#include <cstdint>

#include <clap/clap.h>

namespace reaper {

using INT_PTR = intptr_t;

struct IBitmap {
  virtual ~IBitmap() {}
  virtual unsigned int* getBits() = 0;
  virtual int getWidth() = 0;
  virtual int getHeight() = 0;
  virtual int getRowSpan() = 0; // in sizeof(unsigned int) units
  virtual bool isFlipped() { return false; }
  virtual bool resize(int, int) = 0;
  virtual void* getDC() { return nullptr; }
  virtual INT_PTR Extended(int, void*) { return 0; }
};

struct DrawInfo {
  int context;
  int dpi;
  int mousewheel_amt;
  double _res2;
  int width;
  int height;
  int mouse_x;
  int mouse_y;
  int flags;
  int _res3;
  void* spare[6];
};

struct SizeHints {
  int preferred_aspect; // 16.16 fixed point
  int minimum_aspect;
  int flags;
  int dpi;
  int _res3;
  int _res4;
  int min_width;
  int min_height;
  int max_width;
  int max_height;
};

enum : int {
  WM_IS_SUPPORTED = 0x0000,
  WM_CREATE = 0x0001,
  WM_DESTROY = 0x0002,
  WM_PAINT = 0x000F,
  WM_SETCURSOR = 0x0020,
  WM_GETMINMAXINFO = 0x0024,
  WM_MOUSEMOVE = 0x0200,
  WM_LBUTTONDOWN = 0x0201,
  WM_LBUTTONUP = 0x0202,
  WM_LBUTTONDBLCLK = 0x0203,
  WM_RBUTTONDOWN = 0x0204,
  WM_RBUTTONUP = 0x0205,
  WM_RBUTTONDBLCLK = 0x0206,
  WM_MOUSEWHEEL = 0x020A,
  WM_NOBORDER = 0x100001,
  WM_HITTEST = 0x100002,
};

enum : INT_PTR {
  RETNOTIFY_HANDLED = 0x0000001,
  RETNOTIFY_INVALIDATE = 0x1000000,
};

struct EmbedUI {
  INT_PTR (*inline_editor)(const clap_plugin_t* plugin, int msg, void* param1,
                           void* param2);
};

inline constexpr const char* kExtensionId = "cockos.reaper_embedui";

} // namespace reaper
