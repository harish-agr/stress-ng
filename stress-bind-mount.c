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

#if defined(__linux__) && defined(MS_BIND) && defined(MS_REC)

#define CLONE_STACK_SIZE	(64*1024)

/* Context for clone */
typedef struct {
	uint64_t max_ops;
	uint64_t *counter;
	const char *name;
} context_t;

/*
 *  stress_bind_mount_child()
 *	aggressively perform bind mounts, this can force out of memory
 *	situations
 */
static int stress_bind_mount_child(void *arg)
{
	context_t *context = (context_t *)arg;
	uint64_t *counter = context->counter;

	(void)setpgid(0, pgrp);
	stress_parent_died_alarm();

	do {
		if (mount("/", "/", "", MS_BIND | MS_REC, 0) < 0) {
			pr_fail_err(context->name, "mount");
			break;
		}
		/*
		 *  The following fails with -EBUSY, but try it anyhow
	`	 *  just to make the kernel work harder
		 */
		(void)umount("/");
		(*counter)++;
	} while (opt_do_run &&
		 (!context->max_ops || *counter < context->max_ops));

	return 0;
}

/*
 *  stress_bind_mount()
 *      stress bind mounting
 */
int stress_bind_mount(
        uint64_t *const counter,
        const uint32_t instance,
        const uint64_t max_ops,
        const char *name)
{
	int pid = 0, status;
	context_t context;
	const ssize_t stack_offset =
		stress_get_stack_direction() *
		(CLONE_STACK_SIZE - 64);
	char stack[CLONE_STACK_SIZE];
	char *stack_top = stack + stack_offset;

	(void)instance;

	context.name = name;
	context.max_ops = max_ops;
	context.counter = counter;

	pid = clone(stress_bind_mount_child,
		align_stack(stack_top),
		CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWPID | CLONE_VM,
		&context, 0);
	if (pid < 0) {
		int rc = exit_status(errno);

		pr_fail_err(name, "clone");
		return rc;
	}

	do {
		/* Twiddle thumbs */
		(void)shim_usleep(10000);
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	(void)kill(pid, SIGKILL);
	(void)waitpid(pid, &status, 0);

	return EXIT_SUCCESS;
}
#else
int stress_bind_mount(
        uint64_t *const counter,
        const uint32_t instance,
        const uint64_t max_ops,
        const char *name)
{
	return stress_not_implemented(counter, instance, max_ops, name);
}

#endif
