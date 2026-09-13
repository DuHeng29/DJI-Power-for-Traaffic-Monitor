# 本文件定义 DJIPowerPlugin 的构建、依赖和测试入口。
cmake_minimum_required(VERSION 3.24)
project(DJIPowerForTrafficMonitor VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(TRAFFICMONITOR_SDK_DIR "" CACHE PATH "包含 PluginInterface.h 的 TrafficMonitor include 目录")
if(TRAFFICMONITOR_SDK_DIR)
    set(TM_INCLUDE_DIR "${TRAFFICMONITOR_SDK_DIR}")
else()
    set(TM_INCLUDE_DIR "${CMAKE_BINARY_DIR}/_deps/trafficmonitor/include")
    file(MAKE_DIRECTORY "${TM_INCLUDE_DIR}")
    set(TM_HEADER "${TM_INCLUDE_DIR}/PluginInterface.h")
    if(NOT EXISTS "${TM_HEADER}")
        file(DOWNLOAD
            "https://raw.githubusercontent.com/zhongyang219/TrafficMonitor/master/include/PluginInterface.h"
            "${TM_HEADER}" TLS_VERIFY ON STATUS TM_DOWNLOAD_STATUS)
        list(GET TM_DOWNLOAD_STATUS 0 TM_DOWNLOAD_CODE)
        if(NOT TM_DOWNLOAD_CODE EQUAL 0)
            message(FATAL_ERROR "无法下载 TrafficMonitor SDK；请设置 TRAFFICMONITOR_SDK_DIR")
        endif()
    endif()
endif()

add_library(dji_power_core STATIC
    src/protocol/duml.cpp
    src/common/config.cpp
    src/common/telemetry_store.cpp)
target_include_directories(dji_power_core PUBLIC src)
target_compile_definitions(dji_power_core PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(dji_power_core PUBLIC crypt32)

add_library(DJIPowerPlugin SHARED
    src/plugin/dllmain.cpp
    src/plugin/plugin.cpp
    src/ble/ble_manager.cpp
    src/cloud/pair_key_provider.cpp
    src/ui/options_dialog.cpp
    src/ui/sms_login_dialog.cpp)
target_include_directories(DJIPowerPlugin PRIVATE src "${TM_INCLUDE_DIR}")
target_compile_definitions(DJIPowerPlugin PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(DJIPowerPlugin PRIVATE dji_power_core windowsapp winhttp comctl32 crypt32 uxtheme windowscodecs ole32)
set_target_properties(DJIPowerPlugin PROPERTIES OUTPUT_NAME "DJIPowerPlugin")

if(MSVC)
    target_compile_options(dji_power_core PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(DJIPowerPlugin PRIVATE /W4 /permissive- /utf-8)
endif()

add_executable(ble_scan_probe
    tests/ble_scan_probe.cpp
    src/ble/ble_manager.cpp)
target_include_directories(ble_scan_probe PRIVATE src)
target_compile_definitions(ble_scan_probe PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(ble_scan_probe PRIVATE /W4 /permissive- /utf-8)
target_link_libraries(ble_scan_probe PRIVATE dji_power_core windowsapp)

# 手工联网诊断工具只初始化验证码会话，绝不会发送真实短信。
add_executable(cloud_login_probe
    tests/cloud_login_probe.cpp
    src/cloud/pair_key_provider.cpp)
target_include_directories(cloud_login_probe PRIVATE src)
target_compile_definitions(cloud_login_probe PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(cloud_login_probe PRIVATE /W4 /permissive- /utf-8)
target_link_libraries(cloud_login_probe PRIVATE winhttp ole32)
# 独立认证调试进程：允许真实用户完成登录，并在窗口存活期间保留内存会话。
add_executable(dji_auth_debug WIN32
    tools/auth_debug_main.cpp
    src/cloud/pair_key_provider.cpp
    src/ui/sms_login_dialog.cpp)
target_include_directories(dji_auth_debug PRIVATE src)
target_compile_definitions(dji_auth_debug PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(dji_auth_debug PRIVATE /W4 /permissive- /utf-8)
target_link_libraries(dji_auth_debug PRIVATE winhttp comctl32 windowscodecs ole32)
set_target_properties(dji_auth_debug PROPERTIES OUTPUT_NAME "DJI认证调试器")

# 控制端仅能触发预定义的安全操作，并读取不含凭据的状态文本。
add_executable(dji_auth_ctl tools/auth_debug_control.cpp)
target_include_directories(dji_auth_ctl PRIVATE src)
target_compile_definitions(dji_auth_ctl PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(dji_auth_ctl PRIVATE /W4 /permissive- /utf-8)
target_link_libraries(dji_auth_ctl PRIVATE user32)


include(CTest)
if(BUILD_TESTING)
    add_executable(protocol_tests tests/protocol_tests.cpp)
    target_link_libraries(protocol_tests PRIVATE dji_power_core)
    target_compile_definitions(protocol_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(protocol_tests PRIVATE /utf-8)
    add_test(NAME protocol_tests COMMAND protocol_tests)

    add_executable(cloud_parser_tests
        tests/cloud_parser_tests.cpp
        src/cloud/pair_key_provider.cpp)
    target_include_directories(cloud_parser_tests PRIVATE src)
    target_compile_definitions(cloud_parser_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(cloud_parser_tests PRIVATE /utf-8)
    target_link_libraries(cloud_parser_tests PRIVATE dji_power_core winhttp ole32)
    add_test(NAME cloud_parser_tests COMMAND cloud_parser_tests)

    add_executable(plugin_smoke_tests tests/plugin_smoke_tests.cpp)
    target_include_directories(plugin_smoke_tests PRIVATE "${TM_INCLUDE_DIR}")
    target_compile_definitions(plugin_smoke_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(plugin_smoke_tests PRIVATE /utf-8)
    add_test(NAME plugin_smoke_tests COMMAND plugin_smoke_tests "$<TARGET_FILE:DJIPowerPlugin>")
endif()

