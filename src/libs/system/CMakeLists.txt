# Copyright (c) 2020-2023 The reone project contributors

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

set(SYSTEM_INCLUDE_DIR ${REONE_INCLUDE_DIR}/reone/system)
set(SYSTEM_SOURCE_DIR ${CMAKE_SOURCE_DIR}/src/libs/system)

set(SYSTEM_HEADERS
    ${SYSTEM_INCLUDE_DIR}/arrayref.h
    ${SYSTEM_INCLUDE_DIR}/binaryreader.h
    ${SYSTEM_INCLUDE_DIR}/binarywriter.h
    ${SYSTEM_INCLUDE_DIR}/cast.h
    ${SYSTEM_INCLUDE_DIR}/checkutil.h
    ${SYSTEM_INCLUDE_DIR}/clipboard.h
    ${SYSTEM_INCLUDE_DIR}/clock.h
    ${SYSTEM_INCLUDE_DIR}/crashreport.h
    ${SYSTEM_INCLUDE_DIR}/di/module.h
    ${SYSTEM_INCLUDE_DIR}/di/services.h
    ${SYSTEM_INCLUDE_DIR}/exception/endofstream.h
    ${SYSTEM_INCLUDE_DIR}/exception/filenotfound.h
    ${SYSTEM_INCLUDE_DIR}/exception/notimplemented.h
    ${SYSTEM_INCLUDE_DIR}/exception/validation.h
    ${SYSTEM_INCLUDE_DIR}/fileutil.h
    ${SYSTEM_INCLUDE_DIR}/hexutil.h
    ${SYSTEM_INCLUDE_DIR}/logger.h
    ${SYSTEM_INCLUDE_DIR}/logutil.h
    ${SYSTEM_INCLUDE_DIR}/randomutil.h
    ${SYSTEM_INCLUDE_DIR}/smallset.h
    ${SYSTEM_INCLUDE_DIR}/smallvector.h
    ${SYSTEM_INCLUDE_DIR}/stream/fileinput.h
    ${SYSTEM_INCLUDE_DIR}/stream/fileoutput.h
    ${SYSTEM_INCLUDE_DIR}/stream/input.h
    ${SYSTEM_INCLUDE_DIR}/stream/memoryinput.h
    ${SYSTEM_INCLUDE_DIR}/stream/memoryoutput.h
    ${SYSTEM_INCLUDE_DIR}/stream/output.h
    ${SYSTEM_INCLUDE_DIR}/stringbuilder.h
    ${SYSTEM_INCLUDE_DIR}/stringutil.h
    ${SYSTEM_INCLUDE_DIR}/textbuffer.h
    ${SYSTEM_INCLUDE_DIR}/textreader.h
    ${SYSTEM_INCLUDE_DIR}/textwriter.h
    ${SYSTEM_INCLUDE_DIR}/threadpool.h
    ${SYSTEM_INCLUDE_DIR}/threadutil.h
    ${SYSTEM_INCLUDE_DIR}/timeevents.h
    ${SYSTEM_INCLUDE_DIR}/timer.h
    ${SYSTEM_INCLUDE_DIR}/timespan.h
    ${SYSTEM_INCLUDE_DIR}/types.h
    ${SYSTEM_INCLUDE_DIR}/unicodeutil.h)

set(SYSTEM_SOURCES
    ${SYSTEM_SOURCE_DIR}/binaryreader.cpp
    ${SYSTEM_SOURCE_DIR}/binarywriter.cpp
    ${SYSTEM_SOURCE_DIR}/clipboard.cpp
    ${SYSTEM_SOURCE_DIR}/clock.cpp
    ${SYSTEM_SOURCE_DIR}/crashreport/report.cpp
    ${SYSTEM_SOURCE_DIR}/di/module.cpp
    ${SYSTEM_SOURCE_DIR}/fileutil.cpp
    ${SYSTEM_SOURCE_DIR}/hexutil.cpp
    ${SYSTEM_SOURCE_DIR}/logger.cpp
    ${SYSTEM_SOURCE_DIR}/randomutil.cpp
    ${SYSTEM_SOURCE_DIR}/stream/memoryinput.cpp
    ${SYSTEM_SOURCE_DIR}/stringutil.cpp
    ${SYSTEM_SOURCE_DIR}/textbuffer.cpp
    ${SYSTEM_SOURCE_DIR}/textreader.cpp
    ${SYSTEM_SOURCE_DIR}/textwriter.cpp
    ${SYSTEM_SOURCE_DIR}/threadpool.cpp
    ${SYSTEM_SOURCE_DIR}/threadutil.cpp
    ${SYSTEM_SOURCE_DIR}/timeevents.cpp
    ${SYSTEM_SOURCE_DIR}/unicodeutil.cpp)

if(WIN32)
    list(APPEND SYSTEM_SOURCES ${SYSTEM_SOURCE_DIR}/crashreport/windows.cpp)
else()
    list(APPEND SYSTEM_SOURCES ${SYSTEM_SOURCE_DIR}/crashreport/posix.cpp)
endif()

add_library(system STATIC ${SYSTEM_HEADERS} ${SYSTEM_SOURCES} ${CLANG_FORMAT_PATH})
set_target_properties(system PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}$<$<CONFIG:Debug>:/debug>/lib)
set_target_properties(system PROPERTIES DEBUG_POSTFIX "d")
target_precompile_headers(system PRIVATE ${CMAKE_SOURCE_DIR}/src/pch.h)

if(MSVC)
    target_link_libraries(system PRIVATE SDL3::SDL3)
else()
    target_link_libraries(system PRIVATE ${SDL3_LIBRARIES} ${SDL3_LDFLAGS} Threads::Threads)
endif()

# Backtraces come from Boost.Stacktrace, header-only: libbacktrace reads the
# debug info everywhere but MSVC, which uses dbghelp. Only crashreport/report.cpp
# includes it, so the backend's macros stay out of the rest of the build.
if(MSVC)
    set_source_files_properties(${SYSTEM_SOURCE_DIR}/crashreport/report.cpp PROPERTIES
        COMPILE_DEFINITIONS BOOST_STACKTRACE_USE_WINDBG_CACHED)
else()
    set_source_files_properties(${SYSTEM_SOURCE_DIR}/crashreport/report.cpp PROPERTIES
        COMPILE_DEFINITIONS "BOOST_STACKTRACE_USE_BACKTRACE;BOOST_STACKTRACE_BACKTRACE_INCLUDE_FILE=<backtrace.h>;BOOST_STACKTRACE_GNU_SOURCE_NOT_REQUIRED")
    target_link_libraries(system PRIVATE backtrace)
endif()
if(ENABLE_THROW_SITE_TRACES AND TARGET Boost::stacktrace_from_exception)
    # From Boost 1.92 on, the library's usage requirements switch the
    # Stacktrace headers to linked-library mode, which the header-only
    # backends above can't satisfy. LINK_ONLY drops them only inside an
    # interface's link libraries on the CMake versions reone supports.
    add_library(throw_site_traces INTERFACE)
    target_link_libraries(throw_site_traces INTERFACE $<LINK_ONLY:Boost::stacktrace_from_exception>)
    target_link_libraries(system PRIVATE throw_site_traces)
    target_compile_definitions(system PRIVATE R_ENABLE_THROW_SITE_TRACES)
endif()
