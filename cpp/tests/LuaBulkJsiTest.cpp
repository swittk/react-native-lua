#include "../react-native-lua.h"
#include <hermes/hermes.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

int main(int argc, char** argv) {
  namespace jsi = facebook::jsi;
  if (argc != 2) return 2;
  std::ifstream input(argv[1]);
  if (!input) return 2;
  std::ostringstream source;
  source << input.rdbuf();
  auto runtime = facebook::hermes::makeHermesRuntime();
  SKRNNativeLua::install(*runtime, nullptr);
  runtime->global().setProperty(*runtime, "__log", jsi::Function::createFromHostFunction(
    *runtime, jsi::PropNameID::forAscii(*runtime, "__log"), 1,
    [](jsi::Runtime& rt, const jsi::Value&, const jsi::Value* args, std::size_t count) {
      if (count) std::cout << args[0].asString(rt).utf8(rt) << std::endl;
      return jsi::Value::undefined();
    }));
  runtime->global().setProperty(*runtime, "__sleep", jsi::Function::createFromHostFunction(
    *runtime, jsi::PropNameID::forAscii(*runtime, "__sleep"), 1,
    [](jsi::Runtime&, const jsi::Value&, const jsi::Value* args, std::size_t count) {
      if(count) std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(args[0].asNumber())));
      return jsi::Value::undefined();
    }));
  runtime->global().setProperty(*runtime, "__now", jsi::Function::createFromHostFunction(
    *runtime, jsi::PropNameID::forAscii(*runtime, "__now"), 0,
    [](jsi::Runtime&, const jsi::Value&, const jsi::Value*, std::size_t) {
      return jsi::Value(std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    }));
  try {
    runtime->evaluateJavaScript(std::make_shared<jsi::StringBuffer>(source.str()), "LuaBulkReadTest.js");
  } catch (const std::exception& error) {
    std::cerr << error.what() << std::endl;
    return 1;
  }
  SKRNNativeLua::cleanup(*runtime);
  std::cout << "Native Hermes/JSI bulk reader suite passed" << std::endl;
}
