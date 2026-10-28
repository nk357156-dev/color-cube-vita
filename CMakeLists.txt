cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE
        "$ENV{VITASDK}/share/vita.toolchain.cmake"
        CACHE PATH "VitaSDK toolchain")
endif()

project(ColorCube C)

include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Color Cube")
set(VITA_TITLEID "NKVITAC01")
set(VITA_VERSION "01.00")

add_executable(ColorCube
    src/main.c
)

target_link_libraries(ColorCube
    vita2d
    SceCtrl_stub
    SceDisplay_stub
    SceGxm_stub
    SceSysmodule_stub
    m
)

vita_create_self(eboot.bin ColorCube)

vita_create_vpk(ColorCube.vpk
    NKVITAC01
    eboot.bin
    VERSION 01.00
    NAME "Color Cube"
)