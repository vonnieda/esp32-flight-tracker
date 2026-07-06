#include "touch.hpp"

#include "board_config.hpp"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_lvgl_port.h"

namespace {
constexpr char kTag[] = "touch";
}  // namespace

esp_err_t Touch::init(lv_display_t *display) {
  i2c_master_bus_handle_t i2c_bus = nullptr;
  i2c_master_bus_config_t bus_config{};
  bus_config.i2c_port = board::kTouchI2cPort;
  bus_config.sda_io_num = board::kTouchSdaPin;
  bus_config.scl_io_num = board::kTouchSclPin;
  bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_config.glitch_ignore_cnt = 7;
  bus_config.flags.enable_internal_pullup = true;
  ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &i2c_bus), kTag, "create i2c bus");

  esp_lcd_panel_io_i2c_config_t io_config{};
  io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS;
  io_config.scl_speed_hz = board::kTouchI2cClockHz;
  io_config.control_phase_bytes = 1;
  io_config.dc_bit_offset = 0;
  io_config.lcd_cmd_bits = 8;
  io_config.lcd_param_bits = 8;
  io_config.flags.disable_control_phase = 1;

  esp_lcd_panel_io_handle_t io_handle = nullptr;
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(i2c_bus, &io_config, &io_handle), kTag,
                     "create touch panel io");

  esp_lcd_touch_config_t touch_config{};
  touch_config.x_max = board::kDisplayWidth;
  touch_config.y_max = board::kDisplayHeight;
  touch_config.rst_gpio_num = GPIO_NUM_NC;
  touch_config.int_gpio_num = GPIO_NUM_NC;
  touch_config.levels.reset = 0;
  touch_config.levels.interrupt = 0;
  touch_config.flags.swap_xy = board::kSwapXy;
  touch_config.flags.mirror_x = board::kMirrorX;
  touch_config.flags.mirror_y = board::kMirrorY;
  ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_ft5x06(io_handle, &touch_config, &touch_handle_), kTag,
                     "create ft5x06 touch driver");

  lvgl_port_touch_cfg_t touch_cfg{};
  touch_cfg.disp = display;
  touch_cfg.handle = touch_handle_;
  indev_ = lvgl_port_add_touch(&touch_cfg);
  ESP_RETURN_ON_FALSE(indev_ != nullptr, ESP_FAIL, kTag, "lvgl_port_add_touch failed");

  return ESP_OK;
}
