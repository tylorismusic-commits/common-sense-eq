// Common Sense EQ - user preset tests
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "ui/panel.hpp"

using namespace cse;
using namespace cse::ui;

namespace {
int gFailures = 0;
void check(bool ok, const char* what) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++gFailures;
}
} // namespace

int main() {
  namespace fs = std::filesystem;
  const fs::path root = fs::temp_directory_path() / "cse-preset-test";
  fs::remove_all(root);
  fs::create_directories(root / "config");
  fs::create_directories(root / "home");
  const std::string oldXdg =
      std::getenv("XDG_CONFIG_HOME") ? std::getenv("XDG_CONFIG_HOME") : "";
  const std::string oldHome =
      std::getenv("HOME") ? std::getenv("HOME") : "";
  setenv("XDG_CONFIG_HOME", (root / "config").string().c_str(), 1);
  setenv("HOME", (root / "home").string().c_str(), 1);

  UiState state;
  initState(state);
  Panel panel;
  auto set = [&](uint32_t port, float value) { state.values[port] = value; };

  state.values[PORT_OUTPUT_GAIN] = 3.0f;
  panel.pointerDown(state, 200, 27, false, set);
  panel.pointerUp(state);
  check(state.presetOpen, "preset chip opens the browser");
  panel.pointerDown(state, 90, 245, false, set);
  panel.pointerUp(state);
  check(state.savePreset, "SAVE opens the name field");
  panel.key(state, 0, "My EQ", set);
  panel.key(state, 13, nullptr, set);
  check(state.userPresets.size() == 1, "user preset is written");
  check(state.presetIndex == kPresetCount, "saved preset becomes active");
  state.values[PORT_OUTPUT_GAIN] = 0.0f;
  applyPreset(state, state.presetIndex, set);
  check(std::fabs(state.values[PORT_OUTPUT_GAIN] - 3.0f) < 1e-5f,
        "user preset recalls every parameter");

  panel.pointerDown(state, 200, 27, false, set);
  panel.pointerUp(state);
  panel.pointerDown(state, 90, 275, false, set);
  panel.pointerUp(state);
  panel.key(state, 0, "Renamed", set);
  panel.key(state, 13, nullptr, set);
  check(state.userPresets.size() == 1 &&
            state.userPresets[0].name == "Renamed",
        "user preset renames");

  panel.pointerDown(state, 200, 27, false, set);
  panel.pointerUp(state);
  panel.pointerDown(state, 160, 245, false, set);
  panel.pointerUp(state);
  check(state.userPresets.size() == 2, "DUPLICATE creates a preset");

  panel.pointerDown(state, 200, 27, false, set);
  panel.pointerUp(state);
  panel.pointerDown(state, 230, 275, false, set);
  panel.pointerUp(state);
  const fs::path exported =
      userPresetExportDir() / (std::string("Renamed copy") + kUserPresetExt);
  check(fs::exists(exported), "EXPORT writes a preset file");

  panel.pointerDown(state, 200, 27, false, set);
  panel.pointerUp(state);
  panel.pointerDown(state, 160, 275, false, set);
  panel.pointerUp(state);
  panel.pointerDown(state, 160, 275, false, set);
  panel.pointerUp(state);
  check(state.userPresets.size() == 1, "DELETE removes the preset");
  check(panel.importDroppedFile(state, exported.string(), set),
        "IMPORT accepts a dropped preset file");
  check(state.userPresets.size() == 2, "import adds a preset");

  if (!oldXdg.empty()) setenv("XDG_CONFIG_HOME", oldXdg.c_str(), 1);
  else unsetenv("XDG_CONFIG_HOME");
  if (!oldHome.empty()) setenv("HOME", oldHome.c_str(), 1);
  else unsetenv("HOME");
  fs::remove_all(root);
  std::printf("%d failure(s)\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}
