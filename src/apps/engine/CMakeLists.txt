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
    target_link_options(engine PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sMAXIMUM_MEMORY=1073741824
        -sMIN_WEBGL_VERSION=2
        -sMAX_WEBGL_VERSION=2
        -sFULL_ES3=1
        -sGL_ENABLE_GET_PROC_ADDRESS=1
        -lidbfs.js
        "SHELL:--preload-file \"${CMAKE_SOURCE_DIR}/glsl@/glsl\"")

    if(REONE_WEB_GAME_DIR)
        if(EXISTS "${REONE_WEB_GAME_DIR}")
            if(REONE_WEB_PRELOAD_ITEMS)
                message(STATUS "reone web: preloading selected items from ${REONE_WEB_GAME_DIR}: ${REONE_WEB_PRELOAD_ITEMS}")
                foreach(REONE_WEB_ITEM IN LISTS REONE_WEB_PRELOAD_ITEMS)
                    set(REONE_WEB_ITEM_SOURCE "${REONE_WEB_GAME_DIR}/${REONE_WEB_ITEM}")
                    if(EXISTS "${REONE_WEB_ITEM_SOURCE}")
                        target_link_options(engine PRIVATE
                            "SHELL:--preload-file \"${REONE_WEB_ITEM_SOURCE}@/game/${REONE_WEB_ITEM}\"")
                    else()
                        message(WARNING "reone web: skipping missing preload item: ${REONE_WEB_ITEM_SOURCE}")
                    endif()
                endforeach()
            else()
                set(REONE_WEB_PRELOAD_ARG "${REONE_WEB_GAME_DIR}@/game")
                target_link_options(engine PRIVATE
                    "SHELL:--preload-file \"${REONE_WEB_PRELOAD_ARG}\"")
                message(STATUS "reone web: preloading full game dir ${REONE_WEB_GAME_DIR} to /game")
            endif()
        else()
            message(FATAL_ERROR "REONE_WEB_GAME_DIR does not exist: ${REONE_WEB_GAME_DIR}")
        endif()
    else()
        message(WARNING "REONE_WEB_GAME_DIR is empty; web build will start without game data mounted at /game")
    endif()
endif()

list(APPEND InstallTargets engine)
