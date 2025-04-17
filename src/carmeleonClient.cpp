#include "ArduinoJson/ArduinoJson.h"
#include "Http/HttpSecure.h"
#include "carmeleonClient.h"
#include "Encryption.h"
#include "secret_key.h"
#include "JsonBuilder.h"


class VecWriter {
  public:
    std::vector<uint8_t>& buf;

    VecWriter(std::vector<uint8_t>& b) : buf(b) {}

    size_t write(uint8_t c) {
      buf.push_back(c);
      return 1;
    }

    size_t write(const uint8_t* data, size_t len) {
      buf.insert(buf.end(), data, data + len);
      return len;
    }
};

static void ethernetMaintainTask(void* pvParameters) {
  while (true) {
    Ethernet.maintain();
    vTaskDelay(pdMS_TO_TICKS(300));
  }
}

carmeleonClient::carmeleonClient() : evt(&Http, ""){
  xTaskCreate(ethernetMaintainTask, "EthMaintain", 2048, nullptr, 1, nullptr);
}

Response carmeleonClient::api(
  const String& url,
  std::initializer_list<std::pair<const char*, JsonVariantWrapper>> params,
  const String& userAgent,
  const String& token
) {
  Response res;
  Encryption enc;
  JsonBuilder builder;

  if (!this->Http.begin(url.c_str())) {
    Serial.println("HTTP 시작 실패");
    res.statusCode = 0;
    return res;
  }

  // NTP 동기화
  if (time(nullptr) < 24 * 3600) {
    EthernetClass eth;
    eth.setNTP(eth.getNTPServer());
    int retry = 0;
    while (time(nullptr) < 24 * 3600 && retry++ < 100) delay(100);
  }

  // 시크릿 키 생성
  uint64_t nowMillis = (uint64_t)time(nullptr) * 1000;
  String key = secret_key(nowMillis);

  // JSON 문자열 생성
  String jsonStr = "{";
  builder._buildJson(jsonStr, params);

  // _TOKEN_ 자동 추가
  String tokenVal = token;
  if (tokenVal.isEmpty()) {
    int domainStart = url.indexOf("://") + 3;
    int domainEnd = url.indexOf("/", domainStart);
    String domain = url.substring(domainStart, domainEnd);
    tokenVal = this->Http.getCookie(domain, "_TOKEN_");
    if (!tokenVal.isEmpty()) {
      this->Http.setCookie(domain, "_TOKEN_", tokenVal);
    }
  }

  bool needComma = (params.size() > 0);

  if (!tokenVal.isEmpty()) {
    if (needComma) jsonStr += ",";
    jsonStr += "\"_TOKEN_\":\"" + tokenVal + "\"";
    needComma = true;
  }

  if (!userAgent.isEmpty()) {
    if (needComma) jsonStr += ",";
    jsonStr += "\"_USERAGENT\":\"" + userAgent + "\"";
  }

  jsonStr += "}";

  this->Http.requestHeader("User-Agent", userAgent);
  int status = this->Http.post(jsonStr, "application/json");
  res.statusCode = status;

  String responseBody = this->Http.responseBody();
  this->Http.end();

  // 복호화
  DynamicJsonDocument respDoc(512);
  if (!deserializeJson(respDoc, responseBody)) {
    if (respDoc.is<JsonArray>() && respDoc.size() == 1 && respDoc[0].is<const char*>()) {
      String decrypted = enc.decrypt(respDoc[0], key);
      DeserializationError err = deserializeJson(res.json, decrypted);
      if (err) {
        Serial.println("복호화 JSON 파싱 실패");
      }
    } else {
      res.json = respDoc;
    }
  } else {
    res.json = respDoc;
  }

  return res;
}


void Response::fromMsgpack(const std::vector<uint8_t>& data) {
  DeserializationError err = deserializeMsgPack(json, data.data(), data.size());
  if (err) {
    Serial.print("MsgPack 파싱 실패: ");
    Serial.println(err.c_str());
  }
}

WSEvent::WSEvent() : _http(nullptr), _keepAlive(false), _url("") {}

WSEvent::WSEvent(HttpSecure* http, const String& url) 
  : _http(http), _url(url), _keepAlive(false) {}

void WSEvent::KeepAlive(bool enable) {
  _keepAlive = enable;
  if (_http) _http->KeepAlive(enable);
}

