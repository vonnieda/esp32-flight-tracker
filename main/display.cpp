#include "display.hpp"

#include <algorithm>

#include "board_config.hpp"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_lcd_io_i80.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_st7796.h"
#include "esp_lvgl_port.h"

namespace {
constexpr char kTag[] = "display";

constexpr ledc_mode_t kBacklightSpeedMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t kBacklightTimer = LEDC_TIMER_0;
constexpr ledc_channel_t kBacklightChannel = LEDC_CHANNEL_0;
constexpr ledc_timer_bit_t kBacklightDutyResolution = LEDC_TIMER_10_BIT;
constexpr uint32_t kBacklightMaxDuty = (1u << 10) - 1;
}  // namespace

esp_err_t Display::init_backlight() {
  ledc_timer_config_t timer_config{};
  timer_config.speed_mode = kBacklightSpeedMode;
  timer_config.duty_resolution = kBacklightDutyResolution;
  timer_config.timer_num = kBacklightTimer;
  timer_config.freq_hz = 5000;
  timer_config.clk_cfg = LEDC_AUTO_CLK;
  ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), kTag, "backlight timer config");

  ledc_channel_config_t channel_config{};
  channel_config.gpio_num = board::kLcdBacklightPin;
  channel_config.speed_mode = kBacklightSpeedMode;
  channel_config.channel = kBacklightChannel;
  channel_config.timer_sel = kBacklightTimer;
  channel_config.duty = 0;
  channel_config.hpoint = 0;
  ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_config), kTag, "backlight channel config");

  set_backlight_percent(100);
  return ESP_OK;
}

void Display::set_backlight_percent(uint8_t percent) {
  percent = std::min<uint8_t>(percent, 100);
  const uint32_t duty = kBacklightMaxDuty * percent / 100;
  ledc_set_duty(kBacklightSpeedMode, kBacklightChannel, duty);
  ledc_update_duty(kBacklightSpeedMode, kBacklightChannel);
}

esp_err_t Display::init_panel() {
  esp_lcd_i80_bus_handle_t i80_bus = nullptr;
  esp_lcd_i80_bus_config_t bus_config{};
  bus_config.dc_gpio_num = board::kLcdDcPin;
  bus_config.wr_gpio_num = board::kLcdWrPin;
  bus_config.clk_src = LCD_CLK_SRC_DEFAULT;
  for (size_t i = 0; i < board::kLcdDataPins.size(); ++i) {
    bus_config.data_gpio_nums[i] = board::kLcdDataPins[i];
  }
  bus_config.bus_width = 8;
  bus_config.max_transfer_bytes = board::kDisplayWidth * 40 * sizeof(uint16_t);
  bus_config.dma_burst_size = 64;
  ESP_RETURN_ON_ERROR(esp_lcd_new_i80_bus(&bus_config, &i80_bus), kTag, "create i80 bus");

  esp_lcd_panel_io_i80_config_t io_config{};
  io_config.cs_gpio_num = board::kLcdCsPin;
  io_config.pclk_hz = board::kLcdPixelClockHz;
  io_config.trans_queue_depth = 10;
  io_config.lcd_cmd_bits = 8;
  io_config.lcd_param_bits = 8;
  io_config.dc_levels.dc_idle_level = 0;
  io_config.dc_levels.dc_cmd_level = 0;
  io_config.dc_levels.dc_dummy_level = 0;
  io_config.dc_levels.dc_data_level = 1;
  io_config.flags.swap_color_bytes = 1;
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i80(i80_bus, &io_config, &io_handle_), kTag,
                     "create i80 panel io");

  esp_lcd_panel_dev_config_t panel_config{};
  panel_config.reset_gpio_num = board::kLcdRstPin;
  panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
  panel_config.bits_per_pixel = 16;
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7796(io_handle_, &panel_config, &panel_handle_), kTag,
                     "create st7796 panel");

  ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel_handle_), kTag, "panel reset");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel_handle_), kTag, "panel init");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel_handle_, true), kTag, "panel invert color");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel_handle_, true), kTag, "panel display on");

  return ESP_OK;
}

esp_err_t Display::init() {
  ESP_RETURN_ON_ERROR(init_backlight(), kTag, "init backlight");
  ESP_RETURN_ON_ERROR(init_panel(), kTag, "init panel");

  lvgl_port_display_cfg_t disp_cfg{};
  disp_cfg.io_handle = io_handle_;
  disp_cfg.panel_handle = panel_handle_;
  disp_cfg.buffer_size = board::kDisplayWidth * 40;
  disp_cfg.double_buffer = true;
  disp_cfg.hres = board::kDisplayWidth;
  disp_cfg.vres = board::kDisplayHeight;
  disp_cfg.monochrome = false;
  disp_cfg.rotation.swap_xy = board::kSwapXy;
  disp_cfg.rotation.mirror_x = board::kMirrorX;
  disp_cfg.rotation.mirror_y = board::kMirrorY;
  disp_cfg.color_format = LV_COLOR_FORMAT_RGB565;
  disp_cfg.flags.buff_dma = true;
  disp_cfg.flags.buff_spiram = false;
  disp_cfg.flags.sw_rotate = false;
  disp_cfg.flags.swap_bytes = false;
  disp_cfg.flags.full_refresh = false;
  disp_cfg.flags.direct_mode = false;

  lvgl_display_ = lvgl_port_add_disp(&disp_cfg);
  ESP_RETURN_ON_FALSE(lvgl_display_ != nullptr, ESP_FAIL, kTag, "lvgl_port_add_disp failed");

  return ESP_OK;
}
