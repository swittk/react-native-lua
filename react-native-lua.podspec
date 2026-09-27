require "json"

package = JSON.parse(File.read(File.join(__dir__, "package.json")))
cxx_standard = ENV["RCT_NEW_ARCH_ENABLED"] == "1" ? "c++20" : "c++17"

Pod::Spec.new do |s|
  s.name         = "react-native-lua"
  s.version      = package["version"]
  s.summary      = package["description"]
  s.homepage     = package["homepage"]
  s.license      = package["license"]
  s.authors      = package["author"]

  # React Native 0.73 is the oldest supported host and requires iOS 13.4.
  s.platforms    = { :ios => "13.4" }
  s.source       = {
    :git => "https://github.com/swittk/react-native-lua.git",
    :tag => "#{s.version}"
  }

  s.source_files = "ios/**/*.{h,m,mm}", "cpp/**/*.{h,cpp,c}"
  s.exclude_files = "cpp/lua_src/lua.c", "cpp/lua_src/luac.c",
                    "cpp/lua_luasocket/serial.c", "cpp/tests/**/*"
  s.requires_arc = true
  s.pod_target_xcconfig = {
    "CLANG_CXX_LANGUAGE_STANDARD" => cxx_standard,
    "GCC_C_LANGUAGE_STANDARD" => "c11",
    "GCC_PREPROCESSOR_DEFINITIONS" => "$(inherited) LUA_USE_IOS=1"
  }

  # RN 0.73+ wires legacy dependencies and, when enabled, React-Codegen.
  install_modules_dependencies(s)
end
