#define USE_LITTLEFS
#include "HttpSecure.h"
#include <Ethernet/EthernetESP32.h>

#include <FS.h>
#include <LittleFS.h> 

#include <ArduinoJson/ArduinoJson.h>
#include <map>
#include <vector>

#include <sys/time.h>
#include <time.h>
#include <locale.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <LittleFS.h>
#include "esp_log.h"

#include "mbedtls/base64.h"

HttpSecure::HttpSecure() {
  
  mbedtls_ssl_init(&_ssl);
  mbedtls_ssl_config_init(&_conf);
  mbedtls_ctr_drbg_init(&_ctr_drbg);
  setlocale(LC_TIME, "C"); 

}


void HttpSecure::littleFSTask(void* params) {
  LittleFSTaskParams* taskParams = (LittleFSTaskParams*)params;
  
  // 마운트 시도
  if (!LittleFS.begin(false, "/spiffs", 10, "spiffs")) {
    Serial.println("[HTTP] LittleFS/cookies 마운트 실패! 저장소를 포맷합니다");

    // 포맷 시도
    if (LittleFS.format()) {
      // 포맷 후 다시 마운트 시도
      if (!LittleFS.begin(true, "/spiffs", 10, "spiffs")) {
        Serial.println("[HTTP] LittleFS/cookies 포맷 후 마운트 실패!");
        *(taskParams->pSuccess) = false;
        vTaskDelete(NULL);
        return;
      }
      Serial.println("[HTTP] 쿠키저장소(LittleFS) 초기화 완료 : ");
    
      // 파일 시스템 정보 출력
      Serial.printf("  Total space: %d bytes\n", LittleFS.totalBytes());
      Serial.printf("  Used space: %d bytes\n", LittleFS.usedBytes());

    } else {
      Serial.println("[HTTP] LittleFS/cookies 포맷 실패!");
      *(taskParams->pSuccess) = false;
      vTaskDelete(NULL);
      return;
    }
  }

  // 디렉토리 생성 확인
  if (!LittleFS.open("/cookies", "r")) {
    if (!LittleFS.mkdir("/cookies")) {
      Serial.println("[HTTP] LittleFS/cookies 디렉토리 생성 실패");
      // 실패해도 계속 진행 (파일은 직접 생성 시도)
    }
  }

  *(taskParams->pInitialized) = true;
  *(taskParams->pSuccess) = true;
  
  vTaskDelete(NULL);
}


void HttpSecure::websocketRecvTask(void* arg) {
  HttpSecure* self = static_cast<HttpSecure*>(arg);
  while (self->_connected) {
    self->readFrame();  // 프레임 읽기
    vTaskDelay(10 / portTICK_PERIOD_MS);  // 약간의 휴식
  }
  vTaskDelete(NULL);
}

