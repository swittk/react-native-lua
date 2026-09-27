cmake_minimum_required(VERSION 3.4.1)
project(SKRNNativeLua)

set(CMAKE_C_STANDARD 11)
if(REACT_NATIVE_MINOR_VERSION GREATER_EQUAL 80)
  set(CMAKE_CXX_STANDARD 20)
else()
  set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_VERBOSE_MAKEFILE ON)

file(GLOB LUA_SOURCES "../cpp/lua_src/*.c")
file(GLOB LUASOCKET_SOURCES
    "../cpp/lua_luasocket/*.c"
    "../cpp/lua_luasocket/luasocket_lua_amalgamation/*.c")
list(REMOVE_ITEM LUASOCKET_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../cpp/lua_luasocket/serial.c")
list(REMOVE_ITEM LUA_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../cpp/lua_src/lua.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../cpp/lua_src/luac.c")

set(RNLUA_SOURCES
    ../cpp/LuaRuntime.cpp
    ../cpp/LuaSocket.cpp
    ../cpp/react-native-lua.cpp
    cpp-adapter.cpp
    ${LUA_SOURCES}
    ${LUASOCKET_SOURCES})

if(RNLUA_NEW_ARCH_ENABLED)
  list(APPEND RNLUA_SOURCES LuaBindingsInstaller.cpp)
endif()

add_library(SKRNNativeLua SHARED ${RNLUA_SOURCES})

target_compile_options(SKRNNativeLua PRIVATE
    -O2 -fexceptions -frtti -Wall -fstack-protector-all)
target_compile_definitions(SKRNNativeLua PRIVATE LUA_USE_ANDROID=1)
target_include_directories(SKRNNativeLua PRIVATE
    ../cpp
    ../cpp/lua_src
    ../cpp/lua_luasocket
    ../cpp/lua_luasocket/luasocket_lua_amalgamation
    "${NODE_MODULES_DIR}/react-native/ReactCommon"
    "${NODE_MODULES_DIR}/react-native/ReactCommon/jsi"
    "${NODE_MODULES_DIR}/react-native/ReactCommon/callinvoker"
    "${NODE_MODULES_DIR}/react-native/ReactCommon/react/nativemodule/core"
    "${NODE_MODULES_DIR}/react-native/ReactAndroid/src/main/jni"
    "${NODE_MODULES_DIR}/react-native/ReactAndroid/src/main/java/com/facebook/react/turbomodule/core/jni"
    "${NODE_MODULES_DIR}/react-native/ReactAndroid/src/main/jni/react/turbomodule")

find_package(fbjni REQUIRED CONFIG)
find_package(ReactAndroid REQUIRED CONFIG)
if(REACT_NATIVE_MINOR_VERSION GREATER_EQUAL 76)
  target_link_libraries(SKRNNativeLua
      ReactAndroid::reactnative
      ReactAndroid::jsi
      fbjni::fbjni
      android
      log)
else()
  target_link_libraries(SKRNNativeLua
      ReactAndroid::reactnativejni
      ReactAndroid::jsi
      fbjni::fbjni
      android
      log)
endif()
