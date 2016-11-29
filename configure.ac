AC_PREREQ([2.60])

m4_define([RELEASE], 1.5.2)

AC_INIT([mtd-utils], [RELEASE], [linux-mtd@lists.infradead.org], mtd-utils)


AC_ARG_ENABLE([unit-tests],
	[AS_HELP_STRING([--enable-unit-tests], [Compile unit test programs])],
	[case "${enableval}" in
	yes) AM_CONDITIONAL([UNIT_TESTS], [true]) ;;
	no)  AM_CONDITIONAL([UNIT_TESTS], [false]) ;;
	*) AC_MSG_ERROR([bad value ${enableval} for --enable-unit-tests]) ;;
	esac],
	[AM_CONDITIONAL([UNIT_TESTS], [false])])

AM_COND_IF([UNIT_TESTS], [: ${CFLAGS=""}], [])


AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign subdir-objects dist-bzip2])
AM_SILENT_RULES([yes])
AC_PROG_LIBTOOL
AC_DISABLE_STATIC
AC_PROG_CC
AC_PROG_INSTALL


AC_SEARCH_LIBS([clock_gettime],[rt posix4])
AC_CHECK_FUNCS([clock_gettime])

AC_ARG_ENABLE([tests],
	[AS_HELP_STRING([--disable-tests], [Compile test programs])],
	[case "${enableval}" in
	yes) AM_CONDITIONAL([BUILD_TESTS], [true]) ;;
	no)  AM_CONDITIONAL([BUILD_TESTS], [false]) ;;
	*) AC_MSG_ERROR([bad value ${enableval} for --disable-tests]) ;;
	esac],
	[AM_CONDITIONAL([BUILD_TESTS], [true])])

AM_COND_IF([BUILD_TESTS],
	[AX_PTHREAD([], [AC_MSG_ERROR([pthread missing])])])


AC_ARG_ENABLE([install-tests],
	[AS_HELP_STRING([--enable-install-tests], [Install test programs])],
	[case "${enableval}" in
	yes) AM_CONDITIONAL([INSTALL_TESTS], [true]) ;;
	no)  AM_CONDITIONAL([INSTALL_TESTS], [false]) ;;
	*) AC_MSG_ERROR([bad value ${enableval} for --enable-install-tests]) ;;
	esac],
	[AM_CONDITIONAL([INSTALL_TESTS], [false])])


AM_COND_IF([INSTALL_TESTS],
	[AC_SUBST(testbindir, ["\$libexecpath"])],
	[AC_SUBST(testbindir, ["\".\""])])


AC_CONFIG_FILES([tests/fs-tests/fs_help_all.sh
	tests/fs-tests/fs_run_all.sh
	tests/fs-tests/stress/fs_stress00.sh
	tests/fs-tests/stress/fs_stress01.sh
	tests/ubi-tests/runubitests.sh
	tests/ubi-tests/ubi-stress-test.sh])


AC_ARG_WITH([xattr],
	[AS_HELP_STRING([--without-xattr],
		[Disable support forextended file attributes])],
	[case "${withval}" in
	yes) AM_CONDITIONAL([WITHOUT_XATTR], [false]) ;;
	no)  AM_CONDITIONAL([WITHOUT_XATTR], [true]) ;;
	*) AC_MSG_ERROR([bad value ${withval} for --without-xattr]) ;;
	esac],
	[AM_CONDITIONAL([WITHOUT_XATTR], [false])])

AC_CHECK_HEADER(sys/xattr.h, [], [AM_CONDITIONAL([WITHOUT_XATTR], [true])])
AC_CHECK_HEADER(sys/acl.h, [], [AM_CONDITIONAL([WITHOUT_XATTR], [true])])


AC_ARG_WITH([lzo],
	[AS_HELP_STRING([--without-lzo], [Disable support for LZO compression])],
	[case "${withval}" in
	yes) AM_CONDITIONAL([WITHOUT_LZO], [false]) ;;
	no)  AM_CONDITIONAL([WITHOUT_LZO], [true]) ;;
	*) AC_MSG_ERROR([bad value ${withval} for --without-lzo]) ;;
	esac],
	[AM_CONDITIONAL([WITHOUT_LZO], [false])])


AC_CHECK_HEADERS([execinfo.h], [execinfo_found=yes])
AM_CONDITIONAL([HAVE_EXECINFO], [test "x$execinfo_found" == "xyes"])

PKG_CHECK_MODULES(ZLIB, [ zlib ])
PKG_CHECK_MODULES(UUID, [ uuid ])

AM_COND_IF([UNIT_TESTS], [PKG_CHECK_MODULES(CMOCKA, [ cmocka ])], [])

AM_COND_IF([WITHOUT_LZO], [], [
	have_lzo="yes"
	AC_ARG_VAR([LZO_CFLAGS], [C compiler flags for lzo])
	AC_ARG_VAR([LZO_LIBS], [linker flags for lzo])
	AC_CHECK_LIB([lzo2], [lzo1x_1_15_compress], [LZO_LIBS="-llzo2"],
		[AC_CHECK_LIB([lzo],[lzo1x_1_15_compress],[LZO_LIBS="-llzo"],
			[have_lzo="no"]
		)]
	)
	test "${have_lzo}" != "yes" && AC_MSG_ERROR([lzo missing])
])

AC_OUTPUT([Makefile])