bool HttpSecure::begin(const char* fullUrl) {

  // 기존 연결 및 SSL 상태 정리
  end();  
  _isSecure = false;
  _port = 0;
  _host = "";
  _path = "/";
  _headers.clear();
  _responseHeaders.clear();
  _response = "";
  _statusCode = -1;


  String url = fullUrl;
  url.trim(); // 🔥 앞뒤 공백 제거
  String lower = url;
  lower.toLowerCase(); // 🔥 대소문자 무시 처리

  // LittleFS 초기화가 아직 시작되지 않았을 때만 태스크 생성
  if (!_littlefsInitialized && _littlefsTask == NULL) {
    LittleFSTaskParams params = {
      .pInitialized = &_littlefsInitialized,
      .pSuccess = &_littlefsSuccess
    };

    xTaskCreate(
      littleFSTask,      // 태스크 함수
      "littlefs_task",   // 태스크 이름
      4096,             // 스택 크기 (ESP32는 최소 3KB 권장)
      &params,           // 파라미터
      1,                // 우선순위 (낮음)
      &_littlefsTask     // 태스크 핸들 저장
    );

  }

  if (lower.startsWith("https://")) {
    _isSecure = true;
    url = url.substring(8);
    _port = (_port == 0) ? 443 : _port;
  } else if (lower.startsWith("http://")) {
    _isSecure = false;
    url = url.substring(7);
    _port = (_port == 0) ? 80 : _port;
  } else if (lower.startsWith("wss://")) {
    _isSecure = true;
    url = url.substring(6);
    _port = (_port == 0) ? 443 : _port;
  } else if (lower.startsWith("ws://")) {
    _isSecure = false;
    url = url.substring(5);
    _port = (_port == 0) ? 80 : _port;
  } else {
    Serial.println("[HTTP] 지원하지 않는 프로토콜");
    return false;
  }

  // 2. host, port, path 분리
  int slashIndex = url.indexOf('/');
  int colonIndex = url.indexOf(':');

  if (slashIndex == -1) slashIndex = url.length();  // path가 없으면 끝까지 host

  if (colonIndex != -1 && colonIndex < slashIndex) {
    _host = url.substring(0, colonIndex);
    _port = url.substring(colonIndex + 1, slashIndex).toInt(); // 사용자 지정 포트 사용
  } else {
    _host = url.substring(0, slashIndex);
  }

  _path = (slashIndex < url.length()) ? url.substring(slashIndex) : "/";

  // 3. DNS → IP
  IPAddress ip;
  if (!Ethernet.hostByName(_host.c_str(), ip)) {
    Serial.println("[HTTP] DNS 실패");
    return false;
  }

  // 4. 소켓 연결
  _socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (_socket < 0) {
    Serial.println("[HTTP] 소켓 생성 실패");
    return false;
  }

  struct sockaddr_in server;
  server.sin_family = AF_INET;
  server.sin_port = htons(_port);
  server.sin_addr.s_addr = ip;

  if (connect(_socket, (struct sockaddr*)&server, sizeof(server)) != 0) {
    Serial.println("[HTTP] 서버 연결 실패");
    close(_socket);
    return false;
  }

  // 5. mbedTLS 설정
  if (_isSecure) {
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
        Serial.printf("[HTTP] SSL핸드셰이크 실패: %s\n", errbuf);
        close(_socket);
        return false;
      }
    }
  }

  while (!_littlefsInitialized) {
    delay(10); // 또는 vTaskDelay()
  }

  if (!_littlefsSuccess) {
    Serial.println("[HTTP] LittleFS 초기화 실패!");
    return false;
  }
  
  _connected = true;
  return true;
}


bool HttpSecure::handshake(const char* wsUrl) {
  if (!begin(wsUrl)) return false;

  _isWebSocket = true;
  uint8_t randomKey[16];
  esp_fill_random(randomKey, sizeof(randomKey));
  size_t encodedLen;
  uint8_t encodedBuf[32]; // 16바이트 → Base64 (24바이트 + 널 종료)
  mbedtls_base64_encode(encodedBuf, sizeof(encodedBuf), &encodedLen, 
                       randomKey, sizeof(randomKey));

  String key = String((char*)encodedBuf);

  requestHeader("Upgrade", "websocket");
  requestHeader("Connection", "Upgrade");
  requestHeader("Sec-WebSocket-Key", key);
  requestHeader("Sec-WebSocket-Version", "13");

  int status = get();
  if (status == 101) {
    _connected = true;
    if (_onConnected) {
      _onConnected();
    }

    if (_wsRecvTask == nullptr) {
      xTaskCreate(
        websocketRecvTask,
        "ws_recv_task",
        4096,
        this,
        1,
        &_wsRecvTask
      );
    }

    return true;
  }
  return false;
}




void HttpSecure::sendMsgString(const String& message) {
  if (!_connected) return;
  sendFrame(message);
}

void HttpSecure::sendMsgBinary(const std::vector<uint8_t>& data) {
  if (!_connected) return;

  uint8_t header[10];
  size_t len = data.size();
  size_t offset = 2;

  header[0] = 0x82;  // FIN + opcode 0x2 (binary)
  if (len <= 125) {
    header[1] = len;
  } else if (len <= 65535) {
    header[1] = 126;
    header[2] = (len >> 8) & 0xFF;
    header[3] = len & 0xFF;
    offset = 4;
  } else {
    header[1] = 127;
    // 64bit 길이는 생략 (필요시 추가 구현)
    return;
  }

  _write(header, offset);
  _write(data.data(), len);
}


void HttpSecure::sendFrame(const String& message) {
  uint8_t header[10];
  size_t len = message.length();
  size_t offset = 2;

  header[0] = 0x81; // FIN + text frame
  if (len <= 125) {
    header[1] = len;
  } else if (len <= 65535) {
    header[1] = 126;
    header[2] = (len >> 8) & 0xFF;
    header[3] = len & 0xFF;
    offset = 4;
  } else {
    header[1] = 127;
    // 64bit length not handled for simplicity
  }

  _write(header, offset);
  _write((const uint8_t*)message.c_str(), len);
}

