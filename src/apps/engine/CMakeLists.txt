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

set(_reone_engine_runtime_dir "${CMAKE_BINARY_DIR}$<$<CONFIG:Debug>:/debug>/bin")
# Emscripten cross-compiles with WIN32=OFF; use host OS so Windows developers still match this branch.
if(REONE_PLATFORM_WEB AND CMAKE_HOST_WIN32)
    set(REONE_WEB_WIN_WASM_OUTPUT_DIR "" CACHE PATH
        "Windows host only: explicit directory for engine.html/js/wasm when wasm-ld cannot write under the build tree or %TEMP% (Controlled Folder Access / AV).")
    option(REONE_WEB_WIN_WASM_OUTSIDE_REPO "Write engine.html/js/wasm outside the build tree (see REONE_WEB_WIN_WASM_OUTPUT_DIR or %TEMP%/reone-emscripten-*)" ON)
    if(REONE_WEB_WIN_WASM_OUTSIDE_REPO)
        string(SHA256 _reone_wasm_out_hash "${CMAKE_BINARY_DIR}")
        string(SUBSTRING "${_reone_wasm_out_hash}" 0 16 _reone_wasm_out_tag)
        if(REONE_WEB_WIN_WASM_OUTPUT_DIR)
            set(_reone_engine_runtime_dir "${REONE_WEB_WIN_WASM_OUTPUT_DIR}")
        elseif(NOT "$ENV{TEMP}" STREQUAL "")
            set(_reone_engine_runtime_dir "$ENV{TEMP}/reone-emscripten-${_reone_wasm_out_tag}")
        else()
            set(_reone_engine_runtime_dir "$ENV{TMP}/reone-emscripten-${_reone_wasm_out_tag}")
        endif()
        file(MAKE_DIRECTORY "${_reone_engine_runtime_dir}")
        file(WRITE "${CMAKE_BINARY_DIR}/reone-web-output-dir.txt" "${_reone_engine_runtime_dir}\n")
        message(STATUS "reone web: Windows wasm bundle directory: ${_reone_engine_runtime_dir}")
    else()
        if(EXISTS "${CMAKE_BINARY_DIR}/reone-web-output-dir.txt")
            file(REMOVE "${CMAKE_BINARY_DIR}/reone-web-output-dir.txt")
        endif()
    endif()
elseif(REONE_PLATFORM_WEB)
    if(EXISTS "${CMAKE_BINARY_DIR}/reone-web-output-dir.txt")
        file(REMOVE "${CMAKE_BINARY_DIR}/reone-web-output-dir.txt")
    endif()
endif()

