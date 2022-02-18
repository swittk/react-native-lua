cmake_minimum_required(VERSION 3.4.1)

set (CMAKE_VERBOSE_MAKEFILE ON)
set (CMAKE_CXX_STANDARD 14)
set (CMAKE_CXX_FLAGS "-DFOLLY_NO_CONFIG=1 -DFOLLY_HAVE_CLOCK_GETTIME=1 -DFOLLY_HAVE_MEMRCHR=1 -DFOLLY_USE_LIBCPP=1 -DFOLLY_MOBILE=1 -DON_ANDROID -DONANDROID -DFOR_HERMES=${FOR_HERMES} -fexceptions -fno-omit-frame-pointer -frtti -Wno-sign-compare")

set (PACKAGE_NAME "SKRNNativeLua")

# find fbjni package
set(build_DIR ${CMAKE_SOURCE_DIR}/build)
set(BUILD_DIR ${CMAKE_SOURCE_DIR}/build)

file(GLOB libfbjni_link_DIRS "${build_DIR}/fbjni*.aar/jni/${ANDROID_ABI}")
file(GLOB libfbjni_include_DIRS "${build_DIR}/fbjni-*-headers.jar/")

find_library(FBJNI_LIBRARY fbjni PATHS ${libfbjni_link_DIRS}
NO_CMAKE_FIND_ROOT_PATH)


#set (NODE_MODULES_DIR "../node_modules") # NODE_MODULES_DIR is set from the build.gradle file
# this whole section inspiration from reanimated.
set (RN_SO_DIR ${NODE_MODULES_DIR}/react-native/ReactAndroid/src/main/jni/first-party/react/jni)
set (FBJNI_HEADERS_DIR ${RN_SO_DIR}/../../fbjni/headers)

file (GLOB LIBRN_DIR "${RN_SO_DIR}/${ANDROID_ABI}")
file (GLOB HERMES_DIR "${BUILD_DIR}/third-party-ndk/hermes/jni/${ANDROID_ABI}")

message(WARNING "NODE_MODULES_DIR of React-Native-Lua CMAKE is ${NODE_MODULES_DIR}")
message(WARNING "FOLLY DIR of React-Native-Lua CMAKE is `${BUILD_DIR}/third-party-ndk/folly`")

# couldn't find
# find_library(
#         JSEXECUTOR_LIB
#         jscexecutor
#         PATHS ${LIBRN_DIR}
#         NO_CMAKE_FIND_ROOT_PATH
# )
# find_library(
#         FOLLY_JSON_LIB
#         folly_json
#         PATHS ${LIBRN_DIR}
#         NO_CMAKE_FIND_ROOT_PATH
# )
find_library(
        REACT_NATIVE_JNI_LIB
        reactnativejni
        PATHS ${LIBRN_DIR}
        NO_CMAKE_FIND_ROOT_PATH
)
# couldn't find

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
    set (JSI_LIB "")
else()
    find_library(
        JSI_LIB
        jsi
        PATHS ${LIBRN_DIR}
        NO_CMAKE_FIND_ROOT_PATH
    )
endif()

# set (lua_source_dir "../cpp/lua_src/")
file(GLOB lua_source_dir_dot_c "../cpp/lua_src/*.c")
file(GLOB lua_source_dir_dot_h "../cpp/lua_src/*.h")

add_library(${PACKAGE_NAME}
            SHARED
            ../cpp/react-native-lua.cpp
            cpp-adapter.cpp
            ${lua_source_dir_dot_c}
            ${INCLUDE_JSI_CPP}
            ${INCLUDE_JSIDYNAMIC_CPP}
            "${NODE_MODULES_DIR}/react-native/ReactAndroid/src/main/java/com/facebook/react/turbomodule/core/jni/ReactCommon/CallInvokerHolder.cpp"
)
set_target_properties(${PACKAGE_NAME} PROPERTIES LINKER_LANGUAGE CXX)