void HttpSecure::readFrame() {
  if (!_connected) return;

  uint8_t hdr[2];
  int hdrLen = _read(hdr, 2);
  if (hdrLen <= 0) {
    if (_onDisconnected) _onDisconnected();
    end();
    return;
  }

  uint8_t opcode = hdr[0] & 0x0F;
  bool isMasked = hdr[1] & 0x80;
  uint64_t payloadLen = hdr[1] & 0x7F;

  // 확장 payload 길이 처리
  if (payloadLen == 126) {
    uint8_t ext[2];
    if (_read(ext, 2) != 2) { end(); return; }
    payloadLen = (ext[0] << 8) | ext[1];
  } else if (payloadLen == 127) {
    uint8_t ext[8];
    if (_read(ext, 8) != 8) { end(); return; }
    payloadLen = 0;
    for (int i = 0; i < 4; ++i) payloadLen = (payloadLen << 8) | ext[i + 4];
  }

  if (payloadLen > 4096) {
    Serial.println("[HTTP] WS Payload too large");
    end();
    return;
  }

  uint8_t mask[4] = {0};
  if (isMasked) {
    if (_read(mask, 4) != 4) { end(); return; }
  }

  std::vector<uint8_t> payload(payloadLen);
  size_t totalRead = 0;
  while (totalRead < payloadLen) {
    int r = _read(payload.data() + totalRead, payloadLen - totalRead);
    if (r <= 0) { end(); return; }
    totalRead += r;
  }

  if (isMasked) {
    for (size_t i = 0; i < payloadLen; ++i) {
      payload[i] ^= mask[i % 4];
    }
  }


  switch (opcode) {
    case 0x1: {// Text Frame
      payload.push_back(0);  // null-terminate
      String msg = String((char*)payload.data());
      if (_onMessage) _onMessage(msg);
    
      // 🔁 "ping" (대소문자 무시) → "pong" 문자열로 응답
      msg.toLowerCase();
      if (msg == "ping") {
        sendMsgString("pong");
      }
      break;
    }

    case 0x2: { // Binary Frame
      if (_onMessageBinary) {
        _onMessageBinary(payload);  // 바이너리 콜백 호출
      }
      break;
    }
    case 0x8: {// Close Frame
      if (_onDisconnected) _onDisconnected();
      end();  // 연결 종료
      break;
    }
    case 0x9: {// Ping → Pong 응답
      sendPong(payload);
      break;
    }
    case 0xA: {// Pong (무시 가능)
      break;
    }
    default:{
      Serial.printf("[HTTP] 알 수 없는 WS opcode: 0x%02X\n", opcode);
      break;
    }
  }
}

void HttpSecure::sendPong(const std::vector<uint8_t>& payload) {
  if (!_connected) return;

  uint8_t header[2];
  size_t len = payload.size();
  size_t offset = 2;

  header[0] = 0x8A; // FIN + pong
  if (len <= 125) {
    header[1] = len;
  } else if (len <= 65535) {
    header[1] = 126;
    header[2] = (len >> 8) & 0xFF;
    header[3] = len & 0xFF;
    offset = 4;
  } else {
    header[1] = 127;
    // 64bit 길이까지 대응하지 않음
    return;
  }

  _write(header, offset);
  if (len > 0) _write(payload.data(), len);
}





void HttpSecure::onConnected(std::function<void()> cb) {
  _onConnected = cb;
}

void HttpSecure::onDisconnected(std::function<void()> cb) {
  _onDisconnected = cb;
}

void HttpSecure::onMsgString(std::function<void(String)> cb) {
  _onMessage = cb;
}
void HttpSecure::onMsgBinary(std::function<void(std::vector<uint8_t>)> cb) {
  _onMessageBinary = cb;
}


void HttpSecure::requestHeader(const String& name, const String& value) {
  _headers[name] = value;
}

String HttpSecure::responseHeader(const String& name) {
  String lowerName = name;
  lowerName.toLowerCase();

  for (const auto& header : _responseHeaders) {
    String key = header.first;
    key.toLowerCase();
    if (key == lowerName) {
      if (!header.second.empty()) {
        return header.second.back();  // 가장 마지막 값
      }
    }
  }
  return "";
}

