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
#include "esp_log.h"

HttpSecure::HttpSecure() {
  
  mbedtls_ssl_init(&_ssl);
  mbedtls_ssl_config_init(&_conf);
  mbedtls_ctr_drbg_init(&_ctr_drbg);
  
}

//vfs_api.cpp:99 -> does not exist, no permits for creation 에러 메시지 강제로 삭제
void HttpSecure::suppressLittleFSErrors() {
  esp_log_level_set("fs", ESP_LOG_NONE);
  esp_log_level_set("vfs", ESP_LOG_NONE);    
  esp_log_level_set("vfs_api", ESP_LOG_NONE);         
  esp_log_level_set("VFSImpl", ESP_LOG_NONE);        
  esp_log_level_set("esp_littlefs", ESP_LOG_NONE);
}

bool HttpSecure::begin(const char* fullUrl) {
  String url = fullUrl;

  suppressLittleFSErrors();

  // LittleFS 초기화 부분
  if (!_littlefsInitialized) {
    log_e("[HTTP] 쿠키저장소(LittleFS) 활성화중...");
    
    // 마운트 시도
    if (!LittleFS.begin(false, "/spiffs", 10, "spiffs")) {
      log_e("[HTTP] LittleFS 마운트 실패, 포맷 시도...");
    }

    // 포맷 시도
    if (LittleFS.format()) {
      log_e("[HTTP] LittleFS 포맷 성공");
      // 포맷 후 다시 마운트 시도
      if (!LittleFS.begin(true, "/spiffs", 10, "spiffs")) {
        log_e("[HTTP] LittleFS 포맷 후 마운트 실패!");
        return false;
      }
    } else {
      log_e("[HTTP] LittleFS 포맷 실패!");
      return false;
    }
    
    // 디렉토리 생성 확인
    if (!LittleFS.exists("/cookies")) {
      log_e("[HTTP] /cookies 디렉토리생성");
      if (!LittleFS.mkdir("/cookies")) {
        log_e("[HTTP] /cookies 디렉토리 생성 실패");
        // 실패해도 계속 진행 (파일은 직접 생성 시도)
      }
    }
    
    _littlefsInitialized = true;
    log_e("[HTTP] 쿠키저장소(LittleFS) 초기화 완료");
    
    // 파일 시스템 정보 출력
    log_e("Total space: %d bytes\n", LittleFS.totalBytes());
    log_e("Used space: %d bytes\n", LittleFS.usedBytes());
  }

  // 1. "https://" 제거
  if (url.startsWith("https://")) {
    _isSecure = true;
    url = url.substring(8);
    _port = (_port == 0) ? 443 : _port;  // HTTPS 기본 포트
  } else if (url.startsWith("http://")) {
    _isSecure = false;
    url = url.substring(7);
    _port = (_port == 0) ? 80 : _port;   // HTTP 기본 포트
  } else {
    log_e("[HTTP] 지원하지 않는 프로토콜");
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
    log_e("[HTTP] DNS 실패");
    return false;
  }

  // 4. 소켓 연결
  _socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (_socket < 0) {
    log_e("[HTTP] 소켓 생성 실패");
    return false;
  }

  struct sockaddr_in server;
  server.sin_family = AF_INET;
  server.sin_port = htons(_port);
  server.sin_addr.s_addr = ip;

  if (connect(_socket, (struct sockaddr*)&server, sizeof(server)) != 0) {
    log_e("[HTTP] 서버 연결 실패");
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
        log_e("[HTTP] SSL핸드셰이크 실패: %s", errbuf);
        close(_socket);
        return false;
      }
    }
    log_e("[HTTP] SSL 연결 성공");
  }else{
    log_e("[HTTP] 연결 성공");
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
  log_e("[HTTP] 서버 응답 헤더 목록 : ");
  for (const auto& header : _responseHeaders) {
    const String& key = header.first;
    for (const String& value : header.second) {
      log_e("%s: %s", key.c_str(), value.c_str());
    }
  }
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
      log_e("[HTTP] SSL 전송 실패: %s", err);
    }else{
      log_e("[HTTP] 전송 실패");
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
            key.toLowerCase();  // 👈 대소문자 무시 위해 소문자로 통일
          
            _responseHeaders[key].push_back(value); // 👈 vector에 저장
          
            if (key == "set-cookie") {
              processSetCookieHeader(value);  // 쿠키 저장
            }
          }
        }


      }
    }
  }

  if (len < 0 && len != MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
    log_e("[HTTP] 응답 수신 중 오류 발생");
  }
}

