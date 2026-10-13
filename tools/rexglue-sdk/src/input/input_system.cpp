/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#include <rex/dbg.h>
#include <rex/input/flags.h>
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/input/nop/nop_input_driver.h>
#include <rex/input/sdl/sdl_input_driver.h>
#include <rex/input/xinput/xinput_input_driver.h>
#include <rex/logging.h>

REXCVAR_DEFINE_STRING(input_backend, "sdl", "Input", "Input backend: sdl, xinput")
    .allowed({"sdl", "xinput"});

REXCVAR_DEFINE_BOOL(guide_button, false, "Input", "Enable guide button pass-through");
REXCVAR_DEFINE_STRING(input_inject_file, "", "Input",
                      "Test automation: file polled for injected controller input, one command "
                      "per write: '<control> <hold_seconds> [pad]', pad 1 (default) or 2");
namespace rex::input {

namespace {

// Injected controller input for unattended test runs. The harness writes a
// command such as "A 0.15" to the file; it is consumed on the next poll and
// the control is held for that long on player 1's pad, or on player 2's with
// a third field "2". Player 1's pad is connected while injection is on,
// player 2's from its first command.
class InputInjector {
 public:
  static constexpr uint32_t kPads = 2;

  bool enabled() const { return !REXCVAR_GET(input_inject_file).empty(); }

