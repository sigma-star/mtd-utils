ACLOCAL_AMFLAGS = -I m4

AM_CPPFLAGS = $(WARN_CFLAGS) -D_GNU_SOURCE -std=gnu99 -I$(top_srcdir)/include \
		-include $(top_builddir)/include/config.h

if WITHOUT_XATTR
AM_CPPFLAGS += -DWITHOUT_XATTR
endif

if WITHOUT_LZO
AM_CPPFLAGS += -DWITHOUT_LZO
endif

if WITHOUT_ZSTD
AM_CPPFLAGS += -DWITHOUT_ZSTD
endif

if WITH_SELINUX
AM_CPPFLAGS += -DWITH_SELINUX
endif

if WITH_CRYPTO
AM_CPPFLAGS += -DWITH_CRYPTO
endif

if WITH_GETRANDOM
AM_CPPFLAGS += -DWITH_GETRANDOM
endif

sbin_PROGRAMS =
sbin_SCRIPTS =
check_PROGRAMS =
noinst_LIBRARIES =
noinst_PROGRAMS =
noinst_SCRIPTS =
dist_man1_MANS =
dist_man8_MANS =

testdir = @TESTBINDIR@
test_PROGRAMS =
test_SCRIPTS =

TESTS =
EXTRA_DIST = COPYING

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