void HttpSecure::printAllResponseHeaders() {
  Serial.println("[HTTP] 서버 응답 헤더 목록 : ");
  for (const auto& header : _responseHeaders) {
    const String& key = header.first;
    for (const String& value : header.second) {
      Serial.printf("  %s: %s\n", key.c_str(), value.c_str());
    }
  }
}

int HttpSecure::get() {
  int redirectLimit = 5;
  for (int i = 0; i < redirectLimit; ++i) {
    sendRequest("GET");
    readResponse();

    if (_statusCode == 301 || _statusCode == 302) {
      String location = responseHeader("location");
      location.trim();
      if (location.length() > 0) {
        Serial.printf("[HTTP] 리다이렉션 감지 → %s\n", location.c_str());
        end(); // 현재 연결 종료
        begin(location.c_str()); // 새 URL로 재시도
        continue;
      }
    }
    break; // 리다이렉션이 아닌 경우 정상 종료
  }

  return _statusCode;
}

int HttpSecure::post(const String& body, const String& contentType) {
  sendRequest("POST", body, contentType);
  readResponse();
  return _statusCode;
}

int HttpSecure::put(const String& body, const String& contentType) {
  sendRequest("PUT", body, contentType);
  readResponse();
  return _statusCode;
}

int HttpSecure::patch(const String& body, const String& contentType) {
  sendRequest("PATCH", body, contentType);
  readResponse();
  return _statusCode;
}

int HttpSecure::del() {
  sendRequest("DELETE");
  readResponse();
  return _statusCode;
}

int HttpSecure::head() {
  sendRequest("HEAD");
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
  bool wasConnected = _connected;

  if (_connected) {
    if (_isSecure) {
      mbedtls_ssl_close_notify(&_ssl);
      mbedtls_ssl_free(&_ssl);
      mbedtls_ssl_config_free(&_conf);
      mbedtls_ctr_drbg_free(&_ctr_drbg);
    }

    if (_wsRecvTask) {
      TaskHandle_t tmp = _wsRecvTask;
      _wsRecvTask = nullptr;
      vTaskDelete(tmp);
    }

    close(_socket);
    _connected = false;
    _isWebSocket = false;
    if (wasConnected && _onDisconnected) _onDisconnected();
  }
}

int HttpSecure::_write(const uint8_t* buf, size_t len) {
  if (_isSecure) {
    return mbedtls_ssl_write(&_ssl, buf, len);
  } else {
    return send(_socket, buf, len, 0);
  }
}

int HttpSecure::_read(uint8_t* buf, size_t len) {
  if (_isSecure) {
    return mbedtls_ssl_read(&_ssl, buf, len);
  } else {
    return recv(_socket, buf, len, 0);
  }
}

void HttpSecure::sendRequest(const String& method, const String& body, const String& contentType) {
  if (!_connected) return;

  String req = method + " " + _path + " HTTP/1.1\r\n";

  if (_headers.find("Host") == _headers.end() && _headers.find("host") == _headers.end()) {
    req += "Host: " + _host + "\r\n";
  }

  // 저장된 쿠키 추가
  String cookies = getValidCookiesFromLittleFS();
  if (cookies.length() > 0) {
    req += "Cookie: " + cookies + "\r\n";
  }

  for (auto& kv : _headers) {
    req += kv.first + ": " + kv.second + "\r\n";
  }

  if (method == "POST" || method == "PUT") {
    req += "Content-Type: " + contentType + "\r\n";
    req += "Content-Length: " + String(body.length()) + "\r\n";
  }

  req += "Connection: close\r\n\r\n";

  if (body.length()) req += body;

  int ret = _write((const uint8_t*)req.c_str(), req.length());
  if (ret < 0) {
    if (_isSecure) {
      char err[100];
      mbedtls_strerror(ret, err, sizeof(err));
    }
  }
}

