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

# Finds OpenAL as the OpenAL::OpenAL target, preferring OpenAL Soft wherever it
# is installed. On Apple, OpenAL.framework stands in when it is not. Either way
# the target puts al.h and alc.h on the include path, so sources include those
# by name. With OpenAL Soft the target also defines R_ENABLE_EFX, as only it
# has the effects extension.

# Homebrew keeps OpenAL Soft out of its prefix, so the search is told where it is.
if(APPLE)
    find_program(HOMEBREW_EXECUTABLE brew)
    if(HOMEBREW_EXECUTABLE)
        execute_process(
            COMMAND ${HOMEBREW_EXECUTABLE} --prefix openal-soft
            OUTPUT_VARIABLE HOMEBREW_OPENAL_SOFT_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(IS_DIRECTORY "${HOMEBREW_OPENAL_SOFT_PREFIX}")
            list(APPEND CMAKE_PREFIX_PATH ${HOMEBREW_OPENAL_SOFT_PREFIX})
        endif()
    endif()
endif()

# OpenAL Soft's own package, then pkg-config for installs that ship no package.
# The package search resets how this one was asked for, so that is kept.
set(OPENAL_FIND_REQUIRED_ASKED ${OpenAL_FIND_REQUIRED})
set(OPENAL_FIND_QUIETLY_ASKED ${OpenAL_FIND_QUIETLY})
find_package(OpenAL CONFIG QUIET)
set(OpenAL_FIND_REQUIRED ${OPENAL_FIND_REQUIRED_ASKED})
set(OpenAL_FIND_QUIETLY ${OPENAL_FIND_QUIETLY_ASKED})
if(TARGET OpenAL::OpenAL)
    set_property(TARGET OpenAL::OpenAL APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS R_ENABLE_EFX)
    set(OPENAL_IMPLEMENTATION "OpenAL Soft")
else()
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(OPENAL_SOFT QUIET IMPORTED_TARGET openal)
    endif()
    if(OPENAL_SOFT_FOUND)
        add_library(OpenAL::OpenAL INTERFACE IMPORTED)
        target_link_libraries(OpenAL::OpenAL INTERFACE PkgConfig::OPENAL_SOFT)
        target_compile_definitions(OpenAL::OpenAL INTERFACE R_ENABLE_EFX)
        set(OPENAL_IMPLEMENTATION "OpenAL Soft")
    elseif(APPLE)
        # Only the framework is looked for, so OpenAL Soft's library cannot
        # match the name on a case-insensitive file system.
        set(OPENAL_FIND_FRAMEWORK_ASKED ${CMAKE_FIND_FRAMEWORK})
        set(CMAKE_FIND_FRAMEWORK ONLY)
        find_library(OPENAL_FRAMEWORK OpenAL)
        set(CMAKE_FIND_FRAMEWORK ${OPENAL_FIND_FRAMEWORK_ASKED})
        if(OPENAL_FRAMEWORK)
            add_library(OpenAL::OpenAL INTERFACE IMPORTED)
            target_link_libraries(OpenAL::OpenAL INTERFACE ${OPENAL_FRAMEWORK})
            target_include_directories(OpenAL::OpenAL INTERFACE ${OPENAL_FRAMEWORK}/Headers)
            set(OPENAL_IMPLEMENTATION "OpenAL.framework")
        endif()
    endif()
endif()

if(APPLE)
    set(OPENAL_NOT_FOUND_MESSAGE "Neither OpenAL Soft nor OpenAL.framework found")
else()
    set(OPENAL_NOT_FOUND_MESSAGE "OpenAL Soft not found")
endif()
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OpenAL
    REQUIRED_VARS OPENAL_IMPLEMENTATION
    FAIL_MESSAGE "${OPENAL_NOT_FOUND_MESSAGE}")
