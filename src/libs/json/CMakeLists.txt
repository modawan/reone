# Copyright (c) 2026 The reone project contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

set(JSON_INCLUDE_DIR ${REONE_INCLUDE_DIR}/reone/json)
set(JSON_SOURCE_DIR ${CMAKE_SOURCE_DIR}/src/libs/json)

set(JSON_HEADERS
    ${JSON_INCLUDE_DIR}/resource.h
    ${JSON_INCLUDE_DIR}/util.h)

set(JSON_SOURCES
    ${JSON_SOURCE_DIR}/gff.cpp
    ${JSON_SOURCE_DIR}/gvt.cpp
    ${JSON_SOURCE_DIR}/util.cpp)

add_library(json STATIC ${JSON_HEADERS} ${JSON_SOURCES} ${CLANG_FORMAT_PATH})
set_target_properties(json PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}$<$<CONFIG:Debug>:/debug>/lib)
set_target_properties(json PROPERTIES DEBUG_POSTFIX "d")
target_precompile_headers(json PRIVATE ${CMAKE_SOURCE_DIR}/src/pch.h)
target_link_libraries(json PRIVATE resource ${Boost_JSON_LIBRARY})