  bool connected(uint32_t user_index) {
    if (!enabled() || user_index >= kPads) {
      return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return user_index == 0 || pads_[user_index].seen;
  }

  // Adds the active injected control to the state; returns true if the pad is
  // injected.
  bool Apply(uint32_t user_index, X_INPUT_STATE& state) {
    if (!enabled() || user_index >= kPads) {
      return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    if (now >= next_poll_) {
      next_poll_ = now + std::chrono::milliseconds(20);
      Poll(now);
    }
    Pad& pad = pads_[user_index];
    if (user_index != 0 && !pad.seen) {
      return false;
    }
    uint16_t buttons = 0;
    int16_t lx = 0, ly = 0, rx = 0, ry = 0;
    uint8_t lt = 0, rt = 0;
    if (now < pad.release_at) {
      buttons = pad.buttons;
      lx = pad.lx;
      ly = pad.ly;
      rx = pad.rx;
      ry = pad.ry;
      lt = pad.lt;
      rt = pad.rt;
    }
    if (buttons != pad.last_buttons || lx != pad.last_lx || ly != pad.last_ly) {
      ++pad.packet;
      pad.last_buttons = buttons;
      pad.last_lx = lx;
      pad.last_ly = ly;
    }
    state.packet_number = uint32_t(state.packet_number) + pad.packet;
    state.gamepad.buttons = uint16_t(state.gamepad.buttons) | buttons;
    state.gamepad.left_trigger = std::max(state.gamepad.left_trigger, lt);
    state.gamepad.right_trigger = std::max(state.gamepad.right_trigger, rt);
    if (lx || ly) {
      state.gamepad.thumb_lx = lx;
      state.gamepad.thumb_ly = ly;
    }
    if (rx || ry) {
      state.gamepad.thumb_rx = rx;
      state.gamepad.thumb_ry = ry;
    }
    return true;
  }

 private:
  struct Pad {
    bool seen = false;
    std::chrono::steady_clock::time_point release_at{};
    uint16_t buttons = 0;
    int16_t lx = 0, ly = 0, rx = 0, ry = 0;
    uint8_t lt = 0, rt = 0;
    uint16_t last_buttons = 0;
    int16_t last_lx = 0, last_ly = 0;
    uint32_t packet = 0;
  };

  void Poll(std::chrono::steady_clock::time_point now) {
    const std::string& path = REXCVAR_GET(input_inject_file);
    FILE* file = std::fopen(path.c_str(), "r");
    if (!file) {
      return;
    }
    char control[32] = {};
    double hold = 0.15;
    int pad_number = 1;
    int fields = std::fscanf(file, "%31s %lf %d", control, &hold, &pad_number);
    std::fclose(file);
    std::remove(path.c_str());
    if (fields < 1) {
      return;
    }
    if (pad_number < 1 || pad_number > int(kPads)) {
      REXLOG_WARN("input_inject_file: no pad {}", pad_number);
      return;
    }
    Pad& pad = pads_[pad_number - 1];
    pad.seen = true;
    pad.buttons = 0;
    pad.lx = pad.ly = pad.rx = pad.ry = 0;
    pad.lt = pad.rt = 0;
    // One control, or several joined by '+' and held together (e.g. LS+RS).
    std::string controls(control);
    static const std::pair<const char*, uint16_t> kButtons[] = {
        {"A", X_INPUT_GAMEPAD_A},
        {"B", X_INPUT_GAMEPAD_B},
        {"X", X_INPUT_GAMEPAD_X},
        {"Y", X_INPUT_GAMEPAD_Y},
        {"START", X_INPUT_GAMEPAD_START},
        {"BACK", X_INPUT_GAMEPAD_BACK},
        {"LB", X_INPUT_GAMEPAD_LEFT_SHOULDER},
        {"RB", X_INPUT_GAMEPAD_RIGHT_SHOULDER},
        {"LS", X_INPUT_GAMEPAD_LEFT_THUMB},
        {"RS", X_INPUT_GAMEPAD_RIGHT_THUMB},
        {"UP", X_INPUT_GAMEPAD_DPAD_UP},
        {"DOWN", X_INPUT_GAMEPAD_DPAD_DOWN},
        {"LEFT", X_INPUT_GAMEPAD_DPAD_LEFT},
        {"RIGHT", X_INPUT_GAMEPAD_DPAD_RIGHT},
    };
    constexpr int16_t kFull = 32767;
    size_t start = 0;
    while (start <= controls.size()) {
      size_t end = controls.find('+', start);
      if (end == std::string::npos) {
        end = controls.size();
      }
      const std::string name = controls.substr(start, end - start);
      start = end + 1;
      bool known = false;
      for (const auto& [button_name, mask] : kButtons) {
        if (name == button_name) {
          pad.buttons |= mask;
          known = true;
        }
      }
      if (name == "LT") { pad.lt = 0xFF; known = true; }
      if (name == "RT") { pad.rt = 0xFF; known = true; }
      if (name == "L_UP") { pad.ly = kFull; known = true; }
      if (name == "L_DOWN") { pad.ly = -kFull; known = true; }
      if (name == "L_LEFT") { pad.lx = -kFull; known = true; }
      if (name == "L_RIGHT") { pad.lx = kFull; known = true; }
      if (name == "R_UP") { pad.ry = kFull; known = true; }
      if (name == "R_DOWN") { pad.ry = -kFull; known = true; }
      if (name == "R_LEFT") { pad.rx = -kFull; known = true; }
      if (name == "R_RIGHT") { pad.rx = kFull; known = true; }
      if (!known) {
        REXLOG_WARN("input_inject_file: unknown control '{}'", name);
        pad.buttons = 0;
        pad.lx = pad.ly = pad.rx = pad.ry = 0;
        pad.lt = pad.rt = 0;
        return;
      }
    }
    const std::string& name = controls;
    pad.release_at = now + std::chrono::microseconds(int64_t(hold * 1e6));
    REXLOG_INFO("input_inject_file: {} for {:.2f}s on pad {}", name, hold, pad_number);
  }

  std::mutex mutex_;
  std::chrono::steady_clock::time_point next_poll_{};
  Pad pads_[kPads];
};

InputInjector g_input_injector;

void FillInjectedCapabilities(X_INPUT_CAPABILITIES* out_caps) {
  if (!out_caps) {
    return;
  }
  std::memset(out_caps, 0, sizeof(*out_caps));
  out_caps->type = 0x01;
  out_caps->sub_type = 0x01;
  out_caps->gamepad.buttons = 0xFFFF;
  out_caps->gamepad.left_trigger = 0xFF;
  out_caps->gamepad.right_trigger = 0xFF;
  out_caps->gamepad.thumb_lx = static_cast<int16_t>(0x7FFF);
  out_caps->gamepad.thumb_ly = static_cast<int16_t>(0x7FFF);
  out_caps->gamepad.thumb_rx = static_cast<int16_t>(0x7FFF);
  out_caps->gamepad.thumb_ry = static_cast<int16_t>(0x7FFF);
}

}  // namespace

InputSystem::InputSystem(rex::ui::Window* window) : window_(window) {}

InputSystem::~InputSystem() = default;

X_STATUS InputSystem::Setup() {
  return X_STATUS_SUCCESS;
}

void InputSystem::Shutdown() {
  drivers_.clear();
}

void InputSystem::AddDriver(std::unique_ptr<InputDriver> driver) {
  drivers_.push_back(std::move(driver));
}

void InputSystem::AttachWindow(rex::ui::Window* window) {
  window_ = window;
  for (auto& driver : drivers_) {
    driver->OnWindowAvailable(window);
  }
}

void InputSystem::SetActiveCallback(std::function<bool()> callback) {
  for (auto& driver : drivers_) {
    driver->set_is_active_callback(callback);
  }
}

X_RESULT InputSystem::GetCapabilities(uint32_t user_index, uint32_t flags,
                                      X_INPUT_CAPABILITIES* out_caps) {
  SCOPE_profile_cpu_f("hid");

  bool any_connected = false;
  for (auto& driver : drivers_) {
    X_RESULT result = driver->GetCapabilities(user_index, flags, out_caps);
    if (result != X_ERROR_DEVICE_NOT_CONNECTED) {
      any_connected = true;
    }
    if (result == X_ERROR_SUCCESS) {
      return result;
    }
  }
  if (g_input_injector.connected(user_index)) {
    FillInjectedCapabilities(out_caps);
    return X_ERROR_SUCCESS;
  }
  return any_connected ? X_ERROR_EMPTY : X_ERROR_DEVICE_NOT_CONNECTED;
}

X_RESULT InputSystem::GetState(uint32_t user_index, X_INPUT_STATE* out_state) {
  SCOPE_profile_cpu_f("hid");

  bool any_connected = false;
  bool first_result = true;
  X_INPUT_STATE merged = {};

  for (auto& driver : drivers_) {
    X_INPUT_STATE state = {};
    X_RESULT result = driver->GetState(user_index, &state);
    if (result != X_ERROR_DEVICE_NOT_CONNECTED) {
      any_connected = true;
    }
    if (result == X_ERROR_SUCCESS) {
      if (first_result) {
        merged = state;
        first_result = false;
      } else {
        // Merge: OR buttons, max triggers, max-magnitude sticks
        merged.gamepad.buttons = static_cast<uint16_t>(merged.gamepad.buttons) |
                                 static_cast<uint16_t>(state.gamepad.buttons);
        merged.gamepad.left_trigger =
            std::max(merged.gamepad.left_trigger, state.gamepad.left_trigger);
        merged.gamepad.right_trigger =
            std::max(merged.gamepad.right_trigger, state.gamepad.right_trigger);

        auto merge_axis = [](int16_t a, int16_t b) -> int16_t {
          return (std::abs(static_cast<int>(a)) >= std::abs(static_cast<int>(b))) ? a : b;
        };
        merged.gamepad.thumb_lx = merge_axis(merged.gamepad.thumb_lx, state.gamepad.thumb_lx);
        merged.gamepad.thumb_ly = merge_axis(merged.gamepad.thumb_ly, state.gamepad.thumb_ly);
        merged.gamepad.thumb_rx = merge_axis(merged.gamepad.thumb_rx, state.gamepad.thumb_rx);
        merged.gamepad.thumb_ry = merge_axis(merged.gamepad.thumb_ry, state.gamepad.thumb_ry);

        if (static_cast<uint32_t>(state.packet_number) >
            static_cast<uint32_t>(merged.packet_number)) {
          merged.packet_number = state.packet_number;
        }
      }
    }
  }

  if (g_input_injector.Apply(user_index, merged)) {
    first_result = false;
  }

  if (first_result) {
    return any_connected ? X_ERROR_EMPTY : X_ERROR_DEVICE_NOT_CONNECTED;
  }

  if (out_state) {
    *out_state = merged;
  }
  return X_ERROR_SUCCESS;
}

X_RESULT InputSystem::SetState(uint32_t user_index, X_INPUT_VIBRATION* vibration) {
  SCOPE_profile_cpu_f("hid");

  bool any_connected = false;
  for (auto& driver : drivers_) {
    X_RESULT result = driver->SetState(user_index, vibration);
    if (result != X_ERROR_DEVICE_NOT_CONNECTED) {
      any_connected = true;
    }
    if (result == X_ERROR_SUCCESS) {
      return result;
    }
  }
  return any_connected ? X_ERROR_EMPTY : X_ERROR_DEVICE_NOT_CONNECTED;
}

X_RESULT InputSystem::GetKeystroke(uint32_t user_index, uint32_t flags,
                                   X_INPUT_KEYSTROKE* out_keystroke) {
  SCOPE_profile_cpu_f("hid");

  bool any_connected = false;
  for (auto& driver : drivers_) {
    X_RESULT result = driver->GetKeystroke(user_index, flags, out_keystroke);
    if (result != X_ERROR_DEVICE_NOT_CONNECTED) {
      any_connected = true;
    }
    if (result == X_ERROR_SUCCESS || result == X_ERROR_EMPTY) {
      return result;
    }
  }
  return any_connected ? X_ERROR_EMPTY : X_ERROR_DEVICE_NOT_CONNECTED;
}

std::unique_ptr<InputSystem> CreateDefaultInputSystem(bool tool_mode) {
  auto input = std::make_unique<InputSystem>(nullptr);

  if (!tool_mode) {
#if REX_PLATFORM_WIN32
    if (REXCVAR_GET(input_backend) == "xinput") {
      auto xinput_driver = std::make_unique<xinput::XinputInputDriver>(nullptr, 0);
      if (xinput_driver->Setup() == X_STATUS_SUCCESS) {
        input->AddDriver(std::move(xinput_driver));
      }
    }
#endif

    if (REXCVAR_GET(input_backend) == "sdl") {
      auto sdl_driver = std::make_unique<sdl::SDLInputDriver>(nullptr, 0);
      if (sdl_driver->Setup() == X_STATUS_SUCCESS) {
        input->AddDriver(std::move(sdl_driver));
      }
    }

    // MnK driver (keyboard/mouse -> controller emulation)
    auto mnk_driver = std::make_unique<mnk::MnkInputDriver>(nullptr, 0);
    if (mnk_driver->Setup() == X_STATUS_SUCCESS) {
      input->AddDriver(std::move(mnk_driver));
    }
  }

  // NOP driver (primary in tool mode, fallback otherwise)
  uint8_t nop_index = tool_mode ? 0 : 1;
  input->AddDriver(std::make_unique<nop::NopInputDriver>(nullptr, nop_index));
  return input;
}

}  // namespace rex::input
