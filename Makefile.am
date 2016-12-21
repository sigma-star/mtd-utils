ACLOCAL_AMFLAGS = -I m4

AM_CPPFLAGS = -D_GNU_SOURCE -I$(top_srcdir)/include

if WITHOUT_XATTR
AM_CPPFLAGS += -DWITHOUT_XATTR
endif

if WITHOUT_LZO
AM_CPPFLAGS += -DWITHOUT_LZO
endif

if HAVE_EXECINFO
AM_CPPFLAGS += -DHAVE_EXECINFO
endif

sbin_PROGRAMS =
sbin_SCRIPTS =
noinst_LIBRARIES =
noinst_PROGRAMS =
noinst_SCRIPTS =
pkglibexec_PROGRAMS =
pkglibexec_SCRIPTS =
dist_man1_MANS =
dist_man8_MANS =

EXTRA_DIST =

GLOBAL_HEADER = \
	include

GLOBAL_EXTRA = \
	COPYING

EXTRA_DIST += $(GLOBAL_HEADER) $(GLOBAL_EXTRA)

include lib/Makemodule.am
include ubi-utils/Makemodule.am
include misc-utils/Makemodule.am
include nand-utils/Makemodule.am
include nor-utils/Makemodule.am

if BUILD_UBIFS
include ubifs-utils/Makemodule.am
endif

if BUILD_JFFSX
include jffsX-utils/Makemodule.am
endif

if BUILD_TESTS
include tests/ubi-tests/Makemodule.am
include tests/jittertest/Makemodule.am
include tests/checkfs/Makemodule.am
include tests/fs-tests/Makemodule.am
include tests/mtd-tests/Makemodule.am
endif

if UNIT_TESTS
include tests/unittests/Makemodule.am
endif
