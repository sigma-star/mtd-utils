AM_CPPFLAGS = -D_GNU_SOURCE -I$(top_srcdir)/include

if WITHOUT_XATTR
AM_CPPFLAGS += -DWITHOUT_XATTR
endif

if WITHOUT_LZO
AM_CPPFLAGS += -DWITHOUT_LZO
endif

sbin_PROGRAMS =
sbin_SCRIPTS =
noinst_LIBRARIES =
noinst_PROGRAMS =
dist_man1_MANS =

EXTRA_DIST =

GLOBAL_HEADER = \
	include

GLOBAL_EXTRA = \
	COPYING

EXTRA_DIST += $(GLOBAL_HEADER) $(GLOBAL_EXTRA)

include lib/Makemodule.am
include ubi-utils/Makemodule.am
include ubifs-utils/Makemodule.am
include misc-utils/Makemodule.am
include nand-utils/Makemodule.am
include nor-utils/Makemodule.am
include jffsX-utils/Makemodule.am

SUBDIRS = tests/ubi-tests
