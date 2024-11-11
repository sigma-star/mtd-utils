// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation.
 *
 * Authors: Artem Bityutskiy (Битюцкий Артём)
 *          Adrian Hunter
 */

/*
 * This file implements UBIFS journal.
 *
 * The journal consists of 2 parts - the log and bud LEBs. The log has fixed
 * length and position, while a bud logical eraseblock is any LEB in the main
 * area. Buds contain file system data - data nodes, inode nodes, etc. The log
 * contains only references to buds and some other stuff like commit
 * start node. The idea is that when we commit the journal, we do
 * not copy the data, the buds just become indexed. Since after the commit the
 * nodes in bud eraseblocks become leaf nodes of the file system index tree, we
 * use term "bud". Analogy is obvious, bud eraseblocks contain nodes which will
 * become leafs in the future.
 *
 * The journal is multi-headed because we want to write data to the journal as
 * optimally as possible. It is nice to have nodes belonging to the same inode
 * in one LEB, so we may write data owned by different inodes to different
 * journal heads, although at present only one data head is used.
 *
 * For recovery reasons, the base head contains all inode nodes, all directory
 * entry nodes and all truncate nodes. This means that the other heads contain
 * only data nodes.
 *
 * Bud LEBs may be half-indexed. For example, if the bud was not full at the
 * time of commit, the bud is retained to continue to be used in the journal,
 * even though the "front" of the LEB is now indexed. In that case, the log
 * reference contains the offset where the bud starts for the purposes of the
 * journal.
 *
 * The journal size has to be limited, because the larger is the journal, the
 * longer it takes to mount UBIFS (scanning the journal) and the more memory it
 * takes (indexing in the TNC).
 *
 * All the journal write operations like 'ubifs_jnl_update()' here, which write
 * multiple UBIFS nodes to the journal at one go, are atomic with respect to
 * unclean reboots. Should the unclean reboot happen, the recovery code drops
 * all the nodes.
 */

#include "bitops.h"
#include "kmem.h"
#include "ubifs.h"
#include "defs.h"
#include "debug.h"
#include "key.h"
#include "misc.h"

/**
 * zero_ino_node_unused - zero out unused fields of an on-flash inode node.
 * @ino: the inode to zero out
 */
static inline void zero_ino_node_unused(struct ubifs_ino_node *ino)
{
	memset(ino->padding1, 0, 4);
	memset(ino->padding2, 0, 26);
}

/**
 * zero_dent_node_unused - zero out unused fields of an on-flash directory
 *                         entry node.
 * @dent: the directory entry to zero out
 */
static inline void zero_dent_node_unused(struct ubifs_dent_node *dent)
{
	dent->padding1 = 0;
}

static void ubifs_add_auth_dirt(struct ubifs_info *c, int lnum)
{
	if (ubifs_authenticated(c))
		ubifs_add_dirt(c, lnum, ubifs_auth_node_sz(c));
}

/**
 * reserve_space - reserve space in the journal.
 * @c: UBIFS file-system description object
 * @jhead: journal head number
 * @len: node length
 *
 * This function reserves space in journal head @head. If the reservation
 * succeeded, the journal head stays locked and later has to be unlocked using
 * 'release_head()'. Returns zero in case of success, %-EAGAIN if commit has to
 * be done, and other negative error codes in case of other failures.
 */
