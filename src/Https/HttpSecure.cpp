#include "HttpSecure.h"

HttpSecure::HttpSecure() {
  mbedtls_ssl_init(&_ssl);
  mbedtls_ssl_config_init(&_conf);
  mbedtls_ctr_drbg_init(&_ctr_drbg);
}

bool HttpSecure::begin(const char* fullUrl) {
  // URL 파싱 로직 구현
  // _host, _path, _port 설정
  // mbedTLS 설정 및 소켓 연결
}

void HttpSecure::header(const String& name, const String& value) {
  _headers[name] = value;
}

int HttpSecure::get() {
  sendRequest("GET");
  readResponse();
  return _statusCode;
}

int HttpSecure::post(const String& body, const String& contentType) {
  sendRequest("POST", body, contentType);
  readResponse();
  return _statusCode;
}

String HttpSecure::responseBody() {
  return _response;
}

int HttpSecure::statusCode() {
  return _statusCode;
}

void HttpSecure::end() {
  mbedtls_ssl_close_notify(&_ssl);
  mbedtls_ssl_free(&_ssl);
  mbedtls_ssl_config_free(&_conf);
  mbedtls_ctr_drbg_free(&_ctr_drbg);
  close(_socket);
  _connected = false;
}

void HttpSecure::sendRequest(const String& method, const String& body, const String& contentType) {
  // HTTP 요청 전송 로직 구현
}

void HttpSecure::readResponse() {
  // HTTP 응답 읽기 로직 구현
}