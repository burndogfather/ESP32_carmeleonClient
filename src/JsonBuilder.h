#pragma once
#include "ArduinoJson/ArduinoJson.h"
#include <vector>

class JsonVariantWrapper {
private:
    DynamicJsonDocument _doc;
    JsonVariant _value;
    std::vector<JsonVariant> _array;

public:
    JsonVariantWrapper(const char* val) : _doc(64) { _value = _doc.to<JsonVariant>(); _value.set(val); }
    JsonVariantWrapper(const String& val) : _doc(64) { _value = _doc.to<JsonVariant>(); _value.set(val); }
    JsonVariantWrapper(const std::string& val) : _doc(64) { _value = _doc.to<JsonVariant>(); _value.set(val.c_str()); }
    JsonVariantWrapper(float val) : _doc(64) { _value = _doc.to<JsonVariant>(); _value.set(val); }
    JsonVariantWrapper(double val) : _doc(64) { _value = _doc.to<JsonVariant>(); _value.set(val); }
    JsonVariantWrapper(int val) : _doc(64) { _value = _doc.to<JsonVariant>(); _value.set(val); }
    JsonVariantWrapper(bool val) : _doc(64) { _value = _doc.to<JsonVariant>(); _value.set(val); }
    JsonVariantWrapper(std::initializer_list<const char*> list) : _doc(256) {
        JsonArray arr = _doc.to<JsonArray>();
        for (const char* item : list) arr.add(item);
        for (JsonVariant v : arr) _array.push_back(v);
    }
    JsonVariantWrapper(std::initializer_list<float> list) : _doc(256) {
        JsonArray arr = _doc.to<JsonArray>();
        for (float item : list) arr.add(item);
        for (JsonVariant v : arr) _array.push_back(v);
    }
    
    JsonVariantWrapper(std::initializer_list<double> list) : _doc(256) {
        JsonArray arr = _doc.to<JsonArray>();
        for (double item : list) arr.add(item);
        for (JsonVariant v : arr) _array.push_back(v);
    }
    
    JsonVariantWrapper(std::initializer_list<int> list) : _doc(256) {
        JsonArray arr = _doc.to<JsonArray>();
        for (int item : list) arr.add(item);
        for (JsonVariant v : arr) _array.push_back(v);
    }

    bool isArray() const { return !_array.empty(); }
    JsonVariant getValue() const { return _value; }
    const std::vector<JsonVariant>& getArray() const { return _array; }
};


class JsonBuilder {
    public:
        template <typename K, typename V>
        void _buildJson(String& out, std::initializer_list<std::pair<K, V>> pairs) {
            bool first = true;

            for (const auto& pair : pairs) {
                if (!first) out += ",";
                first = false;

                out += "\"";
                out += pair.first;
                out += "\":";

                JsonVariantWrapper wrapped(pair.second);
                if (wrapped.isArray()) {
                    out += "[";
                    bool firstItem = true;
                    for (const auto& item : wrapped.getArray()) {
                        if (!firstItem) out += ",";
                        firstItem = false;

                        String tmp;
                        serializeJson(item, tmp);
                        out += tmp;
                    }
                    out += "]";
                } else {
                    String tmp;
                    serializeJson(wrapped.getValue(), tmp);
                    out += tmp;
                }
            }
        }

};
