#
# Repository:  https://github.com/kingkybel/FixDecoder
# File Name:   src/CMakeLists.txt
# Description: Build rules for library and executable targets.
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

add_custom_target(update_fix_dicts ALL
        COMMAND "${CMAKE_SOURCE_DIR}/scripts/fetch_quickfix_dicts.sh" "${CMAKE_SOURCE_DIR}/data/quickfix"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Downloading QuickFIX XML dictionaries"
        VERBATIM
)

add_custom_target(generate_fix_decoder_maps ALL
        COMMAND "${CMAKE_SOURCE_DIR}/scripts/generate_fix_decoder_maps.sh" "${CMAKE_SOURCE_DIR}/data/quickfix" "${CMAKE_SOURCE_DIR}/data/generated_src"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Generating FIX decoder map headers from dictionaries"
        DEPENDS update_fix_dicts
        VERBATIM
)

add_custom_target(download_fix_samples ALL
        COMMAND "${CMAKE_SOURCE_DIR}/scripts/fetch_sample_fix_messages.sh" "${CMAKE_SOURCE_DIR}/data/quickfix"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Downloading QuickFIX sample messages"
        DEPENDS update_fix_dicts
        VERBATIM
)

add_custom_target(download_realistic_fix_samples ALL
        COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/generate_realistic_fix_samples_nonfatal.sh"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Downloading market reference data and generating realistic FIX sample messages"
        DEPENDS download_fix_samples
        VERBATIM
)


add_library(fixdecoder
        fix_dictionary.cc
        fix_decoder.cc
        fix_controller.cc
        fix_socket_connection.cc
)

# FIX decoder maps are generated under data/generated_src and are required by fix_decoder.cc.
add_dependencies(fixdecoder generate_fix_decoder_maps)

target_include_directories(fixdecoder
        PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/data/generated_src
        ${CMAKE_SOURCE_DIR}/MessageToObject/include
)

target_link_libraries(fixdecoder
        PUBLIC
        tinyxml2
)

add_executable(example_usage examples.cc)

target_link_libraries(example_usage
        PRIVATE
        fixdecoder
)

add_executable(fix_controller_demo fix_controller_demo.cc)

target_link_libraries(fix_controller_demo
        PRIVATE
        fixdecoder
)
