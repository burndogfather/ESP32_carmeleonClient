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
    Serial.println("[HTTP] LittleFS 마운트 실패!");
  }

  // 포맷 시도
  if (LittleFS.format()) {
    // 포맷 후 다시 마운트 시도
    if (!LittleFS.begin(true, "/spiffs", 10, "spiffs")) {
      Serial.println("[HTTP] LittleFS 포맷 후 마운트 실패!");
      *(taskParams->pSuccess) = false;
      vTaskDelete(NULL);
      return;
    }
  } else {
    Serial.println("[HTTP] LittleFS 포맷 실패!");
    *(taskParams->pSuccess) = false;
    vTaskDelete(NULL);
    return;
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
  Serial.println("[HTTP] 쿠키저장소(LittleFS) 초기화 완료 : ");
    
  // 파일 시스템 정보 출력
  Serial.printf("  Total space: %d bytes\n", LittleFS.totalBytes());
  Serial.printf("  Used space: %d bytes\n", LittleFS.usedBytes());
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
  if (_isSecure) {
    mbedtls_ssl_close_notify(&_ssl);
    mbedtls_ssl_free(&_ssl);
    mbedtls_ssl_config_free(&_conf);
    mbedtls_ctr_drbg_free(&_ctr_drbg);
  }
  close(_socket);
  _connected = false;
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
  // 0. 시간대 강제 설정 (UTC 기준)
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

