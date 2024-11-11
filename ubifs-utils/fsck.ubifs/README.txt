 fsck.ubifs
 ==========
 The fsck.ubifs can check and repair the UBIFS image on a given UBI volume, it
 could fix inconsistent UBIFS image(which is corrupted by hardware exceptions
 or UBIFS realization bugs) and makes filesystem become consistent.


 Manuals
 -------
 There are four modes for fsck.ubifs:
 1. normal mode(no options): Check the filesystem, ask user whether to fix the
    problem as long as inconsistent data is found during fs checking.
 2. safe mode(-a option): Check and automatic safely repair the filesystem, if
    there are any data dropping operations needed by fixing, fsck will fail.
 3. danger mode(-y option): Answer 'yes' to all questions. There are two sub
    modes:
    a) Default submode(no options): Check and automatic repair the filesystem
       according to TNC, data dropping will be reported. If TNC/master/log is
       corrupted, fsck will fail.
    b) rebuild submode(-b option): Check and automatic forcibly repair the
       filesystem, turns to rebuild filesystem if TNC/master/log is corrupted.
       Always make fsck successful.
 4. check mode(-n option): Make no changes to the filesystem, only check the
    filesystem. This mode doesn't check space, because unclean LEBs cannot be
    rewritten in read-only mode.

 The exit code returned by fsck.ubifs is compatible with FSCK(8), which is the
 sum of the following conditions:
 0	- No errors
 1	- File system errors corrected
 2	- System should be rebooted
 4	- File system errors left uncorrected
 8	- Operational error
 16	- Usage or syntax error
 32	- Fsck canceled by user request
 128	- Shared library error


 Designment
 ----------
 There are 2 working modes for fsck: rebuild mode and non-rebuild mode. The main
 idea is that construct all files by scanning the entire filesystem, then check
 the consistency of metadata(file meta information, space statistics, etc.)
 according to the files. The file(xattr is treated as a file) is organized as:
   file tree(rbtree, inum indexed)
            /      \
         file1   file2
         /   \
      file3 file4
 file {
     inode node // each file has 1 inode node
     dentry (sub rb_tree, sqnum indexed)
     // '/' has no dentries, otherwise at least 1 dentry is required.
     trun node // the newest one truncation node
     data (sub rb_tree, block number indexed)
     // Each file may have 0 or many data nodes
     xattrs (sub rb_tree, inum indexed)
     // Each file may have 0 or many xattr files
 }

 Step 0. Both two modes need to read the superblock firstly, fsck fails if
         superblock is corrupted, because fsck has no idea about the location
         of each area(master, log, main, etc.) when the layout is lost.

 A. Rebuild mode:
 Step 1. Scan nodes(inode node/dentry node/data node/truncation node) from all
         LEBs.
         a) Corrupted LEBs(eg. garbage data, corrupted empty space) are dropped
            during scanning.
         b) Corrupted nodes(eg. incorrect crc, bad inode size, bad dentry name
            length, etc.) are dropped during scanning.
         c) Valid inode nodes(nlink > 0) and dentry nodes(inum != 0) are put
            into two valid trees(valid_inos & valid_dents) separately.
         d) Deleted inode nodes (nlink is 0) and deleted dentry nodes(inum is 0)
            are put into two deleted trees(del_inos & del_dents) separately.
         e) Other nodes(data nodes/truncation node) are put into corresponding
            file, if the file doesn't exist, insert a new file into the file
            tree.
 Step 2. Traverse nodes from deleted trees, remove inode nodes and dentry nodes
         with smaller sqnum from valid trees. valid_inos - del_inos = left_inos,
         valid_dents - del_dents = left_dents.
         This step handles the deleting case, for example, file A is deleted,
         deleted inode node and deleted dentry node are written, if we ignore
         the deleted nodes, file A can be recovered after rebuilding because
         undeleted inode node and undeleted dentry node can be scanned. There's
         an exception, if deleted inode node and deleted dentry node are
         reclaimed(by gc) after deletion, file A is recovered. So deleted data
         or files could be recovered by rebuild mode.
 Step 3. Traverse left_inos and left_dents, insert inode node and dentry nodes
         into the corresponding file.
 Step 4. Traverse all files, drop invalid files, move xattr files into the
         corresponding host file's subtree. Invalid files such as:
         a) File has no inode node or inode nlink is zero
         b) Non-consistent file types between inode node and dentry nodes
         c) File has no dentry nodes(excepts '/')
         d) Encrypted file has no xattr information
         e) Non regular file has data nodes
         f) Directory/xattr file has more than one dentries
         g) Xattr file has no host inode, or the host inode is a xattr
         h) Non-xattr file's parent is not a directory
         i) etc.
 Step 5. Extract reachable directory entries tree. Make sure that all files can
         be searched from '/', unreachable file is deleted. Since all xattr
         files are attached to the corresponding host file, only non-xattr
         files should be checked. Luckily, directory file only has one dentry,
         the reachable checking of a dentry becomes easy. Traverse all
         dentries for each file, check whether the dentry is reachable, if not,
         remove dentry from the file. If the file has no dentries, the file is
         unreachable.
 Step 6. Correct the file information. Traverse all files and calculate
         information(nlink, size, xattr_cnt, etc.) for each file just like
         check_leaf(in linux kernel) does, correct the inode node based on the
         calculated information.
 Step 7. Record used LEBs. Traverse all files'(including effective nodes from
         deletion trees in step 2) position, after this step fsck knows which
         LEB is empty.
 Step 8. Re-write data. Read data from LEB and write back data, make sure that
         all LEB is ended with empty data(0xFF). It will prevent failed gc
         scanning in the next mounting.
 Step 9. Build TNC. Construct TNC according to all files' nodes, just like mkfs
         does(refer to add_to_index in mkfs), then write TNC(refer to
         write_index in mkfs) on flash. (If there are no files, create a new
         root dir file.)
 Step 10.Build LPT. Construct LPT according to all nodes' position and length,
         just like mkfs does, then write LPT(refer to write_lpt) on flash.
 Step 11.Clean up log area and orphan area. Log area and orphan area can be
         erased.
 Step 12.Write master node. Since all meta areas are ready, master node can be
         updated.

 B. Non-rebuild mode:
 Step 1. Read master & init lpt.
         a) Scan master nodes failed or master node is invalid (which is not
            caused by invalid space statistics), danger mode with rebuild_fs and
            normal mode with 'yes' answer will turn to rebuild mode, other modes
            will exit. Fsck cannot find the right TNC/LPT if the master node is
            invalid, which affects subsequent steps, so this problem must be
            fixed.
         b) Invalid space statistics in master node, set %FR_LPT_INCORRECT for
            for lpt status and ignore the error.
         c) LPT node is corrupted, set %FR_LPT_CORRUPTED for lpt status and
            ignore the error.
 Step 2. Replay journal.
         I. Scan log LEBs to get all buds.
            a) Nodes in log LEBs are invalid/corrupted, danger mode with
               rebuild_fs and normal mode with 'yes' answer will turn to rebuild
               mode, other modes will exit. Corrupted log LEB could fail
               ubifs_consolidate_log, which may lead to commit failure by out of
	       space in the log area, so this problem must be fixed.
         II. Scan bud LEBs to get all nodes.
             a) Nodes in bud LEBs are invalid/corrupted, danger mode and normal
                mode with 'yes' answer will drop bud LEB and set
                %FR_LPT_INCORRECT for lpt status, other modes will exit.
                Corrupted LEB will make gc failed, so this problem must be
                fixed.
	 III. Record isize into size tree according to data/truncation/inode
              nodes.
         IV. Apply nodes to TNC & LPT, update property for bud LEBs.
             a) Corrupted/Invalid node searched from TNC, skip node and set
                %FR_LPT_INCORRECT in lpt status for danger mode and normal mode
                with 'yes' answer, other modes will exit. The space statistics
                depend on a valid TNC, so this problem must be fixed.
             b) Corrupted/Invalid index node read from TNC, danger mode with
                rebuild_fs and normal mode with 'yes' answer will turn to
                rebuild filesystem, other modes will exit. The space statistics
                depend on a valid TNC, so this problem must be fixed.
             c) Corrupted/Invalid lpt node, Set %FR_LPT_CORRUPTED for lpt status
                and ignore the error.
             d) Incorrect LEB property: Set %FR_LPT_INCORRECT for lpt status and
                ignore the error.
             e) If lpt status is not empty, skip updating lpt, because incorrect
                LEB property could trigger assertion failure in ubifs_change_lp.
 Step 3. Handle orphan nodes.
         I. Scan orphan LEB to get all orphan nodes.
            a) Corrupted/Invalid orphan node: danger mode and normal mode with
               'yes' answer will drop orphan LEB, other modes will exit.
               Corrupted orphan area could lead to mounting/committing failure,
               so this problem must be fixed.
         II. Parse orphan node, find the original inode for each inum.
             a) Corrupted/Invalid node searched from TNC, skip node for danger
                mode and normal mode with 'yes' answer, other modes will exit.
             b) Corrupted/Invalid index node read from TNC, danger mode with
                rebuild_fs and normal mode with 'yes' answer will turn to
                rebuild filesystem, other modes will exit. The space statistics
                depend on a valid TNC, so this problem must be fixed.
         III. Remove inode for each inum, update TNC & LPT.
              a) Corrupted/Invalid node searched from TNC, skip node for danger
                 mode and normal mode with 'yes' answer, other modes will exit.
              b) Corrupted/Invalid index node read from TNC, danger mode with
                 rebuild_fs and normal mode with 'yes' answer will turn to
                 rebuild filesystem, other modes will exit. The space statistics
                 depend on a valid TNC, so this problem must be fixed.
              c) Corrupted/Invalid lpt node, Set %FR_LPT_CORRUPTED for lpt
                 status and ignore the error.
              d) Incorrect LEB property: Set %FR_LPT_INCORRECT for lpt status
                 and ignore the error.
              e) If lpt status is not empty, skip updating lpt, because
                 incorrect LEB property could trigger assertion failure in
                 ubifs_change_lp.
 Step 4. Consolidate log area.
         a) Corrupted data in log LEBs, danger mode with rebuild_fs and normal
            mode with 'yes' answer will turn to rebuild filesystem, other modes
            will exit. It could make commit failed by out of space in log area,
            so this problem must be fixed.
 Step 5. Recover isize.
         I. Traverse size tree, lookup corresponding inode from TNC.
            a) Corrupted/Invalid node searched from TNC, skip node for danger
               mode and normal mode with 'yes' answer, other modes will exit.
            b) Corrupted/Invalid index node read from TNC, danger mode with
               rebuild_fs and normal mode with 'yes' answer will turn to
               rebuild filesystem, other modes will exit. The space statistics
               depend on a valid TNC, so this problem must be fixed.
         II. Update isize for inode. Keep <inum, isize> in size tree for check
             mode, remove <inum, isize> from the size tree and update inode
             node in place for other modes.
 Step 6. Traverse TNC and construct files.
         I. Traverse TNC, check whether the leaf node is valid, remove invalid
            nodes, construct file for valid node and insert the file into the
            file tree.
            a) Corrupted/Invalid node searched from TNC, remove corresponding
               TNC branch for danger mode and normal mode with 'yes' answer,
               other modes will exit. The space statistics depend on a valid
               TNC, so this problem must be fixed.
            b) Corrupted/Invalid index node read from TNC, danger mode with
               rebuild_fs and normal mode with 'yes' answer will turn to
               rebuild filesystem, other modes will exit. The space statistics
               depend on a valid TNC, so this problem must be fixed.
         II. Scan all LEBs(contain TNC) for non check mode(unclean LEBs cannot
             be fixed in read-only mode, so scanning may fail in check mode,
             then space statistics won't be checked in check mode), remove TNC
             branch which points to corrupted LEB.
             a) Corrupted data is found by scanning. If the current node is
                index node, danger mode with rebuild_fs and normal mode with
                'yes' answer will turn to rebuild filesystem, other modes will
                exit; If the current node is non-index node, danger mode and
                normal mode with 'yes' answer will remove all TNC branches which
                point to the corrupted LEB, other modes will exit. The space
                statistics depend on valid LEB scanning, so this problem must
                be fixed.
             b) LEB contains both index and non-index nodes, danger mode with
                rebuild_fs and normal mode with 'yes' answer will turn to
                rebuild filesystem, other modes will exit. Invalid LEB will make
                gc failed, so this problem must be fixed.
 Step 7. Update files' size for check mode. Update files' size according to the
          size tree for check mode.
 Step 8. Check and handle invalid files. Similar to rebuild mode, but the
         methods of handling are different:
         a) Move unattached(file has no dentries) regular file into disconnected
            list for safe mode, danger mode and normal mode with 'yes' answer,
            let subsequent steps to handle them with lost+found. Other modes
            will exit. Disconnected file affects the result of calculated
            information(which will be used in subsequent steps) for its' parent
            file(eg. nlink, size), so this problem must be fixed.
         b) Make file type be consistent between inode, detries and data nodes
            by deleting dentries or data nodes, for danger mode and normal mode
            with 'yes' answer, other modes will exit.
         c) Delete file for other invalid cases(eg. file has no inode) in
            danger mode and normal mode with 'yes' answer, other modes will
            exit.
 Step 9. Extract reachable directory entries tree. Similar to rebuild mode, but
         the methods of handling are different:
         a) Remove unreachable dentry for danger mode and normal mode with 'yes'
            answer, other modes will exit. Unreachable dentry affects the
            calculated information(which will be used in subsequent steps) for
            its' file(eg. nlink), so this problem must be fixed.
         b) Delete unreachable non-regular file for danger mode and normal mode
            with 'yes' answer, other modes will exit. Unreachable file affects
            the calculated information(which will be used in subsequent steps)
            for its' parent file(eg. nlink, size), so this problem must be
            fixed.
         c) Move unreachable regular file into disconnected list for safe mode,
            danger mode and normal mode with 'yes' answer, let subsequent steps
            to handle them with lost+found. Other modes will exit. Disconnected
            file affects the calculated information(which will be used in
            subsequent steps) for its' parent file(eg. nlink, size), so this
            problem must be fixed.
 Step 10.Correct the file information. Similar to rebuild mode, but the methods
         of handling are different:
         a) Correct the file information for safe mode, danger mode and normal
            mode with 'yes' answer, other modes will exit. Incorrect file
            information affects the new creations(which will be used in handling
            lost+found), so this problem must be fixed.
 Step 11.Check whether the TNC is empty. Empty TNC is equal to corrupted TNC,
         which means that zero child count for root znode. If TNC is empty(All
         nodes are invalid and are deleted from TNC), turn to rebuild mode for
         danger mode with rebuild_fs and normal mode with 'yes' answer, other
         modes will exit.
 Step 12.Check and correct the space statistics.
         I. Exit for check mode, if %FR_LPT_CORRUPTED or %FR_LPT_INCORRECT is
            set in lpt status, the exit code should have %FSCK_UNCORRECTED.
         II. Check lpt status, if %FR_LPT_CORRUPTED is set in lpt status, normal
             mode with 'no' answer will exit, other modes will rebuild lpt. New
             creations could be done in subsequent steps, which depends on
             correct space statistics, so this problem must be fixed.
         III. Traverse LPT nodes, check the correctness of nnode and pnode,
              compare LEB scanning result with LEB properties.
              a) LPT node is corrupted, normal mode with 'no' answer will exit,
                 rebuild lpt for other modes. New creations could be done in
                 subsequent steps, which depends on the correct space
                 statistics, so this problem must be fixed.
              b) Incorrect nnode/pnode, normal mode with 'no' answer will exit,
                 other modes will correct the nnode/pnode. New creations could
                 be done in subsequent steps, which depends on correct space
                 statistics, so this problem must be fixed.
              c) Inconsistent comparing result, normal mode with 'no' answer
                 will exit, other modes will correct the space statistics. New
                 creations could be done in subsequent steps, which depends on
                 correct space statistics, so this problem must be fixed.
         IV. Compare LPT area scanning result with lprops table information.
             a) LPT area is corrupted, normal mode with 'no' answer will exit,
                rebuild lpt for other modes. Commit could fail in doing LPT gc
                caused by scanning corrupted data, so this problem must be
                fixed.
             b) Inconsistent comparing result, normal mode with 'no' answer
                will exit, other modes will correct the lprops table
                information. Commit could fail in writing LPT with %ENOSPC
                return code caused by incorrect space statistics in the LPT
                area, so this problem must be fixed.
 Step 13.Do commit, commit problem fixing modifications to disk. The index size
         checking depends on this step.
 Step 14.Check and correct the index size. Check and correct the index size by
         traversing TNC just like dbg_check_idx_size does. This step should be
         executed after first committing, because 'c->calc_idx_sz' can be
         changed in 'ubifs_tnc_start_commit' and the initial value of
         'c->calc_idx_sz' read from the disk is untrusted. Correct the index
         size for safe mode, danger mode and normal mode with 'yes' answer,
         other modes will exit. New creations could be done in subsequent steps,
         which depends on the correct index size, so this problem must be fixed.
 Step 15.Check and create the root dir. Check whether the root dir exists,
         create a new one if it is not found, for safe mode, danger mode and
         normal mode with 'yes' answer, other modes will exit. Mounting depends
         on the root dir, so this problem must be fixed.
 Step 16.Check and create the lost+found.
         I. If the root dir is encrypted, set lost+found as invalid. Because it
            is impossible to check whether the lost+found exists in an encrypted
            directory.
         II. Search the lost+found under root dir.
             a) Found a lost+found, lost+found is a non-encrypted directory, set
                lost+found as valid, otherwise set lost+found as invalid.
             b) Not found the lost+found, create a new one. If creation is
                failed by %ENOSPC, set lost+found as invalid.
 Step 17.Handle each file from the disconnected list.
         I. If lost+found is invalid, delete file for danger mode and normal
            mode with 'yes' answer, other modes will skip and set the exit code
            with %FSCK_UNCORRECTED.
        II. If lost+found is valid, link disconnected file under lost+found
            directory with the name of the corresponding inode number
            (INO_<inum>_<index>, index(starts from 0) is used to handle the
             conflicted names).
             a) Fails in handling conflicted file names, delete file for danger
                mode and normal mode with 'yes' answer, other modes will skip
                and set the exit code with %FSCK_UNCORRECTED.
             b) Fails in linking caused by %ENOSPC, delete file for danger mode
                and normal mode with 'yes' answer, other modes will skip and set
                the exit code with %FSCK_UNCORRECTED.
 Step 18.Do final commit, commit problem fixing modifications to disk and clear
         %UBIFS_MST_DIRTY flag for master node.


 Advantage
 ---------
 1. Can be used for any UBIFS image, fsck has nothing to do with kernel version.
 2. Fsck is tolerant with power-cut, fsck will always succeed in a certain mode
    without changing mode even power-cut happens in checking and repairing. In
    other words, fsck won't let UBIFS image become worse in abnormal situations.
 3. It is compatible with FSCK(8), the exit code returned by fsck.ubifs is same
    as FSCK, the command options used by fsck are supported in fsck.ubifs too.
 4. The UBIFS image can be fixed as long as the super block is not corrupted.
 5. Encrypted UBIFS image is supported, because dentry name and data content of
    file are not necessary for fsck.


 Limitations
 -----------
 1. UBIFS image file is not supported(Not like ext4). The UBIFS image file is
    not equal to UBI volume, empty LEBs are not included in image file, so UBIFS
    cannot allocate empty space when file recovering is needed. Another reason
    is that atomic LEB changing is not supported by image file.
 2. Authenticated UBIFS image is not supported, UBIFS metadata(TNC/LPT) parsing
    depends on the authentication key which is not supported in fsck options.


 Authors
 -------
 Zhihao Cheng <chengzhihao1@huawei.com>
 Zhang Yi <yi.zhang@huawei.com>
 Xiang Yang <xiangyang3@huawei.com>
 Huang Xiaojia <huangxiaojia2@huawei.com>
