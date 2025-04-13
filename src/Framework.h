#pragma once

#include "ArduinoJson/ArduinoJson.h"
#include "Http/HttpSecure.h"
#include <map>
#include <string>
#include <vector>
#include <mbedtls/base64.h>
#include <mbedtls/aes.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <time.h>
#include "secret_key.h"

#define MBEDTLS_PKCS5_C

class Response {
public:
    using JsonDocument = DynamicJsonDocument;
    JsonDocument doc;
    int statusCode;

    Response() : doc(8192) {}

    void fromJson(const String& json) {
        deserializeJson(doc, json);
    }

    ArduinoJson::JsonVariant any(const String& key) {
        return doc[key];
    }

    template<typename T>
    bool is(const String& key) {
        return doc[key].is<T>();
    }

    void prettyPrint() {
        serializeJsonPretty(doc, Serial);
        Serial.println();
    }
};

class CarmeleonFramework {
public:
    Response api(const std::string& url, const std::map<std::string, std::string>& params) {
        HttpSecure http;
        EthernetClass eth;
        http.begin(url.c_str());

        // Ensure time is synced
        if (time(nullptr) < 24 * 3600) {
            eth.setNTP(eth.getNTPServer());
            time_t now = 0;
            int retry = 0;
            while ((now = time(nullptr)) < 24 * 3600 && retry++ < 100) {
                delay(100);
            }
        }

        uint64_t millis_now = (uint64_t)time(nullptr) * 1000;
        String _secret_key = secret_key(millis_now);

        // Build POST body
        String body = "";
        for (const auto& p : params) {
            if (!body.isEmpty()) body += "&";
            body += p.first.c_str();
            body += "=";
            body += p.second.c_str();
        }

        int status = http.post(body, "application/x-www-form-urlencoded");
        String responseBody = http.responseBody();
        http.end();

        Response res;
        res.statusCode = status;

        // Try to parse as wrapped encrypted MsgPack
        DynamicJsonDocument wrapper(1024);
        DeserializationError err = deserializeJson(wrapper, responseBody);
        if (!err && wrapper.is<JsonArray>() && wrapper.size() == 1 && wrapper[0].is<const char*>()) {
            // Base64 decode
            String encoded = wrapper[0].as<String>();
            std::vector<uint8_t> decoded(encoded.length() * 3 / 4 + 1);
            size_t outlen = 0;
            if (mbedtls_base64_decode(decoded.data(), decoded.size(), &outlen,
                                      (const uint8_t*)encoded.c_str(), encoded.length()) == 0) {
                decoded.resize(outlen);
                DynamicJsonDocument encDoc(8192);
                if (!deserializeJson(encDoc, decoded.data(), decoded.size())) {
                    String ciphertext = encDoc["ciphertext"].as<String>();
                    String iv = encDoc["iv"].as<String>();
                    String salt = encDoc["salt"].as<String>();
                    int iterations = encDoc["iterations"].as<int>();

                    // Derive AES key
                    uint8_t key[32];
                    mbedtls_pkcs5_pbkdf2_hmac_ext(
                        MBEDTLS_MD_SHA256,  // ✔️ enum 값 직접 사용
                        (const uint8_t*)_secret_key.c_str(), _secret_key.length(),
                        (const uint8_t*)salt.c_str(), salt.length(),
                        iterations,
                        32, key
                    );

                    // Decode ciphertext
                    std::vector<uint8_t> cipher(ciphertext.length() * 3 / 4);
                    size_t cipher_len;
                    if (mbedtls_base64_decode(cipher.data(), cipher.size(), &cipher_len,
                                              (const uint8_t*)ciphertext.c_str(), ciphertext.length()) == 0) {
                        cipher.resize(cipher_len);

                        uint8_t iv_bin[16];
                        memcpy(iv_bin, iv.c_str(), 16);

                        std::vector<uint8_t> plain(cipher.size());
                        mbedtls_aes_context aes;
                        mbedtls_aes_init(&aes);
                        mbedtls_aes_setkey_dec(&aes, key, 256);
                        mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, cipher.size(), iv_bin, cipher.data(), plain.data());
                        mbedtls_aes_free(&aes);

                        Serial.println("[DEBUG] 복호화 결과 (hex):");
                        for (size_t i = 0; i < cipher_len; ++i) {
                            Serial.printf("%02X ", plain[i]);
                        }
                        Serial.println();
                        Serial.println("[DEBUG] 복호화 결과 (text):");
                        Serial.println((const char*)plain.data());

                        plain.push_back('\0');
                        const char* jsonStart = strchr((const char*)plain.data(), '{');
                        if (jsonStart && !deserializeJson(res.doc, jsonStart)) {
                            Serial.println("[Framework] ✅ AES 복호화 및 JSON 파싱 성공");
                        } else {
                            Serial.println("[Framework] ❌ 복호화된 JSON 파싱 실패");
                        }
                    } else {
                        Serial.println("[Framework] ❌ ciphertext Base64 디코딩 실패");
                    }
                } else {
                    Serial.println("[Framework] ❌ MsgPack 암호화 JSON 파싱 실패");
                }
            } else {
                Serial.println("[Framework] ❌ 응답 Base64 디코딩 실패");
            }
        } else {
            res.fromJson(responseBody);
        }

        return res;
    }
};