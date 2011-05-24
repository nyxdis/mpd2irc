#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.65])
AC_INIT([mpd2irc], [0.2.0], [mende.christoph@gmail.com])
AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([dist-bzip2 foreign])

# Checks for programs.
AC_PROG_CC_C99

# Checks for libraries.
PKG_PROG_PKG_CONFIG([0.24])
PKG_CHECK_MODULES([glib], [glib-2.0 >= 2.28])
PKG_CHECK_MODULES([gio], [gio-2.0 >= 2.28])
PKG_CHECK_MODULES([libmpdclient], [libmpdclient >= 2.4])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
