#include "HttpSecure.h"
#include <Ethernet/EthernetESP32.h>

HttpSecure::HttpSecure() {
  mbedtls_ssl_init(&_ssl);
  mbedtls_ssl_config_init(&_conf);
  mbedtls_ctr_drbg_init(&_ctr_drbg);
}

bool HttpSecure::begin(const char* fullUrl) {
  String url = fullUrl;

  // 1. "https://" 제거
  if (url.startsWith("https://")) {
    url = url.substring(8);
  }

  // 2. host, port, path 분리
  int slashIndex = url.indexOf('/');
  int colonIndex = url.indexOf(':');

  if (slashIndex == -1) slashIndex = url.length();  // path가 없으면 끝까지 host

  if (colonIndex != -1 && colonIndex < slashIndex) {
    _host = url.substring(0, colonIndex);
    _port = url.substring(colonIndex + 1, slashIndex).toInt();
  } else {
    _host = url.substring(0, slashIndex);
    _port = 443;
  }

  _path = (slashIndex < url.length()) ? url.substring(slashIndex) : "/";

  // 3. DNS → IP
  IPAddress ip;
  if (!Ethernet.hostByName(_host.c_str(), ip)) {
    Serial.println("[HTTPS] DNS 실패");
    return false;
  }

  // 4. 소켓 연결
  _socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (_socket < 0) {
    Serial.println("[HTTPS] 소켓 생성 실패");
    return false;
  }

  struct sockaddr_in server;
  server.sin_family = AF_INET;
  server.sin_port = htons(_port);
  server.sin_addr.s_addr = ip;

  if (connect(_socket, (struct sockaddr*)&server, sizeof(server)) != 0) {
    Serial.println("[HTTPS] 서버 연결 실패");
    close(_socket);
    return false;
  }

  // 5. mbedTLS 설정
  const char* pers = "carmeleon_https";
  mbedtls_ctr_drbg_seed(&_ctr_drbg,
                        [](void*, unsigned char* output, size_t len) {
                          for (size_t i = 0; i < len; ++i) output[i] = esp_random() & 0xFF;
                          return 0;
                        },
                        nullptr,
                        (const unsigned char*)pers,
                        strlen(pers));

  mbedtls_ssl_config_defaults(&_conf,
                              MBEDTLS_SSL_IS_CLIENT,
                              MBEDTLS_SSL_TRANSPORT_STREAM,
                              MBEDTLS_SSL_PRESET_DEFAULT);

  mbedtls_ssl_conf_authmode(&_conf, MBEDTLS_SSL_VERIFY_NONE);  // 인증서 무시
  mbedtls_ssl_conf_rng(&_conf, mbedtls_ctr_drbg_random, &_ctr_drbg);

  mbedtls_ssl_setup(&_ssl, &_conf);
  mbedtls_ssl_set_hostname(&_ssl, _host.c_str());
  mbedtls_ssl_set_bio(&_ssl, &_socket,
                      [](void* ctx, const unsigned char* buf, size_t len) {
                        return send(*(int*)ctx, buf, len, 0);
                      },
                      [](void* ctx, unsigned char* buf, size_t len) {
                        return recv(*(int*)ctx, buf, len, 0);
                      },
                      nullptr);

  // 6. SSL 핸드셰이크
  int ret;
  while ((ret = mbedtls_ssl_handshake(&_ssl)) != 0) {
    if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
      char errbuf[128];
      mbedtls_strerror(ret, errbuf, sizeof(errbuf));
      Serial.print("[HTTPS] 핸드셰이크 실패: ");
      Serial.println(errbuf);
      close(_socket);
      return false;
    }
  }

  Serial.println("[HTTPS] 연결 성공");
  _connected = true;
  return true;
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
  if (!_connected) return;

  String req = method + " " + _path + " HTTP/1.1\r\n";
  req += "Host: " + _host + "\r\n";

  for (auto& kv : _headers) {
    req += kv.first + ": " + kv.second + "\r\n";
  }

  if (method == "POST" || method == "PUT") {
    req += "Content-Type: " + contentType + "\r\n";
    req += "Content-Length: " + String(body.length()) + "\r\n";
  }

  req += "Connection: close\r\n\r\n";

  if (body.length()) req += body;

  int ret = mbedtls_ssl_write(&_ssl, (const unsigned char*)req.c_str(), req.length());
  if (ret < 0) {
    char err[100];
    mbedtls_strerror(ret, err, sizeof(err));
    Serial.print("[HTTPS] 전송 실패: ");
    Serial.println(err);
  }
}

void HttpSecure::readResponse() {
  _response = "";
  _statusCode = -1;

  const size_t bufSize = 1024;
  char buf[bufSize + 1];
  int len = 0;
  bool headerEnded = false;
  bool statusParsed = false;

  while ((len = mbedtls_ssl_read(&_ssl, (unsigned char*)buf, bufSize)) > 0) {
    buf[len] = '\0';
    _response += buf;

    if (!statusParsed) {
      int idx = _response.indexOf("\r\n");
      if (idx != -1) {
        String statusLine = _response.substring(0, idx);
        int space1 = statusLine.indexOf(' ');
        int space2 = statusLine.indexOf(' ', space1 + 1);
        if (space1 != -1 && space2 != -1) {
          _statusCode = statusLine.substring(space1 + 1, space2).toInt();
          statusParsed = true;
        }
      }
    }

    if (!headerEnded) {
      int bodyStart = _response.indexOf("\r\n\r\n");
      if (bodyStart != -1) {
        _response = _response.substring(bodyStart + 4);  // 본문만 남기기
        headerEnded = true;
      } else {
        _response = "";  // 헤더 끝날 때까지 버퍼링
      }
    }
  }

  if (len < 0 && len != MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
    Serial.println("[HTTPS] 응답 수신 중 오류 발생");
  }
}