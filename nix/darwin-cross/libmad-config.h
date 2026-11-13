/* libmad 0.15.1b config.h for aarch64-apple-darwin, as its configure script
 * produced it when run with zig cc against the macOS 26 SDK
 * (--host=aarch64-apple-darwin --enable-fpm=64bit --disable-shared).
 * FPM_64BIT is passed on the command line, as the generated Makefile would.
 * Recorded here because that configure script (2004 libtool) also probes
 * for ld, a C++ compiler and a C++ preprocessor on the build machine, none
 * of which the cross build has or needs. */

#define HAVE_ASSERT_H 1
#define HAVE_DLFCN_H 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL 1
#define HAVE_FCNTL_H 1
#define HAVE_FORK 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIMITS_H 1
#define HAVE_MEMORY_H 1
#define HAVE_PIPE 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_WAIT_H 1
#define HAVE_UNISTD_H 1
#define HAVE_WAITPID 1
#define PACKAGE "libmad"
#define PACKAGE_BUGREPORT "support@underbit.com"
#define PACKAGE_NAME "MPEG Audio Decoder"
#define PACKAGE_STRING "MPEG Audio Decoder 0.15.1b"
#define PACKAGE_TARNAME "libmad"
#define PACKAGE_VERSION "0.15.1b"
#define SIZEOF_INT 4
#define SIZEOF_LONG 8
#define SIZEOF_LONG_LONG 8
#define STDC_HEADERS 1
#define VERSION "0.15.1b"
