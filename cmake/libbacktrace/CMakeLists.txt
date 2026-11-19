# Builds libbacktrace from the subtree in extern/libbacktrace (upstream
# https://github.com/ianlancetaylor/libbacktrace) without its autotools
# scripts. The checks below stand in for its configure script on the
# platforms reone builds for. MSVC writes PDBs, which libbacktrace can't read,
# so it isn't built there.

include(CheckCSourceCompiles)
include(CheckIncludeFile)
include(CheckSymbolExists)

set(BACKTRACE_DIR ${CMAKE_SOURCE_DIR}/extern/libbacktrace)
set(BACKTRACE_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)

set(BACKTRACE_SOURCES
    ${BACKTRACE_DIR}/atomic.c
    ${BACKTRACE_DIR}/backtrace.c
    ${BACKTRACE_DIR}/dwarf.c
    ${BACKTRACE_DIR}/fileline.c
    ${BACKTRACE_DIR}/posix.c
    ${BACKTRACE_DIR}/print.c
    ${BACKTRACE_DIR}/simple.c
    ${BACKTRACE_DIR}/sort.c
    ${BACKTRACE_DIR}/state.c)

set(BACKTRACE_SUPPORTED 1)
set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)

# The object file format of the executables this build produces.
if(APPLE)
    list(APPEND BACKTRACE_SOURCES ${BACKTRACE_DIR}/macho.c)
    set(BACKTRACE_SUPPORTS_DATA 1)
elseif(WIN32)
    list(APPEND BACKTRACE_SOURCES ${BACKTRACE_DIR}/pecoff.c)
    set(BACKTRACE_SUPPORTS_DATA 0)
else()
    list(APPEND BACKTRACE_SOURCES ${BACKTRACE_DIR}/elf.c)
    math(EXPR BACKTRACE_ELF_SIZE "${CMAKE_SIZEOF_VOID_P} * 8")
    set(BACKTRACE_SUPPORTS_DATA 1)
endif()

# Memory and file views: mmap where the platform has it, malloc and read
# otherwise.
check_symbol_exists(mmap sys/mman.h HAVE_MMAP)
check_symbol_exists(MAP_ANONYMOUS sys/mman.h HAVE_MAP_ANONYMOUS)
check_symbol_exists(MAP_ANON sys/mman.h HAVE_MAP_ANON)
if(HAVE_MMAP)
    list(APPEND BACKTRACE_SOURCES ${BACKTRACE_DIR}/mmapio.c)
else()
    list(APPEND BACKTRACE_SOURCES ${BACKTRACE_DIR}/read.c)
endif()
if(HAVE_MMAP AND (HAVE_MAP_ANONYMOUS OR HAVE_MAP_ANON))
    list(APPEND BACKTRACE_SOURCES ${BACKTRACE_DIR}/mmap.c)
    set(BACKTRACE_USES_MALLOC 0)
else()
    list(APPEND BACKTRACE_SOURCES ${BACKTRACE_DIR}/alloc.c)
    set(BACKTRACE_USES_MALLOC 1)
endif()

check_c_source_compiles("
int main(void) {
    int i = 0;
    __sync_bool_compare_and_swap(&i, i, i);
    __sync_lock_test_and_set(&i, 1);
    __sync_lock_release(&i);
    return 0;
}" HAVE_SYNC_FUNCTIONS)
check_c_source_compiles("
int main(void) {
    int i = 0;
    __atomic_load_n(&i, __ATOMIC_ACQUIRE);
    __atomic_store_n(&i, 1, __ATOMIC_RELEASE);
    return 0;
}" HAVE_ATOMIC_FUNCTIONS)
if(HAVE_ATOMIC_FUNCTIONS)
    set(BACKTRACE_SUPPORTS_THREADS 1)
else()
    set(BACKTRACE_SUPPORTS_THREADS 0)
endif()
check_c_source_compiles("
#include <unwind.h>
struct _Unwind_Context *context;
int main(void) {
    int ip_before_insn = 0;
    return (int) _Unwind_GetIPInfo(context, &ip_before_insn);
}" HAVE_GETIPINFO)

check_include_file(link.h HAVE_LINK_H)
check_include_file(sys/link.h HAVE_SYS_LINK_H)
check_include_file(mach-o/dyld.h HAVE_MACH_O_DYLD_H)
check_include_file(windows.h HAVE_WINDOWS_H)
check_include_file(tlhelp32.h HAVE_TLHELP32_H)
check_symbol_exists(dl_iterate_phdr link.h HAVE_DL_ITERATE_PHDR)
check_symbol_exists(fcntl fcntl.h HAVE_FCNTL)
check_symbol_exists(lstat sys/stat.h HAVE_LSTAT)
check_symbol_exists(readlink unistd.h HAVE_READLINK)
check_symbol_exists(clock_gettime time.h HAVE_CLOCK_GETTIME)
check_symbol_exists(strnlen string.h HAVE_DECL_STRNLEN)
check_symbol_exists(getpagesize unistd.h HAVE_DECL_GETPAGESIZE)
check_symbol_exists(_pgmptr stdlib.h HAVE_DECL__PGMPTR)

unset(CMAKE_REQUIRED_DEFINITIONS)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake.in ${BACKTRACE_GENERATED_DIR}/config.h)
configure_file(${BACKTRACE_DIR}/backtrace-supported.h.in ${BACKTRACE_GENERATED_DIR}/backtrace-supported.h @ONLY)

add_library(backtrace STATIC ${BACKTRACE_SOURCES})
set_target_properties(backtrace PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}$<$<CONFIG:Debug>:/debug>/lib)
set_target_properties(backtrace PROPERTIES DEBUG_POSTFIX "d")
target_include_directories(backtrace PRIVATE ${BACKTRACE_GENERATED_DIR})
target_include_directories(backtrace SYSTEM PUBLIC ${BACKTRACE_DIR})
target_compile_definitions(backtrace PRIVATE _GNU_SOURCE)
target_compile_options(backtrace PRIVATE -funwind-tables)
