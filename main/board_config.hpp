#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "driver/i2c_types.h"

// Pin mapping for the Smart Panlee ZX3D50CE08S-USRC-4832 board, sold as the
// "WT32-SC01 Plus": ESP32-S3, ST7796 320x480 IPS panel over an 8-bit i80
// (Intel 8080) parallel bus, FT6336U (FT5x06-family) capacitive touch on I2C.
namespace board {

// --- LCD panel: ST7796 over 8-bit i80 parallel bus ---

inline constexpr std::array<gpio_num_t, 8> kLcdDataPins = {
    GPIO_NUM_9, GPIO_NUM_46, GPIO_NUM_3, GPIO_NUM_8,
    GPIO_NUM_18, GPIO_NUM_17, GPIO_NUM_16, GPIO_NUM_15,
};
inline constexpr gpio_num_t kLcdDcPin = GPIO_NUM_0;
inline constexpr gpio_num_t kLcdWrPin = GPIO_NUM_47;
inline constexpr gpio_num_t kLcdCsPin = GPIO_NUM_NC;
inline constexpr gpio_num_t kLcdRstPin = GPIO_NUM_4;
inline constexpr gpio_num_t kLcdBacklightPin = GPIO_NUM_45;

inline constexpr int kLcdPixelClockHz = 40 * 1000 * 1000;

// Native panel resolution (portrait, as the controller sees it).
inline constexpr size_t kPanelWidth = 320;
inline constexpr size_t kPanelHeight = 480;

// The board is mounted landscape, so the display presented to LVGL is
// rotated 90 degrees relative to the panel's native orientation.
inline constexpr size_t kDisplayWidth = kPanelHeight;   // 480
inline constexpr size_t kDisplayHeight = kPanelWidth;   // 320

// Hardware rotation/mirroring applied via esp_lvgl_port. If the image comes
// up mirrored/rotated on your unit, flip these and reflash.
inline constexpr bool kSwapXy = true;
inline constexpr bool kMirrorX = false;
inline constexpr bool kMirrorY = false;

// --- Touch controller: FT6336U (FT5x06-compatible) on internal I2C bus ---

inline constexpr gpio_num_t kTouchSdaPin = GPIO_NUM_6;
inline constexpr gpio_num_t kTouchSclPin = GPIO_NUM_5;
inline constexpr i2c_port_num_t kTouchI2cPort = I2C_NUM_1;
inline constexpr uint32_t kTouchI2cClockHz = 400 * 1000;

// Deliberately separate from kSwapXy/kMirrorX/kMirrorY above rather than
// reusing them: esp_lcd_panel's swap/mirror (used for the LCD image) and
// esp_lcd_touch's swap/mirror (used here) apply in different orders --
// esp_lcd_touch mirrors the raw pre-swap axes (using x_max/y_max as the
// *native* panel dimensions, kPanelWidth/kPanelHeight, not the post-rotation
// kDisplayWidth/kDisplayHeight) and only swaps afterward -- so despite both
// wrapping the same physical rotation, an image-correct combination doesn't
// necessarily also give a touch-correct one. Tuned against the physical
// unit: touch alone came up with its narrow (post-swap Y) axis inverted
// (tapping the bottom-left hit what was drawn top-left), fixed by mirroring
// the touch controller's raw X axis (which becomes the display's Y axis
// after the swap).
inline constexpr bool kTouchSwapXy = true;
inline constexpr bool kTouchMirrorX = true;
inline constexpr bool kTouchMirrorY = false;

}  // namespace board
