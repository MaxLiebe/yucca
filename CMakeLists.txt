cmake_minimum_required(VERSION 3.16)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Bluepad32 (and the BTstack it vendors) is a git submodule:
#   git submodule update --init --recursive
set(BLUEPAD32_ROOT "${CMAKE_CURRENT_LIST_DIR}/external/bluepad32")
set(BTSTACK_DIR "${BLUEPAD32_ROOT}/external/btstack")

if(NOT EXISTS "${BTSTACK_DIR}/src")
    message(FATAL_ERROR "Submodules missing. Run: git submodule update --init --recursive")
endif()

# Bluepad32 ships BTstack patches (e.g. DualShock 3 pairing). Apply each one
# that is not applied yet, so a plain `idf.py build` gets them.
file(GLOB BLUEPAD32_PATCHES "${BLUEPAD32_ROOT}/external/patches/*.patch")
foreach(patch ${BLUEPAD32_PATCHES})
    execute_process(COMMAND git apply --reverse --check "${patch}"
        WORKING_DIRECTORY "${BTSTACK_DIR}" RESULT_VARIABLE already_applied
        OUTPUT_QUIET ERROR_QUIET)
    if(NOT already_applied EQUAL 0)
        execute_process(COMMAND git apply "${patch}"
            WORKING_DIRECTORY "${BTSTACK_DIR}" RESULT_VARIABLE apply_failed)
        if(NOT apply_failed EQUAL 0)
            message(FATAL_ERROR "Could not apply ${patch} to BTstack")
        endif()
        message(STATUS "Applied BTstack patch ${patch}")
    endif()
endforeach()

set(ENV{BTSTACK_ROOT} "${BTSTACK_DIR}")

# Firmware version from the tag. Not `--dirty`: the BTstack patches above
# always leave the submodule modified.
execute_process(COMMAND git describe --tags --always
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}" OUTPUT_VARIABLE PROJECT_VER
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)

# Bluepad32 and its BTstack port live under bluepad32/src/components.
set(EXTRA_COMPONENT_DIRS "${BLUEPAD32_ROOT}/src/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include($ENV{BTSTACK_ROOT}/port/esp32/components/btstack/cmake/BTstack_project.cmake)

project(yucca)
