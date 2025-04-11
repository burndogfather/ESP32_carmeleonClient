#define USE_LITTLEFS
#include "HttpSecure.h"
#include <Ethernet/EthernetESP32.h>
#include <FS.h>
#include <LittleFS.h> 
#include <ArduinoJson/ArduinoJson.h>
#include <map>

HttpSecure::HttpSecure() {
  
  mbedtls_ssl_init(&_ssl);
  mbedtls_ssl_config_init(&_conf);
  mbedtls_ctr_drbg_init(&_ctr_drbg);

}

bool HttpSecure::begin(const char* fullUrl) {
  String url = fullUrl;

  //쿠키를 저장할 LittleFS 초기화
  if (!_littlefsInitialized) {
    if (!LittleFS.begin(false, "/spiffs", 10, "spiffs")) {
      LittleFS.format(); 
      Serial.println("[HTTP] LittleFS 포맷시도");
      if (!LittleFS.begin(true, "/spiffs", 10, "spiffs")) {
        Serial.println("[HTTP] LittleFS 마운트실패");
        return false;
      }
    }
    if (!LittleFS.exists("/cookies")) {
      if (!LittleFS.mkdir("/cookies")) {
        Serial.println("[COOKIE] 디렉토리 생성 실패");
      }
    }
    _littlefsInitialized = true;
    Serial.println("[HTTP] LittleFS 초기화 완료");
  }

  // 1. "https://" 제거
  if (url.startsWith("https://")) {
    _isSecure = true;
    url = url.substring(8);
  } else if (url.startsWith("http://")) {
    _isSecure = false;
    url = url.substring(7);
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
    _port = url.substring(colonIndex + 1, slashIndex).toInt();
  } else {
    _host = url.substring(0, slashIndex);
    _port = (_port == 0) ? (_isSecure ? 443 : 80) : _port;
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
        Serial.print("[HTTP] SSL핸드셰이크 실패: ");
        Serial.println(errbuf);
        close(_socket);
        return false;
      }
    }
    Serial.println("[HTTP] SSL 연결 성공");
  }else{
    Serial.println("[HTTP] 연결 성공");
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
      return header.second;
    }
  }
  return "";
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
  req += "Host: " + _host + "\r\n";

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
      Serial.print("[HTTP] SSL 전송 실패: ");
      Serial.println(err);
    }else{
      Serial.print("[HTTP] 전송 실패: ");
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
      int headerEnd = _response.indexOf("\r\n\r\n");
      if (headerEnd != -1) {
        String headerPart = _response.substring(0, headerEnd);
        _response = _response.substring(headerEnd + 4);
        headerEnded = true;

        int lineStart = 0;
        while (true) {
          int lineEnd = headerPart.indexOf("\r\n", lineStart);
          if (lineEnd == -1) break;
          String line = headerPart.substring(lineStart, lineEnd);
          lineStart = lineEnd + 2;

          int colon = line.indexOf(':');
          if (colon != -1) {
            String key = line.substring(0, colon);
            String value = line.substring(colon + 1);
            key.trim(); value.trim();

            _responseHeaders[key] = value;

            if (key.equalsIgnoreCase("Set-Cookie")) {
              processSetCookieHeader(value);
            }
          }
        }
      }
    }
  }

  if (len < 0 && len != MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
    Serial.println("[HTTP] 응답 수신 중 오류 발생");
  }
}

String HttpSecure::getCookieFilePath() {
  String dir = "/cookies";
  
  // 디렉토리 존재 확인 및 생성
  if (!LittleFS.exists(dir)) {
    Serial.printf("[COOKIE] 디렉토리 생성 시도: %s\n", dir.c_str());
    if (!LittleFS.mkdir(dir)) {
      Serial.println("[COOKIE] 디렉토리 생성 실패!");
      return ""; // 생성 실패 시 빈 문자열 반환
    }
  }
  
  String filePath = dir + "/" + _host + ".json";
  Serial.printf("[COOKIE] 쿠키 파일 경로: %s\n", filePath.c_str());

  String newPath = filePath;
  newPath.replace("/spiffs", "");
  return newPath;
}

