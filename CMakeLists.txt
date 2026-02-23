#
# Repository:  https://github.com/kingkybel/FixDecoder
# File Name:   CMakeLists.txt
# Description: Project build configuration.
#
# Copyright (C) 2026 Dieter J Kybelksties <github@kybelksties.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
#
# @date: 2026-02-11
# @author: Dieter J Kybelksties
#

cmake_minimum_required(VERSION 3.26)
project(FixDecoder
        VERSION 1.0.0
        DESCRIPTION "A fix decoder library"
        HOMEPAGE_URL "https://github.com/kingkybel/FixDecoder")

cmake_policy(SET CMP0135 NEW)

include(GNUInstallDirs)

if (CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif ()

message("BUILD TYPE " ${CMAKE_BUILD_TYPE})
set(CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE} CACHE STRING "Debug Release RelWithDebInfo MinSizeRel Coverage")
set(DISPLAY_BUILD_TYPE ${CMAKE_BUILD_TYPE})
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;RelWithDebInfo;MinSizeRel;Coverage" CACHE STRING "Configs" FORCE)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/lib)

execute_process(
        COMMAND bash -c "printf \"%s\" $(cat /etc/os-release | grep -E \"^ID=[^\\n]*|^VERSION_ID=[^\\n]*\" | cut -f2 -d= | tr -d '\"')"
        OUTPUT_VARIABLE LINUX_VERSION
)
message("Linux version is '${LINUX_VERSION}'")

set(COVERAGE_COMPILER_FLAGS "-g -O0 -fno-default-inline --coverage -fprofile-abs-path -fprofile-arcs -fno-inline -fno-inline-small-functions -ftest-coverage -lgcov" CACHE INTERNAL "")

set(CMAKE_CXX_FLAGS "-rdynamic -Wall -Werror" CACHE STRING "Default C++ compiler flags")
set(CMAKE_CXX_FLAGS_DEBUG "-g" CACHE STRING "g++ debug flags")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-g -O2" CACHE STRING "g++ rel+dbg flags")
set(CMAKE_CXX_FLAGS_COVERAGE "${COVERAGE_COMPILER_FLAGS}" CACHE STRING "g++ coverage flags." FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "-O2" CACHE STRING "g++ release flags")
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_FIND_LIBRARY_PREFIXES lib)
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_EXE_LINKER_FLAGS "-static")
option(FIXDECODER_FETCH_TINYXML2 "Fetch tinyxml2 with CMake" ON)
option(FIXDECODER_FETCH_GOOGLETEST "Fetch GoogleTest with CMake" ON)

include(FetchContent)

if (FIXDECODER_FETCH_TINYXML2)
    FetchContent_Declare(
            tinyxml2
            URL https://github.com/leethomason/tinyxml2/archive/9.0.0.tar.gz
    )
    FetchContent_MakeAvailable(tinyxml2)
else ()
    find_package(tinyxml2 REQUIRED)
endif ()

if (FIXDECODER_FETCH_GOOGLETEST)
    set(INSTALL_GTEST ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    )
    FetchContent_MakeAvailable(googletest)
else ()
    find_package(GTest REQUIRED)
endif ()

include_directories(
        BEFORE SYSTEM ${CMAKE_SOURCE_DIR}/include
        system
)
link_directories(
        /usr/local/lib
        system
)

add_custom_target(update_fix_dicts ALL
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/fetch_quickfix_dicts.sh" "${CMAKE_CURRENT_SOURCE_DIR}/data/quickfix"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Downloading QuickFIX XML dictionaries"
        VERBATIM
)

add_custom_target(generate_fix_decoder_maps ALL
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_fix_decoder_maps.sh" "${CMAKE_CURRENT_SOURCE_DIR}/data/quickfix" "${CMAKE_CURRENT_SOURCE_DIR}/data/generated_src"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Generating FIX decoder map headers from dictionaries"
        DEPENDS update_fix_dicts
        VERBATIM
)

add_custom_target(download_fix_samples ALL
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/fetch_sample_fix_messages.sh" "${CMAKE_CURRENT_SOURCE_DIR}/data/quickfix"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Downloading QuickFIX sample messages"
        DEPENDS update_fix_dicts
        VERBATIM
)

add_custom_target(download_realistic_fix_samples ALL
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_realistic_fix_samples.py"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Downloading market reference data and generating realistic FIX sample messages"
        DEPENDS download_fix_samples
        VERBATIM
)

enable_testing()

add_subdirectory(src)
add_subdirectory(test/unit)

set(CMAKE_CXX_CLANG_TIDY "clang-tidy;-checks=* -p ${CMAKE_SOURCE_DIR}/build")

file(GLOB HEADER_FILES ${CMAKE_SOURCE_DIR}/include/*.h)

# Install the header files directly under 'include/dkyb'
install(FILES ${HEADER_FILES}
        DESTINATION include/dkyb)
