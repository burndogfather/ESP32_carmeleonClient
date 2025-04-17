#include "ArduinoJson/ArduinoJson.h"
#include <string>
#include <vector>
#include <mbedtls/base64.h>
#include <mbedtls/aes.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>

#define MBEDTLS_PKCS5_C

class Encryption {
private:
    String bytesToHex(const uint8_t* data, size_t length) {
        String hex;
        hex.reserve(length * 2);
        for (size_t i = 0; i < length; i++) {
            char buf[3];
            sprintf(buf, "%02x", data[i]);
            hex += buf;
        }
        return hex;
    }

    std::vector<uint8_t> hexToBytes(const String& hex) {
        std::vector<uint8_t> bytes;
        bytes.reserve(hex.length() / 2);
        for (size_t i = 0; i < hex.length(); i += 2) {
            String byteStr = hex.substring(i, i + 2);
            bytes.push_back(strtol(byteStr.c_str(), nullptr, 16));
        }
        return bytes;
    }

    String base64Encode(const uint8_t* input, size_t length) {
        if (!input || length == 0) {
            Serial.println("[CustomBase64] Invalid input");
            return "";
        }

        const char* base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        String output;
        output.reserve(((length + 2) / 3) * 4);

        for (size_t i = 0; i < length; i += 3) {
            uint32_t octet_a = i < length ? input[i] : 0;
            uint32_t octet_b = i + 1 < length ? input[i + 1] : 0;
            uint32_t octet_c = i + 2 < length ? input[i + 2] : 0;

            uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

            output += base64_chars[(triple >> 18) & 63];
            output += base64_chars[(triple >> 12) & 63];
            output += (i + 1 < length) ? base64_chars[(triple >> 6) & 63] : '=';
            output += (i + 2 < length) ? base64_chars[triple & 63] : '=';
        }

        Serial.print("[CustomBase64] Encoded: ");
        Serial.println(output);
        return output;
    }

