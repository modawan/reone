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

set(UNERF_SOURCE_DIR ${CMAKE_SOURCE_DIR}/src/apps/unerf)

set(UNERF_HEADERS)

set(UNERF_SOURCES
    ${UNERF_SOURCE_DIR}/main.cpp)

add_executable(unerf ${UNERF_SOURCES} ${UNERF_HEADERS} ${CLANG_FORMAT_PATH})
set_target_properties(unerf PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}$<$<CONFIG:Debug>:/debug>/bin)
target_precompile_headers(unerf PRIVATE ${CMAKE_SOURCE_DIR}/src/pch.h)
target_link_libraries(unerf PRIVATE resource system ${Boost_PROGRAM_OPTIONS_LIBRARY})