String HttpSecure::getCookieFilePath() {
  // 디렉토리 경로 설정 (절대 경로 사용)
  String dir = "/cookies";
  
  // 디렉토리 존재 확인 및 생성
  if (!LittleFS.exists(dir)) {
    log_e("[HTTP] /cookies 디렉토리 생성: %s\n", dir.c_str());
    if (!LittleFS.mkdir(dir)) {
      log_e("[HTTP] /cookies 디렉토리 생성 실패! 대체 경로 사용");
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
    log_d("[HTTP] 쿠키 속성: %s", attr.c_str());

    if (attr.startsWith("Domain=")) {
      domain = attr.substring(7);
    } else if (attr.startsWith("Path=")) {
      path = attr.substring(5);
    } else if (attr.startsWith("Max-Age=")) {
      String maxAgeStr = attr.substring(8);
      time_t maxAge = maxAgeStr.toInt();
      expire = (maxAge > 0) ? time(nullptr) + maxAge : 0;
    } else if (attr.startsWith("Expires=") && expire == 0) {
      String expiresStr = attr.substring(8);
      expire = parseGMTToTimeT(expiresStr);
    } else if (attr.equalsIgnoreCase("Secure")) {
      secure = true;
    } else if (attr.equalsIgnoreCase("HttpOnly")) {
      httpOnly = true;
    }
  }

  if (domain.length() == 0) {
    domain = _host;
  }

  if (expire == 0) {
    log_e("[HTTP] 유효한 만료 시간이 설정되지 않음. 기본값: 0");
  }

  storeCookieToLittleFS(name, value, expire);
  log_i("[HTTP] 쿠키 저장됨: %s=%s (만료: %ld)", name.c_str(), value.c_str(), expire);
}

void HttpSecure::storeCookieToLittleFS(const String& name, const String& value, time_t expire) {
  String path = getCookieFilePath();
  if (path.length() == 0) {
    log_e("[HTTP] 쿠키의 유효한 경로 없음 - 저장 실패");
    return;
  }

  const size_t maxCookies = 20;
  DynamicJsonDocument doc(4096);
  JsonArray arr;

  // 1. 읽기 모드로 열어서 파싱
  if (LittleFS.exists(path)) {
    File file = LittleFS.open(path, "r");
    if (file) {
      DeserializationError err = deserializeJson(doc, file);
      file.close();
      if (err) {
        log_e("[HTTP] 기존 쿠키 파싱 실패, 새로 생성");
        doc.to<JsonArray>();
      }
    }
  } else {
    doc.to<JsonArray>();
  }

  arr = doc.as<JsonArray>();

  bool updated = false;
  time_t now = time(nullptr);

  // 2. 기존 쿠키 정리
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

  // 3. 새 쿠키 추가
  if (!updated && arr.size() < maxCookies) {
    JsonObject c = arr.createNestedObject();
    c["name"] = name;
    c["value"] = value;
    c["expire"] = expire;
    c["domain"] = _host;
    c["path"] = "/";
  }

  // 4. 쓰기 모드로 다시 열어서 저장
  File file = LittleFS.open(path, "w");
  if (file) {
    if (serializeJson(doc, file) == 0) {
      log_e("[HTTP] 쿠키파일 쓰기 실패");
    }
    file.close();
  } else {
    log_e("[HTTP] 쿠키 파일 쓰기 실패: %s\n", path.c_str());
  }
}

String HttpSecure::getValidCookiesFromLittleFS() {
  String path = getCookieFilePath();
  String result = "";
  time_t now = time(nullptr);

  if (!LittleFS.exists(path)) return "";

  // 1. 읽기 전용으로 열기
  File file = LittleFS.open(path, "r");
  if (!file) {
    log_e("[HTTP] 쿠키 파일 열기 실패: %s", path.c_str());
    return "";
  }

  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, file);
  file.close();

  if (err) {
    log_e("[HTTP] 쿠키 JSON 파싱 오류: %s", path.c_str());
    return "";
  }

  JsonArray arr = doc.as<JsonArray>();
  bool changed = false;

  // 2. 만료 쿠키 제거 및 유효 쿠키 수집
  for (int i = arr.size() - 1; i >= 0; --i) {
    JsonObject c = arr[i];

    if (!c.containsKey("name") || !c.containsKey("expire")) {
      arr.remove(i);
      changed = true;
      continue;
    }

    time_t expire = c["expire"].as<time_t>();
    if (expire > 0 && expire < now) {
      log_e("[HTTP] 쿠키 만료됨: %s (만료시간: %ld, 현재: %ld)", 
            c["name"].as<const char*>(), expire, now);
      arr.remove(i);
      changed = true;
      continue;
    }

    // 쿠키를 result 문자열에 추가
    if (result.length()) result += "; ";
    result += String(c["name"].as<const char*>()) + "=" + String(c["value"].as<const char*>());
  }

  // 3. 변경사항 있으면 다시 쓰기
  if (changed) {
    File file = LittleFS.open(path, "w");
    if (file) {
      serializeJson(doc, file);
      file.close();
      log_e("[HTTP] 만료된 쿠키 정리 완료");
    } else {
      log_e("[HTTP] 쿠키파일 쓰기 실패 (경로: %s)", path.c_str());
    }
  }

  return result;
}

