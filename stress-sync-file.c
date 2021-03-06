/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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
#include "stress-ng.h"

#if defined(__linux__) && \
    (defined(__NR_sync_file_range) || defined(__NR_sync_file_range2)) && \
    NEED_GLIBC(2,10,0)
#define HAVE_SYNC_FILE_RANGE
#endif

#if defined(HAVE_SYNC_FILE_RANGE)
static const int sync_modes[] = {
	SYNC_FILE_RANGE_WAIT_BEFORE | SYNC_FILE_RANGE_WRITE,
	SYNC_FILE_RANGE_WAIT_BEFORE | SYNC_FILE_RANGE_WRITE | SYNC_FILE_RANGE_WAIT_AFTER,
	SYNC_FILE_RANGE_WRITE,
	SYNC_FILE_RANGE_WAIT_BEFORE,
	SYNC_FILE_RANGE_WAIT_AFTER,
	0	/* No-op */
};
#endif

static off_t opt_sync_file_bytes = DEFAULT_SYNC_FILE_BYTES;
static bool set_sync_file_bytes = false;

void stress_set_sync_file_bytes(const char *optarg)
{
	set_sync_file_bytes = true;
	opt_sync_file_bytes = (off_t)get_uint64_byte(optarg);
	check_range("sync_file-bytes", opt_sync_file_bytes,
		MIN_SYNC_FILE_BYTES, MAX_SYNC_FILE_BYTES);
}

#if defined(HAVE_SYNC_FILE_RANGE)

static inline int shim_sync_file_range(
	int fd,
	off64_t offset,
	off64_t nbytes,
	unsigned int flags)
{
#if defined(__NR_sync_file_range)
	return syscall(__NR_sync_file_range, fd, offset, nbytes, flags);
#elif defined(__NR_sync_file_range2)
	/*
	 * from sync_file_range(2):
	 * "Some architectures (e.g., PowerPC, ARM) need  64-bit  arguments  to  be
         * aligned  in  a  suitable pair of registers.  On such architectures, the
         * call signature of sync_file_range() shown in the SYNOPSIS would force a
         * register  to  be wasted as padding between the fd and offset arguments.
         * (See syscall(2) for details.)  Therefore, these architectures define  a
         * different system call that orders the arguments suitably"
	 */
	return syscall(__NR_sync_file_range2, fd, flags, offset, nbytes);
#else
	(void)fd;
	(void)offset;
	(void)nbytes;
	(void)flags;

	error = -ENOSYS;
	return -1;
#endif
}

/*
 *  shrink and re-allocate the file to be sync'd
 *
 */
static int stress_sync_allocate(const char *name, const int fd)
{
	int ret;

	ret = ftruncate(fd, 0);
	if (ret < 0) {
		pr_err(stderr, "%s: ftruncate failed: errno=%d (%s)\n",
			name, errno, strerror(errno));
		return -errno;
	}

	ret = fdatasync(fd);
	if (ret < 0) {
		pr_err(stderr, "%s: fdatasync failed: errno=%d (%s)\n",
			name, errno, strerror(errno));
		return -errno;
	}

	ret = shim_fallocate(fd, 0, (off_t)0, opt_sync_file_bytes);
	if (ret < 0) {
		pr_err(stderr, "%s: fallocate failed: errno=%d (%s)\n",
			name, errno, strerror(errno));
		return -errno;
	}
	return 0;
}

/*
 *  stress_sync_file
 *	stress the sync_file_range system call
 */
int stress_sync_file(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const pid_t pid = getpid();
	int fd, ret;
	char filename[PATH_MAX];

	if (!set_sync_file_bytes) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_sync_file_bytes = MAX_SYNC_FILE_BYTES;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_sync_file_bytes = MIN_SYNC_FILE_BYTES;
	}

	ret = stress_temp_dir_mk(name, pid, instance);
	if (ret < 0)
		return exit_status(-ret);

	(void)stress_temp_filename(filename, sizeof(filename),
		name, pid, instance, mwc32());
	(void)umask(0077);
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		ret = exit_status(errno);
		pr_fail_err(name, "open");
		(void)stress_temp_dir_rm(name, pid, instance);
		return ret;
	}
	(void)unlink(filename);

	do {
		off64_t i, offset;
		const size_t mode_index = mwc32() % SIZEOF_ARRAY(sync_modes);
		const int mode = sync_modes[mode_index];

		if (stress_sync_allocate(name, fd) < 0)
			break;
		for (offset = 0; offset < (off64_t)opt_sync_file_bytes; ) {
			off64_t sz = (mwc32() & 0x1fc00) + KB;
			ret = shim_sync_file_range(fd, offset, sz, mode);
			if (ret < 0)
				pr_fail_err(name, "sync_file_range (forward)");
			offset += sz;
		}
		if (!opt_do_run)
			break;

		if (stress_sync_allocate(name, fd) < 0)
			break;
		for (offset = 0; offset < (off64_t)opt_sync_file_bytes; ) {
			off64_t sz = (mwc32() & 0x1fc00) + KB;

			ret = shim_sync_file_range(fd, opt_sync_file_bytes - offset, sz, mode);
			if (ret < 0)
				pr_fail_err(name, "sync_file_range (reverse)");
			offset += sz;
		}
		if (!opt_do_run)
			break;

		if (stress_sync_allocate(name, fd) < 0)
			break;
		for (i = 0; i < (off64_t)(opt_sync_file_bytes / (128 * KB)); i++) {
			offset = (mwc64() % opt_sync_file_bytes) & ~((128 * KB) - 1);
			ret = shim_sync_file_range(fd, offset, 128 * KB, mode);
			if (ret < 0)
				pr_fail_err(name, "sync_file_range (random)");
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	(void)close(fd);
	(void)stress_temp_dir_rm(name, pid, instance);

	return EXIT_SUCCESS;
}
#else
int stress_sync_file(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	return stress_not_implemented(counter, instance, max_ops, name);
}
#endif
