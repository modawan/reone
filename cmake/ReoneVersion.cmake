# Sets REONE_VERSION_STRING, the version the applications report.
#
# -DREONE_VERSION=... wins (packagers building from a tarball, the Nix flake);
# otherwise `git describe --tags --always --dirty` of the source tree, read when
# CMake configures, so a build keeps the version it was configured with until
# CMake runs again; otherwise "unknown".

set(REONE_VERSION "" CACHE STRING "Version the applications report; empty to use git describe")

if(REONE_VERSION)
    set(REONE_VERSION_STRING "${REONE_VERSION}")
else()
    set(REONE_VERSION_STRING "unknown")
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" describe --tags --always --dirty
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            OUTPUT_VARIABLE _reone_describe
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _reone_describe_result
            ERROR_QUIET)
        if(_reone_describe_result EQUAL 0 AND _reone_describe)
            set(REONE_VERSION_STRING "${_reone_describe}")
        endif()
    endif()
endif()

# Only characters that are safe inside a C string literal.
string(REGEX REPLACE "[^A-Za-z0-9._+~-]" "_" REONE_VERSION_STRING "${REONE_VERSION_STRING}")
message(STATUS "reone version: ${REONE_VERSION_STRING}")
