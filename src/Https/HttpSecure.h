#ifndef HTTP_SECURE_H
#define HTTP_SECURE_H

#include <Arduino.h>
#include <map>

extern "C" {
  #include "lwip/sockets.h"
  #include "mbedtls/ssl.h"
  #include "mbedtls/net_sockets.h"
  #include "mbedtls/entropy.h"
  #include "mbedtls/ctr_drbg.h"
  #include "mbedtls/error.h"
}

class carmeleonHttpSecure {
public:
  carmeleonHttpSecure();

  bool begin(const char* fullUrl);  // e.g., https://host:port/path
  void header(const String& name, const String& value);
  int get();
  int post(const String& body, const String& contentType = "application/json");
  String responseBody();
  int statusCode();
  void end();

private:
  String _host;
  String _path;
  uint16_t _port;
  std::map<String, String> _headers;

  int _socket;
  mbedtls_ssl_context _ssl;
  mbedtls_ssl_config _conf;
  mbedtls_ctr_drbg_context _ctr_drbg;
  bool _connected = false;

  int _statusCode = -1;
  String _response;

  void sendRequest(const String& method, const String& body = "", const String& contentType = "");
  void readResponse();
};

#endif