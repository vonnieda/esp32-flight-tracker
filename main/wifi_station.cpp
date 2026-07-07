#include "wifi_station.hpp"

#include <cstdio>
#include <cstring>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

namespace {
constexpr char kTag[] = "wifi";
constexpr EventBits_t kConnectedBit = BIT0;

EventGroupHandle_t g_event_group = nullptr;

void on_wifi_or_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
  (void)arg;
  (void)data;
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGW(kTag, "disconnected, reconnecting");
    esp_wifi_connect();
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    ESP_LOGI(kTag, "connected");
    xEventGroupSetBits(g_event_group, kConnectedBit);
  }
}
}  // namespace

esp_err_t WifiStation::connect(const std::string &ssid, const std::string &password) {
  g_event_group = xEventGroupCreate();

  ESP_RETURN_ON_ERROR(esp_netif_init(), kTag, "netif init");
  ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), kTag, "event loop create");
  esp_netif_create_default_wifi_sta();

  const wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), kTag, "wifi init");

  esp_event_handler_instance_t wifi_event_instance;
  esp_event_handler_instance_t ip_event_instance;
  ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                           &on_wifi_or_ip_event, nullptr,
                                                           &wifi_event_instance),
                      kTag, "register wifi event handler");
  ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                           &on_wifi_or_ip_event, nullptr,
                                                           &ip_event_instance),
                      kTag, "register ip event handler");

  wifi_config_t wifi_config{};
  std::snprintf(reinterpret_cast<char *>(wifi_config.sta.ssid), sizeof(wifi_config.sta.ssid), "%s",
               ssid.c_str());
  std::snprintf(reinterpret_cast<char *>(wifi_config.sta.password),
               sizeof(wifi_config.sta.password), "%s", password.c_str());

  ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), kTag, "set mode");
  ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), kTag, "set config");
  ESP_RETURN_ON_ERROR(esp_wifi_start(), kTag, "wifi start");

  ESP_LOGI(kTag, "connecting to %s...", ssid.c_str());
  xEventGroupWaitBits(g_event_group, kConnectedBit, pdFALSE, pdFALSE, portMAX_DELAY);

  return ESP_OK;
}