void HttpSecure::readResponse() {
  _response = "";
  _responseHeaders.clear();
  _statusCode = -1;

  const size_t bufSize = 1024;
  char buf[bufSize + 1];
  int len = 0;
  bool headerEnded = false;
  bool statusParsed = false;
  String headerBuffer = "";  // 🔥 헤더 버퍼 분리

  std::function<int(uint8_t*, size_t)> readFunc;
  if (_isSecure) {
    readFunc = [&](uint8_t* b, size_t l) {
      return mbedtls_ssl_read(&_ssl, (unsigned char*)b, l);
    };
  } else {
    readFunc = [&](uint8_t* b, size_t l) {
      return _read(b, l);
    };
  }

  while ((len = readFunc((uint8_t*)buf, bufSize)) > 0) {
    buf[len] = '\0';

    if (!headerEnded) {
      headerBuffer += buf;

      int headerEnd = headerBuffer.indexOf("\r\n\r\n");
      if (headerEnd != -1) {
        String headerPart = headerBuffer.substring(0, headerEnd);
        _response = headerBuffer.substring(headerEnd + 4);
        headerEnded = true;

        // 🔍 상태코드 파싱
        int statusLineEnd = headerPart.indexOf("\r\n");
        if (statusLineEnd != -1) {
          String statusLine = headerPart.substring(0, statusLineEnd);
          int space1 = statusLine.indexOf(' ');
          int space2 = statusLine.indexOf(' ', space1 + 1);
          if (space1 != -1 && space2 != -1) {
            _statusCode = statusLine.substring(space1 + 1, space2).toInt();
            statusParsed = true;
          }
        }

        // 🔍 헤더 파싱
        int lineStart = headerPart.indexOf("\r\n") + 2;
        while (lineStart < headerPart.length()) {
          int lineEnd = headerPart.indexOf("\r\n", lineStart);
          if (lineEnd == -1) {
          
            String line = headerPart.substring(lineStart); // 남은 전체 줄
            int colon = line.indexOf(':');
            if (colon != -1) {
              String key = line.substring(0, colon);
              String value = line.substring(colon + 1);
              key.trim(); value.trim();
              key.toLowerCase();
              _responseHeaders[key].push_back(value);

              if (key == "set-cookie") {
                processSetCookieHeader(value);
              }
            }
            break;
          }
          String line = headerPart.substring(lineStart, lineEnd);
          lineStart = lineEnd + 2;

          int colon = line.indexOf(':');
          if (colon != -1) {
            String key = line.substring(0, colon);
            String value = line.substring(colon + 1);
            key.trim(); value.trim();
            key.toLowerCase();
            _responseHeaders[key].push_back(value);

            if (key == "set-cookie") {
              processSetCookieHeader(value);
            }
          }
        }

        // WebSocket이면 여기서 끝냄
        if (_isWebSocket) return;

      }
    } else {
      _response += buf;
    }
  }

  if (len < 0 && len != MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
    Serial.println("[HTTP] 응답 수신 중 오류 발생");
  }
}


String HttpSecure::getCookieFilePath() {

  // 디렉토리 경로 설정 (절대 경로 사용)
  String dir = "/cookies";
  
  // 디렉토리 존재 확인 및 생성
  if (!LittleFS.open(dir, "r")) {
    if (!LittleFS.mkdir(dir)) {
      Serial.println("[HTTP] LittleFS/cookies 디렉토리 생성 실패! 대체 경로 사용");
      dir = "/"; // 루트 디렉토리로 대체
    }
  }
  
  // 파일 경로 생성
  String filename = _host + ".json";
  filename.replace(":", "_"); // 포트 번호가 있는 경우 대체
  
  String fullPath = dir + "/" + filename;
  return fullPath;
}

void HttpSecure::processSetCookieHeader(const String& cookieHeader) {

  if (time(nullptr) < 24 * 3600) {
    log_w("시간정보가 잘못되어 쿠키만료일이 맞지 않을 수 있습니다!");
  }

  String name, value, domain, path = "/";
  time_t expire = 0;
  bool secure = false;
  bool httpOnly = false;

  std::vector<String> tokens;
  int start = 0;
  while (start < cookieHeader.length()) {
    int semi = cookieHeader.indexOf(';', start);
    if (semi == -1) semi = cookieHeader.length();
    String token = cookieHeader.substring(start, semi);
    token.trim();
    tokens.push_back(token);
    start = semi + 1;
  }

  if (tokens.empty()) return;

  int eq = tokens[0].indexOf('=');
  if (eq == -1) return;

  name = tokens[0].substring(0, eq);
  value = tokens[0].substring(eq + 1);

  for (size_t i = 1; i < tokens.size(); i++) {
    String attr = tokens[i];
    String attrLower = attr;
    attrLower.toLowerCase();

    if (attrLower.startsWith("domain=")) {
      domain = attr.substring(7);
    } else if (attrLower.startsWith("path=")) {
      path = attr.substring(5);
    } else if (attrLower.startsWith("max-age=")) {
      String maxAgeStr = attr.substring(8);
      time_t maxAge = maxAgeStr.toInt();
      expire = (maxAge > 0) ? time(nullptr) + maxAge : 0;
    } else if (attrLower.startsWith("expires=") && expire == 0) {
      String expiresStr = attr.substring(8);
      expire = parseGMTToTimeT(expiresStr);
    } else if (attrLower == "secure") {
      secure = true;
    } else if (attrLower == "httponly") {
      httpOnly = true;
    }
  }

  if (domain.isEmpty()) domain = _host;
  storeCookieToLittleFS(name, value, expire);
}


