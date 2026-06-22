# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 wentywenty

# Board-specific USB PID for DFU
if(BOARD STREQUAL "5evk")
    set(MFG_USB_VID_PID "0x34b7,0x0001" CACHE STRING "USB VID,PID for DFU connection")
else()
    set(MFG_USB_VID_PID "0x34b7,0x0008" CACHE STRING "USB VID,PID for DFU connection")
endif()

# ============================================================================
# J-Link Flash Programming Support
# ============================================================================

set(JLINK_EXE "JLINK_EXE-NOTFOUND" CACHE FILEPATH "Path to JLinkExe executable")

if(NOT JLINK_EXE OR JLINK_EXE STREQUAL "JLINK_EXE-NOTFOUND")
    if(DEFINED ENV{JLINK_PATH})
        find_program(JLINK_EXE
            NAMES JLink JLink.exe JLinkExe JLinkExe.exe JLink_CommandLine JLink_CommandLine.sh
            PATHS "$ENV{JLINK_PATH}"
            NO_DEFAULT_PATH)
    endif()
    if(NOT JLINK_EXE OR JLINK_EXE STREQUAL "JLINK_EXE-NOTFOUND")
        find_program(JLINK_EXE
            NAMES JLink JLink.exe JLinkExe JLinkExe.exe JLink_CommandLine JLink_CommandLine.sh)
    endif()
endif()

if(JLINK_EXE)
    message(STATUS "J-Link found: ${JLINK_EXE}")
    set(JLINK_FOUND TRUE)
else()
    message(STATUS "J-Link not found. Flash targets will not be available.")
    message(STATUS "   Please set JLINK_PATH environment variable or install J-Link from https://www.segger.com/downloads/jlink/")
    set(JLINK_FOUND FALSE)
endif()

set(JLINK_SCRIPT_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/jlink_scripts")
set(JLINK_SCRIPT_DIR "${BOARD_SEARCH_PATH}/${BOARD}")

file(MAKE_DIRECTORY ${JLINK_SCRIPT_BUILD_DIR})
set(JLINK_ELF_PATH "${EXECUTABLE_OUTPUT_PATH}/${APP_ELF_NAME}")
set(JLINK_BIN_PATH "${EXECUTABLE_OUTPUT_PATH}/${APP_BIN_NAME}")
configure_file(${JLINK_SCRIPT_DIR}/jlink_flash.jlink.in     ${JLINK_SCRIPT_BUILD_DIR}/jlink_flash.jlink @ONLY)
configure_file(${JLINK_SCRIPT_DIR}/jlink_flash_bin.jlink.in  ${JLINK_SCRIPT_BUILD_DIR}/jlink_flash_bin.jlink @ONLY)
configure_file(${JLINK_SCRIPT_DIR}/jlink_verify.jlink.in      ${JLINK_SCRIPT_BUILD_DIR}/jlink_verify.jlink @ONLY)

if(JLINK_FOUND)
    add_custom_target(jlink_flash
        COMMAND ${JLINK_EXE} -nogui 1 -CommanderScript "${JLINK_SCRIPT_BUILD_DIR}/jlink_flash.jlink"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS ${APP_ELF_NAME}
        COMMENT "Programming flash via J-Link..."
        USES_TERMINAL
    )
    add_custom_target(jlink_flash_bin
        COMMAND ${JLINK_EXE} -nogui 1 -CommanderScript "${JLINK_SCRIPT_BUILD_DIR}/jlink_flash_bin.jlink"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS ${APP_ELF_NAME}
        COMMENT "Programming flash via J-Link (binary)..."
        USES_TERMINAL
    )
    add_custom_target(jlink_erase
        COMMAND ${JLINK_EXE} -nogui 1 -CommanderScript "${JLINK_SCRIPT_DIR}/jlink_erase.jlink"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Erasing flash via J-Link..."
        USES_TERMINAL
    )
    add_custom_target(jlink_verify
        COMMAND ${JLINK_EXE} -nogui 1 -CommanderScript "${JLINK_SCRIPT_BUILD_DIR}/jlink_verify.jlink"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS ${APP_ELF_NAME}
        COMMENT "Verifying flash via J-Link..."
        USES_TERMINAL
    )
else()
    message(WARNING "J-Link targets not available. Install J-Link and ensure JLinkExe is in PATH.")
endif()

# ============================================================================
# HPM Manufacturing Tool (DFU via USB-HID)
# ============================================================================

set(MFG_TOOL "MFG_TOOL-NOTFOUND" CACHE FILEPATH "Path to hpm_manufacturing_cmd.exe")

if(NOT MFG_TOOL OR MFG_TOOL STREQUAL "MFG_TOOL-NOTFOUND")
    if(DEFINED ENV{MFG_TOOL_PATH})
        find_program(MFG_TOOL
            NAMES hpm_manufacturing_cmd.exe
            PATHS "$ENV{MFG_TOOL_PATH}"
            NO_DEFAULT_PATH)
    endif()
    if(NOT MFG_TOOL OR MFG_TOOL STREQUAL "MFG_TOOL-NOTFOUND")
        find_program(MFG_TOOL
            NAMES hpm_manufacturing_cmd.exe)
    endif()
endif()

if(MFG_TOOL)
    message(STATUS "MFG Tool found: ${MFG_TOOL}")
    set(MFG_TOOL_FOUND TRUE)
else()
    message(STATUS "MFG Tool not found. DFU flash target not available.")
    set(MFG_TOOL_FOUND FALSE)
endif()

if(MFG_TOOL_FOUND)
    add_custom_target(mfg_flash
        COMMAND ${MFG_TOOL} -u "${MFG_USB_VID_PID}"
                -r "write-memory 0x0 0x200 [[0xfcf90002,0x05,0x1000,0x0]]"
                -r "config-memory 0x10000 0x200"
                -r "write-memory 0x10000 0x80000400 ${APP_BIN_DIR}/output/${APP_BIN_NAME}"
        DEPENDS ${APP_ELF_NAME}
        COMMENT "DFU flash via HPM Manufacturing Tool..."
        USES_TERMINAL
    )
endif()