static int reserve_space(struct ubifs_info *c, int jhead, int len)
{
	int err = 0, err1, retries = 0, avail, lnum, offs, squeeze;
	struct ubifs_wbuf *wbuf = &c->jheads[jhead].wbuf;

	/*
	 * Typically, the base head has smaller nodes written to it, so it is
	 * better to try to allocate space at the ends of eraseblocks. This is
	 * what the squeeze parameter does.
	 */
	ubifs_assert(c, !c->ro_media && !c->ro_mount);
	squeeze = (jhead == BASEHD);
again:
	mutex_lock_nested(&wbuf->io_mutex, wbuf->jhead);

	if (c->ro_error) {
		err = -EROFS;
		goto out_unlock;
	}

	avail = c->leb_size - wbuf->offs - wbuf->used;
	if (wbuf->lnum != -1 && avail >= len)
		return 0;

	/*
	 * Write buffer wasn't seek'ed or there is no enough space - look for an
	 * LEB with some empty space.
	 */
	lnum = ubifs_find_free_space(c, len, &offs, squeeze);
	if (lnum >= 0)
		goto out;

	err = lnum;
	if (err != -ENOSPC)
		goto out_unlock;

	/*
	 * No free space, we have to run garbage collector to make
	 * some. But the write-buffer mutex has to be unlocked because
	 * GC also takes it.
	 */
	dbg_jnl("no free space in jhead %s, run GC", dbg_jhead(jhead));
	mutex_unlock(&wbuf->io_mutex);

	lnum = ubifs_garbage_collect(c, 0);
	if (lnum < 0) {
		err = lnum;
		if (err != -ENOSPC)
			return err;

		/*
		 * GC could not make a free LEB. But someone else may
		 * have allocated new bud for this journal head,
		 * because we dropped @wbuf->io_mutex, so try once
		 * again.
		 */
		dbg_jnl("GC couldn't make a free LEB for jhead %s",
			dbg_jhead(jhead));
		if (retries++ < 2) {
			dbg_jnl("retry (%d)", retries);
			goto again;
		}

		dbg_jnl("return -ENOSPC");
		return err;
	}

	mutex_lock_nested(&wbuf->io_mutex, wbuf->jhead);
	dbg_jnl("got LEB %d for jhead %s", lnum, dbg_jhead(jhead));
	avail = c->leb_size - wbuf->offs - wbuf->used;

	if (wbuf->lnum != -1 && avail >= len) {
		/*
		 * Someone else has switched the journal head and we have
		 * enough space now. This happens when more than one process is
		 * trying to write to the same journal head at the same time.
		 */
		dbg_jnl("return LEB %d back, already have LEB %d:%d",
			lnum, wbuf->lnum, wbuf->offs + wbuf->used);
		err = ubifs_return_leb(c, lnum);
		if (err)
			goto out_unlock;
		return 0;
	}

	offs = 0;

out:
	/*
	 * Make sure we synchronize the write-buffer before we add the new bud
	 * to the log. Otherwise we may have a power cut after the log
	 * reference node for the last bud (@lnum) is written but before the
	 * write-buffer data are written to the next-to-last bud
	 * (@wbuf->lnum). And the effect would be that the recovery would see
	 * that there is corruption in the next-to-last bud.
	 */
	err = ubifs_wbuf_sync_nolock(wbuf);
	if (err)
		goto out_return;
	err = ubifs_add_bud_to_log(c, jhead, lnum, offs);
	if (err)
		goto out_return;
	err = ubifs_wbuf_seek_nolock(wbuf, lnum, offs);
	if (err)
		goto out_unlock;

	return 0;

out_unlock:
	mutex_unlock(&wbuf->io_mutex);
	return err;

out_return:
	/* An error occurred and the LEB has to be returned to lprops */
	ubifs_assert(c, err < 0);
	err1 = ubifs_return_leb(c, lnum);
	if (err1 && err == -EAGAIN)
		/*
		 * Return original error code only if it is not %-EAGAIN,
		 * which is not really an error. Otherwise, return the error
		 * code of 'ubifs_return_leb()'.
		 */
		err = err1;
	mutex_unlock(&wbuf->io_mutex);
	return err;
}

static int ubifs_hash_nodes(struct ubifs_info *c, void *node,
			     int len, struct shash_desc *hash)
{
	int auth_node_size = ubifs_auth_node_sz(c);
	int err;

	while (1) {
		const struct ubifs_ch *ch = node;
		int nodelen = le32_to_cpu(ch->len);

		ubifs_assert(c, len >= auth_node_size);

		if (len == auth_node_size)
			break;

		ubifs_assert(c, len > nodelen);
		ubifs_assert(c, ch->magic == cpu_to_le32(UBIFS_NODE_MAGIC));

		err = ubifs_shash_update(c, hash, (void *)node, nodelen);
		if (err)
			return err;

		node += ALIGN(nodelen, 8);
		len -= ALIGN(nodelen, 8);
	}

	return ubifs_prepare_auth_node(c, node, hash);
}

/**
 * write_head - write data to a journal head.
 * @c: UBIFS file-system description object
 * @jhead: journal head
 * @buf: buffer to write
 * @len: length to write
 * @lnum: LEB number written is returned here
 * @offs: offset written is returned here
 * @sync: non-zero if the write-buffer has to by synchronized
 *
 * This function writes data to the reserved space of journal head @jhead.
 * Returns zero in case of success and a negative error code in case of
 * failure.
 */
