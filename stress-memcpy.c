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

#define ALIGN_SIZE	(64)

static uint8_t buffer[STR_SHARED_SIZE + ALIGN_SIZE];

/*
 *  stress_memcpy()
 *	stress memory copies
 */
int stress_memcpy(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	uint8_t *str_shared = shared->str_shared;
	uint8_t *aligned_buf = align_address(buffer, ALIGN_SIZE);

	(void)instance;
	(void)name;

	do {
		memcpy(aligned_buf, str_shared, STR_SHARED_SIZE);
		memcpy(str_shared, aligned_buf, STR_SHARED_SIZE);
		memmove(aligned_buf, aligned_buf + 64, STR_SHARED_SIZE - 64);
		memmove(aligned_buf + 64, aligned_buf, STR_SHARED_SIZE - 64);
		memmove(aligned_buf + 1, aligned_buf, STR_SHARED_SIZE - 1);
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}
