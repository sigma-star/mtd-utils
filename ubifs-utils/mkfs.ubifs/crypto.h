/*
 * Copyright (C) 2017 sigma star gmbh
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
 * Authors: David Oberhollenzer <david.oberhollenzer@sigma-star.at>
 */

#ifndef UBIFS_CRYPTO_H
#define UBIFS_CRYPTO_H

#include <sys/types.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>


struct cipher {
	const char *name;

	ssize_t (*encrypt_block)(const void *plaintext, size_t size,
				 const void *key, uint64_t block_index,
				 void *ciphertext);

	ssize_t (*encrypt_fname)(const void *plaintext, size_t size,
				 const void *key, void *ciphertext);
};


int crypto_init(void);

void crypto_cleanup(void);

ssize_t encrypt_block_aes128_cbc(const void *plaintext, size_t size,
				 const void *key, uint64_t block_index,
				 void *ciphertext);

ssize_t encrypt_block_aes256_xts(const void *plaintext, size_t size,
				 const void *key, uint64_t block_index,
				 void *ciphertext);

ssize_t encrypt_aes128_cbc_cts(const void *plaintext, size_t size,
			       const void *key, void *ciphertext);

ssize_t encrypt_aes256_cbc_cts(const void *plaintext, size_t size,
			       const void *key, void *ciphertext);

ssize_t derive_key_aes(const void *deriving_key, const void *source_key,
		       void *derived_key);


struct cipher *get_cipher(const char *name);

void list_ciphers(FILE *fp);

#endif /* UBIFS_CRYPTO_H */

