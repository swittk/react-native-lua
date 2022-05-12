cmake_minimum_required(VERSION 3.4.1)

set (CMAKE_VERBOSE_MAKEFILE ON)
set (CMAKE_CXX_STANDARD 14)


set (PACKAGE_NAME "SKRNNativeLua")

# find fbjni package
set(build_DIR ${CMAKE_SOURCE_DIR}/build)

file(GLOB libfbjni_link_DIRS "${build_DIR}/fbjni*.aar/jni/${ANDROID_ABI}")
file(GLOB libfbjni_include_DIRS "${build_DIR}/fbjni-*-headers.jar/")

find_library(FBJNI_LIBRARY fbjni PATHS ${libfbjni_link_DIRS}
NO_CMAKE_FIND_ROOT_PATH)


#set (NODE_MODULES_DIR "../node_modules") # NODE_MODULES_DIR is set from the build.gradle file
set (RN_SO_DIR ${NODE_MODULES_DIR}/react-native/ReactAndroid/src/main/jni/first-party/react/jni)
set (FBJNI_HEADERS_DIR ${RN_SO_DIR}/../../fbjni/headers)

message(STATUS "NODE_MODULES_DIR of React-Native-Lua CMAKE is ${NODE_MODULES_DIR}")

# REACT_NATIVE_VERSION is set from the build.gradle file
if(${REACT_NATIVE_VERSION} LESS 66)
set (
    INCLUDE_JSI_CPP
    "${NODE_MODULES_DIR}/react-native/ReactCommon/jsi/jsi/jsi.cpp"
)
#   set (
#           INCLUDE_JSIDYNAMIC_CPP
#           "${NODE_MODULES_DIR}/react-native/ReactCommon/jsi/jsi/JSIDynamic.cpp"
#   )
endif()

# set (lua_source_dir "../cpp/lua_src/")
file(GLOB lua_source_dir_dot_c "../cpp/lua_src/*.c")
file(GLOB lua_source_dir_dot_h "../cpp/lua_src/*.h")
file(GLOB lua_source_socket_dir_dot_c "../cpp/lua_luasocket/*.c")
file(GLOB lua_source_socket_dir_dot_h "../cpp/lua_luasocket/**/*.h")


add_library(${PACKAGE_NAME}
            SHARED
            ../cpp/react-native-lua.cpp
            cpp-adapter.cpp
            
            ${lua_source_dir_dot_c}
            ${lua_source_socket_dir_dot_c}
            ../cpp/lua_luasocket/luasocket_lua_amalgamation/luasocket_lua_amalgamation.c
            ${INCLUDE_JSI_CPP}
            ${INCLUDE_JSIDYNAMIC_CPP}
            "${NODE_MODULES_DIR}/react-native/ReactAndroid/src/main/java/com/facebook/react/turbomodule/core/jni/ReactCommon/CallInvokerHolder.cpp"
)

# Specifies a path to native header files.
include_directories(
            ../cpp
            ../cpp/lua_src
            ../cpp/lua_luasocket
            ../cpp/lua_luasocket/luasocket_lua_amalgamation
            "${FBJNI_HEADERS_DIR}"
            "${NODE_MODULES_DIR}/react-native/React"
            "${NODE_MODULES_DIR}/react-native/React/Base"
            "${NODE_MODULES_DIR}/react-native/ReactCommon"
            "${NODE_MODULES_DIR}/react-native/ReactCommon/jsi"
            "${NODE_MODULES_DIR}/react-native/ReactCommon/callinvoker"
            "${NODE_MODULES_DIR}/react-native/ReactCommon/runtimeexecutor"
            "${NODE_MODULES_DIR}/react-native/ReactAndroid/src/main/java/com/facebook/react/turbomodule/core/jni"
            "${NODE_MODULES_DIR}/react-native/ReactAndroid/src/main/jni/react/jni/JavaScriptExecutorHolder.h"
            "${NODE_MODULES_DIR}/react-native/ReactAndroid/src/main/jni/react/jni"
            "${NODE_MODULES_DIR}/react-native/ReactAndroid/src/main/jni/react"
            "${NODE_MODULES_DIR}/react-native/ReactCommon/turbomodule/core"
            "${NODE_MODULES_DIR}/react-native/ReactCommon/turbomodule"
            "${NODE_MODULES_DIR}/react-native/React"
            "${NODE_MODULES_DIR}/react-native/React/Base"
            "${NODE_MODULES_DIR}/react-native/ReactCommon/jsi"
)

# target_link_libraries(${PACKAGE_NAME} fbjni::fbjni)
target_include_directories(${PACKAGE_NAME} PRIVATE
  // Additional header directories here
  ${libfbjni_include_DIRS}
)
target_link_libraries(${PACKAGE_NAME} ${FBJNI_LIBRARY} android log)