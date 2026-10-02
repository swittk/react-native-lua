#include "LuaValueInputJsi.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace rnlua {
namespace {
namespace jsi = facebook::jsi;

[[noreturn]] void invalid(jsi::Runtime& rt, const std::string& message) {
  throw jsi::JSError(rt, "Lua value input: " + message);
}

std::size_t optionLimit(
    jsi::Runtime& rt,
    const jsi::Object& object,
    const char* name,
    std::size_t fallback,
    std::size_t maximum) {
  const auto value = object.getProperty(rt, name);
  if (value.isUndefined()) return fallback;
  if (!value.isNumber() || !std::isfinite(value.getNumber()) ||
      value.getNumber() < 0 || std::floor(value.getNumber()) != value.getNumber() ||
      value.getNumber() > static_cast<double>(maximum)) {
    invalid(rt, std::string("invalid option ") + name);
  }
  return static_cast<std::size_t>(value.getNumber());
}

void parseLimits(
    jsi::Runtime& rt,
    const jsi::Value& value,
    ValueLimits& limits) {
  if (value.isUndefined()) return;
  if (!value.isObject() || value.asObject(rt).isArray(rt)) {
    invalid(rt, "limits must be an object");
  }
  const auto object = value.asObject(rt);
  limits.maxDepth = optionLimit(
      rt, object, "maxDepth", limits.maxDepth, kMaxValueDepth);
  limits.maxEntries = optionLimit(
      rt, object, "maxEntries", limits.maxEntries, kMaxValueEntries);
  limits.maxStringBytes = optionLimit(
      rt, object, "maxStringBytes", limits.maxStringBytes, kMaxValueStringBytes);
  if (!object.getProperty(rt, "emptyTables").isUndefined()) {
    invalid(rt, "emptyTables is a read-only option");
  }
}

struct Parser {
  jsi::Runtime& rt;
  ValueLimits limits;
  jsi::Function objectKeys;
  jsi::Function objectSymbols;
  jsi::Function objectGetPrototypeOf;
  jsi::Object objectPrototype;
  std::vector<jsi::Value> ancestors;
  std::size_t entries = 0;
  std::size_t stringBytes = 0;

  Parser(jsi::Runtime& runtime, ValueLimits bounds)
      : rt(runtime),
        limits(bounds),
        objectKeys(runtime.global()
                       .getPropertyAsObject(runtime, "Object")
                       .getPropertyAsFunction(runtime, "keys")),
        objectSymbols(runtime.global()
                          .getPropertyAsObject(runtime, "Object")
                          .getPropertyAsFunction(runtime, "getOwnPropertySymbols")),
        objectGetPrototypeOf(runtime.global()
                                 .getPropertyAsObject(runtime, "Object")
                                 .getPropertyAsFunction(runtime, "getPrototypeOf")),
        objectPrototype(runtime.global()
                            .getPropertyAsObject(runtime, "Object")
                            .getPropertyAsObject(runtime, "prototype")) {
    ancestors.reserve(limits.maxDepth);
  }

  void requirePlainObject(const jsi::Object& object) {
    const auto prototype = objectGetPrototypeOf.call(rt, jsi::Value(rt, object));
    if (prototype.isNull()) return;
    if (!prototype.isObject() ||
        !jsi::Object::strictEquals(rt, prototype.asObject(rt), objectPrototype)) {
      invalid(rt, "only plain objects, null-prototype objects, and arrays are transferable");
    }
  }

  void rejectSymbolKeys(const jsi::Object& object) {
    auto result = objectSymbols.call(rt, jsi::Value(rt, object));
    if (!result.isObject() || !result.asObject(rt).isArray(rt)) {
      invalid(rt, "Object.getOwnPropertySymbols returned an invalid result");
    }
    if (result.asObject(rt).asArray(rt).size(rt) != 0) {
      invalid(rt, "symbol-keyed object properties are not transferable");
    }
  }

  std::string text(const jsi::String& original, const char* what) {
    auto value = original.utf8(rt);
    if (!isValidUtf8(value.data(), value.size())) {
      invalid(rt, std::string(what) + " must be valid UTF-8 text");
    }
    const auto roundTrip = jsi::String::createFromUtf8(
        rt,
        reinterpret_cast<const uint8_t*>(value.data()),
        value.size());
    if (!jsi::String::strictEquals(rt, original, roundTrip)) {
      invalid(rt, std::string(what) + " must round-trip through UTF-8 without loss");
    }
    if (value.size() > limits.maxStringBytes - stringBytes) {
      invalid(rt, "maxStringBytes exceeded");
    }
    stringBytes += value.size();
    return value;
  }

