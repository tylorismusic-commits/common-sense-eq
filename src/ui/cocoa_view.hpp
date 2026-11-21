// Common Sense EQ - Cocoa view host
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace cse {
namespace ui {

struct UiState;
class Panel;

// Hosts the existing cairo Panel inside an NSView.  The panel itself is
// platform-neutral; this class only owns the view, the timer that pulls new
// meter values and the mouse/key translation.
class CocoaView {
public:
  using Setter = std::function<void(uint32_t, float)>;
  using Sync = std::function<void(UiState&)>;

  CocoaView();
  ~CocoaView();
  CocoaView(const CocoaView&) = delete;
  CocoaView& operator=(const CocoaView&) = delete;

  void setContent(Panel* panel, UiState* state, Setter set, Sync sync);
  void attach(void* parentNsView);
  void detach();
  void resize(int width, int height);
  void refresh();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ui
} // namespace cse
