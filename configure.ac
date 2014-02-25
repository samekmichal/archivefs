dnl Process this file with autoconf to produce a configure script.
AC_INIT(configure.ac)
AC_CONFIG_MACRO_DIR([m4])

AC_SYS_LARGEFILE
AC_DEFINE([NDEBUG],[],[Define to omit all debuging sections])

AM_INIT_AUTOMAKE(afs, 20122004)
AM_SILENT_RULES

LT_INIT([disable-static])

dnl *** check for programs ************************************************************
# check for c compiler
AC_PROG_CXX
AC_PROG_LIBTOOL
AC_PROG_RANLIB

BOOST_REQUIRE([])

dnl *** check for libraries ***********************************************************
AC_CHECK_LIB(fuse, fuse_main, , AC_MSG_ERROR(Cannot find libfuse - is it installed?.))
AC_CHECK_LIB(zip, zip_open, , AC_MSG_ERROR(Cannot find libzip - is it installed?.))
AC_CHECK_LIB(tar, tar_open, , AC_MSG_ERROR(Cannot find libtar - is it installed?.))
AC_CHECK_LIB(z, gzdopen, , AC_MSG_ERROR(Cannot find zlib - is it installed?.))
AC_CHECK_LIB(isofs, iso_init, , AC_MSG_ERROR(Cannot find libisofs - is it installed?.))
AC_CHECK_LIB(magic, magic_open, , )

dnl *** compiler characteristic *******************************************************
# Use -Wall
CXXFLAGS="$CXXFLAGS -O0 -ggdb3 -W -Wall -Wno-unused-result -pedantic -D_FILE_OFFSET_BITS=64"

dnl *** package settings **************************************************************
# Checks for config file.
AC_CHECK_FILE(/usr/share/file/magic, [
   AC_DEFINE(FILE_MAGIC, "/usr/share/file/magic",
      [Define to the full filename of the `magic' file from libmagic.])], [
AC_CHECK_FILE(/usr/share/misc/file/magic, [
   AC_DEFINE(FILE_MAGIC, "/usr/share/misc/file/magic",
      [Define to the full filename of the `magic' file from libmagic.])], [
AC_CHECK_FILE(/usr/share/misc/magic, [
   AC_DEFINE(FILE_MAGIC, "/usr/share/misc/magic",
      [Define to the full filename of the `magic' file from libmagic.])], [
AC_DEFINE(FILE_MAGIC, "/usr/share/file/magic",
   [Define to the full filename of the `magic' file from libmagic.])])])])

dnl *** Autoconf output - Makefiles  **************************************************
AC_OUTPUT([Makefile src/Makefile drivers/Makefile])
