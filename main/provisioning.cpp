#include "provisioning.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "config_store.hpp"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

namespace {
constexpr char kTag[] = "provisioning";
constexpr uint16_t kDnsPort = 53;

// Set once the SoftAP is up; every DNS query gets spoofed to this address so
// phones/laptops resolve any hostname (including their captive-portal probe
// domains) straight to us.
esp_ip4_addr_t g_ap_ip;

const char kSetupPage[] = R"html(<!DOCTYPE html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Flight Tracker Setup</title>
<style>
body{font-family:sans-serif;margin:2em auto;max-width:400px;padding:0 1em;background:#111;color:#eee}
label{display:block;margin-top:1em;font-weight:600}
input{width:100%;padding:.5em;box-sizing:border-box;font-size:1em;margin-top:.25em}
button{margin-top:1.5em;width:100%;padding:.75em;font-size:1em}
</style></head><body>
<h2>Flight Tracker Setup</h2>
<form method="POST" action="/save">
<label>WiFi SSID</label><input name="ssid" required>
<label>WiFi Password</label><input name="password">
<label>OpenSky Client ID</label><input name="opensky_id">
<label>OpenSky Client Secret</label><input name="opensky_secret">
<label>Home Latitude</label><input name="lat" type="number" step="any" required>
<label>Home Longitude</label><input name="lon" type="number" step="any" required>
<button type="submit">Save &amp; Connect</button>
</form>
</body></html>
)html";

const char kSavedPage[] =
    "<!DOCTYPE html><html><body style=\"font-family:sans-serif\">"
    "<h2>Saved</h2><p>Restarting into normal operation...</p>"
    "</body></html>";

char hex_to_nibble(char c) {
  if (c >= '0' && c <= '9') return static_cast<char>(c - '0');
  if (c >= 'a' && c <= 'f') return static_cast<char>(c - 'a' + 10);
  if (c >= 'A' && c <= 'F') return static_cast<char>(c - 'A' + 10);
  return 0;
}

std::string url_decode(const std::string &in) {
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '+') {
      out.push_back(' ');
    } else if (in[i] == '%' && i + 2 < in.size()) {
      out.push_back(static_cast<char>((hex_to_nibble(in[i + 1]) << 4) | hex_to_nibble(in[i + 2])));
      i += 2;
    } else {
      out.push_back(in[i]);
    }
  }
  return out;
}

// Looks up `key` in a application/x-www-form-urlencoded body and
// url-decodes its value into out. Returns false if the key isn't present.
bool find_field(const std::string &body, const char *key, std::string &out) {
  const std::string needle = std::string(key) + "=";
  size_t pos = 0;
  while (pos < body.size()) {
    const size_t amp = body.find('&', pos);
    const size_t end = amp == std::string::npos ? body.size() : amp;
    if (body.compare(pos, needle.size(), needle) == 0) {
      out = url_decode(body.substr(pos + needle.size(), end - pos - needle.size()));
      return true;
    }
    if (amp == std::string::npos) {
      break;
    }
    pos = amp + 1;
  }
  return false;
}

esp_err_t handle_root(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, kSetupPage, HTTPD_RESP_USE_STRLEN);
}

esp_err_t handle_save(httpd_req_t *req) {
  if (req->content_len == 0 || req->content_len > 2048) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad request");
    return ESP_FAIL;
  }

  std::string body;
  body.resize(req->content_len);
  size_t received = 0;
  while (received < body.size()) {
    const int ret = httpd_req_recv(req, body.data() + received, body.size() - received);
    if (ret <= 0) {
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "read failed");
      return ESP_FAIL;
    }
    received += static_cast<size_t>(ret);
  }

  config_store::Config config;
  std::string lat_str;
  std::string lon_str;
  const bool ok = find_field(body, "ssid", config.wifi_ssid) && !config.wifi_ssid.empty() &&
                  find_field(body, "lat", lat_str) && find_field(body, "lon", lon_str);
  find_field(body, "password", config.wifi_password);
  find_field(body, "opensky_id", config.opensky_client_id);
  find_field(body, "opensky_secret", config.opensky_client_secret);

  if (!ok) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, kSetupPage, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  config.home_latitude_deg = std::strtof(lat_str.c_str(), nullptr);
  config.home_longitude_deg = std::strtof(lon_str.c_str(), nullptr);

  const esp_err_t save_err = config_store::save(config);
  if (save_err != ESP_OK) {
    ESP_LOGE(kTag, "failed to save config: %s", esp_err_to_name(save_err));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
    return ESP_FAIL;
  }

  ESP_LOGI(kTag, "config saved, restarting");
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, kSavedPage, HTTPD_RESP_USE_STRLEN);

  // Restart from a separate task, after a short delay, so this response
  // actually reaches the client instead of the connection dying mid-send.
  xTaskCreate(
      [](void *) {
        vTaskDelay(pdMS_TO_TICKS(1500));
        esp_restart();
      },
      "restart", 2048, nullptr, tskIDLE_PRIORITY + 1, nullptr);

  return ESP_OK;
}

