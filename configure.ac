#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])
AC_INIT([klvanc-tool], [1.0], [stoth@kernellabs.com])
AM_INIT_AUTOMAKE
AC_CONFIG_MACRO_DIR([m4])
AC_CANONICAL_HOST
AM_SILENT_RULES([yes])

# Checks for programs.
AC_PROG_CC
AC_PROG_CC_C99
AC_PROG_CXX
PLAT_PREFIX=Linux
if test `uname -s` = "Darwin"
then
CFLAGS+=" -I/usr/local/include "
LDFLAGS+=" -L/usr/local/lib -framework CoreFoundation"
PLAT_PREFIX=Mac
fi
CFLAGS+=" -DKL_USERSPACE "

# We always build with -fPIC in case our static libraries end up
# being linked into a consumer's shared library
AC_MSG_CHECKING(whether fPIC compiler option is accepted)
SAVED_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -fPIC -Werror"
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [return 0;])],
    [AC_MSG_RESULT(yes)
     CFLAGS="$SAVED_CFLAGS -fPIC"],
    [AC_MSG_RESULT(no)
     CFLAGS="$SAVED_CFLAGS"])

AC_PROG_RANLIB
AC_PROG_LIBTOOL

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h netinet/in.h stdint.h stdlib.h string.h sys/ioctl.h sys/socket.h sys/time.h unistd.h])
AC_CHECK_HEADERS([libklvanc/vanc.h],
                 [break],
                 [AC_MSG_ERROR([libklvanc headers not found or not usable])])

PKG_CHECK_MODULES(ZLIB, zlib >= 1.2.9)

# Check for curses (optional)
AC_CHECK_HEADERS([curses.h], HEADER_LIBCURSES="yes")
if test "x$HEADER_LIBCURSES" == "xyes" ; then
    LDFLAGS+=" -lcurses "
fi
AM_CONDITIONAL(LIBCURSES, test x"$HEADER_LIBCURSES" == x"yes")

AC_CHECK_HEADERS([libklmonitoring/klmonitoring.h], HEADER_LIBKLMONITORING="yes")
if test "x$HEADER_LIBKLMONITORING" == "xyes" ; then
    LDFLAGS+=" -lklmonitoring "
fi
AM_CONDITIONAL(LIBKLMONITORING, test x"$HEADER_LIBKLMONITORING" == x"yes")

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_UINT16_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_CHECK_FUNCS([memset strrchr])

# Add debug support
AC_ARG_ENABLE(debug,
  AS_HELP_STRING(
    [--enable-debug],
    [enable debugging, default: no]),
    [case "${enableval}" in
      yes) debug=true ;;
      no)  debug=false ;;
      *)   AC_MSG_ERROR([bad value ${enableval} for --enable-debug]) ;;
    esac],
    [debug=false])
AM_CONDITIONAL(DEBUG, test x"$debug" = x"true")
AM_COND_IF(DEBUG,
    AC_DEFINE(DEBUG, 1, [Define to 0 if this is a release build]),
    AC_DEFINE(DEBUG, 0, [Define to 1 or higher if this is a debug build]))

# Path to Blackmagic SDK
AC_ARG_WITH([bmsdk],
            [AS_HELP_STRING([--with-bmsdk=path],
              [specify alternate path to Blackmagic SDK])],
            [],
            [with_bmsdk=no])

AS_IF([test "x$with_bmsdk" != xno],
            [AC_CHECK_FILE([$with_bmsdk/$PLAT_PREFIX/include/DeckLinkAPI.h],
              [AC_SUBST([BLACKMAGIC_SDK_PATH], [$with_bmsdk/$PLAT_PREFIX/include])
               AC_DEFINE([HAVE_BMSDK], [1],
                         [Define if you have BlackMagic SDK installed])
              ],
              [AC_MSG_FAILURE(
                 [--with-bmsdk was given, but SDK not found at specified path])]
              )],[AC_MSG_WARN([Falling back to Linux SDK which won't work on other platforms]); AC_SUBST([BLACKMAGIC_SDK_PATH], [${ac_pwd}/include/decklink-sdk])])

AC_CONFIG_FILES([Makefile tools/Makefile])
AC_OUTPUT

