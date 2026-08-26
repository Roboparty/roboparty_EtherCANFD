# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 wentywenty

# ---- Toolchain ----
set(ENV{GNURISCV_TOOLCHAIN_PATH} "$ENV{HPM_SDK_BASE}/../toolchains/rv32imac_zicsr_zifencei_multilib_b_ext-win")
set(ENV{HPM_SDK_TOOLCHAIN_VARIANT} "")

cmake_minimum_required(VERSION 3.13)

# ---- Project settings ----
set(CONFIG_EEPROM_EMULATION 1)
set(CONFIG_CHERRYUSB_DEVICE 1)
set(CONFIG_USB_HS 1)
set(CONFIG_CHERRYUSB 1)

if("${HPM_BUILD_TYPE}" STREQUAL "")
    set(HPM_BUILD_TYPE flash_xip)
endif()

set(BOARD_SEARCH_PATH "${CMAKE_CURRENT_LIST_DIR}/boards" CACHE PATH "")
set(BOARD ethercanfd CACHE STRING "")
set(APP_NAME ethercanfd CACHE STRING "Output binary name")

if(NOT DEFINED hpm-sdk_DIR)
    set(hpm-sdk_DIR "$ENV{HPM_SDK_BASE}/cmake")
endif()
find_package(hpm-sdk REQUIRED HINTS $ENV)

project(ethercanfd)

# ---- Version (from git tag, fallback 0.0.0) ----
execute_process(
    COMMAND git describe --tags --always
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_DESCRIBE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(GIT_DESCRIBE)
    string(REGEX REPLACE "^v" "" FIRMWARE_VERSION "${GIT_DESCRIBE}")
else()
    set(FIRMWARE_VERSION "0.0.0")
endif()

string(TIMESTAMP BUILD_DATE "%Y%m%d")

# Parse major.minor.patch
if(FIRMWARE_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(FW_MAJOR ${CMAKE_MATCH_1})
    set(FW_MINOR ${CMAKE_MATCH_2})
    set(FW_PATCH ${CMAKE_MATCH_3})
else()
    set(FW_MAJOR 0)
    set(FW_MINOR 0)
    set(FW_PATCH 0)
endif()

# ---- USB bcdDevice (MM.m.p, displayed by lsusb as MM.mp) ----
if(FW_MAJOR GREATER 99 OR FW_MINOR GREATER 9 OR FW_PATCH GREATER 9)
    message(FATAL_ERROR "Firmware version ${FW_MAJOR}.${FW_MINOR}.${FW_PATCH} cannot be encoded in USB bcdDevice")
endif()
math(EXPR _bcd_major_tens "${FW_MAJOR} / 10")
math(EXPR _bcd_major_ones "${FW_MAJOR} % 10")
math(EXPR FW_BCD_DEVICE "${_bcd_major_tens} * 4096 + ${_bcd_major_ones} * 256 + ${FW_MINOR} * 16 + ${FW_PATCH}")

# ---- EtherCAT REVISION_NUMBER ----
math(EXPR FW_REVISION "${FW_MAJOR} * 65536 + ${FW_MINOR} * 256 + ${FW_PATCH}")

# ---- Generate version header (only main.c depends on it) ----
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h)

# ---- Global compile definitions ----
add_compile_definitions(
    FW_BCD_DEVICE=${FW_BCD_DEVICE}
    REVISION_NUMBER=${FW_REVISION}
)

# ---- Sources ----
add_subdirectory(SSC)

# Suppress SSC-generated null-character warnings in user code

set(ECAT_PORT_DIR "$ENV{HPM_SDK_BASE}/samples/ethercat/port")
sdk_inc(${ECAT_PORT_DIR})
sdk_src(${ECAT_PORT_DIR}/hpm_ecat_hw.c)
sdk_src(${ECAT_PORT_DIR}/hpm_ecat_phy.c)
if(CONFIG_EEPROM_EMULATION)
    sdk_src(${ECAT_PORT_DIR}/hpm_ecat_e2p_emulation.c)
endif()

sdk_inc(src)
sdk_inc(${CMAKE_CURRENT_BINARY_DIR}/generated)
sdk_src(src/EtherCANFD.c)
sdk_app_src(src/canfd_ecat.c)
sdk_app_src(src/gs_usb.c)
sdk_app_src(src/main.c)

if(CONFIG_CHERRYUSB_DEVICE)
    sdk_inc(${HPM_SDK_BASE}/components/usb/device)
    sdk_src(${HPM_SDK_BASE}/components/usb/device/hpm_usb_device.c)
endif()

# ---- SDK compile definitions ----
sdk_compile_definitions(-DCONFIG_USB_HS=1)
sdk_compile_definitions(-DCONFIG_CHERRYUSB_DEVICE=1)
sdk_compile_definitions(-DUSB_MAX_EP0_SIZE=64)
sdk_compile_definitions(-DCONFIG_CANFD_ECHO=1)
sdk_compile_options(-Wno-null-character) 

# ---- Finalize ----
generate_ide_projects()

add_custom_command(TARGET ${APP_ELF_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${APP_BIN_DIR}/output/${APP_BIN_NAME}"
        "${APP_BIN_DIR}/output/${APP_NAME}_v${FIRMWARE_VERSION}-${BUILD_DATE}.bin"
    COMMENT "Versioned: ${APP_NAME}_v${FIRMWARE_VERSION}-${BUILD_DATE}.bin"
)

add_custom_target(ssc_clean
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_CURRENT_SOURCE_DIR}/SSC/Src"
    COMMAND ${CMAKE_COMMAND} -E echo "Removed SSC/Src/. Run 'Remove-Item -Recurse -Force build' to delete build dir."
)

include(${CMAKE_CURRENT_LIST_DIR}/boards/board.cmake)
