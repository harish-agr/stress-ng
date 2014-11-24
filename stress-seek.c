/*
 * Copyright (C) 2013-2014 Canonical, Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "stress-ng.h"

/*
 *  stress_seek
 *	stress I/O via random seeks and read/writes
 */
int stress_seek(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	uint64_t i;
	const pid_t pid = getpid();
	int fd, rc = EXIT_FAILURE;
	char filename[PATH_MAX];
	uint8_t buf[512];
	off_t len = opt_seek_size - sizeof(buf);

	if (stress_temp_dir_mk(name, pid, instance) < 0)
		return EXIT_FAILURE;

	for (i = 0; i < sizeof(buf); i++)
		buf[i] = (uint8_t)mwc();

	(void)stress_temp_filename(filename, sizeof(filename),
		name, pid, instance, mwc());
	(void)umask(0077);
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		pr_failed_err(name, "open");
		goto finish;
	}
	(void)unlink(filename);
	/* Generate file with hole at the end */
	if (lseek(fd, len, SEEK_SET) < 0) {
		pr_failed_err(name, "lseek");
		goto close_finish;
	}
	if (write(fd, buf, sizeof(buf)) < 0) {
		pr_failed_err(name, "write");
		goto close_finish;
	}

	do {
		off_t offset;
		uint8_t tmp[512];
		ssize_t ret;

		offset = (off_t)mwc() % len;
		if (lseek(fd, offset, SEEK_SET) < 0) {
			pr_failed_err(name, "lseek");
			goto close_finish;
		}
		if (write(fd, buf, sizeof(buf)) < 0) {
			pr_failed_err(name, "write");
			goto close_finish;
		}

		offset = (off_t)mwc() % len;
		if (lseek(fd, offset, SEEK_SET) < 0) {
			pr_failed_err(name, "lseek");
			goto close_finish;
		}
		if ((ret = read(fd, tmp, sizeof(tmp))) < 0) {
			pr_failed_err(name, "read");
			goto close_finish;
		}
		if ((ret != sizeof(tmp)) &&
		    (opt_flags & OPT_FLAGS_VERIFY)) {
			pr_fail(stderr, "incorrect read size, expecting 512 bytes");
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	rc = EXIT_SUCCESS;
close_finish:
	(void)close(fd);
finish:
	(void)stress_temp_dir_rm(name, pid, instance);
	return rc;
}