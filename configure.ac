#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.59])
AC_INIT([mpd2irc], [0.1.0], [angelos@unkreativ.org])
AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([dist-bzip2])

# Checks for programs.
AC_PROG_CC_C99

# Checks for libraries.
PKG_CHECK_MODULES([confuse], [libconfuse])
AC_SUBST(confuse_LIBS)
AC_SUBST(confuse_CFLAGS)

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h netdb.h netinet/in.h stdlib.h string.h sys/socket.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.
AC_CHECK_FUNCS([gethostbyname select socket strdup strstr])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
