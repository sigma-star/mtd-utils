# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [2.1.6] - 2023-08-30
### Added
 - flash_speed: Measure read while write latency
 - Support `mtd:<num>` syntax for several tools

### Fixed
 - flashcp: check for lseek errors
 - flashcp: fix buffer overflow
 - flashcp: verify data in `--partition`
 - flashcp: abort on `--partition` and `--erase-all`
 - flashcp: correct casting for percent display
 - mtdpart: document partition of size 0
 - mkfs.ubifs: Non-terminated string related failure with option selinux
 - nandtest: handle nand devices larger than 4G
 - Fix printf format specifiers for 64 bit integer types

### Changed
 - flashcp: merge duplicate write code paths
 - flashcp: merge duplicate MEMERASE code paths
 - flashcp: simplify logging

## [2.1.5] - 2022-10-07
### Fixed
 - mkfs.jffs2: spelling of `--compression-mode` parameter in help text
 - ubinfo: `--vol_id` return code for absent volume id
 - nandflipbits: fix corrupted oob
 - libmtd: do not ignore non-zero eraseblock size when `MTD_NO_ERASE` is set
 - jffs2reader: warning about unaligned pointer
 - tests: Remove unused linux/fs.h header from includes
 - fix test bashism
 - nanddump: fix writing big images on 32bit machines
 - nor-utils: fix memory leak

### Changed
 - flash_otp_dump: make offset optional
 - nandwrite: warn about writing 0xff blocks

## [2.1.4] - 2022-10-07
### Added
 - ubiscan debugging and statistics utility

### Fixed
 - Some mtd-tests erroneously using sub-pages instead of the full page size
 - Buffer overrun in fectest
 - Build failures due to missing jffs2 kernel header

## [2.1.3] - 2021-07-25
### Added
 - flashcp: Add new function that copy only different blocks
 - flash_erase: Add flash erase chip
 - Add flash_otp_erase
 - Add an ubifs mount helper
 - Add nandflipbits tool

### Fixed
 - mkfs.ubifs: Fix runtime assertions when running without crypto
 - Use AC_SYS_LARGEFILE
 - Fix test binary installation
 - libmtd: avoid divide by zero
 - ubihealthd: fix UBIFS build dependency
 - mkfs.ubifs: remove `OPENSSL_no_config()`
 - misc-utils: Add fectest to build system
 - mkfs.ubifs: Fix build with SELinux
 - Fix typos found by Debian's lintian tool
 - Fix jffs2 build if zlib or lzo headers are not in default paths

## [2.1.2] - 2020-07-13
### Added
 - flashcp: Add option `-A`, `--erase-all`
 - mtd-utils: add optional offset parameter to `flash_otp_dump`
 - ubi-utils: Implement a ubihealthd
 - mkfs.ubifs: Add authentication support

### Fixed
 - mtd-utils: Fix return value of ubiformat
 - ubiupdatevol: Prevent null pointer dereference
 - libubigen: remove unnecessary include
 - libubi: remove private kernel header from includes
 - mkfs.ubifs: fscrypt: bail from encrypt_block if gen_essiv_salt fails
 - mkfs.ubifs: abort add_directory if readdir fails
 - mkfs.ubifs: close file descriptor in add_file error path
 - mkfs.ubifs: don't leak copied command line arguments
 - mkfs.ubifs: free derived fscrypt context in add_directory error paths
 - mkfs.ubifs: don't leak hastable iterators
 - mkfs.ubifs: don't leak temporary buffers
 - mkfs.ubifs: propperly cleanup in ALL interpret_table_entry error paths
 - mkfs.jffs2: don't leak temporary buffer if readlink fails
 - libmtd: don't leak temporary buffers
 - ftl_check: don't leak temporary buffers
 - ftl_format: don't leak temporary buffers
 - ubiformat: don't leak file descriptors
 - nanddump: don't leak copied command line arguments
 - mtd_debug: cleanup error handling in flash_to_file
 - jittertest: fix error check for open system call
 - fs-tests: don't leak temporary buffers
 - mtd-utils: Fix printf format specifiers with the wrong type
 - mtd-utils: Fix potential negative arguments passed to close(2)
 - mtd-utils: Fix various TOCTOU issues
 - mtd-utils: Fix some simple cases of uninitialized value reads
 - mtd-utils: Fix wrong argument to sizeof in nanddump
 - mtd-utils: Fix "are we really at EOF" test logic in libubi read_data
 - mtd-utils: Fix potentially unterminated strings
 - mtd-utils: Add checks to code that copies strings into fixed sized buffers
 - mkfs.ubifs: fix broken build if fscrtyp is disabled

