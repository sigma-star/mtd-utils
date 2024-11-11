
 ubifs_tools tests
 ==================

 There are seven testcases for fsck.ubifs on encryption/non-encryption
 situations:
   1) authentication_refuse: Currently authenticated UBIFS image is not
      supported for fsck.ubifs, check whether fsck.ubifs can refuse the
      authenticated UBIFS image.
   2) random_corrupted_fsck: Inject random corruption on UBIFS image
      by writing random data on kinds of mtd devices (eg. nand, nor),
      check the consistency of UBIFS after fsck.
      This testcase simulate random bad UBIFS image caused by hardware
      exceptions(eg. ecc uncorrectable, unwritten), and makes sure that
      fsck.ubifs could make UBIFS be consistent after repairing UBIFS
      image.
   3) cycle_corrupted_fsck_fault_inject: Inject memory/io fault while
      doing fsck for corrupted UBIFS images.
      This testcase mainly checks whether fsck.ubifs has problems (eg.
      UAF, null-ptr-def, etc.) in random error paths. Besides, it
      provides a similar way to simulate powercut during fsck, and
      checks whether the fsck.ubifs can fix an UBIFS image after many
      rounds interrupted by kinds of errors.
   4) cycle_powercut_mount_fsck: Inject powercut while doing fsstress
      on mounted UBIFS, check the consistency of UBIFS after fsck.
      This testscase mainly makes sure that fsck.ubifs can make UBIFS
      image be consistent in common stress cases and powercut cases.
   5) powercut_fsck_mount: Inject powercut while doing fsstress on
      mounted UBIFS for kinds of flashes (eg. nand, nor).
      This testcase mainly makes sure that fsck.ubifs can make UBIFS
      image be consistent on different flashes (eg. nand, nor). Because
      the min_io_size of nor flash is 1, the UBIFS image on nor flash
      will be different from nand flash after doing powercut, so we need
      make sure fsck.ubifs can handle these two types of flash.
   6) cycle_mount_fsck_check: Do fsstress and fsck ubifs image, make
      sure all files(and their data) are not lost after fsck.
      This testcase mainly checks whether fsck.ubifs could corrupt the
      filesystem content in common case.
   7) fsck_bad_image: For kinds of inconsistent UBIFS images(which
      can simulate corruptions caused by some potentional UBIFS bug), check
      the result of fsck.
      This testcase mainly checks whether the behavior is in expected after
      repairing specific inconsistent UBIFS image. There is no debugfs tools
      (for example: debugfs[ext4], xfs_db) for UBIFS, so no way to inject
      precise corruption into UBIFS image, we have to prepare inconsistent
      UBIFS images in advance like e2fsprogs[1] does. (Goto [2] to see how to
      generate inconsistent UBIFS images).
      Original UBIFS image content:
      /
      ├── corrupt_file (xattr - user.corrupt:123, 2K data)
      ├── dir
      │   ├── block_dev
      │   ├── char_dev
      │   ├── dir
      │   └── file (content: '123')
      ├── hardl_corrupt_file => corrupt_file
      └── softl_corrupt_file -> corrupt_file
      Here's a descriptons of the various test images:
      =========================================================================
            image         |     Description     |          expectancy
      -------------------------------------------------------------------------
      good                | good image contains | fsck success, fs content is
                          | kinds of files.     | not changed.
      -------------------------------------------------------------------------
      sb_fanout           | invalid fanout in   | fsck failed.
                          | superblock.         |
      -------------------------------------------------------------------------
      sb_fmt_version      | invalid fmt_version | fsck failed.
                          | in superblock.      |
      -------------------------------------------------------------------------
      sb_leb_size         | invalid leb_size in | fsck failed.
                          | superblock.         |
      -------------------------------------------------------------------------
      sb_log_lebs         | invalid log lebs in | fsck failed.
                          | superblock.         |
      -------------------------------------------------------------------------
      sb_min_io_size      | invalid min_io_size | fsck failed.
                          | in superblock.      |
      -------------------------------------------------------------------------
      master_highest_inum | invalid highest_inum| fsck success, fs content is
                          | in master nodes.    | not changed.
      -------------------------------------------------------------------------
      master_lpt          | bad lpt pos in      | fsck success, fs content is
                          | master nodes.       | not changed.
      -------------------------------------------------------------------------
      master_tnc          | bad tnc pos in      | fsck success, fs content is
                          | master nodes.       | not changed.
      -------------------------------------------------------------------------
      master_total_dead   | bad total_dead in   | fsck success, fs content is
                          | master nodes.       | not changed.
      -------------------------------------------------------------------------
      master_total_dirty  | bad total_dirty in  | fsck success, fs content is
                          | master nodes.       | not changed.
      -------------------------------------------------------------------------
      master_total_free   | bad total_free in   | fsck success, fs content is
                          | master nodes.       | not changed.
      -------------------------------------------------------------------------
      journal_log         | corrupted log area. | fsck success, fs content is
                          |                     | not changed.
      -------------------------------------------------------------------------
      journal_bud         | corrupted bud area. | fsck success, file data is
                          |                     | lost.
      -------------------------------------------------------------------------
      orphan_node         | bad orphan node.    | fsck success, file is
                          |                     | deleted as expected.
      -------------------------------------------------------------------------
      lpt_dirty           | bad dirty in pnode. | fsck success, fs content is
                          |                     | not changed.
      -------------------------------------------------------------------------
      lpt_flags           | bad flags in pnode  | fsck success, fs content is
                          | (eg. index).        | not changed.
      -------------------------------------------------------------------------
      lpt_free            | bad free in pnode.  | fsck success, fs content is
                          |                     | not changed.
      -------------------------------------------------------------------------
      lpt_pos             | bad pos in nnode.   | fsck success, fs content is
                          |                     | not changed.
      -------------------------------------------------------------------------
      ltab_dirty          | bad dirty in lprops | fsck success, fs content is
                          | table.              | not changed.
      -------------------------------------------------------------------------
      ltab_free           | bad free in lprops  | fsck success, fs content is
                          | table.              | not changed.
      -------------------------------------------------------------------------
      index_size          | bad index size in   | fsck success, fs content is
                          | master nodes.       | not changed.
      -------------------------------------------------------------------------
      tnc_lv0_key         | bad key in lv0      | fsck success, fs content is
                          | znode.              | not changed.
      -------------------------------------------------------------------------
      tnc_lv0_len         | bad len in lv0      | fsck success, fs content is
                          | znode.              | not changed.
      -------------------------------------------------------------------------
      tnc_lv0_pos         | bad pos in lv0      | fsck success, fs content is
                          | znode.              | not changed.
      -------------------------------------------------------------------------
      tnc_noleaf_key      | bad key in non-leaf | fsck success, fs content is
                          | znode.              | not changed.
      -------------------------------------------------------------------------
      tnc_noleaf_len      | bad len in non-leaf | fsck success, fs content is
                          | znode.              | not changed.
      -------------------------------------------------------------------------
      tnc_noleaf_pos      | bad pos in non-leaf | fsck success, fs content is
                          | znode.              | not changed.
      -------------------------------------------------------------------------
      corrupted_data_leb  | corrupted data leb. | fsck success, partial data of
                          |                     | file is lost.
      -------------------------------------------------------------------------
      corrupted_idx_leb   | corrupted index leb.| fsck success, fs content is
                          |                     | not changed.
      -------------------------------------------------------------------------
      inode_data          | bad data node.      | fsck success, file content
                          |                     | is changed, other files are
                          |                     | not changed.
      -------------------------------------------------------------------------
      inode_mode          | bad inode mode for  | fsck success, file is
                          | file.               | dropped, other files are not
                          |                     | changed.
      -------------------------------------------------------------------------
      inode_nlink         | wrong nlink for     | fsck success, nlink is
                          | file.               | corrected, fs content is not
                          |                     | changed.
      -------------------------------------------------------------------------
      inode_size          | wrong inode size    | fsck success, inode size is
                          | for file.           | corrected, fs content is not
                          |                     | changed.
      -------------------------------------------------------------------------
      inode_xcnt          | wrong inode         | fsck success, xattr_cnt is
                          | xattr_cnt for file. | corrected, fs content is not
                          |                     | changed.
      -------------------------------------------------------------------------
      soft_link_inode_mode| bad inode mode for  | fsck success, soft link
                          | solf link file.     | file is dropped, other files
                          |                     | are not changed.
      -------------------------------------------------------------------------
      soft_link_data_len  | bad inode data_len  | fsck success, soft link
                          | for solt link file. | file is dropped, other files
                          |                     | are not changed.
      -------------------------------------------------------------------------
      dentry_key          | bad dentry key for  | fsck success, dentry is
                          | file.               | removed, other files are
                          |                     | not changed.
      -------------------------------------------------------------------------
      dentry_nlen         | inconsistent nlen   | fsck success, dentry is
                          | and name in dentry  | removed, other files are
                          | for file.           | not changed.
      -------------------------------------------------------------------------
      dentry_type         | inconsistent type   | fsck success, dentry is
                          | between dentry and  | removed, other files are
                          | inode for file.     | not changed.
      -------------------------------------------------------------------------
      xinode_flags        | lost UBIFS_XATTR_FL | fsck success, xattr is
                          | in xattr inode      | removed, other files are
                          | flags for file.     | not changed.
      -------------------------------------------------------------------------
      xinode_key          | bad xattr inode key | fsck success, xattr is
                          | for file.           | removed, other files are
                          |                     | not changed.
      -------------------------------------------------------------------------
      xinode_mode         | bad xattr inode     | fsck success, xattr is
                          | mode for file.      | removed, other files are
                          |                     | not changed.
      -------------------------------------------------------------------------
      xentry_key          | bad xattr entry key | fsck success, xattr is
                          | for file.           | removed, other files are
                          |                     | not changed.
      -------------------------------------------------------------------------
      xentry_nlen         | inconsistent nlen   | fsck success, xattr is
                          | and name in xattr   | removed, other files are
                          | entry for file.     | not changed.
      -------------------------------------------------------------------------
      xentry_type         | inconsistent type   | fsck success, xattr is
                          | between xattr entry | removed, other files are
                          | and xattr inode for | not changed.
                          | file.               |
      -------------------------------------------------------------------------
      xent_host           | the xattr's host    | fsck success, file, hard
                          | is a xattr too, the | link and soft link are
                          | flag of corrupt_file| dropped, other files are
                          | inode is modified.  | not changed.
      -------------------------------------------------------------------------
      dir_many_dentry     | dir has too many    | fsck success, hard link is
                          | dentries, the dentry| dropped, other files are not
                          | of hard link is     | changed.
                          | modified.           |
      -------------------------------------------------------------------------
      dir_lost            | bad dentry for dir. | fsck success, the 'file' is
                          |                     | recovered under lost+found,
                          |                     | left files under dir are
                          |                     | removed, other files are not
                          |                     | changed.
      -------------------------------------------------------------------------
      dir_lost_duplicated | bad inode for dir,  | fsck success, the 'file' is
                          | there is a file     | recovered with INO_<inum>_1
                          | under lost+found,   | under lost+found, left files
                          | which named with the| under dir are removed, other
                          | inum of the 'file'. | files are not changed.
      -------------------------------------------------------------------------
      dir_lost_not_recover| bad inode for dir,  | fsck success, all files
                          | lost+found is a     | under dir are removed,
                          | regular file and    | other files are not changed.
                          | exists under root   |
                          | dir.                |
      -------------------------------------------------------------------------
      root_dir            | bad '/'.            | fsck success, create new
                          |                     | root dir('/'). All regular
                          |                     | files are reocovered under
                          |                     | lost+found, other files are
                          |                     | removed.
      -------------------------------------------------------------------------
      empty_tnc           | all files have bad  | fsck success, fs content
                          | inode.              | becomes empty.
      =========================================================================

 There is one testcase for mkfs.ubifs on encryption/non-encryption
 situations:
 1) build_fs_from_dir: Initialize UBIFS image from a given directory, then
    check whether the fs content in mounted UBIFS is consistent with the
    original directory. Both UBI volume and file are chosen as storage
    mediums to test. This testcase mainly ensures that mkfs.ubifs can
    format an UBIFS image as user expected.

 Dependence
 ----------
 kernel configs:
   CONFIG_MTD_NAND_NANDSIM=m
   CONFIG_MTD_MTDRAM=m
   CONFIG_MTD_UBI=m
   CONFIG_UBIFS_FS=m
   CONFIG_UBIFS_FS_XATTR=y
   CONFIG_UBIFS_FS_AUTHENTICATION=y
   CONFIG_FS_ENCRYPTION=y
   CONFIG_FAILSLAB=y
   CONFIG_FAIL_PAGE_ALLOC=y

 tools:
   fsstress		[3][4]
   keyctl		[5]
   fscryptctl		[6]
   setfattr/getfattr	[7]

 Running
 -------

 Please build and install mtd-utils first.
 Run single case:
   cd $INSTALL_DIR/libexec/mtd-utils
   ./powercut_fsck_mount.sh
   ./random_corrupted_fsck.sh
   ./cycle_mount_fsck_check.sh
   ./build_fs_from_dir.sh
 Run all cases: sh $INSTALL_DIR/libexec/mtd-utils/ubifs_tools_run_all.sh

 References
 ----------

 [1] https://git.kernel.org/pub/scm/fs/ext2/e2fsprogs.git/tree/tests/README
 [2] https://bugzilla.kernel.org/show_bug.cgi?id=218924
 [3] https://git.kernel.org/pub/scm/fs/xfs/xfstests-dev.git/tree/ltp/fsstress.c
 [4] https://github.com/linux-test-project/ltp/blob/master/testcases/kernel/fs/fsstress/fsstress.c
 [5] https://github.com/torvalds/linux/blob/master/security/keys/keyctl.c
 [6] https://github.com/google/fscryptctl
 [7] https://github.com/philips/attr/tree/master