void HttpSecure::printAllCookies() {
  String path = getCookieFilePath();
  time_t now = time(nullptr);

  if (!LittleFS.exists(path)) {
    log_e("[HTTP] 쿠키 파일 없음: %s", path.c_str());
    return;
  }

  File file = LittleFS.open(path, "r");
  if (!file) {
    log_e("[HTTP] 쿠키 파일 열기 실패: %s", path.c_str());
    return;
  }

  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, file);
  file.close();

  if (err) {
    log_e("[HTTP] 쿠키 JSON 파싱 오류: %s", path.c_str());
    return;
  }

  JsonArray arr = doc.as<JsonArray>();
  log_e("[HTTP] 저장된 쿠키 목록 (%s):", path.c_str());

  for (JsonObject c : arr) {
    if (!c.containsKey("name") || !c.containsKey("expire")) continue;

    time_t expire = c["expire"].as<time_t>();
    String status = (expire > 0 && expire < now) ? "  [만료]" : "  [유효]";

    log_e("%s %s=%s", status.c_str(),
          c["name"].as<const char*>(),
          c["value"].as<const char*>());
    log_e("    도메인: %s, 만료: %ld",
          c.containsKey("domain") ? c["domain"].as<const char*>() : "(없음)",
          expire);
  }

}

void HttpSecure::clearAllCookies() {
  String dirPath = "/cookies";

  if (!LittleFS.exists(dirPath)) {
    log_e("[HTTP] 쿠키 디렉토리 없음: %s", dirPath.c_str());
    return;
  }

  File dir = LittleFS.open(dirPath);
  if (!dir || !dir.isDirectory()) {
    log_e("[HTTP] 쿠키 디렉토리 열기 실패 또는 디렉토리 아님");
    return;
  }

  File file = dir.openNextFile();
  while (file) {
    String filename = file.name();
    file.close();

    if (LittleFS.remove(filename)) {
      log_e("[HTTP] 쿠키 삭제됨: %s", filename.c_str());
    } else {
      log_e("[HTTP] 쿠키 삭제 실패: %s", filename.c_str());
    }

    file = dir.openNextFile();
  }

  log_e("[HTTP] 모든 쿠키 삭제 완료");
}










time_t HttpSecure::parseGMTToTimeT(const String& gmtStr) {
  struct tm tm = {0};
  if (!strptime(gmtStr.c_str(), "%a, %d %b %Y %H:%M:%S GMT", &tm)) {
      log_e("Failed to parse Expires: %s", gmtStr.c_str());
      return 0;
  }

  // ESP-IDF에서 timegm()이 없을 경우 mktime()으로 대체
  #if !defined(timegm)
      struct timeval tv;
      struct timezone tz;
      gettimeofday(&tv, &tz);
      long timezone_offset = tz.tz_minuteswest * 60;
      return mktime(&tm) - timezone_offset;
  #else
      return timegm(&tm); // UTC 변환
  #endif
}

void HttpSecure::debugCookiesystem() {
  log_e("[HTTP] 쿠키 디버깅 정보 :");

  if (!LittleFS.begin(false, "/spiffs", 10, "spiffs")) {
    log_e("[HTTP] LittleFS 마운트 실패");
    return;
  }

  File dir = LittleFS.open("/cookies");
  if (dir && dir.isDirectory()) {
    File file = dir.openNextFile();
    while (file) {
      log_e("파일: %s, 크기: %d bytes\n", file.name(), file.size());
      file = dir.openNextFile();
    }
  }

}