set_target_properties(engine PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${_reone_engine_runtime_dir}")
target_precompile_headers(engine PRIVATE ${CMAKE_SOURCE_DIR}/src/pch.h)

if(REONE_PLATFORM_WEB)
    target_link_libraries(engine PRIVATE game)
else()
    target_link_libraries(engine PRIVATE game ${Boost_PROGRAM_OPTIONS_LIBRARY})
endif()

if(REONE_PLATFORM_WEB)
    if(REONE_WEB_ASSET_PROFILE STREQUAL "minimal")
        message(FATAL_ERROR [[REONE_WEB_ASSET_PROFILE=minimal was removed. Use none (default KotOR.js-parity: File System Access), full, or custom. Clear CMake cache when upgrading.]])
    endif()
    if(EXISTS "${CMAKE_BINARY_DIR}/bin/engine.data.orig")
        message(FATAL_ERROR "Stale ${CMAKE_BINARY_DIR}/bin/engine.data.orig detected. This usually means an accidental embedded preload from an old build tree. Delete it before configuring.")
    endif()
    # Lazy range reads keep game archives out of MEMFS; allow growth for decoded resources and renderer state.
    set(_REONE_WEB_MAX_MEMORY 4294967296)
    target_compile_options(engine PRIVATE
        $<$<CONFIG:Release>:-Oz>
        $<$<CONFIG:Release>:-g0>)
    target_link_options(engine PRIVATE
        "SHELL:--pre-js \"${CMAKE_SOURCE_DIR}/tools/web/gamefs.js\""
        # WebFileInputStream uses EM_ASYNC_JS + fetch/File System Access; sync XHR cannot read binary on the main thread.
        -sASYNCIFY=1
        "SHELL:-sASYNCIFY_IMPORTS=['reone_web_file_length','reone_web_file_read']"
        # KotOR resource init unwinds a deep virtual-dispatch stack (loadGlobalResources -> ErfReader -> readString
        # -> WebFileInputStream::refillBuffer -> async import). 128 KiB overflows that during rewind and corrupts the
        # indirect call table -> "null function or function signature mismatch". 1 MiB cleared the minimal-data init
        # paths, but a *retail* install drives much deeper async suspends (menu model/TPC/walkmesh reads under
        # GUI::load + scene build), which overflow 1 MiB and re-trap "null function" before the menu draws. 16 MiB
        # clears those; the Asyncify stack is a single fixed buffer, negligible against MAXIMUM_MEMORY=4 GiB.
        "SHELL:-sASYNCIFY_STACK_SIZE=16777216"
        # Do not use ASYNCIFY_ONLY here: EM_ASYNC_JS suspends deep under Engine::run; instrumenting
        # only main leaves unwound frames un-instrumented → RuntimeError: unreachable in Asyncify.
        -sALLOW_MEMORY_GROWTH=1
        "SHELL:-sMAXIMUM_MEMORY=${_REONE_WEB_MAX_MEMORY}"
        -sENVIRONMENT=web
        -sASSERTIONS=0
        -sEXCEPTION_DEBUG=0
        -sMIN_WEBGL_VERSION=2
        -sMAX_WEBGL_VERSION=2
        -sFULL_ES3=1
        -sGL_ENABLE_GET_PROC_ADDRESS=1
        # gamefs.js (pre-js) marshals a module name into a heap buffer and calls reone_web_warp;
        # pin these so DCE/minification cannot drop them under -Oz / -sASSERTIONS=0.
        "SHELL:-sEXPORTED_FUNCTIONS=['_main','_malloc','_free','_reone_web_warp']"
        -lidbfs.js
        "SHELL:--preload-file \"${CMAKE_SOURCE_DIR}/glsl@/glsl\"")
    # ``--pre-js`` is not modeled as a link input for Ninja; tie gamefs.js to main.o so edits relink wasm.
    set_source_files_properties(${ENGINE_SOURCE_DIR}/main.cpp PROPERTIES
        OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/tools/web/gamefs.js")
    message(STATUS "reone web: Asyncify is on for lazy /game I/O; wasm links are much faster with -DCMAKE_BUILD_TYPE=Release than Debug")
    if(REONE_WEB_DEBUG_RUNTIME)
        # --profiling-funcs keeps the wasm name section so "null function or function signature
        # mismatch" traps symbolicate to real C++ names instead of wasm-function[NNNN].
        target_link_options(engine PRIVATE -sASSERTIONS=2 --profiling-funcs)
    else()
        # Override CMAKE_EXE_LINKER_FLAGS/toolchain presets that inject -sASSERTIONS=2 (last emcc flag wins).
        target_link_options(engine PRIVATE "SHELL:-sSTACK_OVERFLOW_CHECK=0" "SHELL:-sASSERTIONS=0")
    endif()
    if(REONE_WEB_SAFE_HEAP)
        target_link_options(engine PRIVATE -sSAFE_HEAP=2)
    endif()

    if(REONE_WEB_GAME_DIR)
        if(NOT REONE_WEB_ALLOW_EMBEDDED_GAME_BUNDLE)
            message(FATAL_ERROR [[
REONE_WEB_GAME_DIR is set but REONE_WEB_ALLOW_EMBEDDED_GAME_BUNDLE=OFF.

KotOR.js (https://github.com/KobaltBlu/KotOR.js) does not ship the install in the web bundle; it uses File System Access and reads files on demand.

Reone matches that by default: unset REONE_WEB_GAME_DIR and use tools/web/gamefs.js (folder picker + IndexedDB handle + on-demand range reads).

If you truly need a non-parity embedded tree (e.g. Safari CI), configure with -DREONE_WEB_ALLOW_EMBEDDED_GAME_BUNDLE=ON explicitly.
]])
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
        message(STATUS "reone web: no REONE_WEB_GAME_DIR — game files come from File System Access at runtime; see tools/web/gamefs.js")
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

    # Real wasm/html/js land under %TEMP%/… or REONE_WEB_WIN_WASM_OUTPUT_DIR; copy into CMAKE_BINARY_DIR/bin
    # so ``serve.py --directory …/bin`` matches docs and cannot show a lone stale engine.data.
    if(CMAKE_HOST_WIN32 AND REONE_WEB_WIN_WASM_OUTSIDE_REPO)
        get_filename_component(_reone_web_stage_bin "${CMAKE_BINARY_DIR}/bin" ABSOLUTE)
        get_filename_component(_reone_web_run_bin "${_reone_engine_runtime_dir}" ABSOLUTE)
        # Normalize slashes so we never skip staging when paths differ only by \ vs /.
        file(TO_CMAKE_PATH "${_reone_web_stage_bin}" _reone_web_stage_cmp)
        file(TO_CMAKE_PATH "${_reone_web_run_bin}" _reone_web_run_cmp)
        if(NOT _reone_web_stage_cmp STREQUAL _reone_web_run_cmp)
            add_custom_command(
                TARGET engine
                POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${_reone_web_stage_bin}"
                COMMAND ${CMAKE_COMMAND} -E copy "${_reone_web_run_bin}/engine.html" "${_reone_web_stage_bin}/engine.html"
                COMMAND ${CMAKE_COMMAND} -E copy "${_reone_web_run_bin}/engine.js" "${_reone_web_stage_bin}/engine.js"
                COMMAND ${CMAKE_COMMAND} -E copy "${_reone_web_run_bin}/engine.wasm" "${_reone_web_stage_bin}/engine.wasm"
                COMMAND ${CMAKE_COMMAND} -E copy "${_reone_web_run_bin}/engine.data" "${_reone_web_stage_bin}/engine.data"
                COMMENT "Stage wasm bundle into build tree bin/ for serve.py --directory bin")
        endif()
    endif()

    find_program(REONE_PYTHON_VERIFY NAMES python3 python)
    if(REONE_PYTHON_VERIFY)
        add_custom_command(
            TARGET engine
            POST_BUILD
            COMMAND "${REONE_PYTHON_VERIFY}" "${CMAKE_SOURCE_DIR}/tools/web/verify_wasm_bundle.py"
                    "$<TARGET_FILE_DIR:engine>"
            COMMENT "Verify wasm bundle at Emscripten output directory")
        if(CMAKE_HOST_WIN32 AND REONE_WEB_WIN_WASM_OUTSIDE_REPO)
            get_filename_component(_reone_web_verify_stage "${CMAKE_BINARY_DIR}/bin" ABSOLUTE)
            get_filename_component(_reone_web_verify_run "${_reone_engine_runtime_dir}" ABSOLUTE)
            file(TO_CMAKE_PATH "${_reone_web_verify_stage}" _reone_web_verify_stage_c)
            file(TO_CMAKE_PATH "${_reone_web_verify_run}" _reone_web_verify_run_c)
            if(NOT _reone_web_verify_stage_c STREQUAL _reone_web_verify_run_c)
                add_custom_command(
                    TARGET engine
                    POST_BUILD
                    COMMAND "${REONE_PYTHON_VERIFY}" "${CMAKE_SOURCE_DIR}/tools/web/verify_wasm_bundle.py"
                            "${_reone_web_verify_stage}"
                    COMMENT "Verify staged wasm bundle under build tree bin/")
            endif()
        endif()
    else()
        message(WARNING "reone web: Python not found — wasm bundle POST_BUILD verification skipped")
    endif()
endif()

list(APPEND InstallTargets engine)