void HttpSecure::storeCookieToLittleFS(const String& name, const String& value, time_t expire) {
  
  if (time(nullptr) < 24 * 3600) {
    Serial.println("[HTTP] ❌ 시스템 시간이 아직 설정되지 않아 쿠키 저장을 건너뜁니다.");
    return;
  }

  if (!_littlefsInitialized) {
    Serial.println("[HTTP] 오류! begin() 이후 사용이 가능합니다");
    return;
  }

  setenv("TZ", "UTC", 1); // 모든 시간을 UTC로 처리
  tzset();

  String path = getCookieFilePath();
  if (path.isEmpty()) {
    Serial.println("[HTTP] 쿠키 경로 없음 - 저장 실패");
    return;
  }

  const size_t maxCookies = 20;
  DynamicJsonDocument doc(4096);
  JsonArray arr;

  // 1. 기존 쿠키 파일 로드
  File file = LittleFS.open(path, "r");
  if (file) {
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) {
      Serial.println("[HTTP] 기존 쿠키 파싱 실패, 새 배열 생성");
      doc.clear();
    }
  }
  
  if (doc.isNull()) {
    doc.to<JsonArray>();
  }
  arr = doc.as<JsonArray>();

  // 2. 기존 쿠키 정리 (UTC 기준으로 비교)
  time_t now = time(nullptr);
  bool updated = false;
  
  for (int i = arr.size() - 1; i >= 0; --i) {
    JsonObject c = arr[i];
    if (!c.containsKey("name") || !c.containsKey("expire")) {
      arr.remove(i);
      continue;
    }

    time_t cookieExpire = c["expire"].as<time_t>();
    if (cookieExpire > 0 && cookieExpire < now) { // UTC 비교
      arr.remove(i);
      continue;
    }

    if (c["name"] == name) {
      c["value"] = value;
      c["expire"] = expire; // UTC 값 그대로 저장
      updated = true;
    }
  }


  // 3. 새 쿠키 추가 (도메인/경로는 선택적)
  if (!updated && arr.size() < maxCookies) {
    JsonObject c = arr.createNestedObject();
    c["name"] = name;
    c["value"] = value;
    c["expire"] = expire; // UTC 값 저장
    if (!_host.isEmpty()) c["domain"] = _host;
    c["path"] = "/";
  }

  // 4. 파일 저장
  File save = LittleFS.open(path, "w");
  if (!save) {
    return;
  }

  if (serializeJson(doc, save) == 0) {
    Serial.println("[HTTP] 쿠키 직렬화 실패");
  }
  save.close();
}

String HttpSecure::getValidCookiesFromLittleFS() {
  String path = getCookieFilePath();
  String result = "";
  time_t now = time(nullptr);

  File file = LittleFS.open(path, "r");
  if (!file) {
    return "";
  }

  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, file);
  file.close();

  if (err) {
    Serial.printf("[HTTP] 쿠키 JSON 파싱 오류: %s\n", path.c_str());
    return "";
  }

  JsonArray arr = doc.as<JsonArray>();
  bool changed = false;

  for (int i = arr.size() - 1; i >= 0; --i) {
    JsonObject c = arr[i];

    if (!c.containsKey("name") || !c.containsKey("expire")) {
      arr.remove(i);
      changed = true;
      continue;
    }

    int64_t expireRaw = c["expire"];
    time_t expire = (time_t)expireRaw;

    if (expire > 0 && expire < now) {
      Serial.printf("[HTTP] 쿠키 만료됨: %s (만료시간: %lld, 현재: %ld)\n",
                    c["name"].as<const char*>(), (long long)expire, now);
      arr.remove(i);
      changed = true;
      continue;
    }

    // 유효 쿠키 추가
    if (result.length()) result += "; ";
    result += String(c["name"].as<const char*>()) + "=" + String(c["value"].as<const char*>());
  }

  if (changed) {
    File file = LittleFS.open(path, "w");
    if (file) {
      serializeJson(doc, file);
      file.close();
      Serial.println("[HTTP] 만료된 쿠키 정리 완료");
    } else {
      Serial.printf("[HTTP] 쿠키파일 쓰기 실패 (경로: %s)\n", path.c_str());
    }
  }

  return result;
}


