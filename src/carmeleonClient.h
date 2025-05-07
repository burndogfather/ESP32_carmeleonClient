#ifndef CARMELEON_CLIENT_H
#define CARMELEON_CLIENT_H

#include <Arduino.h>
#include <map>
#include "ArduinoJson/ArduinoJson.h"
#include "Ethernet/EthernetESP32.h"
#include "Http/HttpSecure.h"
#include "HttpsOTAWrapper.h"
#include "JsonBuilder.h"

class Response {
  public:
      DynamicJsonDocument json;
      int statusCode;
  
      Response() : json(8192), statusCode(0) {}
  
      void prettyPrint() {
          serializeJsonPretty(json, Serial);
          Serial.println();
      }

  
      template <typename T>
      bool is(const String& key) {
          return json[key].is<T>();
      }
  
      template <typename T>
      T get(const String& key) {
          return json[key].as<T>();
      }
  
      bool contains(const String& key) {
          return json.containsKey(key);
      }

      JsonVariant operator[](const String& key) { return json[key]; }
      JsonVariantConst operator[](const String& key) const { return json[key]; }
      JsonVariant operator[](size_t index) { return json[index]; }
      JsonVariantConst operator[](size_t index) const { return json[index]; }


      void fromMsgpack(const std::vector<uint8_t>& data);

};

class WSEvent : public JsonBuilder {
  friend class carmeleonClient;

  private:
    HttpSecure* _http;
    bool _keepAlive = false;
    bool _WSconn = false;
    String _url;
    std::map<String, String> _customHeaders;

  public:
    
    std::function<void()> _onConnected;
    std::function<void()> _onDisconnected;
    std::function<void(String)> _onReceiveString;
    std::function<void(Response)> _onReceive;
    std::function<void(String)> _onSend;

    WSEvent();
    WSEvent(HttpSecure* http, const String& url);

    void KeepAlive(bool enable);
    void onConnected(std::function<void()> cb);
    void onDisconnected(std::function<void()> cb);
    void onReceiveString(std::function<void(String)> cb);
    void onReceive(std::function<void(Response)> cb);
    void onSend(std::function<void(String)> cb);

    void send(const String& msg);
    void send(const std::vector<uint8_t>& binaryData);
    void send(JsonDocument& doc);
    void send(std::initializer_list<std::pair<const char*, JsonVariantWrapper>> kv);

    void start();
    void close();
};
  
class carmeleonClient {
  public:
    EthernetClass Eth;
    HttpSecure Http;
    HttpsOTAWrapper Ota;
    WSEvent evt; 
    
  
    carmeleonClient();

    Response api(
        const String& url,
        std::initializer_list<std::pair<const char*, JsonVariantWrapper>> params = {},
        const String& userAgent = "carmeleonclient/1.0",
        const String& token = ""
    );

    WSEvent& ws(const String& url, const std::map<String,String>& headers = {});


};
  

#endif