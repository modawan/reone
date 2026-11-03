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

find_path(OPENAL_INCLUDE_DIR al.h DOC "OPENAL include directory"
    HINTS
        ENV OPENALDIR
    PATHS
        /opt/homebrew/opt/openal-soft)

find_library(OPENAL_LIBRARY NAMES openal DOC "OPENAL library"
    HINTS
        ENV OPENALDIR
    PATHS
        /opt/homebrew/opt/openal-soft)

if(OPENAL_INCLUDE_DIR AND OPENAL_LIBRARY)
    set(OPENAL_FOUND 1)
    set(OPENAL_LIBRARIES ${OPENAL_LIBRARY})
    set(OPENAL_INCLUDE_DIRS ${OPENAL_INCLUDE_DIR})
else()
    set(OPENAL_FOUND 0)
    set(OPENAL_LIBRARIES)
    set(OPENAL_INCLUDE_DIRS)
endif()

if(NOT OPENAL_FOUND)
    set(OPENAL_NOT_FOUND_MESSAGE "OPENAL library not found. Set OPENAL_INCLUDE_DIR and OPENAL_LIBRARY manually.")
    if(OPENAL_FIND_REQUIRED)
        message(FATAL_ERROR "${OPENAL_NOT_FOUND_MESSAGE}")
    endif()
else()
    message("OPENAL library found")
endif()