  std::string text(const jsi::Value& value, const char* what) {
    if (!value.isString()) invalid(rt, std::string(what) + " must be a string");
    return text(value.getString(rt), what);
  }

  bool isAncestor(const jsi::Object& object) {
    for (const auto& ancestorValue : ancestors) {
      const auto ancestor = ancestorValue.asObject(rt);
      if (jsi::Object::strictEquals(rt, ancestor, object)) return true;
    }
    return false;
  }

  void enterObject(const jsi::Object& object, std::size_t depth) {
    if (depth >= limits.maxDepth) invalid(rt, "maxDepth exceeded");
    if (isAncestor(object)) invalid(rt, "cyclic JavaScript object");
    ancestors.emplace_back(rt, object);
  }

  void leaveObject() {
    ancestors.pop_back();
  }

  void countEntry() {
    if (entries >= limits.maxEntries) invalid(rt, "maxEntries exceeded");
    ++entries;
  }

  void parse(const jsi::Value& input, LuaValue& output, std::size_t depth) {
    if (input.isNull()) return;
    if (input.isBool()) {
      output.kind = LuaValue::Kind::Boolean;
      output.boolean = input.getBool();
      return;
    }
    if (input.isNumber()) {
      const double number = input.getNumber();
      if (!std::isfinite(number)) invalid(rt, "numbers must be finite");
      output.kind = LuaValue::Kind::Number;
      output.number = number;
      return;
    }
    if (input.isString()) {
      output.kind = LuaValue::Kind::String;
      output.text = text(input.getString(rt), "strings");
      return;
    }
    if (!input.isObject()) {
      invalid(rt, "undefined, symbols and other non-data values are not transferable");
    }

    auto object = input.asObject(rt);
    if (object.isFunction(rt)) {
      invalid(rt, "functions are not transferable values");
    }
    enterObject(object, depth);
    try {
      if (object.isArray(rt)) {
        rejectSymbolKeys(object);
        auto array = object.asArray(rt);
        const auto length = array.size(rt);
        auto keyValue = objectKeys.call(rt, jsi::Value(rt, object));
        if (!keyValue.isObject() || !keyValue.asObject(rt).isArray(rt)) {
          invalid(rt, "Object.keys returned an invalid result");
        }
        auto keys = keyValue.asObject(rt).asArray(rt);
        if (keys.size(rt) < length) {
          invalid(rt, "sparse arrays are not transferable");
        }
        if (keys.size(rt) > length) {
          invalid(rt, "arrays with extra enumerable properties are not transferable");
        }
        for (std::size_t i = 0; i < length; ++i) {
          const auto keyValueAtIndex = keys.getValueAtIndex(rt, i);
          if (!keyValueAtIndex.isString() ||
              keyValueAtIndex.getString(rt).utf8(rt) != std::to_string(i)) {
            invalid(rt, "arrays must contain only dense indexed elements");
          }
        }
        if (length > limits.maxEntries - entries) invalid(rt, "maxEntries exceeded");
        output.kind = LuaValue::Kind::Array;
        output.children.resize(length);
        for (std::size_t i = 0; i < length; ++i) {
          countEntry();
          const auto child = array.getValueAtIndex(rt, i);
          if (child.isUndefined()) invalid(rt, "sparse arrays and undefined entries are not transferable");
          parse(child, output.children[i], depth + 1);
        }
      } else {
        requirePlainObject(object);
        rejectSymbolKeys(object);
        auto keyValue = objectKeys.call(rt, jsi::Value(rt, object));
        if (!keyValue.isObject() || !keyValue.asObject(rt).isArray(rt)) {
          invalid(rt, "Object.keys returned an invalid result");
        }
        auto keys = keyValue.asObject(rt).asArray(rt);
        const auto count = keys.size(rt);
        if (count > limits.maxEntries - entries) invalid(rt, "maxEntries exceeded");
        output.kind = LuaValue::Kind::Object;
        output.keys.reserve(count);
        output.children.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
          countEntry();
          const auto keyValueAtIndex = keys.getValueAtIndex(rt, i);
          if (!keyValueAtIndex.isString()) invalid(rt, "object keys must be strings");
          const auto keyString = keyValueAtIndex.getString(rt);
          auto key = text(keyString, "object keys");
          const auto property = jsi::PropNameID::forUtf8(
              rt,
              reinterpret_cast<const uint8_t*>(key.data()),
              key.size());
          const auto child = object.getProperty(rt, property);
          if (child.isUndefined()) {
            invalid(rt, "undefined object properties are not transferable");
          }
          output.keys.push_back(std::move(key));
          parse(child, output.children[i], depth + 1);
        }
      }
    } catch (...) {
      leaveObject();
      throw;
    }
    leaveObject();
  }
};

