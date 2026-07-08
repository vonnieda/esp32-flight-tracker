#include "http_fetch.hpp"

#include <cstring>

#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

namespace {
constexpr char kTag[] = "http_fetch";

esp_err_t append_body_handler(esp_http_client_event_t *evt) {
  if (evt->event_id == HTTP_EVENT_ON_DATA) {
    auto *body = static_cast<std::string *>(evt->user_data);
    body->append(static_cast<const char *>(evt->data), evt->data_len);
  }
  return ESP_OK;
}
}  // namespace

esp_err_t http_fetch(const char *url, const char *header_name, const char *header_value,
                     const char *post_body, std::string &out_body, int *out_status) {
  esp_http_client_config_t config{};
  config.url = url;
  config.method = post_body != nullptr ? HTTP_METHOD_POST : HTTP_METHOD_GET;
  config.event_handler = append_body_handler;
  config.user_data = &out_body;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.timeout_ms = 15000;
  config.buffer_size = 4096;
  config.buffer_size_tx = 4096;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  ESP_RETURN_ON_FALSE(client != nullptr, ESP_FAIL, kTag, "failed to init http client");

  if (header_name != nullptr) {
    esp_http_client_set_header(client, header_name, header_value);
  }
  if (post_body != nullptr) {
    esp_http_client_set_post_field(client, post_body, static_cast<int>(std::strlen(post_body)));
  }

  const esp_err_t err = esp_http_client_perform(client);
  const int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (err != ESP_OK) {
    ESP_LOGE(kTag, "request to %s failed (err=%s)", url, esp_err_to_name(err));
    return ESP_FAIL;
  }
  if (out_status != nullptr) {
    *out_status = status;
    return ESP_OK;
  }
  if (status != 200) {
    ESP_LOGE(kTag, "request to %s failed (status=%d, body=%s)", url, status, out_body.c_str());
    return ESP_FAIL;
  }
  return ESP_OK;
}
