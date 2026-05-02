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

set(ENGINE_SOURCE_DIR ${CMAKE_SOURCE_DIR}/src/apps/engine)

set(ENGINE_HEADERS
    ${ENGINE_SOURCE_DIR}/console.h
    ${ENGINE_SOURCE_DIR}/engine.h
    ${ENGINE_SOURCE_DIR}/options.h
    ${ENGINE_SOURCE_DIR}/profiler.h)

set(ENGINE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/apps/highperfgfx.cpp
    ${ENGINE_SOURCE_DIR}/console.cpp
    ${ENGINE_SOURCE_DIR}/engine.cpp
    ${ENGINE_SOURCE_DIR}/main.cpp
    ${ENGINE_SOURCE_DIR}/profiler.cpp)

if(NOT REONE_PLATFORM_WEB)
    list(APPEND ENGINE_HEADERS ${ENGINE_SOURCE_DIR}/optionsparser.h)
    list(APPEND ENGINE_SOURCES ${ENGINE_SOURCE_DIR}/optionsparser.cpp)
endif()

if(WIN32)
    list(APPEND ENGINE_HEADERS ${CMAKE_SOURCE_DIR}/assets/reone.rc)
endif()

add_executable(engine ${ENGINE_HEADERS} ${ENGINE_SOURCES} ${CLANG_FORMAT_PATH})
set_target_properties(engine PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}$<$<CONFIG:Debug>:/debug>/bin)
target_precompile_headers(engine PRIVATE ${CMAKE_SOURCE_DIR}/src/pch.h)

if(REONE_PLATFORM_WEB)
    target_link_libraries(engine PRIVATE game)
else()
    target_link_libraries(engine PRIVATE game ${Boost_PROGRAM_OPTIONS_LIBRARY})
endif()