void HttpSecure::processSetCookieHeader(const String& cookieHeader) {
  String name, value, domain, path;
  time_t expire = 0;
  bool secure = false;
  bool httpOnly = false;

  int nameEnd = cookieHeader.indexOf('=');
  if (nameEnd == -1) return;

  name = cookieHeader.substring(0, nameEnd);
  int valueEnd = cookieHeader.indexOf(';', nameEnd);
  value = cookieHeader.substring(nameEnd + 1, valueEnd == -1 ? cookieHeader.length() : valueEnd);

  // 속성 파싱
  int attrStart = valueEnd;
  while (attrStart != -1 && attrStart < cookieHeader.length()) {
    attrStart = cookieHeader.indexOf(';', attrStart + 1);
    if (attrStart == -1) break;

    int attrEnd = cookieHeader.indexOf(';', attrStart + 1);
    if (attrEnd == -1) attrEnd = cookieHeader.length();

    String attr = cookieHeader.substring(attrStart + 1, attrEnd);
    attr.trim();

    if (attr.startsWith("Domain=")) {
      domain = attr.substring(7);
    } else if (attr.startsWith("Path=")) {
      path = attr.substring(5);
    } else if (attr.startsWith("Expires=")) {
      String expiresStr = attr.substring(8);
      // 간단한 날짜 파싱 (실제 구현에서는 더 정교한 파싱 필요)
      struct tm tm;
      if (strptime(expiresStr.c_str(), "%a, %d %b %Y %H:%M:%S GMT", &tm)) {
        expire = mktime(&tm);
      }
    } else if (attr.startsWith("Max-Age=")) {
      String maxAgeStr = attr.substring(8);
      time_t maxAge = maxAgeStr.toInt();
      if (maxAge > 0) {
        expire = time(nullptr) + maxAge;
      }
    } else if (attr.equalsIgnoreCase("Secure")) {
      secure = true;
    } else if (attr.equalsIgnoreCase("HttpOnly")) {
      httpOnly = true;
    }
  }

  // 도메인이 지정되지 않은 경우 현재 호스트 사용
  if (domain.length() == 0) {
    domain = _host;
  }

  // 쿠키 저장
  storeCookieToLittleFS(name, value, expire);
  Serial.printf("[COOKIE] 저장됨: %s=%s (만료: %ld)\n", name.c_str(), value.c_str(), expire);
}

void HttpSecure::storeCookieToLittleFS(const String& name, const String& value, time_t expire) {
  String path = getCookieFilePath();
  if (path.length() == 0) {
    Serial.println("[COOKIE] 유효한 경로 없음 - 저장 실패");
    return;
  }

  // 파일 존재 여부와 상관없이 쓰기 모드로 열기
  File file = LittleFS.open(path, "w+");
  if (!file) {
    Serial.printf("[COOKIE] 파일 열기 실패: %s\n", path.c_str());
    return;
  }

  const size_t maxCookies = 20;  // 최대 쿠키 수 증가
  DynamicJsonDocument doc(4096);  // 버퍼 크기 증가

  // 파일 읽기 시도 (존재하지 않으면 빈 배열로 시작)
  if (file.size() > 0) {
    DeserializationError err = deserializeJson(doc, file);
    if (err) {
      Serial.println("[COOKIE] JSON 파싱 오류, 새 파일 생성");
      doc.to<JsonArray>();
    }
    file.seek(0); // 파일 포인터 처음으로 이동
  } else {
    doc.to<JsonArray>();
  }

  JsonArray arr = doc.as<JsonArray>();
  bool updated = false;
  time_t now = time(nullptr);

  // 기존 쿠키 검색 및 업데이트
  for (int i = arr.size() - 1; i >= 0; --i) {
    JsonObject c = arr[i];
    if (!c.containsKey("name") || !c.containsKey("expire") || 
        (c["expire"].as<time_t>() > 0 && c["expire"].as<time_t>() < now)) {
      arr.remove(i);
      continue;
    }
    if (c["name"] == name) {
      c["value"] = value;
      c["expire"] = expire;
      updated = true;
    }
  }

  // 새 쿠키 추가
  if (!updated && arr.size() < maxCookies) {
    JsonObject c = arr.createNestedObject();
    c["name"] = name;
    c["value"] = value;
    c["expire"] = expire;
    c["domain"] = _host;
    c["path"] = "/";
  }

  // 파일 쓰기
  if (serializeJson(doc, file) == 0) {
    Serial.println("[COOKIE] 파일 쓰기 실패");
  }
  file.close();
}

