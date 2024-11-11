/*
 * Copyright (C) 2008 Nokia Corporation.
 * Copyright (C) 2008 University of Szeged, Hungary
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
 * Authors: Artem Bityutskiy
 *          Adrian Hunter
 *          Zoltan Sogor
 */

#ifndef __DEVTABLE_H__
#define __DEVTABLE_H__

/**
 * struct path_htbl_element - an element of the path hash table.
 * @path: the UBIFS path the element describes (the key of the element)
 * @name_htbl: one more (nested) hash table containing names of all
 *             files/directories/device nodes which should be created at this
 *             path
 *
 * See device table handling for more information.
 */
struct path_htbl_element {
	const char *path;
	struct hashtable *name_htbl;
};

/**
 * struct name_htbl_element - an element in the name hash table
 * @name: name of the file/directory/device node (the key of the element)
 * @mode: accsess rights and file type
 * @uid: user ID
 * @gid: group ID
 * @major: device node major number
 * @minor: device node minor number
 *
 * This is an element of the name hash table. Name hash table sits in the path
 * hash table elements and describes file names which should be created/changed
 * at this path.
 */
struct name_htbl_element {
	const char *name;
	unsigned int mode;
	unsigned int uid;
	unsigned int gid;
	dev_t dev;
};

struct hashtable_itr;

int parse_devtable(const char *tbl_file);
struct path_htbl_element *devtbl_find_path(const char *path);
struct name_htbl_element *devtbl_find_name(struct path_htbl_element *ph_elt,
					   const char *name);
int override_attributes(struct stat *st, struct path_htbl_element *ph_elt,
			struct name_htbl_element *nh_elt);
struct name_htbl_element *
first_name_htbl_element(struct path_htbl_element *ph_elt,
			struct hashtable_itr **itr);
struct name_htbl_element *
next_name_htbl_element(struct path_htbl_element *ph_elt,
		       struct hashtable_itr **itr);
void free_devtable_info(void);

#endif