### Changed
 - ubifs-media: Update to Linux-5.3-rc3

## [2.1.1] - 2019-07-21
### Added
 - mkfs.ubifs: Add ZSTD compression

### Fixed
 - ubiformat: Dont ignore sequence number CLI option
 - mkfs.ubifs: fix build without openssl
 - mkfs.ubifs: fix regression when trying to store device special files
 - mkfs.ubifs: fix description of favor_lzo
 - unittests/test_lib: Include proper header for `_IOC_SIZE`
 - unittests/libmtd_test: Include fcntl header
 - unittests: Define the use of `_GNU_SOURCE`
 - ubinize: Exit with non-zero exit code on error.
 - mtd-tests: nandbiterrs: Fix issue that just insert error at bit 7
 - ubi-tests: ubi_mkvol_request: Fully initialize `struct ubi_mkvol_request req`
 - ubi-tests: io_read: Filter invalid offset before `lseek` in `io_read` test
 - ubi-tests: mkvol test: Checks return value `ENOSPC` for `ubi_mkvol`
 - ubi-tests: fm_param: Replace `fm_auto` with `fm_autoconvert`

## [2.1.0] - 2019-03-19
### Added
 - mkfs.ubifs: Implement support for file system encryption
 - mkfs.ubifs: Implement selinux labelling support
 - ubinize: add support for skipping CRC check of a static volume when opening
 - ubimkvol: add support for skipping CRC check of a static volume when opening
 - Add lsmtd program

### Fixed
 - update various kernel headers
 - Instead of doing preprocessor magic, just output off_t as long long
 - fix verification percent display in flashcp
 - mkfs.ubifs: fix double free
 - mkfs.ubifs: Fix xattr nlink value
 - ubinize: avoid to create two `UBI_LAYOUT_VOLUME_ID` volume
 - common.h: fix prompt function
 - libmtd: don't print an error message for devices without ecc support
 - io_paral: Fix error handling of update_volume()
 - ubimkvol: Initialize req to zero to make sure no flags are set by default
 - libubi: add volume flags to `ubi_mkvol_request`
 - mkfs.ubifs: add_xattr is not depending on host XATTR support
 - Revert "Return correct error number in ubi_get_vol_info1" which
   introduced a regression.
 - make sure pkg-config is installed in configure script
 - ubiformat: process command line arguments before handling file arguments

### Changed
 - ubiformat: remove no-volume-table option

## [2.0.2] - 2018-04-16
### Added
 - libmtd: Add support to access OOB available size
 - mkfs.ubifs: Allow root entry in device table

### Fixed
 - Fix unit-test header and file paths for out of tree builds
 - Fix unit test mockup for oobavail sysfs file
 - misc-utils: flash_erase: Fix Jffs2 type flash erase problem
 - libmtd_legacy: Fix some function description mismatches
 - mtd-utils: ubifs: fix typo in without_lzo definition
 - mtd: tests: check erase block count in page test
 - mtd: unittests: Stop testing stat() calls
 - mtd: unittests: Decode arg size from ioctl request
 - mtd: unittests: Use proper unsigned long type for ioctl requests
 - mtd: tests: Fix check on ebcnt in nandpagetest
 - ubi-utils: ubicrc32: process command line arguments first
 - nandbiterrs: Fix erroneous counter increment in for loop body
 - jittertest: Use the appropriate versions of abs()
 - Mark or fix switch cases that fall through
 - mkfs.ubifs: ignore EOPNOTSUPP when listing extended attributes
 - misc-utils: initialize "ip" in docfdisk to NULL
 - mkfs.ubifs: Apply squash-uids to the root node

### Changed
 - ubi-utils: ubiformat.c: convert to integer arithmetic
 - mtd-utils: common.c: convert to integer arithmetic
 - Run unit test programs through "make check"
 - Enable more compiler warning flags, fix warnings
 - Add no-return attribute to usage() style functions
 - Remove self-assignments of unused paramters
 - tests: checkfs: Remove unused source file from makefiles
 - ubi-tests: io_update: fix missleading indentation
 - Add ctags files to .gitignore
 - libscan: fix a comment typo in libscan.h
 - libmtd: fix a comment typo in dev_node2num

## [2.0.1] - 2017-08-24
### Added
 - nandbiterrs: Add Erased Pages Bit Flip Test
 - mkfs.ubifs: Add support for symlinks in device table
 - nanddump: Add `--skip-bad-blocks-to-start` option
 - nandwrite: Add `--skip-bad-blocks-to-start` option

