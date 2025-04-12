// ArduinoJson - https://arduinojson.org
// Copyright © 2014-2025, Benoit BLANCHON
// MIT License

#pragma once

#if __cplusplus < 201103L && (!defined(_MSC_VER) || _MSC_VER < 1910)
#  error ArduinoJson requires C++11 or newer. Configure your compiler for C++11 or downgrade ArduinoJson to 6.20.
#endif

#include "Configuration.hpp"

// Include Arduino.h before stdlib.h to avoid conflict with atexit()
// https://github.com/bblanchon/pull/1693#issuecomment-1001060240
#if ARDUINOJSON_ENABLE_ARDUINO_STRING || ARDUINOJSON_ENABLE_ARDUINO_STREAM || \
    ARDUINOJSON_ENABLE_ARDUINO_PRINT ||                                       \
    (ARDUINOJSON_ENABLE_PROGMEM && defined(ARDUINO))
#  include <Arduino.h>
#endif

#if !ARDUINOJSON_DEBUG
#  ifdef __clang__
#    pragma clang system_header
#  elif defined __GNUC__
#    pragma GCC system_header
#  endif
#endif

#include "Array/JsonArray.hpp"
#include "Object/JsonObject.hpp"
#include "Variant/JsonVariantConst.hpp"

#include "Document/JsonDocument.hpp"

#include "Array/ArrayImpl.hpp"
#include "Array/ElementProxy.hpp"
#include "Array/Utilities.hpp"
#include "Collection/CollectionImpl.hpp"
#include "Memory/ResourceManagerImpl.hpp"
#include "Object/MemberProxy.hpp"
#include "Object/ObjectImpl.hpp"
#include "Variant/ConverterImpl.hpp"
#include "Variant/JsonVariantCopier.hpp"
#include "Variant/VariantCompare.hpp"
#include "Variant/VariantImpl.hpp"
#include "Variant/VariantRefBaseImpl.hpp"

#include "Json/JsonDeserializer.hpp"
#include "Json/JsonSerializer.hpp"
#include "Json/PrettyJsonSerializer.hpp"
#include "MsgPack/MsgPackBinary.hpp"
#include "MsgPack/MsgPackDeserializer.hpp"
#include "MsgPack/MsgPackExtension.hpp"
#include "MsgPack/MsgPackSerializer.hpp"

#include "compatibility.hpp"
