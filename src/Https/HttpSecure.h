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


class HttpSecure {
public:
  HttpSecure();

  bool begin(const char* fullUrl);  // 예: https://host:port/path
  void requestHeader(const String& name, const String& value);
  String responseHeader(const String& name);
  int get();
  int post(const String& body, const String& contentType = "application/json");
  int put(const String& body, const String& contentType);
  int patch(const String& body, const String& contentType);
  int del();
  int head();
  String responseBody();
  int statusCode();
  void printAllCookies();
  void clearAllCookies();
  void end();

  void debugFilesystem();

private:
  bool _isSecure = false;
  String _host;
  String _path;
  uint16_t _port;
  std::map<String, String> _headers;
  bool _littlefsInitialized = false;

  int _socket;
  mbedtls_ssl_context _ssl;
  mbedtls_ssl_config _conf;
  mbedtls_ctr_drbg_context _ctr_drbg;
  bool _connected = false;

  int _statusCode = -1;
  String _response;
  std::map<String, String> _responseHeaders;

  int _write(const uint8_t* buf, size_t len);
  int _read(uint8_t* buf, size_t len);
  void sendRequest(const String& method, const String& body = "", const String& contentType = "");
  void readResponse();
  String getCookieFilePath();
  void processSetCookieHeader(const String& cookieHeader);
  void storeCookieToLittleFS(const String& name, const String& value, time_t expire);
  String getValidCookiesFromLittleFS();
  
};

#endif