#include "display.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "touch.hpp"
#include "ui.hpp"

namespace {
constexpr char kTag[] = "main";

Display display;
Touch touch;
}  // namespace

extern "C" void app_main() {
  const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
  ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

  ESP_ERROR_CHECK(display.init());
  ESP_ERROR_CHECK(touch.init(display.lvgl_display()));

  if (lvgl_port_lock(0)) {
    ui::build_home_screen();
    lvgl_port_unlock();
  } else {
    ESP_LOGE(kTag, "Failed to lock LVGL for UI setup");
  }
}