# Specifies a path to native header files.
include_directories(
            ../cpp
            ${lua_source_dir_dot_h}
            "${FBJNI_HEADERS_DIR}"
            # "${NODE_MODULES_DIR}/react-native/React"
            # "${NODE_MODULES_DIR}/react-native/React/Base"
            # "${NODE_MODULES_DIR}/react-native/ReactCommon"
            # "${NODE_MODULES_DIR}/react-native/ReactCommon/jsi"
            # "${NODE_MODULES_DIR}/react-native/ReactCommon/callinvoker"
            # "${NODE_MODULES_DIR}/react-native/ReactAndroid/src/main/java/com/facebook/react/turbomodule/core/jni"
            # "${NODE_MODULES_DIR}/react-native/ReactAndroid/src/main/jni/react/jni/JavaScriptExecutorHolder.h"
            # "${NODE_MODULES_DIR}/react-native/ReactAndroid/src/main/jni/react/jni"
            # "${NODE_MODULES_DIR}/react-native/ReactAndroid/src/main/jni/react"
            # "${NODE_MODULES_DIR}/react-native/ReactCommon/turbomodule/core"
            # "${NODE_MODULES_DIR}/react-native/ReactCommon/turbomodule"
            # "${NODE_MODULES_DIR}/react-native/React"
            # "${NODE_MODULES_DIR}/react-native/React/Base"
            # "${NODE_MODULES_DIR}/react-native/ReactCommon/jsi"
            "${NODE_MODULES_DIR}/react-native/React"
            "${NODE_MODULES_DIR}/react-native/React/Base"
            "${NODE_MODULES_DIR}/react-native/ReactAndroid/src/main/jni"
            "${NODE_MODULES_DIR}/react-native/ReactAndroid/src/main/java/com/facebook/react/turbomodule/core/jni"
            "${NODE_MODULES_DIR}/react-native/ReactCommon"
            "${NODE_MODULES_DIR}/react-native/ReactCommon/callinvoker"
            "${NODE_MODULES_DIR}/react-native/ReactCommon/jsi"
            "${NODE_MODULES_DIR}/react-native/ReactCommon/turbomodule/core"
            "${NODE_MODULES_DIR}/react-native/ReactCommon/turbomodule"
            "${NODE_MODULES_DIR}/hermes-engine/android/include/"    
)


# target_link_libraries(${PACKAGE_NAME} fbjni::fbjni)
target_include_directories(
    ${PACKAGE_NAME} 
    PRIVATE
  // Additional header directories here
  ${libfbjni_include_DIRS}
  "${BUILD_DIR}/third-party-ndk/boost/boost_${BOOST_VERSION}"
  "${BUILD_DIR}/third-party-ndk/double-conversion"
  "${BUILD_DIR}/third-party-ndk/folly"
  "${BUILD_DIR}/third-party-ndk/glog/exported"
"${NODE_MODULES_DIR}/react-native/React"
  "${NODE_MODULES_DIR}/react-native/React/Base"
  "${NODE_MODULES_DIR}/react-native/ReactAndroid/src/main/jni"
  "${NODE_MODULES_DIR}/react-native/ReactAndroid/src/main/java/com/facebook/react/turbomodule/core/jni"
  "${NODE_MODULES_DIR}/react-native/ReactCommon"
  "${NODE_MODULES_DIR}/react-native/ReactCommon/callinvoker"
  "${NODE_MODULES_DIR}/react-native/ReactCommon/jsi"
  "${NODE_MODULES_DIR}/react-native/ReactCommon/turbomodule/core"
  "${NODE_MODULES_DIR}/react-native/ReactCommon/turbomodule"
  "${NODE_MODULES_DIR}/hermes-engine/android/include/"
)
target_link_libraries(
    ${PACKAGE_NAME} ${FBJNI_LIBRARY} 
    android log
    ${JSI_LIB}
    # ${FOLLY_JSON_LIB}
    # ${JSEXECUTOR_LIB}
    # ${REACT_NATIVE_JNI_LIB}
)