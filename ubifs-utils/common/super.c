#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "defs.h"

/**
 * open_ubi - open the libubi.
 * @c: the UBIFS file-system description object
 * @node: name of the UBI volume character device to fetch information about
 *
 * This function opens libubi, and initialize device & volume information
 * according to @node. Returns %0 in case of success and %-1 in case of failure.
 */
int open_ubi(struct ubifs_info *c, const char *node)
{
	struct stat st;

	if (stat(node, &st) || !S_ISCHR(st.st_mode))
		return -1;

	c->libubi = libubi_open();
	if (!c->libubi)
		return -1;
	if (ubi_get_vol_info(c->libubi, node, &c->vi))
		goto out_err;
	if (ubi_get_dev_info1(c->libubi, c->vi.dev_num, &c->di))
		goto out_err;

	return 0;

out_err:
	close_ubi(c);
	return -1;
}

void close_ubi(struct ubifs_info *c)
{
	if (c->libubi) {
		libubi_close(c->libubi);
		c->libubi = NULL;
	}
}

/**
 * open_target - open the output target.
 * @c: the UBIFS file-system description object
 *
 * Open the output target. The target can be an UBI volume
 * or a file.
 *
 * Returns %0 in case of success and %-1 in case of failure.
 */
int open_target(struct ubifs_info *c)
{
	if (c->libubi) {
		c->dev_fd = open(c->dev_name, O_RDWR | O_EXCL);

		if (c->dev_fd == -1)
			return sys_errmsg("cannot open the UBI volume '%s'",
					   c->dev_name);
		if (ubi_set_property(c->dev_fd, UBI_VOL_PROP_DIRECT_WRITE, 1)) {
			close(c->dev_fd);
			return sys_errmsg("ubi_set_property(set direct_write) failed");
		}
	} else {
		c->dev_fd = open(c->dev_name, O_CREAT | O_RDWR | O_TRUNC,
			      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
		if (c->dev_fd == -1)
			return sys_errmsg("cannot create output file '%s'",
					   c->dev_name);
	}
	return 0;
}


/**
 * close_target - close the output target.
 * @c: the UBIFS file-system description object
 *
 * Close the output target. If the target was an UBI
 * volume, also close libubi.
 *
 * Returns %0 in case of success and %-1 in case of failure.
 */
int close_target(struct ubifs_info *c)
{
	if (c->dev_fd >= 0) {
		if (c->libubi && ubi_set_property(c->dev_fd, UBI_VOL_PROP_DIRECT_WRITE, 0))
			return sys_errmsg("ubi_set_property(clear direct_write) failed");
		if (close(c->dev_fd) == -1)
			return sys_errmsg("cannot close the target '%s'", c->dev_name);
	}
	return 0;
}

/**
 * check_volume_empty - check if the UBI volume is empty.
 * @c: the UBIFS file-system description object
 *
 * This function checks if the UBI volume is empty by looking if its LEBs are
 * mapped or not.
 *
 * Returns %0 in case of success, %1 is the volume is not empty,
 * and a negative error code in case of failure.
 */
int check_volume_empty(struct ubifs_info *c)
{
	int lnum, err;

	for (lnum = 0; lnum < c->vi.rsvd_lebs; lnum++) {
		err = ubi_is_mapped(c->dev_fd, lnum);
		if (err < 0)
			return err;
		if (err == 1)
			return 1;
	}
	return 0;
}
