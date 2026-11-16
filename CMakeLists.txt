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

cmake_minimum_required(VERSION 3.16)
project(reone)

set(CMAKE_CXX_STANDARD 17)
set(CLANG_FORMAT_PATH ${CMAKE_SOURCE_DIR}/.clang-format)

# Options

option(BUILD_TESTS "build tests" ON)
option(BUILD_LAUNCHER "build launcher application" ON)
option(BUILD_TOOLKIT "build toolkit application" ON)
option(BUILD_DATAMINER "build dataminer application" ON)
option(BUILD_GFF2JSON "build gff2json application" ON)
option(BUILD_UNERF "build unerf application" ON)
option(BUILD_NCSRUN "build ncsrun application" ON)

option(ENABLE_MOVIE "enable movie playback" ON)
option(ENABLE_ASAN "enable address sanitizer" OFF)
option(ENABLE_THROW_SITE_TRACES "log where fatal exceptions are thrown" ON)

# END Options

# Dependencies

list(PREPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(ReoneVersion)

# Install layout on Linux and other Unix systems except macOS: the engine,
# launcher and toolkit go to libexec/reone and are reached through bin/reone,
# bin/reone-launcher and bin/reone-toolkit (their own names are too generic
# for bin/); the files they ship with go to share/reone. The applications
# find those files relative to themselves, so an installed tree works from any
# prefix, with any DESTDIR, moved or packaged.
if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    set(REONE_INSTALL_LAYOUT ON)
    file(RELATIVE_PATH REONE_RELATIVE_DATA_DIR
        "${CMAKE_INSTALL_FULL_LIBEXECDIR}/reone" "${CMAKE_INSTALL_FULL_DATADIR}/reone")
else()
    set(REONE_INSTALL_LAYOUT OFF)
    set(REONE_RELATIVE_DATA_DIR "")
endif()

find_package(Boost REQUIRED COMPONENTS program_options exception json)
find_package(OpenGL REQUIRED)
find_package(MAD REQUIRED)
find_package(OpenAL REQUIRED)
if(MSVC)
    find_package(SDL3 CONFIG REQUIRED)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL3 REQUIRED sdl3)
    find_package(Threads REQUIRED)
endif()

if(ENABLE_MOVIE)
    if(MSVC)
        find_package(FFMPEG REQUIRED)
    else()
        pkg_check_modules(FFMPEG REQUIRED libavcodec libavformat libavutil libswresample libswscale)
    endif()
    include_directories(${FFMPEG_INCLUDE_DIRS})
endif()

if(BUILD_LAUNCHER OR BUILD_TOOLKIT)
    # Prefer wxWidgets' own package config, fall back to module mode. That fixes MSYS2 build.
    find_package(wxWidgets CONFIG QUIET COMPONENTS core base adv stc gl aui)
    if(NOT wxWidgets_FOUND)
        find_package(wxWidgets REQUIRED COMPONENTS core base adv stc gl aui)
        include(${wxWidgets_USE_FILE})
    endif()
endif()

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/extern/dearimgui)

