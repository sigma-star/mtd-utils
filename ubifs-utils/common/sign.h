/*
 * Copyright (C) 2018 Pengutronix
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Author: Sascha Hauer
 */

#ifndef __UBIFS_SIGN_H__
#define __UBIFS_SIGN_H__

#include <openssl/evp.h>

struct shash_desc {
	void *ctx;
};

int hash_digest(const void *buf, unsigned int len, uint8_t *hash);
int hash_digest_init(void);
int hash_digest_update(const void *buf, int len);
int hash_digest_final(void *hash);
int init_authentication(const char *algo_name, int *hash_len, int *hash_algo);
void exit_authentication(void);
void mst_node_calc_hash(const void *node, uint8_t *hash);
int hash_sign_node(const char *auth_key_filename, const char *auth_cert_filename,
		   void *buf, int *len, void *outbuf);

#endif /* __UBIFS_SIGN_H__ */
