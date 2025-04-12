#ifndef HTTPS_OTA_WRAPPER_H
#define HTTPS_OTA_WRAPPER_H

#include <esp_https_ota.h>
#include <esp_ota_ops.h>
#include <esp_crt_bundle.h>  // ⭐️ crt_bundle_attach 를 위해 필요
#include <Update.h>
#include <functional>

class HttpsOTAWrapper {
public:
    bool begin(const char* url, const char* cert_pem = nullptr, bool skipCertCheck = true) {
        static esp_http_client_config_t http_config = {};
        memset(&http_config, 0, sizeof(http_config));

        http_config.url = url;
        http_config.timeout_ms = 10000;
        http_config.skip_cert_common_name_check = skipCertCheck;

        // ✅ 인증서가 없으면 crt_bundle_attach로 대체
        if (cert_pem) {
            http_config.cert_pem = cert_pem;
        } else {
            http_config.crt_bundle_attach = esp_crt_bundle_attach;
        }

        static esp_https_ota_config_t ota_config = {};
        memset(&ota_config, 0, sizeof(ota_config));
        ota_config.http_config = &http_config;

        if (_onConnected) _onConnected();

        esp_https_ota_handle_t ota_handle;
        esp_err_t err = esp_https_ota_begin(&ota_config, &ota_handle);
        if (err != ESP_OK) {
            if (_onFail) _onFail("esp_https_ota_begin 실패: " + String(esp_err_to_name(err)));
            return false;
        }

        while ((err = esp_https_ota_perform(ota_handle)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            if (_onWriting) _onWriting();
        }

        if (esp_https_ota_is_complete_data_received(ota_handle) != true) {
            esp_https_ota_abort(ota_handle);
            if (_onFail) _onFail("OTA 중 일부 데이터 누락");
            return false;
        }

        err = esp_https_ota_finish(ota_handle);
        if (err == ESP_OK) {
            if (_onSuccess) _onSuccess();
            return true;
        } else {
            if (_onFail) _onFail("esp_https_ota_finish 실패: " + String(esp_err_to_name(err)));
            return false;
        }
    }

    // 이벤트 콜백 등록
    void onConnected(std::function<void()> cb)             { _onConnected = cb; }
    void onWriting(std::function<void()> cb)               { _onWriting = cb; }
    void onSuccess(std::function<void()> cb){
        esp_ota_mark_app_valid_cancel_rollback();
        _onSuccess = cb; 
    }
    void onFail(std::function<void(String)> cb)            { _onFail = cb; }

private:
    std::function<void()> _onConnected = nullptr;
    std::function<void()> _onWriting   = nullptr;
    std::function<void()> _onSuccess   = nullptr;
    std::function<void(String)> _onFail = nullptr;
};

#endif
