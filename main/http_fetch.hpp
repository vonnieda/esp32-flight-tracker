#pragma once

#include <string>

#include "esp_err.h"

// Shared HTTP plumbing for the REST clients: HTTPS via the certificate
// bundle, the response body collected into out_body, and any non-200 status
// treated as an error. A non-null post_body makes the request a POST; a
// non-null header_name adds one request header.
//
// When out_status is non-null, a completed request always returns ESP_OK and
// the caller inspects the status code itself (used where a 404 is meaningful
// rather than a failure).
esp_err_t http_fetch(const char *url, const char *header_name, const char *header_value,
                     const char *post_body, std::string &out_body, int *out_status = nullptr);