static int write_head(struct ubifs_info *c, int jhead, void *buf, int len,
		      int *lnum, int *offs, int sync)
{
	int err;
	struct ubifs_wbuf *wbuf = &c->jheads[jhead].wbuf;

	ubifs_assert(c, jhead != GCHD);

	*lnum = c->jheads[jhead].wbuf.lnum;
	*offs = c->jheads[jhead].wbuf.offs + c->jheads[jhead].wbuf.used;
	dbg_jnl("jhead %s, LEB %d:%d, len %d",
		dbg_jhead(jhead), *lnum, *offs, len);

	if (ubifs_authenticated(c)) {
		err = ubifs_hash_nodes(c, buf, len, c->jheads[jhead].log_hash);
		if (err)
			return err;
	}

	err = ubifs_wbuf_write_nolock(wbuf, buf, len);
	if (err)
		return err;
	if (sync)
		err = ubifs_wbuf_sync_nolock(wbuf);
	return err;
}

/**
 * make_reservation - reserve journal space.
 * @c: UBIFS file-system description object
 * @jhead: journal head
 * @len: how many bytes to reserve
 *
 * This function makes space reservation in journal head @jhead. The function
 * takes the commit lock and locks the journal head, and the caller has to
 * unlock the head and finish the reservation with 'finish_reservation()'.
 * Returns zero in case of success and a negative error code in case of
 * failure.
 *
 * Note, the journal head may be unlocked as soon as the data is written, while
 * the commit lock has to be released after the data has been added to the
 * TNC.
 */
static int make_reservation(struct ubifs_info *c, int jhead, int len)
{
	int err, cmt_retries = 0, nospc_retries = 0;

again:
	down_read(&c->commit_sem);
	err = reserve_space(c, jhead, len);
	if (!err)
		/* c->commit_sem will get released via finish_reservation(). */
		return 0;
	up_read(&c->commit_sem);

	if (err == -ENOSPC) {
		/*
		 * GC could not make any progress. We should try to commit
		 * once because it could make some dirty space and GC would
		 * make progress, so make the error -EAGAIN so that the below
		 * will commit and re-try.
		 */
		if (nospc_retries++ < 2) {
			dbg_jnl("no space, retry");
			err = -EAGAIN;
		}

		/*
		 * This means that the budgeting is incorrect. We always have
		 * to be able to write to the media, because all operations are
		 * budgeted. Deletions are not budgeted, though, but we reserve
		 * an extra LEB for them.
		 */
	}

	if (err != -EAGAIN)
		goto out;

	/*
	 * -EAGAIN means that the journal is full or too large, or the above
	 * code wants to do one commit. Do this and re-try.
	 */
	if (cmt_retries > 128) {
		/*
		 * This should not happen unless the journal size limitations
		 * are too tough.
		 */
		ubifs_err(c, "stuck in space allocation");
		err = -ENOSPC;
		goto out;
	} else if (cmt_retries > 32)
		ubifs_warn(c, "too many space allocation re-tries (%d)",
			   cmt_retries);

	dbg_jnl("-EAGAIN, commit and retry (retried %d times)",
		cmt_retries);
	cmt_retries += 1;

	err = ubifs_run_commit(c);
	if (err)
		return err;
	goto again;

out:
	ubifs_err(c, "cannot reserve %d bytes in jhead %d, error %d",
		  len, jhead, err);
	if (err == -ENOSPC) {
		/* This are some budgeting problems, print useful information */
		down_write(&c->commit_sem);
		dump_stack();
		ubifs_dump_budg(c, &c->bi);
		ubifs_dump_lprops(c);
		cmt_retries = dbg_check_lprops(c);
		up_write(&c->commit_sem);
	}
	return err;
}

/**
 * release_head - release a journal head.
 * @c: UBIFS file-system description object
 * @jhead: journal head
 *
 * This function releases journal head @jhead which was locked by
 * the 'make_reservation()' function. It has to be called after each successful
 * 'make_reservation()' invocation.
 */
static inline void release_head(struct ubifs_info *c, int jhead)
{
	mutex_unlock(&c->jheads[jhead].wbuf.io_mutex);
}

/**
 * finish_reservation - finish a reservation.
 * @c: UBIFS file-system description object
 *
 * This function finishes journal space reservation. It must be called after
 * 'make_reservation()'.
 */
