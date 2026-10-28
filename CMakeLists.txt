cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE
        "$ENV{VITASDK}/share/vita.toolchain.cmake"
        CACHE PATH "VitaSDK toolchain")
endif()

project(VitaMQTT C)

include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Vita MQTT Broker")
set(VITA_TITLEID "NKVITAC01")
set(VITA_VERSION "01.00")

add_executable(VitaMQTT
    src/main.c
)

target_link_libraries(VitaMQTT
    SceNet_stub
    SceNetCtl_stub
    SceAppMgr_stub
    SceSysmodule_stub
    m
)

vita_create_self(eboot.bin VitaMQTT)

vita_create_vpk(VitaMQTT.vpk
    NKVITAC01
    eboot.bin
    VERSION 01.00
    NAME "Vita MQTT Broker"
)