if(REONE_PLATFORM_WEB)
    if(REONE_WEB_ASSET_PROFILE STREQUAL "minimal")
        message(FATAL_ERROR "REONE_WEB_ASSET_PROFILE=minimal was removed. Use none (default KotOR.js-parity: File System Access), full, or custom. Clear CMake cache when upgrading.")
    endif()
    # Embedded /game trees exceed 1 GiB once unpacked in MEMFS; FS-Access-only builds stay smaller at runtime.
    set(_REONE_WEB_MAX_MEMORY 1073741824)
    if(REONE_WEB_GAME_DIR)
        set(_REONE_WEB_MAX_MEMORY 4294967296)
    endif()
    target_link_options(engine PRIVATE
        "SHELL:--pre-js \"${CMAKE_SOURCE_DIR}/tools/web/gamefs.js\""
        -sALLOW_MEMORY_GROWTH=1
        "SHELL:-sMAXIMUM_MEMORY=${_REONE_WEB_MAX_MEMORY}"
        -sENVIRONMENT=web
        -sMIN_WEBGL_VERSION=2
        -sMAX_WEBGL_VERSION=2
        -sFULL_ES3=1
        -sGL_ENABLE_GET_PROC_ADDRESS=1
        -lidbfs.js
        "SHELL:--preload-file \"${CMAKE_SOURCE_DIR}/glsl@/glsl\"")

    if(REONE_WEB_GAME_DIR)
        if(NOT REONE_WEB_ALLOW_EMBEDDED_GAME_BUNDLE)
            message(FATAL_ERROR
                "REONE_WEB_GAME_DIR is set but REONE_WEB_ALLOW_EMBEDDED_GAME_BUNDLE=OFF.\n"
                "KotOR.js (https://github.com/KobaltBlu/KotOR.js) does not ship the install in the web bundle — it uses File System Access and reads files on demand.\n"
                "Reone matches that by default: unset REONE_WEB_GAME_DIR and use tools/web/gamefs.js (folder picker → MEMFS copy / IndexedDB handle).\n"
                "If you truly need a non-parity embedded tree (e.g. Safari CI), configure with -DREONE_WEB_ALLOW_EMBEDDED_GAME_BUNDLE=ON explicitly.")
        endif()
        if(REONE_WEB_ASSET_PROFILE STREQUAL "none")
            message(FATAL_ERROR "REONE_WEB_GAME_DIR is set but REONE_WEB_ASSET_PROFILE=none (default KotOR.js-parity build embeds no game files). Unset REONE_WEB_GAME_DIR, or set REONE_WEB_ASSET_PROFILE to full or custom.")
        endif()
        if(EXISTS "${REONE_WEB_GAME_DIR}")
            if(REONE_WEB_ASSET_PROFILE STREQUAL "full")
                set(REONE_WEB_PRELOAD_ARG "${REONE_WEB_GAME_DIR}@/game")
                target_link_options(engine PRIVATE
                    "SHELL:--preload-file \"${REONE_WEB_PRELOAD_ARG}\"")
                message(STATUS "reone web: preloading full game dir ${REONE_WEB_GAME_DIR} to /game (non-parity embedded package)")
            elseif(REONE_WEB_ASSET_PROFILE STREQUAL "custom")
                if(NOT REONE_WEB_PRELOAD_ITEMS)
                    message(FATAL_ERROR "REONE_WEB_ASSET_PROFILE=custom requires REONE_WEB_PRELOAD_ITEMS to be set")
                endif()
                set(REONE_WEB_RESOLVED_PRELOAD_ITEMS ${REONE_WEB_PRELOAD_ITEMS})
                message(STATUS "reone web: preloading explicit REONE_WEB_PRELOAD_ITEMS from ${REONE_WEB_GAME_DIR}: ${REONE_WEB_RESOLVED_PRELOAD_ITEMS}")
                foreach(REONE_WEB_ITEM IN LISTS REONE_WEB_RESOLVED_PRELOAD_ITEMS)
                    set(REONE_WEB_ITEM_SOURCE "${REONE_WEB_GAME_DIR}/${REONE_WEB_ITEM}")
                    if(EXISTS "${REONE_WEB_ITEM_SOURCE}")
                        target_link_options(engine PRIVATE
                            "SHELL:--preload-file \"${REONE_WEB_ITEM_SOURCE}@/game/${REONE_WEB_ITEM}\"")
                    else()
                        message(WARNING "reone web: skipping missing preload item: ${REONE_WEB_ITEM_SOURCE}")
                    endif()
                endforeach()
            else()
                message(FATAL_ERROR "Invalid REONE_WEB_ASSET_PROFILE='${REONE_WEB_ASSET_PROFILE}'. Expected none, full, or custom")
            endif()
        else()
            message(FATAL_ERROR "REONE_WEB_GAME_DIR does not exist: ${REONE_WEB_GAME_DIR}")
        endif()
    else()
        message(STATUS "reone web: no REONE_WEB_GAME_DIR — game files come from File System Access at runtime (KotOR.js parity); see tools/web/gamefs.js")
    endif()

    if(REONE_WEB_GAME_DIR AND NOT REONE_WEB_ASSET_PROFILE STREQUAL "none")
        if(REONE_WEB_LZ4_PRELOAD)
            target_link_options(engine PRIVATE -sLZ4)
            message(STATUS "reone web: embedded preload will use -sLZ4 (smaller .data, streaming decode)")
        endif()
        find_program(REONE_PYTHON_EMBED NAMES python3 python)
        if(REONE_PYTHON_EMBED)
            add_custom_command(
                TARGET engine
                POST_BUILD
                COMMAND "${REONE_PYTHON_EMBED}" "${CMAKE_SOURCE_DIR}/tools/web/patch_engine_embedded_preload.py"
                        "$<TARGET_FILE_DIR:engine>/engine.html"
                COMMENT "Patch engine.html for embedded /game (skip File System Access picker)")
        endif()
    endif()
endif()

list(APPEND InstallTargets engine)