void WSEvent::start() {
  if (!_http->begin(_url.c_str())) {
    Serial.println("WebSocket begin 실패");
    if (_onDisconnected) _onDisconnected();
    return;
  }

  //라다이렉트후 KeepAlive 유지
  _http->KeepAlive(_keepAlive);

  _http->onHandshake([this]() {
    if (_onConnected) _onConnected();
  });

  _http->onDisconnected([this]() {
    if (_onDisconnected) _onDisconnected();
  });

  _http->onMsgString([this](String msg) {
    if (_onReceiveString) _onReceiveString(msg);
  });

  _http->onMsgBinary([this](std::vector<uint8_t> data) {
    
    Response res;
    res.fromMsgpack(data);

    //redirect 자동처리
    if (res.json.containsKey("redirect")) {

      if (res.json.containsKey("redirect")) {
        JsonVariant redirVal = res["redirect"];
    
        // 문자열 타입이면 재연결
        if (redirVal.is<const char*>()) {
          String newRedirect = redirVal.as<const char*>();
        
          String newUrl;
          if (newRedirect.startsWith("wss://") || newRedirect.startsWith("ws://")) {
            // 전체 주소 그대로 사용
            newUrl = newRedirect;
          } else {
            // 기존 도메인 추출: wss://host:port/path
            int protoEnd = _url.indexOf("://") + 3;
            int pathStart = _url.indexOf("/", protoEnd);
        
            String baseUrl = (pathStart > 0) ? _url.substring(0, pathStart) : _url;  // wss://host[:port]
            
            // redirect가 /로 시작하지 않으면 / 붙여줌
            if (!newRedirect.startsWith("/")) {
              newRedirect = "/" + newRedirect;
            }
        
            newUrl = baseUrl + newRedirect;
          }
        
          Serial.printf("redirect 감지 → 재연결 시도: %s\n", newUrl.c_str());
        
          KeepAlive(false);
          _http->end();
          _url = newUrl;
        
          for (const auto& h : _customHeaders) {
            _http->requestHeader(h.first, h.second);
          }
        
          if (!_http->begin(_url.c_str())) {
            if (_onDisconnected) _onDisconnected();
            return;
          }

          //라다이렉트후 KeepAlive 유지
          _http->KeepAlive(_keepAlive); 
        
          if (!_http->handshake()) {
            if (_onDisconnected) _onDisconnected();
            return;
          }
        
          if (_onConnected) _onConnected();
          return;
        }


      }

    }

    if (_onReceive) {
      _onReceive(res);
    }
  });

  if (!_http->handshake()) {
    Serial.println("WebSocket handshake 실패");
    if (_onDisconnected) _onDisconnected();
  }
}

void WSEvent::close() {
  if (_http){
    _http->KeepAlive(false);
    _http->sendMsgString("bye");
    _http->end();
  }
  
}

void WSEvent::onConnected(std::function<void()> cb) { _onConnected = cb; }
void WSEvent::onDisconnected(std::function<void()> cb) { _onDisconnected = cb; }
void WSEvent::onReceiveString(std::function<void(String)> cb) { _onReceiveString = cb; }
void WSEvent::onReceive(std::function<void(Response)> cb) { _onReceive = cb; }
void WSEvent::onSend(std::function<void(String)> cb) { _onSend = cb; }

void WSEvent::send(const String& msg) {
  if (_http) _http->sendMsgString(msg);
  if (_onSend) _onSend(msg);
}

void WSEvent::send(const std::vector<uint8_t>& binaryData) {
  if (_http) _http->sendMsgBinary(binaryData);
  if (_onSend) {
    String hexStr;
    for (uint8_t b : binaryData) {
      if (b < 16) hexStr += "0";
      hexStr += String(b, HEX);
      hexStr += " ";
    }
    hexStr.trim();
    _onSend(hexStr);
  }
}

void WSEvent::send(JsonDocument& doc) {
  std::vector<uint8_t> binaryData;
  VecWriter writer(binaryData);  
  serializeMsgPack(doc, writer);  
  if (_http) _http->sendMsgBinary(binaryData);

  if (_onSend) {
    String jsonStr;
    serializeJson(doc, jsonStr);
    _onSend(jsonStr);
  }
}

void WSEvent::send(std::initializer_list<std::pair<const char*, JsonVariantWrapper>> kv) {
  String jsonStr = "{";
  _buildJson(jsonStr, kv); 
  jsonStr += "}";

  DynamicJsonDocument doc(512);
  deserializeJson(doc, jsonStr);

  std::vector<uint8_t> binaryData;
  VecWriter writer(binaryData);  
  serializeMsgPack(doc, writer);

  if (_http) _http->sendMsgBinary(binaryData);

  if (_onSend) {
    _onSend(jsonStr);
  }

}

WSEvent& carmeleonClient::ws(const String& url, const std::map<String, String>& headers) {
  for (const auto& h : headers) {
    Http.requestHeader(h.first, h.second);
    evt._customHeaders[h.first] = h.second;
  }
  evt._url = url;    
  evt._http = &Http; 
  return evt;
}