void HttpSecure::printAllCookies() {

  if (!_littlefsInitialized) {
    if (!LittleFS.begin(false, "/spiffs", 10, "spiffs")) {
      Serial.println("[HTTP] clearAllCookies 실패 - LittleFS 마운트되지 않음");
      return;
    }
  }

  String path = getCookieFilePath();
  time_t now = time(nullptr);

  File file = LittleFS.open(path, "r");
  if (!file) {
    return;
  }

  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, file);
  file.close();

  if (err) {
    Serial.printf("[HTTP] 쿠키 JSON 파싱 오류: %s\n", path.c_str());
    return;
  }

  JsonArray arr = doc.as<JsonArray>();
  Serial.printf("[HTTP] 저장된 쿠키 목록 (%s):\n", path.c_str());

  for (JsonObject c : arr) {
    if (!c.containsKey("name") || !c.containsKey("expire")) continue;
  
    int64_t expireRaw = c["expire"];
    time_t expire = (time_t)expireRaw;
  
    String status = (expire > 0 && expire < now) ? "  - 만료:" : "  - 유효:";
  
    Serial.printf("%s %s=%s\n", status.c_str(),
          c["name"].as<const char*>(),
          c["value"].as<const char*>());
    Serial.printf("    도메인: %s, 만료: %lld\n",  // %lld 사용
          c.containsKey("domain") ? c["domain"].as<const char*>() : "(없음)",
          (long long)expire);
  }

}

void HttpSecure::clearAllCookies() {

  if (!_littlefsInitialized) {
    if (!LittleFS.begin(false, "/spiffs", 10, "spiffs")) {
      Serial.println("[HTTP] clearAllCookies 실패 - LittleFS 마운트되지 않음");
      return;
    }
  }

  String dirPath = "/cookies";

  File dir = LittleFS.open(dirPath, "w+");
  if (!dir || !dir.isDirectory()) {
    Serial.println("[HTTP] 쿠키 디렉토리 열기 실패 또는 디렉토리 아님");
    return;
  }

  File file = dir.openNextFile();
  while (file) {
    String filename = file.name();
    file.close();

    if (LittleFS.remove(dirPath + "/" + filename)) {
      Serial.printf("[HTTP] 쿠키 삭제됨: %s\n", filename.c_str());
    } else {
      Serial.printf("[HTTP] 쿠키 삭제 실패: %s\n", filename.c_str());
    }

    file = dir.openNextFile();
  }

  Serial.println("[HTTP] 모든 쿠키 삭제 완료");
}

String HttpSecure::getCookie(const String& domain, const String& name) {

  if (!_littlefsInitialized) {
    if (!LittleFS.begin(false, "/spiffs", 10, "spiffs")) {
      Serial.println("[HTTP] clearAllCookies 실패 - LittleFS 마운트되지 않음");
      return "";
    }
  }

  String path = "/cookies/" + domain + ".json";
  path.replace(":", "_");

  File file = LittleFS.open(path, "r");
  if (!file) return "";

  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) return "";

  JsonArray arr = doc.as<JsonArray>();
  time_t now = time(nullptr);

  for (JsonObject c : arr) {
    if (!c.containsKey("name") || !c.containsKey("value") || !c.containsKey("expire")) continue;
    if (String(c["name"].as<const char*>()) == name) {
      time_t exp = c["expire"].as<time_t>();
      if (exp == 0 || exp > now) {
        return String(c["value"].as<const char*>());
      }
    }
  }

  return "";
}



