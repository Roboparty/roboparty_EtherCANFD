# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 wentywenty
#
# Requires $env:SSC_TOOL_DIR set (run "source env.ps1" or set manually)

set(SSC_TOOL_CMD "$ENV{SSC_TOOL_DIR}/SSC Tool Cmd.exe")
set(SSC_ESP      "${CMAKE_CURRENT_SOURCE_DIR}/EtherCANFD.esp")
set(SSC_XLSX     "${CMAKE_CURRENT_SOURCE_DIR}/EtherCANFD.xlsx")
set(SSC_GEN_TMP  "${CMAKE_CURRENT_BINARY_DIR}/ssc_gen_tmp")
set(SSC_GEN_STAMP "${CMAKE_CURRENT_BINARY_DIR}/ssc_gen.stamp")

if(NOT EXISTS "${SSC_GEN_STAMP}"
   OR "${SSC_ESP}" IS_NEWER_THAN "${SSC_GEN_STAMP}"
   OR "${SSC_XLSX}" IS_NEWER_THAN "${SSC_GEN_STAMP}")
    message(STATUS "SSC generation: ${SSC_ESP} -> Src/")
    file(REMOVE_RECURSE "${SSC_GEN_TMP}")
    file(MAKE_DIRECTORY "${SSC_GEN_TMP}")
    execute_process(
        COMMAND "${SSC_TOOL_CMD}" -o "${SSC_GEN_TMP}" -p "${SSC_ESP}"
        RESULT_VARIABLE _ssc_res
    )
    if(NOT _ssc_res EQUAL 0)
        message(FATAL_ERROR "SSC generation failed (exit ${_ssc_res})")
    endif()
    file(REMOVE_RECURSE "${CMAKE_CURRENT_SOURCE_DIR}/Src")
    file(COPY "${SSC_GEN_TMP}/src/" DESTINATION "${CMAKE_CURRENT_SOURCE_DIR}/Src")
    file(COPY "${SSC_GEN_TMP}/EtherCANFD.xml" DESTINATION "${CMAKE_CURRENT_SOURCE_DIR}/")
    file(REMOVE_RECURSE "${SSC_GEN_TMP}")
    file(TOUCH "${SSC_GEN_STAMP}")
endif()

set(SSC_GEN_SOURCES
    Src/coeappl.c
    Src/ecatappl.c
    Src/ecatcoe.c
    Src/ecatslv.c
    Src/mailbox.c
    Src/objdef.c
    Src/sdoserv.c
)

sdk_inc(Src)

foreach(src ${SSC_GEN_SOURCES})
    sdk_src(${src})
endforeach()

set_source_files_properties(
    ${SSC_GEN_SOURCES}
    PROPERTIES COMPILE_OPTIONS
    "-Wno-strict-prototypes;-Wno-missing-braces;-Wno-missing-field-initializers;\
-Wno-implicit-fallthrough;-Wno-unused-parameter;-Wno-type-limits;\
-Wno-overflow;-Wno-address-of-packed-member;-Wno-address;\
-Wno-null-character"
)
