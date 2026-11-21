#pragma once

#ifndef USE_NEOPIXEL
#define USE_NEOPIXEL true
#endif

#include <stdint.h>
#if USE_NEOPIXEL
#include <Adafruit_NeoPixel.h>
#endif

namespace SagToolV2 {

enum class Mode : uint8_t { Rider, Static, Live };
enum class State : uint8_t { Ready, Armed, Measuring, Captured };

class StatusPixel {
 public:
  // Use in setup(), before OLED, sensor, or network startup.
  void begin() {
#if USE_NEOPIXEL
    if (pixel_.numPixels() != 1 || !pixel_.begin()) return;
    pixel_.setBrightness(25);  // 25/255, as in the older Sag Tool sketches.
    pixel_.clear();
    pixel_.show();
    started_ = true;
#endif
  }

  // Use after the existing sensor read and measurement-state update.
  // Pass a current encoder-health flag and the sketch's current millisecond time.
  void update(Mode mode, State state, bool encoderOK, uint32_t nowMs) {
#if USE_NEOPIXEL
    if (!started_) return;
    uint32_t color = kReady;
    if (!encoderOK) {
      color = kError;
    } else {
      switch (state) {
        case State::Armed: color = kArmed; break;
        case State::Measuring: color = kMeasuring; break;
        case State::Captured: color = kCaptured; break;
        case State::Ready:
          if (mode == Mode::Static) color = kStatic;
          if (mode == Mode::Live) color = kLive;
          break;
      }
    }

    if (color != activeColor_) {
      activeColor_ = color;
      phaseStartMs_ = nowMs;
    }
    const uint32_t elapsedMs = nowMs - phaseStartMs_;
    if (color == kMeasuring && elapsedMs % 500 >= 250) color = 0;
    if (color == kError && elapsedMs % 1000 >= 500) color = 0;

    // Send a frame only when the visible color changes. No animation delays.
    if (color != shownColor_ && pixel_.canShow()) {
      pixel_.setPixelColor(0, color);
      pixel_.show();
      shownColor_ = color;
    }
#else
    (void)mode;
    (void)state;
    (void)encoderOK;
    (void)nowMs;
#endif
  }

 private:
#if USE_NEOPIXEL
  static constexpr uint8_t kPin = 18;  // XIAO ESP32-C6 pad D10
  static constexpr uint32_t kReady = 0x0028FF;
  static constexpr uint32_t kStatic = 0x7800FF;
  static constexpr uint32_t kLive = 0x00C8FF;
  static constexpr uint32_t kArmed = 0xFF7800;
  static constexpr uint32_t kMeasuring = 0xFFD000;
  static constexpr uint32_t kCaptured = 0x00FF00;
  static constexpr uint32_t kError = 0xFF0000;

  Adafruit_NeoPixel pixel_{1, kPin, NEO_GRB + NEO_KHZ800};
  bool started_ = false;
  uint32_t activeColor_ = 0;
  uint32_t shownColor_ = 0;
  uint32_t phaseStartMs_ = 0;
#endif
};

}  // namespace SagToolV2