void HttpSecure::setCookie(const String& domain, const String& name, const String& value, time_t expire) {
  if (time(nullptr) < 24 * 3600) {
    log_w("시간정보가 잘못되어 쿠키만료일이 맞지 않을 수 있습니다!");
  }

  if (!_littlefsInitialized) {
    if (!LittleFS.begin(false, "/spiffs", 10, "spiffs")) {
      Serial.println("[HTTP] clearAllCookies 실패 - LittleFS 마운트되지 않음");
      return;
    }
  }

  if (expire == 0) expire = time(nullptr) + 86400 * 30; // 기본 유효기간: 30일

  String path = "/cookies/" + domain + ".json";
  path.replace(":", "_");

  DynamicJsonDocument doc(4096);
  JsonArray arr;

  File file = LittleFS.open(path, "r");
  if (file) {
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) doc.clear();
  }

  if (doc.isNull()) doc.to<JsonArray>();
  arr = doc.as<JsonArray>();

  bool updated = false;
  for (JsonObject c : arr) {
    if (String(c["name"].as<const char*>()) == name) {
      c["value"] = value;
      c["expire"] = expire;
      updated = true;
      break;
    }
  }

  if (!updated) {
    JsonObject c = arr.createNestedObject();
    c["name"] = name;
    c["value"] = value;
    c["expire"] = expire;
    c["domain"] = domain;
    c["path"] = "/";
  }

  File save = LittleFS.open(path, "w");
  if (save) {
    serializeJson(doc, save);
    save.close();
  }
}


void HttpSecure::removeCookie(const String& domain, const String& name) {

  if (!_littlefsInitialized) {
    if (!LittleFS.begin(false, "/spiffs", 10, "spiffs")) {
      Serial.println("[HTTP] clearAllCookies 실패 - LittleFS 마운트되지 않음");
      return;
    }
  }

  String path = "/cookies/" + domain + ".json";
  path.replace(":", "_");

  File file = LittleFS.open(path, "r");
  if (!file) return;

  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) return;

  JsonArray arr = doc.as<JsonArray>();
  bool changed = false;

  for (int i = arr.size() - 1; i >= 0; --i) {
    JsonObject c = arr[i];
    if (c.containsKey("name") && String(c["name"].as<const char*>()) == name) {
      arr.remove(i);
      changed = true;
      break;
    }
  }

  if (changed) {
    File save = LittleFS.open(path, "w");
    if (save) {
      serializeJson(doc, save);
      save.close();
      Serial.printf("[HTTP] 쿠키 삭제됨: %s (%s)\n", domain.c_str(), name.c_str());
    } else {
      Serial.printf("[HTTP] 쿠키파일 쓰기 실패 (경로: %s)\n", path.c_str());
    }
  }
}



time_t HttpSecure::parseGMTToTimeT(const String& gmtStr) {
  setlocale(LC_TIME, "C");

  struct tm tm = {0};
  const char* formats[] = {
    "%a, %d %b %Y %H:%M:%S GMT",
    "%A, %d %b %Y %H:%M:%S GMT",
    "%a, %d-%b-%Y %H:%M:%S GMT",
    "%A, %d-%b-%Y %H:%M:%S GMT",
    nullptr
  };

  char* parsed = nullptr;
  for (int i = 0; formats[i] != nullptr; i++) {
    memset(&tm, 0, sizeof(tm));
    parsed = strptime(gmtStr.c_str(), formats[i], &tm);
    if (parsed != nullptr && *parsed == '\0') break;
  }

  if (parsed == nullptr || *parsed != '\0') {
    Serial.printf("[HTTP] 쿠키만료시간 파싱 오류: %s\n", gmtStr.c_str());
    return 0;
  }

  tm.tm_isdst = -1;  // 일광절약시간 고려 안함

#if defined(timegm)
  return timegm(&tm);
#else
  return mktime(&tm); 
#endif
}


void HttpSecure::debugCookiesystem() {
  Serial.println("[HTTP] 쿠키 디버깅 정보 :");

  if (!LittleFS.begin(false, "/spiffs", 10, "spiffs")) {
    Serial.println("[HTTP] LittleFS 마운트 실패");
    return;
  }

  File dir = LittleFS.open("/cookies");
  if (dir && dir.isDirectory()) {
    File file = dir.openNextFile();
    if(file){
      while (file) {
        Serial.printf("  - 파일: %s, 크기: %d bytes\n", file.name(), file.size());
        file = dir.openNextFile();
      }
    }else{
      Serial.println("  - 저장된 정보 없음");
    }
  }

}

