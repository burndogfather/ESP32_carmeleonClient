#ifndef HTTP_SECURE_H
#define HTTP_SECURE_H

#include <Arduino.h>
#include <map>
#include <vector>


extern "C" {
  #include "lwip/sockets.h"
  #include "mbedtls/esp_config.h"
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
  void printAllResponseHeaders();
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

  String getCookie(const String& domain, const String& name);
  void setCookie(const String& domain, const String& name, const String& value, time_t expire = 0);
  void removeCookie(const String& domain, const String& name);

  void end();
  void debugCookiesystem();

private:

  bool _littlefsInitialized = false;
  bool _littlefsSuccess = false;  // 초기화 성공 여부
  TaskHandle_t _littlefsTask = NULL;
  struct LittleFSTaskParams {
    bool* pInitialized;
    bool* pSuccess;
  };

  bool _isSecure = false;
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
  std::map<String, std::vector<String>> _responseHeaders;

  static void littleFSTask(void* params);

  int _write(const uint8_t* buf, size_t len);
  int _read(uint8_t* buf, size_t len);
  void sendRequest(const String& method, const String& body = "", const String& contentType = "");
  void readResponse();
  String getCookieFilePath();
  void processSetCookieHeader(const String& cookieHeader);
  void storeCookieToLittleFS(const String& name, const String& value, time_t expire);
  
  String getValidCookiesFromLittleFS();
  time_t parseGMTToTimeT(const String& gmtStr);
  
};

#endif