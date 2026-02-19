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



add_library(fixdecoder
        fix_dictionary.cc
        fix_decoder.cc
        fix_controller.cc
        fix_socket_connection.cc
)

target_include_directories(fixdecoder
        PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/data/generated
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