void parseGlobalName(
    jsi::Runtime& rt,
    Parser& parser,
    const jsi::Value& value,
    std::vector<std::string>& names) {
  names.push_back(parser.text(value, "global names"));
}

} // namespace

ValueInputRequest parseValueInputRequest(
    facebook::jsi::Runtime& rt,
    const std::string& method,
    const facebook::jsi::Value* arguments,
    std::size_t count) {
  const bool setGlobal = method == "setGlobal";
  const bool setGlobals = method == "setGlobals";
  const bool pushValue = method == "pushValue";
  const bool pushValues = method == "pushValues";
  if (!setGlobal && !setGlobals && !pushValue && !pushValues) {
    invalid(rt, "unknown bulk input method");
  }

  ValueInputRequest request;
  request.globals = setGlobal || setGlobals;
  request.singular = setGlobal || pushValue;

  std::size_t optionsIndex = 0;
  if (setGlobal) {
    if (count < 2 || count > 3) {
      invalid(rt, "setGlobal expects name, value, and optional limits");
    }
    optionsIndex = 2;
  } else {
    if (count < 1 || count > 2) {
      invalid(rt, method + " expects a value and optional limits");
    }
    optionsIndex = 1;
  }
  if (count > optionsIndex) parseLimits(rt, arguments[optionsIndex], request.limits);
  validateValueLimits(request.limits);

  Parser parser(rt, request.limits);
  if (setGlobal) {
    parseGlobalName(rt, parser, arguments[0], request.names);
    request.values.resize(1);
    parser.parse(arguments[1], request.values[0], 0);
    return request;
  }

  if (setGlobals) {
    if (!arguments[0].isObject()) invalid(rt, "setGlobals expects an object map");
    auto object = arguments[0].asObject(rt);
    if (object.isArray(rt) || object.isFunction(rt)) {
      invalid(rt, "setGlobals expects an object map");
    }
    parser.requirePlainObject(object);
    parser.rejectSymbolKeys(object);
    auto keyResult = parser.objectKeys.call(rt, jsi::Value(rt, object));
    if (!keyResult.isObject() || !keyResult.asObject(rt).isArray(rt)) {
      invalid(rt, "Object.keys returned an invalid result");
    }
    auto keys = keyResult.asObject(rt).asArray(rt);
    const auto length = keys.size(rt);
    if (length > kMaxValueRoots) invalid(rt, "at most 256 globals are supported");
    request.names.reserve(length);
    request.values.resize(length);
    for (std::size_t i = 0; i < length; ++i) {
      const auto keyValue = keys.getValueAtIndex(rt, i);
      parseGlobalName(rt, parser, keyValue, request.names);
      const auto& key = request.names.back();
      const auto property = jsi::PropNameID::forUtf8(
          rt,
          reinterpret_cast<const uint8_t*>(key.data()),
          key.size());
      const auto value = object.getProperty(rt, property);
      if (value.isUndefined()) invalid(rt, "undefined globals are not transferable");
      parser.parse(value, request.values[i], 0);
    }
    return request;
  }

  if (pushValue) {
    request.values.resize(1);
    parser.parse(arguments[0], request.values[0], 0);
    return request;
  }

  if (!arguments[0].isObject() || !arguments[0].asObject(rt).isArray(rt)) {
    invalid(rt, "pushValues expects an array of root values");
  }
  auto roots = arguments[0].asObject(rt).asArray(rt);
  const auto length = roots.size(rt);
  if (length > kMaxValueRoots) invalid(rt, "at most 256 pushed values are supported");
  request.values.resize(length);
  for (std::size_t i = 0; i < length; ++i) {
    const auto value = roots.getValueAtIndex(rt, i);
    if (value.isUndefined()) invalid(rt, "sparse root arrays are not transferable");
    parser.parse(value, request.values[i], 0);
  }
  return request;
}

} // namespace rnlua
