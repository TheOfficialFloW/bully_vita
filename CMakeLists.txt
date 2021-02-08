cmake_minimum_required(VERSION 2.8)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(Bully C CXX)
include("${VITASDK}/share/vita.cmake" REQUIRED)
set(VITA_APP_NAME "Bully")
set(VITA_TITLEID  "BULLY0000")
set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Wl,-q -Wall -O3 -mfloat-abi=softfp")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++11")

add_executable(Bully
  main.c
  so_util.c
  jni_patch.c
  openal_patch.c
  opengl_patch.c
)

target_link_libraries(Bully
  -Wl,--whole-archive pthread -Wl,--no-whole-archive
  stdc++
  m
  mathneon
  openal
  vitaGL
  vitashark
  kubridge_stub
  SceAppMgr_stub
  SceAudio_stub
  SceAudioIn_stub
  SceCtrl_stub
  SceCommonDialog_stub
  SceDisplay_stub
  SceFios2_stub
  SceGxm_stub
  SceShaccCg_stub
  SceSysmodule_stub
  ScePower_stub
  SceTouch_stub
  SceAppUtil_stub
)

vita_create_self(eboot.bin Bully UNSAFE)
vita_create_vpk(Bully.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
)

add_custom_target(copy
  COMMAND cp eboot.bin E:/app/${VITA_TITLEID}/eboot.bin
  DEPENDS eboot.bin
)