String HttpSecure::getValidCookiesFromLittleFS() {
  String path = getCookieFilePath();
  String result = "";
  time_t now = time(nullptr);

  File file = LittleFS.open(path, "r");
  if (!file) return "";

  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, file);
  file.close();

  if (err) {
    Serial.println("[COOKIE] JSON 파싱 오류");
    return "";
  }

  JsonArray arr = doc.as<JsonArray>();
  bool changed = false;

  // 역순으로 순회하며 만료된 �키 삭제
  for (int i = arr.size() - 1; i >= 0; --i) {
    JsonObject c = arr[i];
    if (!c.containsKey("name") || !c.containsKey("expire")) {
      arr.remove(i);
      changed = true;
      continue;
    }

    time_t expire = c["expire"].as<time_t>();
    if (expire > 0 && expire < now) {
      Serial.printf("[COOKIE] 만료됨: %s (만료시간: %ld, 현재: %ld)\n", 
                   c["name"].as<const char*>(), expire, now);
      arr.remove(i);
      changed = true;
      continue;
    }

    if (result.length()) result += "; ";
    result += String(c["name"].as<const char*>()) + "=" + String(c["value"].as<const char*>());
  }

  if (changed) {
    file = LittleFS.open(path, "w");
    if (file) {
      serializeJson(doc, file);
      file.close();
      Serial.println("[COOKIE] 만료된 쿠키 정리 완료");
    } else {
      Serial.println("[COOKIE] 파일 쓰기 실패");
    }
  }

  return result;
}

void HttpSecure::printAllCookies() {
  String path = getCookieFilePath();
  time_t now = time(nullptr);

  File file = LittleFS.open(path, "w+");
  if (!file) {
    Serial.println("[COOKIE] 쿠키 파일 없음");
    return;
  }

  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, file);
  file.close();

  if (err) {
    Serial.println("[COOKIE] JSON 파싱 오류");
    return;
  }

  JsonArray arr = doc.as<JsonArray>();
  Serial.println("===== 저장된 쿠키 목록 =====");
  for (JsonObject c : arr) {
    if (!c.containsKey("name") || !c.containsKey("expire")) continue;
    
    time_t expire = c["expire"].as<time_t>();
    String status = (expire > 0 && expire < now) ? "[만료]" : "[유효]";
    
    Serial.printf("%s %s=%s\n", status.c_str(), 
                  c["name"].as<const char*>(), 
                  c["value"].as<const char*>());
    Serial.printf("  도메인: %s, 만료: %ld\n", 
                  c.containsKey("domain") ? c["domain"].as<const char*>() : "(없음)",
                  expire);
  }
  Serial.println("========================");
}

void HttpSecure::clearAllCookies() {
  File root = LittleFS.open("/cookies", "w+");
  if (!root || !root.isDirectory()) return;

  File file = root.openNextFile();
  while (file) {
    String name = file.name();
    file.close();
    LittleFS.remove(name);
    file = root.openNextFile();
  }
  Serial.println("[COOKIE] 모든 쿠키 삭제 완료");
}












void HttpSecure::debugFilesystem() {
  Serial.println("\n===== 파일 시스템 디버깅 정보 =====");
  Serial.printf("LittleFS 마운트 상태: %s\n", LittleFS.begin(false, "/spiffs", 10, "spiffs") ? "성공" : "실패");
  
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  while(file){
    Serial.printf("파일: %s, 크기: %d bytes\n", file.name(), file.size());
    file = root.openNextFile();
  }
  Serial.println("===============================\n");
}