static void finish_reservation(struct ubifs_info *c)
{
	up_read(&c->commit_sem);
}

/**
 * ubifs_get_dent_type - translate VFS inode mode to UBIFS directory entry type.
 * @mode: inode mode
 */
int ubifs_get_dent_type(int mode)
{
	switch (mode & S_IFMT) {
	case S_IFREG:
		return UBIFS_ITYPE_REG;
	case S_IFDIR:
		return UBIFS_ITYPE_DIR;
	case S_IFLNK:
		return UBIFS_ITYPE_LNK;
	case S_IFBLK:
		return UBIFS_ITYPE_BLK;
	case S_IFCHR:
		return UBIFS_ITYPE_CHR;
	case S_IFIFO:
		return UBIFS_ITYPE_FIFO;
	case S_IFSOCK:
		return UBIFS_ITYPE_SOCK;
	default:
		BUG();
	}
	return 0;
}

static void set_dent_cookie(struct ubifs_info *c, struct ubifs_dent_node *dent)
{
	if (c->double_hash)
		dent->cookie = (__force __le32) get_random_u32();
	else
		dent->cookie = 0;
}

/**
 * pack_inode - pack an ubifs inode node.
 * @c: UBIFS file-system description object
 * @ino: buffer in which to pack inode node
 * @ui: ubifs inode to pack
 * @last: indicates the last node of the group
 */
static void pack_inode(struct ubifs_info *c, struct ubifs_ino_node *ino,
		       const struct ubifs_inode *ui, int last)
{
	const struct inode *inode = &ui->vfs_inode;
	int data_len = 0, last_reference = !inode->nlink;

	ino->ch.node_type = UBIFS_INO_NODE;
	ino_key_init_flash(c, &ino->key, inode->inum);
	ino->creat_sqnum = cpu_to_le64(ui->creat_sqnum);
	ino->atime_sec  = cpu_to_le64(inode->atime_sec);
	ino->atime_nsec = cpu_to_le32(inode->atime_nsec);
	ino->ctime_sec  = cpu_to_le64(inode->ctime_sec);
	ino->ctime_nsec = cpu_to_le32(inode->ctime_nsec);
	ino->mtime_sec  = cpu_to_le64(inode->mtime_sec);
	ino->mtime_nsec = cpu_to_le32(inode->mtime_nsec);
	ino->uid   = cpu_to_le32(inode->uid);
	ino->gid   = cpu_to_le32(inode->gid);
	ino->mode  = cpu_to_le32(inode->mode);
	ino->flags = cpu_to_le32(ui->flags);
	ino->size  = cpu_to_le64(ui->ui_size);
	ino->nlink = cpu_to_le32(inode->nlink);
	ino->compr_type  = cpu_to_le16(ui->compr_type);
	ino->data_len    = cpu_to_le32(ui->data_len);
	ino->xattr_cnt   = cpu_to_le32(ui->xattr_cnt);
	ino->xattr_size  = cpu_to_le32(ui->xattr_size);
	ino->xattr_names = cpu_to_le32(ui->xattr_names);
	zero_ino_node_unused(ino);

	/*
	 * Drop the attached data if this is a deletion inode, the data is not
	 * needed anymore.
	 */
	if (!last_reference) {
		memcpy(ino->data, ui->data, ui->data_len);
		data_len = ui->data_len;
	}

	ubifs_prep_grp_node(c, ino, UBIFS_INO_NODE_SZ + data_len, last);
}

/**
 * ubifs_jnl_update_file - update file.
 * @c: UBIFS file-system description object
 * @dir_ui: parent ubifs inode
 * @nm: directory entry name
 * @ui: ubifs inode to update
 *
 * This function updates an file by writing a directory entry node, the inode
 * node itself, and the parent directory inode node to the journal. If the
 * @dir_ui and @nm are NULL, only update @ui.
 *
 * Returns zero on success. In case of failure, a negative error code is
 * returned.
 */
