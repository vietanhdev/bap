#pragma once
// Optional password for the settings web page (see assistant_auth.cc).
#include <esp_http_server.h>

#include <string>

namespace assistant {

bool HasWebPassword();
void SetWebPassword(const std::string& password);  // empty = remove the password
// Registers a route that answers 401 until the request carries the password (when one is set).
esp_err_t RegisterGuarded(void* server, const httpd_uri_t& route);

}  // namespace assistant
