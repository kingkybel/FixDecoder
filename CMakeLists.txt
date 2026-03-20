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
include(CTest)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake-common/cmake/DkybBuildSettings.cmake)
dkyb_apply_common_settings(CXX_STANDARD 23)
dkyb_update_submodules()

execute_process(
        COMMAND bash -c "printf \"%s\" $(cat /etc/os-release | grep -E \"^ID=[^\\n]*|^VERSION_ID=[^\\n]*\" | cut -f2 -d= | tr -d '\"')"
        OUTPUT_VARIABLE LINUX_VERSION
)
message("Linux version is '${LINUX_VERSION}'")

set(CMAKE_FIND_LIBRARY_PREFIXES lib)
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_EXE_LINKER_FLAGS "-static")
option(FIXDECODER_FETCH_TINYXML2 "Fetch tinyxml2 with CMake" ON)
option(FIXDECODER_FETCH_GOOGLETEST "Fetch GoogleTest with CMake" ON)

include(FetchContent)

if(FIXDECODER_FETCH_TINYXML2)
        # Avoid building upstream tinyxml2 test executable (xmltest) with this
        # project's stricter warning policy (e.g. -Werror).
        set(tinyxml2_BUILD_TESTING OFF CACHE BOOL "Disable tinyxml2 tests" FORCE)
        FetchContent_Declare(
                tinyxml2
                URL https://github.com/leethomason/tinyxml2/archive/9.0.0.tar.gz
        )
        FetchContent_MakeAvailable(tinyxml2)
else()
        find_package(tinyxml2 REQUIRED)
endif()

if(FIXDECODER_FETCH_GOOGLETEST)
        set(INSTALL_GTEST ON CACHE BOOL "" FORCE)
        FetchContent_Declare(
                googletest
                URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
        )
        FetchContent_MakeAvailable(googletest)
else()
        find_package(GTest REQUIRED)
endif()

include_directories(
        BEFORE SYSTEM ${CMAKE_SOURCE_DIR}/include
        system
)
link_directories(
        /usr/local/lib
        system
)

enable_testing()

add_subdirectory(src)
add_subdirectory(test/unit)

set(CMAKE_CXX_CLANG_TIDY "clang-tidy;-checks=* -p ${CMAKE_SOURCE_DIR}/build")

file(GLOB HEADER_FILES ${CMAKE_SOURCE_DIR}/include/*.h)

# Install the header files directly under 'include/dkyb'
install(FILES ${HEADER_FILES}
        DESTINATION include/dkyb)