### Fixed
 - common: Always terminate with failure status if command line options
   are unknown or missing
 - common: Fix format specifier definitions for `off_t` and `loff_t`
 - common: More consistent exit codes
 - libmtd: Fix error status if MTD is not present on the system
 - libubi: Add klibc specific fixes for `ioctl`
 - libubi: Fix error status in `ubi_get_vol_info1` for non-existing volumes
 - misc-utils: Support jffs2 flash-erase for large OOB (>32b)
 - mkfs.jffs2: Add missing header inclusions required for build with musl
 - mkfs.ubifs: Fix alignment trap triggered by NEON instructions
 - mkfs.ubifs: Fix uuid.h path
 - mkfs.ubifs: Replace broken ubifs_assert with libc assert
 - nandbiterrs: Actually get the new ECC bit flip count before comparing stats
 - nandpagetest: Improved argument sanity checking
 - nandwrite: Fix bad block skipping
 - nandwrite: Improved argument sanity checking
 - ubinfo: Improved argument sanity checking
 - ubi-tests: Replace variable-length array with `malloc`
 - ubi-tests: Support up to 64k NAND page size

### Changed
 - build-system: Enable compiler warnings
 - build-system: Restructure autoconf dependency checking
 - common: Add const modifier to read only strings and string constants
 - common: Eliminate warnings about missing prototypes
 - common: Get rid of rpmatch usage
 - common: Remove README.udev from ubi-tests extra dist
 - common: Remove unused variables and functions
 - common: Silence warnings about unused arguments
 - flashcp: Drop custom defines for `EXIT_FAILURE` and `EXIT_SUCCESS`
 - libiniparser: remove unused function needing float
 - libmissing: Use autoconf header detection directly
 - libubi: Remove `UDEV_SETTLE_HACK`
 - misc-utils: Move libfec to common public header & library directory
 - nandwrite: replace erase loop with mtd_erase_multi
 - serve_image: Use PRIdoff_t as format specifier.
 - ubi-tests: Speedup io_paral by using rand_r()
 - ubirename: Fix spelling

## [2.0.0] - 2016-12-22
### Added
 - libmissing with stubs for functions not present in libraries like musl
 - unittests for libmtd and libubi
 - port most kernel space mtd test modules to userspace
 - mkfs.ubifs: extended attribute support
 - ubinize: Move lengthy help text to a man page
 - nandwrite: Add skip-all-ff-pages option
 - flash_{un,}lock: support for MEMISLOCKED
 - nandtest: support hex/dec/oct for `--offset` and `--length`

### Fixed
 - common: Fix 'unchecked return code' warnings
 - common: Fix PRI{x,d}off definitions for x86_64 platform
 - common: include sys/sysmacros.h for major/minor/makedev
 - common: fix wrong format specifiers on mips32
 - libmtd: Fix uninitialized buffers
 - libmtd: Eliminate warnings about implicit non-const casting
 - libmtd: Fix return status in mtd_torture test function
 - libmtd: mtd_read: Take the buffer offset into account when reading
 - mkfs.ubifs: use gid from table instead 2x uid
 - mkfs.ubifs: fix compiler warning for WITHOUT_LZO
 - mkfs.ubifs: fix build when WITHOUT_LZO is set
 - mkfs.ubifs: correct the size of nnode in memset
 - mkfs.jffs2: initialize lzo decompression buffer size
 - mkfs.jffs2: Fix scanf() formatstring for modern C version
 - nanddump: check write function result for errors
 - nanddump: write requested length only
 - flash_{un,}lock: don't allow "last byte + 1"
 - flash_{un,}lock: improve strtol() error handling
 - ubinize: Always return error code (at least -1) in case of an error
 - recv_image: fix build warnings w/newer glibc & _BSD_SOURCE
 - serve_image: use proper POSIX_C_SOURCE value
 - flashcp: Use %llu to print filestat.st_size
 - mtd_debug: check amount of data read.
 - fs-tests: integrity: don't include header <bits/stdio_lim.h>
 - tests: Fix endian issue with CRC generation algorithm
 - make_a_release.sh: fix MTD spelling
 - Fix packaging of unit test files
 - Correct casting for final status report in flashcp

### Changed
 - autotools based build system
 - complete restructuring of the source tree
 - cleanup of some utilities
 - removal of some very old, unused or duplicated files from the source tree
 - libmtd: removal of very old, completely unused and broken functions
 - nandwrite: Factor out buffer checking code