    bool base64Decode(const String& input, std::vector<uint8_t>& output) {
        if (input.isEmpty()) {
            Serial.println("[Base64] Decode failed: empty input");
            return false;
        }

        // Base64 디코딩 예상 크기
        size_t maxOutputLen = (input.length() * 3) / 4 + 1;
        output.resize(maxOutputLen);

        size_t outlen = 0;
        int ret = mbedtls_base64_decode(output.data(), maxOutputLen, &outlen,
                                       (const uint8_t*)input.c_str(), input.length());
        if (ret != 0) {
            Serial.printf("[Base64] Decode failed, error code: %d\n", ret);
            return false;
        }
        output.resize(outlen); // 실제 디코딩된 크기로 조정
        return true;
    }

public:
    String encrypt(const String& plaintext, const String& key) {
        // 1. IV와 Salt 생성
        uint8_t iv[16], salt[256];
        esp_fill_random(iv, sizeof(iv));
        esp_fill_random(salt, sizeof(salt));

        // 2. 키 파생 (PBKDF2, SHA512, 999 iterations)
        uint8_t derivedKey[32];
        int ret = mbedtls_pkcs5_pbkdf2_hmac_ext(
            MBEDTLS_MD_SHA512,
            (const uint8_t*)key.c_str(), key.length(),
            salt, sizeof(salt),
            999,
            sizeof(derivedKey),
            derivedKey
        );
        if (ret != 0) {
            Serial.println("[Encrypt] Key derivation failed");
            return "";
        }

        // 3. 패딩 및 암호화 준비
        size_t blockSize = 16;
        size_t paddedLen = ((plaintext.length() / blockSize) + 1) * blockSize;
        std::vector<uint8_t> padded(paddedLen);
        memcpy(padded.data(), plaintext.c_str(), plaintext.length());
        uint8_t padVal = paddedLen - plaintext.length();
        memset(padded.data() + plaintext.length(), padVal, padVal);

        // 4. AES-256-CBC 암호화
        std::vector<uint8_t> encrypted(paddedLen);
        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);
        ret = mbedtls_aes_setkey_enc(&aes, derivedKey, 256);
        if (ret != 0) {
            Serial.println("[Encrypt] AES key setup failed");
            mbedtls_aes_free(&aes);
            return "";
        }
        ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, paddedLen, iv, padded.data(), encrypted.data());
        if (ret != 0) {
            Serial.println("[Encrypt] AES encryption failed");
            mbedtls_aes_free(&aes);
            return "";
        }
        mbedtls_aes_free(&aes);

        // 5. 암호문 Base64 인코딩
        String base64Cipher = base64Encode(encrypted.data(), paddedLen);
        if (base64Cipher.isEmpty()) {
            Serial.println("[Encrypt] Failed to encode ciphertext");
            return "";
        }

        // 6. JSON 객체 생성
        DynamicJsonDocument doc(2048); // Salt가 크므로 충분한 크기 확보
        doc["ciphertext"] = base64Cipher;
        doc["iv"] = bytesToHex(iv, sizeof(iv));
        doc["salt"] = bytesToHex(salt, sizeof(salt));
        doc["iterations"] = 999;

        String jsonStr;
        serializeJson(doc, jsonStr);

        // 7. JSON 문자열 Base64 인코딩
        String finalOutput = base64Encode((const uint8_t*)jsonStr.c_str(), jsonStr.length());
        if (finalOutput.isEmpty()) {
            Serial.println("[Encrypt] Failed to encode JSON");
            return "";
        }

        return finalOutput;
    }

    String decrypt(const String& encryptedString, const String& key) {
        // 1. Base64 디코딩
        std::vector<uint8_t> decoded;
        if (!base64Decode(encryptedString, decoded)) {
            Serial.println("[Decrypt] Base64 decode failed");
            return "";
        }

        // 2. JSON 파싱
        DynamicJsonDocument doc(2048);
        if (deserializeJson(doc, decoded.data(), decoded.size())) {
            Serial.println("[Decrypt] JSON parse failed");
            return "";
        }

        String ciphertext = doc["ciphertext"].as<String>();
        String ivStr = doc["iv"].as<String>();
        String saltStr = doc["salt"].as<String>();
        int iterations = doc["iterations"] | 999;

        // 3. Hex 문자열을 바이트로 변환
        std::vector<uint8_t> iv = hexToBytes(ivStr);
        std::vector<uint8_t> salt = hexToBytes(saltStr);

        // 4. 키 파생
        uint8_t derivedKey[32];
        int ret = mbedtls_pkcs5_pbkdf2_hmac_ext(
            MBEDTLS_MD_SHA512,
            (const uint8_t*)key.c_str(), key.length(),
            salt.data(), salt.size(),
            iterations,
            sizeof(derivedKey),
            derivedKey
        );
        if (ret != 0) {
            Serial.println("[Decrypt] Key derivation failed");
            return "";
        }

        // 5. 암호문 Base64 디코딩
        std::vector<uint8_t> encryptedData;
        if (!base64Decode(ciphertext, encryptedData)) {
            Serial.println("[Decrypt] Ciphertext decode failed");
            return "";
        }

        // 6. AES-256-CBC 복호화
        std::vector<uint8_t> decrypted(encryptedData.size());
        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);
        ret = mbedtls_aes_setkey_dec(&aes, derivedKey, 256);
        if (ret != 0) {
            Serial.println("[Decrypt] AES key setup failed");
            mbedtls_aes_free(&aes);
            return "";
        }
        ret = mbedtls_aes_crypt_cbc(
            &aes, MBEDTLS_AES_DECRYPT,
            encryptedData.size(),
            iv.data(),
            encryptedData.data(),
            decrypted.data()
        );
        if (ret != 0) {
            Serial.println("[Decrypt] AES decryption failed");
            mbedtls_aes_free(&aes);
            return "";
        }
        mbedtls_aes_free(&aes);

        // 7. PKCS#7 패딩 제거
        size_t padLength = decrypted[encryptedData.size() - 1];
        if (padLength > 16 || padLength == 0) {
            Serial.println("[Decrypt] Invalid padding");
            return "";
        }
        size_t decryptedLen = encryptedData.size() - padLength;
        String result((char*)decrypted.data(), decryptedLen);

        return result;
    }
};