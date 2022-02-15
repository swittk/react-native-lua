cmake_minimum_required(VERSION 3.4.1)

set (CMAKE_VERBOSE_MAKEFILE ON)
set (CMAKE_CXX_STANDARD 14)
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

add_library(SKRNNativeLua
            SHARED
            ../cpp/react-native-lua.cpp
            cpp-adapter.cpp
            ${lua_source_dir_dot_c}
            ${INCLUDE_JSI_CPP}
            ${INCLUDE_JSIDYNAMIC_CPP}
)

# Specifies a path to native header files.
include_directories(
            ../cpp
            ${lua_source_dir_dot_h}
            "${FBJNI_HEADERS_DIR}"
            "${NODE_MODULES_DIR}/react-native/React"
            "${NODE_MODULES_DIR}/react-native/React/Base"
            "${NODE_MODULES_DIR}/react-native/ReactCommon"
            "${NODE_MODULES_DIR}/react-native/ReactCommon/jsi"
            "${NODE_MODULES_DIR}/react-native/ReactCommon/callinvoker"
            "${NODE_MODULES_DIR}/react-native/ReactAndroid/src/main/java/com/facebook/react/turbomodule/core/jni/ReactCommon"
            # ../node_modules/react-native/ReactAndroid/src/main/jni/react/jni/JavaScriptExecutorHolder.h
            # ../node_modules/react-native/ReactAndroid/src/main/jni/react/jni
            "${NODE_MODULES_DIR}/react-native/ReactAndroid/src/main/jni/react"
            "${NODE_MODULES_DIR}/react-native/ReactCommon/turbomodule/core"
            "${NODE_MODULES_DIR}/react-native/ReactCommon/turbomodule"
            "${NODE_MODULES_DIR}/react-native/React"
            "${NODE_MODULES_DIR}/react-native/React/Base"
            "${NODE_MODULES_DIR}/react-native/ReactCommon/jsi"
)