int ubifs_jnl_update_file(struct ubifs_info *c,
			  const struct ubifs_inode *dir_ui,
			  const struct fscrypt_name *nm,
			  const struct ubifs_inode *ui)
{
	const struct inode *dir = NULL, *inode = &ui->vfs_inode;
	int err, dlen, ilen, len, lnum, ino_offs, dent_offs, dir_ilen;
	int aligned_dlen, aligned_ilen;
	struct ubifs_dent_node *dent;
	struct ubifs_ino_node *ino;
	union ubifs_key dent_key, ino_key;
	u8 hash_dent[UBIFS_HASH_ARR_SZ];
	u8 hash_ino[UBIFS_HASH_ARR_SZ];
	u8 hash_ino_dir[UBIFS_HASH_ARR_SZ];

	ubifs_assert(c, (!nm && !dir_ui) || (nm && dir_ui));
	ubifs_assert(c, inode->nlink != 0);

	ilen = UBIFS_INO_NODE_SZ + ui->data_len;

	if (nm)
		dlen = UBIFS_DENT_NODE_SZ + fname_len(nm) + 1;
	else
		dlen = 0;

	if (dir_ui) {
		dir = &dir_ui->vfs_inode;
		ubifs_assert(c, dir->nlink != 0);
		dir_ilen = UBIFS_INO_NODE_SZ + dir_ui->data_len;
	} else
		dir_ilen = 0;

	aligned_dlen = ALIGN(dlen, 8);
	aligned_ilen = ALIGN(ilen, 8);
	len = aligned_dlen + aligned_ilen + dir_ilen;
	if (ubifs_authenticated(c))
		len += ALIGN(dir_ilen, 8) + ubifs_auth_node_sz(c);

	dent = kzalloc(len, GFP_NOFS);
	if (!dent)
		return -ENOMEM;

	/* Make reservation before allocating sequence numbers */
	err = make_reservation(c, BASEHD, len);
	if (err)
		goto out_free;

	if (nm) {
		dent->ch.node_type = UBIFS_DENT_NODE;
		dent_key_init(c, &dent_key, dir->inum, nm);

		key_write(c, &dent_key, dent->key);
		dent->inum = cpu_to_le64(inode->inum);
		dent->type = ubifs_get_dent_type(inode->mode);
		dent->nlen = cpu_to_le16(fname_len(nm));
		memcpy(dent->name, fname_name(nm), fname_len(nm));
		dent->name[fname_len(nm)] = '\0';
		set_dent_cookie(c, dent);

		zero_dent_node_unused(dent);
		ubifs_prep_grp_node(c, dent, dlen, 0);
		err = ubifs_node_calc_hash(c, dent, hash_dent);
		if (err)
			goto out_release;
	}

	ino = (void *)dent + aligned_dlen;
	pack_inode(c, ino, ui, dir_ui == NULL ? 1 : 0);
	err = ubifs_node_calc_hash(c, ino, hash_ino);
	if (err)
		goto out_release;

	if (dir_ui) {
		ino = (void *)ino + aligned_ilen;
		pack_inode(c, ino, dir_ui, 1);
		err = ubifs_node_calc_hash(c, ino, hash_ino_dir);
		if (err)
			goto out_release;
	}

	err = write_head(c, BASEHD, dent, len, &lnum, &dent_offs, 0);
	if (err)
		goto out_release;
	release_head(c, BASEHD);
	kfree(dent);
	ubifs_add_auth_dirt(c, lnum);

	if (nm) {
		err = ubifs_tnc_add_nm(c, &dent_key, lnum, dent_offs, dlen,
				       hash_dent, nm);
		if (err) {
			ubifs_assert(c, !get_failure_reason_callback(c));
			goto out_ro;
		}
	}

	ino_key_init(c, &ino_key, inode->inum);
	ino_offs = dent_offs + aligned_dlen;
	err = ubifs_tnc_add(c, &ino_key, lnum, ino_offs, ilen, hash_ino);
	if (err) {
		ubifs_assert(c, !get_failure_reason_callback(c));
		goto out_ro;
	}

	if (dir_ui) {
		ino_key_init(c, &ino_key, dir->inum);
		ino_offs += aligned_ilen;
		err = ubifs_tnc_add(c, &ino_key, lnum, ino_offs, dir_ilen,
				    hash_ino_dir);
		if (err) {
			ubifs_assert(c, !get_failure_reason_callback(c));
			goto out_ro;
		}
	}

	finish_reservation(c);
	return 0;

out_free:
	kfree(dent);
	return err;

out_release:
	release_head(c, BASEHD);
	kfree(dent);
out_ro:
	ubifs_ro_mode(c, err);
	finish_reservation(c);
	return err;
}