set(REONE_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)
set(GLAD_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/extern/glad/include)
include_directories(${Boost_INCLUDE_DIRS} ${SDL3_INCLUDE_DIRS} ${MAD_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/extern/glm ${REONE_INCLUDE_DIR} ${GLAD_INCLUDE_DIR} ${IMGUI_DIR})

add_compile_definitions(BOOST_BIND_GLOBAL_PLACEHOLDERS)

# GLM GTX extensions (euler_angles et al.) are gated behind this define in
# recent GLM releases; required when a system/vcpkg GLM shadows extern/glm.
add_compile_definitions(GLM_ENABLE_EXPERIMENTAL)

if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

if(NOT MSVC)
    add_subdirectory(cmake/libbacktrace ${CMAKE_BINARY_DIR}/extern/libbacktrace) # backtrace static library
endif()

if(ENABLE_THROW_SITE_TRACES)
    find_package(Boost QUIET OPTIONAL_COMPONENTS stacktrace_from_exception)
    if(TARGET Boost::stacktrace_from_exception)
        message(STATUS "Throw-site traces: on")
    else()
        message(STATUS "Throw-site traces: off, Boost doesn't provide them")
    endif()
endif()

# END Dependencies

# Compile options

if(ENABLE_ASAN)
    if (MSVC)
        add_compile_options("/fsanitize=address")
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
    endif()
endif()

# Clang stamps a precompiled header with the mtimes of the headers it includes,
# so without this a fresh checkout or a branch switch misses ccache everywhere.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:SHELL:-Xclang -fno-pch-timestamp>")
endif()

# END Compile options

# Libraries

add_subdirectory(src/libs/system) # system static library
add_subdirectory(src/libs/resource) # resource static library
add_subdirectory(src/libs/graphics) # graphics static library
add_subdirectory(src/libs/audio) # audio static library
add_subdirectory(src/libs/input) # input static library
add_subdirectory(src/libs/json) # json static library
add_subdirectory(src/libs/gui) # gui static library
add_subdirectory(src/libs/scene) # scene static library
add_subdirectory(src/libs/movie) # movie static library
add_subdirectory(src/libs/script) # script static library
add_subdirectory(src/libs/game) # game static library
add_subdirectory(src/libs/tools) # tools static library

# END Libraries

# Applications

add_subdirectory(src/apps/shaderpack) # shaderpack application
add_subdirectory(src/apps/engine) # engine application

if(BUILD_LAUNCHER)
    add_subdirectory(src/apps/launcher) # launcher application
endif()

if(BUILD_TOOLKIT)
    add_subdirectory(src/apps/toolkit) # toolkit application
endif()

if(BUILD_DATAMINER)
    add_subdirectory(src/apps/dataminer) # dataminer application
endif()

if(BUILD_GFF2JSON)
    add_subdirectory(src/apps/gff2json) # gff2json application
endif()

if(BUILD_UNERF)
    add_subdirectory(src/apps/unerf) # unpack ERF files
endif()

if(BUILD_NCSRUN)
    add_subdirectory(src/apps/ncsrun) # execute NCS program
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(test) # tests executable
endif()

# END Applications

# Installation

if(REONE_INSTALL_LAYOUT)
    set(REONE_LIBEXEC_APPS engine launcher toolkit)
    foreach(target ${InstallTargets})
        if(target IN_LIST REONE_LIBEXEC_APPS)
            install(TARGETS ${target} DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/reone)
        else()
            install(TARGETS ${target} DESTINATION ${CMAKE_INSTALL_BINDIR})
        endif()
    endforeach()

    install(FILES ${CMAKE_BINARY_DIR}$<$<CONFIG:Debug>:/debug>/bin/shaderpack.erf DESTINATION ${CMAKE_INSTALL_DATADIR}/reone)
    if(BUILD_TOOLKIT)
        install(FILES ${CMAKE_SOURCE_DIR}/extern/cmudict/cmudict.dict DESTINATION ${CMAKE_INSTALL_DATADIR}/reone)
    endif()

    # bin/reone, bin/reone-launcher and bin/reone-toolkit: relative links to
    # libexec/reone, made here and installed as files, so they follow --prefix
    # and DESTDIR and are listed in install_manifest.txt.
    file(RELATIVE_PATH REONE_BIN_TO_LIBEXEC "${CMAKE_INSTALL_FULL_BINDIR}" "${CMAKE_INSTALL_FULL_LIBEXECDIR}/reone")
    set(REONE_BIN_LINKS_DIR ${CMAKE_BINARY_DIR}/install-links)
    file(REMOVE_RECURSE ${REONE_BIN_LINKS_DIR})
    file(MAKE_DIRECTORY ${REONE_BIN_LINKS_DIR})
    set(REONE_BIN_LINKS reone=engine)
    if(BUILD_LAUNCHER)
        list(APPEND REONE_BIN_LINKS reone-launcher=launcher)
    endif()
    if(BUILD_TOOLKIT)
        list(APPEND REONE_BIN_LINKS reone-toolkit=toolkit)
    endif()
    foreach(pair ${REONE_BIN_LINKS})
        string(REPLACE "=" ";" pair ${pair})
        list(GET pair 0 link)
        list(GET pair 1 app)
        file(CREATE_LINK "${REONE_BIN_TO_LIBEXEC}/${app}" ${REONE_BIN_LINKS_DIR}/${link} SYMBOLIC)
        install(FILES ${REONE_BIN_LINKS_DIR}/${link} DESTINATION ${CMAKE_INSTALL_BINDIR})
    endforeach()

    # Desktop entries, icons and AppStream metadata. The reone entry starts
    # the launcher.
    if(BUILD_LAUNCHER)
        install(FILES ${CMAKE_SOURCE_DIR}/packaging/linux/io.github.modawan.reone.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
        install(FILES ${CMAKE_SOURCE_DIR}/packaging/linux/io.github.modawan.reone.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
        install(FILES ${CMAKE_SOURCE_DIR}/assets/reone.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps RENAME io.github.modawan.reone.png)
    endif()
    if(BUILD_TOOLKIT)
        install(FILES ${CMAKE_SOURCE_DIR}/packaging/linux/io.github.modawan.reone.toolkit.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
        install(FILES ${CMAKE_SOURCE_DIR}/assets/toolkit.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps RENAME io.github.modawan.reone.toolkit.png)
    endif()
else()
    install(TARGETS ${InstallTargets} DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# END Installation
