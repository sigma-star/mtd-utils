AC_PREREQ([2.60])

AC_INIT([mtd-utils], 1.5.2, [linux-mtd@lists.infradead.org], mtd-utils)
AM_INIT_AUTOMAKE([foreign subdir-objects dist-bzip2])
AM_SILENT_RULES([yes])
AC_PROG_LIBTOOL
AC_DISABLE_STATIC
AC_PROG_CC
AC_PROG_INSTALL

AC_ARG_WITH([xattr],
	[AS_HELP_STRING([--without-xattr],
		[Disable support forextended file attributes])],
	[case "${withval}" in
	yes) AM_CONDITIONAL([WITHOUT_XATTR], [false]) ;;
	no)  AM_CONDITIONAL([WITHOUT_XATTR], [true]) ;;
	*) AC_MSG_ERROR([bad value ${withval} for --without-xattr]) ;;
	esac],
	[AM_CONDITIONAL([WITHOUT_XATTR], [false])])


AC_ARG_WITH([lzo],
	[AS_HELP_STRING([--without-lzo], [Disable support for LZO compression])],
	[case "${withval}" in
	yes) AM_CONDITIONAL([WITHOUT_LZO], [false]) ;;
	no)  AM_CONDITIONAL([WITHOUT_LZO], [true]) ;;
	*) AC_MSG_ERROR([bad value ${withval} for --without-lzo]) ;;
	esac],
	[AM_CONDITIONAL([WITHOUT_LZO], [false])])


PKG_CHECK_MODULES(ZLIB, [ zlib ])
PKG_CHECK_MODULES(UUID, [ uuid ])

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
