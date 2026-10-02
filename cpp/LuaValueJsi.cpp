#include "LuaValueJsi.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace rnlua {
namespace {
namespace jsi = facebook::jsi;

std::size_t optionLimit(jsi::Runtime& rt, const jsi::Object& object,
                       const char* name, std::size_t fallback, std::size_t maximum) {
  const auto value = object.getProperty(rt, name);
  if (value.isUndefined()) return fallback;
  if (!value.isNumber() || !std::isfinite(value.getNumber()) ||
      value.getNumber() < 0 || std::floor(value.getNumber()) != value.getNumber() ||
      value.getNumber() > static_cast<double>(maximum)) {
    throw jsi::JSError(rt, std::string("Invalid Lua bulk read option: ") + name);
  }
  return static_cast<std::size_t>(value.getNumber());
}

void addIndex(jsi::Runtime& rt, const jsi::Value& value, ValueReadRequest& request) {
  if (!value.isNumber() || !std::isfinite(value.getNumber()) ||
      std::floor(value.getNumber()) != value.getNumber() || value.getNumber() == 0 ||
      value.getNumber() < std::numeric_limits<int>::min() ||
      value.getNumber() > std::numeric_limits<int>::max()) {
    throw jsi::JSError(rt, "Lua stack indices must be nonzero integers");
  }
  request.indices.push_back(static_cast<int>(value.getNumber()));
}

void addName(jsi::Runtime& rt, const jsi::Value& value, ValueReadRequest& request,
             std::size_t& nameBytes) {
  if (!value.isString()) throw jsi::JSError(rt, "Lua global names must be strings");
  const auto original = value.getString(rt);
  auto name = original.utf8(rt);
  if (!isValidUtf8(name.data(), name.size()))
    throw jsi::JSError(rt, "Lua global names must be valid UTF-8 text");
  const auto roundTrip = jsi::String::createFromUtf8(rt,
      reinterpret_cast<const uint8_t*>(name.data()), name.size());
  if (!jsi::String::strictEquals(rt, original, roundTrip))
    throw jsi::JSError(rt, "Lua global names must round-trip through UTF-8 without loss");
  if (name.size() > request.options.maxStringBytes - nameBytes) {
    throw jsi::JSError(rt, "Lua global names exceed maxStringBytes");
  }
  nameBytes += name.size();
  request.names.push_back(std::move(name));
}

jsi::Value toJsi(jsi::Runtime& rt, const LuaValue& value,
                 const jsi::Function& objectCreate) {
  switch (value.kind) {
    case LuaValue::Kind::Null: return jsi::Value::null();
    case LuaValue::Kind::Boolean: return jsi::Value(value.boolean);
    case LuaValue::Kind::Number: return jsi::Value(value.number);
    case LuaValue::Kind::String:
      return jsi::String::createFromUtf8(rt,
          reinterpret_cast<const uint8_t*>(value.text.data()), value.text.size());
    case LuaValue::Kind::Array: {
      jsi::Array array(rt, value.children.size());
      for (std::size_t i = 0; i < value.children.size(); ++i)
        array.setValueAtIndex(rt, i, toJsi(rt, value.children[i], objectCreate));
      return array;
    }
    case LuaValue::Kind::Object: {
      auto object = objectCreate.call(rt, jsi::Value::null()).asObject(rt);
      for (std::size_t i = 0; i < value.keys.size(); ++i) {
        const auto& key = value.keys[i];
        const auto property = jsi::PropNameID::forUtf8(rt,
            reinterpret_cast<const uint8_t*>(key.data()), key.size());
        object.setProperty(rt, property, toJsi(rt, value.children[i], objectCreate));
      }
      return object;
    }
  }
  throw jsi::JSError(rt, "Invalid internal Lua snapshot kind");
}
} // namespace

ValueReadRequest parseValueReadRequest(
    facebook::jsi::Runtime& rt, const std::string& method,
    const facebook::jsi::Value* arguments, std::size_t count) {
  namespace jsi = facebook::jsi;
  if (count < 1 || count > 2) throw jsi::JSError(rt, "Expected a read target and optional limits");
  ValueReadRequest request;
  request.globals = method == "readGlobals" || method == "readGlobal";
  request.singular = method == "readGlobal" || method == "readValue";
  if (count == 2 && !arguments[1].isUndefined()) {
    if (!arguments[1].isObject() || arguments[1].asObject(rt).isArray(rt))
      throw jsi::JSError(rt, "Lua read options must be an object");
    const auto object = arguments[1].asObject(rt);
    request.options.maxDepth = optionLimit(rt, object, "maxDepth", request.options.maxDepth, kMaxReadDepth);
    request.options.maxEntries = optionLimit(rt, object, "maxEntries", request.options.maxEntries, kMaxReadEntries);
    request.options.maxStringBytes = optionLimit(rt, object, "maxStringBytes", request.options.maxStringBytes, kMaxReadStringBytes);
    const auto empty = object.getProperty(rt, "emptyTables");
    if (!empty.isUndefined()) {
      if (!empty.isString()) throw jsi::JSError(rt, "emptyTables must be object or array");
      const auto mode = empty.asString(rt).utf8(rt);
      if (mode != "object" && mode != "array")
        throw jsi::JSError(rt, "emptyTables must be object or array");
      request.options.emptyTablesAsArrays = mode == "array";
    }
  }
  std::size_t nameBytes = 0;
  if (request.singular) {
    if (request.globals) addName(rt, arguments[0], request, nameBytes);
    else addIndex(rt, arguments[0], request);
  } else {
    if (!arguments[0].isObject() || !arguments[0].asObject(rt).isArray(rt))
      throw jsi::JSError(rt, "Bulk read targets must be an array");
    const auto targets = arguments[0].asObject(rt).asArray(rt);
    const auto length = targets.size(rt);
    if (length > kMaxReadRoots) throw jsi::JSError(rt, "At most 256 read targets are supported");
    for (std::size_t i = 0; i < length; ++i) {
      const auto target = targets.getValueAtIndex(rt, i);
      if (request.globals) addName(rt, target, request, nameBytes);
      else addIndex(rt, target, request);
    }
  }
  return request;
}

facebook::jsi::Value valueReadResult(
    facebook::jsi::Runtime& rt, const ValueReadRequest& request,
    const std::vector<LuaValue>& values, const facebook::jsi::Function& objectCreate) {
  namespace jsi = facebook::jsi;
  if (request.singular) return toJsi(rt, values.at(0), objectCreate);
  if (request.globals) {
    auto object = objectCreate.call(rt, jsi::Value::null()).asObject(rt);
    for (std::size_t i = 0; i < request.names.size(); ++i) {
      const auto& key = request.names[i];
      const auto property = jsi::PropNameID::forUtf8(rt,
          reinterpret_cast<const uint8_t*>(key.data()), key.size());
      object.setProperty(rt, property, toJsi(rt, values[i], objectCreate));
    }
    return object;
  }
  jsi::Array array(rt, values.size());
  for (std::size_t i = 0; i < values.size(); ++i)
    array.setValueAtIndex(rt, i, toJsi(rt, values[i], objectCreate));
  return array;
}

} // namespace rnlua