void start_http_server() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.uri_match_fn = httpd_uri_match_wildcard;

  httpd_handle_t server = nullptr;
  ESP_ERROR_CHECK(httpd_start(&server, &config));

  const httpd_uri_t save_uri = {
      .uri = "/save", .method = HTTP_POST, .handler = handle_save, .user_ctx = nullptr};
  httpd_register_uri_handler(server, &save_uri);

  // Wildcard catch-all: serves the same form for "/" and for whatever
  // probe path (e.g. "/generate_204", "/hotspot-detect.html") each OS uses
  // to detect captive portals, which is what makes them pop the page up
  // automatically.
  const httpd_uri_t root_uri = {
      .uri = "/*", .method = HTTP_GET, .handler = handle_root, .user_ctx = nullptr};
  httpd_register_uri_handler(server, &root_uri);
}

struct DnsHeader {
  uint16_t id;
  uint16_t flags;
  uint16_t qdcount;
  uint16_t ancount;
  uint16_t nscount;
  uint16_t arcount;
} __attribute__((packed));

void dns_server_task(void *arg) {
  (void)arg;
  const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    ESP_LOGE(kTag, "dns socket() failed: errno %d", errno);
    vTaskDelete(nullptr);
    return;
  }

  sockaddr_in bind_addr{};
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_port = htons(kDnsPort);
  bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(sock, reinterpret_cast<sockaddr *>(&bind_addr), sizeof(bind_addr)) < 0) {
    ESP_LOGE(kTag, "dns bind() failed: errno %d", errno);
    close(sock);
    vTaskDelete(nullptr);
    return;
  }

  ESP_LOGI(kTag, "dns server listening on port %d", kDnsPort);

  uint8_t buf[512];
  while (true) {
    sockaddr_in from{};
    socklen_t from_len = sizeof(from);
    const int len =
        recvfrom(sock, buf, sizeof(buf), 0, reinterpret_cast<sockaddr *>(&from), &from_len);
    if (len < static_cast<int>(sizeof(DnsHeader))) {
      continue;
    }

    // Walk past the question section (length-prefixed labels terminated by
    // a zero byte, then 4 bytes of qtype+qclass) to find where to append
    // our answer record.
    size_t pos = sizeof(DnsHeader);
    while (pos < static_cast<size_t>(len) && buf[pos] != 0) {
      pos += buf[pos] + 1;
    }
    pos += 1 + 4;  // terminating zero byte + qtype + qclass
    if (pos > static_cast<size_t>(len)) {
      continue;  // Malformed query; drop it.
    }

    auto *header = reinterpret_cast<DnsHeader *>(buf);
    header->flags = htons(0x8180);  // standard response, recursion available, no error
    header->ancount = header->qdcount;
    header->nscount = 0;
    header->arcount = 0;

    uint8_t answer[16] = {
        0xC0, 0x0C,              // name: pointer back to the question at offset 12
        0x00, 0x01,              // type A
        0x00, 0x01,              // class IN
        0x00, 0x00, 0x00, 0x3C,  // TTL: 60s
        0x00, 0x04,              // rdlength: 4 bytes
    };
    std::memcpy(answer + 12, &g_ap_ip.addr, 4);

    uint8_t response[512];
    if (pos + sizeof(answer) > sizeof(response)) {
      continue;
    }
    std::memcpy(response, buf, pos);
    std::memcpy(response + pos, answer, sizeof(answer));

    sendto(sock, response, pos + sizeof(answer), 0, reinterpret_cast<sockaddr *>(&from), from_len);
  }
}

void start_ap() {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();

  const wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init_config));

  wifi_config_t wifi_config{};
  std::snprintf(reinterpret_cast<char *>(wifi_config.ap.ssid), sizeof(wifi_config.ap.ssid), "%s",
               provisioning::kApSsid);
  wifi_config.ap.ssid_len = std::strlen(provisioning::kApSsid);
  wifi_config.ap.channel = 1;
  wifi_config.ap.max_connection = 4;
  wifi_config.ap.authmode = WIFI_AUTH_OPEN;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  esp_netif_ip_info_t ip_info;
  ESP_ERROR_CHECK(esp_netif_get_ip_info(ap_netif, &ip_info));
  g_ap_ip = ip_info.ip;

  ESP_LOGI(kTag, "AP '%s' up at " IPSTR, provisioning::kApSsid, IP2STR(&g_ap_ip));
}
}  // namespace

void provisioning::run() {
  start_ap();
  xTaskCreate(dns_server_task, "dns_server", 4096, nullptr, tskIDLE_PRIORITY + 1, nullptr);
  start_http_server();

  ESP_LOGI(kTag, "connect to WiFi '%s' and open http://" IPSTR "/ to configure", kApSsid,
          IP2STR(&g_ap_ip));

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
