// SPDX-License-Identifier: GPL-2.0
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2018 Pengutronix, Sascha Hauer <s.hauer@pengutronix.de>
 */

/*
 * This file implements various helper functions for UBIFS authentication support
 */

#include "linux_err.h"
#include "ubifs.h"
#include "sign.h"
#include "defs.h"

int ubifs_shash_init(const struct ubifs_info *c,
		     __unused struct shash_desc *desc)
{
	if (ubifs_authenticated(c))
		return hash_digest_init();
	else
		return 0;
}

int ubifs_shash_update(const struct ubifs_info *c,
		       __unused struct shash_desc *desc,
		       const void *buf, unsigned int len)
{
	int err = 0;

	if (ubifs_authenticated(c)) {
		err = hash_digest_update(buf, len);
		if (err < 0)
			return err;
	}

	return 0;
}

int ubifs_shash_final(const struct ubifs_info *c,
		      __unused struct shash_desc *desc, u8 *out)
{
	return ubifs_authenticated(c) ? hash_digest_final(out) : 0;
}

struct shash_desc *ubifs_hash_get_desc(const struct ubifs_info *c)
{
	int err;

	err = ubifs_shash_init(c, NULL);
	if (err)
		return ERR_PTR(err);

	return NULL;
}

/**
 * ubifs_node_calc_hash - calculate the hash of a UBIFS node
 * @c: UBIFS file-system description object
 * @node: the node to calculate a hash for
 * @hash: the returned hash
 *
 * Returns 0 for success or a negative error code otherwise.
 */
int __ubifs_node_calc_hash(__unused const struct ubifs_info *c,
			   const void *node, u8 *hash)
{
	const struct ubifs_ch *ch = node;

	return hash_digest(node, le32_to_cpu(ch->len), hash);
}

/**
 * ubifs_master_node_calc_hash - calculate the hash of a UBIFS master node
 * @node: the node to calculate a hash for
 * @hash: the returned hash
 */
int ubifs_master_node_calc_hash(const struct ubifs_info *c, const void *node,
				uint8_t *hash)
{
	if (!ubifs_authenticated(c))
		return 0;

	return hash_digest(node + sizeof(struct ubifs_ch),
			   UBIFS_MST_NODE_SZ - sizeof(struct ubifs_ch), hash);
}

int ubifs_sign_superblock_node(struct ubifs_info *c, void *node)
{
	int err, len;
	struct ubifs_sig_node *sig = node + UBIFS_SB_NODE_SZ;

	if (!ubifs_authenticated(c))
		return 0;

	err = hash_sign_node(c->auth_key_filename, c->auth_cert_filename, node,
			     &len, sig + 1);
	if (err)
		return err;

	sig->type = UBIFS_SIGNATURE_TYPE_PKCS7;
	sig->len = cpu_to_le32(len);
	sig->ch.node_type  = UBIFS_SIG_NODE;

	return 0;
}

/**
 * ubifs_bad_hash - Report hash mismatches
 * @c: UBIFS file-system description object
 * @node: the node
 * @hash: the expected hash
 * @lnum: the LEB @node was read from
 * @offs: offset in LEB @node was read from
 *
 * This function reports a hash mismatch when a node has a different hash than
 * expected.
 */
void ubifs_bad_hash(const struct ubifs_info *c, const void *node, const u8 *hash,
		    int lnum, int offs)
{
	int len = min(c->hash_len, 20);
	int cropped = len != c->hash_len;
	const char *cont = cropped ? "..." : "";

	u8 calc[UBIFS_HASH_ARR_SZ];

	__ubifs_node_calc_hash(c, node, calc);

	ubifs_err(c, "hash mismatch on node at LEB %d:%d", lnum, offs);
	ubifs_err(c, "hash expected:   %*ph%s", len, hash, cont);
	ubifs_err(c, "hash calculated: %*ph%s", len, calc, cont);
}

/**
 * ubifs_init_authentication - initialize UBIFS authentication support
 * @c: UBIFS file-system description object
 *
 * This function returns 0 for success or a negative error code otherwise.
 */
int ubifs_init_authentication(struct ubifs_info *c)
{
	int err, hash_len, hash_algo;

	if (!c->auth_key_filename && !c->auth_cert_filename && !c->hash_algo_name)
		return 0;

	if (!c->auth_key_filename) {
		ubifs_err(c, "authentication key not given (--auth-key)");
		return -EINVAL;
	}

	if (!c->hash_algo_name) {
		ubifs_err(c, "Hash algorithm not given (--hash-algo)");
		return -EINVAL;
	}

	err = init_authentication(c->hash_algo_name, &hash_len, &hash_algo);
	if (err) {
		ubifs_err(c, "Init authentication failed");
		return err;
	}

	c->hash_len = hash_len;
	c->hash_algo = hash_algo;
	c->authenticated = 1;

	return 0;
}

void __ubifs_exit_authentication(__unused struct ubifs_info *c)
{
	exit_authentication();
}
