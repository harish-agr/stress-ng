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

#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1

#if defined(__linux__)
#include <sys/utsname.h>
#include <sys/timex.h>
#endif

#define check_do_run()		\
	if (!opt_do_run)	\
		break;		\

#define GIDS_MAX 	(1024)

static const int rusages[] = {
#if defined(RUSAGE_SELF)
	RUSAGE_SELF,
#endif
#if defined(RUSAGE_CHILDREN)
	RUSAGE_CHILDREN,
#endif
#if defined(RUSAGE_THREAD)
	RUSAGE_THREAD
#endif
};

static const int rlimits[] = {
#if defined(RLIMIT_AS)
	RLIMIT_AS,
#endif
#if defined(RLIMIT_CORE)
	RLIMIT_CORE,
#endif
#if defined(RLIMIT_CPU)
	RLIMIT_CPU,
#endif
#if defined(RLIMIT_DATA)
	RLIMIT_DATA,
#endif
#if defined(RLIMIT_FSIZE)
	RLIMIT_FSIZE,
#endif
#if defined(RLIMIT_MEMLOCK)
	RLIMIT_MEMLOCK,
#endif
#if defined(RLIMIT_MSGQUEUE)
	RLIMIT_MSGQUEUE,
#endif
#if defined(RLIMIT_NICE)
	RLIMIT_NICE,
#endif
#if defined(RLIMIT_NOFILE)
	RLIMIT_NOFILE,
#endif
#if defined(RLIMIT_RSS)
	RLIMIT_RSS,
#endif
#if defined(RLIMIT_RTPRIO)
	RLIMIT_RTPRIO,
#endif
#if defined(RLIMIT_RTTIME)
	RLIMIT_RTTIME,
#endif
#if defined(RLIMIT_SIGPENDING)
	RLIMIT_SIGPENDING,
#endif
#if defined(RLIMIT_STACK)
	RLIMIT_STACK
#endif
};

static const int priorities[] = {
#if defined(PRIO_PROCESS)
	PRIO_PROCESS,
#endif
#if defined(PRIO_PGRP)
	PRIO_PGRP,
#endif
#if defined(PRIO_USER)
	PRIO_USER
#endif
};

/*
 *  stress on get*() calls
 *	stress system by rapid get*() system calls
 */
int stress_get(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const bool verify = (opt_flags & OPT_FLAGS_VERIFY);

	(void)instance;

	do {
		char path[PATH_MAX];
		char *ptr;
		gid_t gids[GIDS_MAX];
		unsigned cpu, node;
#if defined(__linux__)
		gid_t rgid, egid, sgid;
		uid_t ruid, euid, suid;
		struct utsname utsbuf;
		struct timex timexbuf;
#endif
#if !defined(__minix__)
		const pid_t mypid = getpid();
#endif
		int ret;
		size_t i;
		struct timeval delta, tv;
		time_t t;

		check_do_run();

		(void)getppid();
		check_do_run();

		ptr = getcwd(path, sizeof path);
		if (verify && !ptr)
			pr_fail_err(name, "getcwd");
		check_do_run();

		(void)getgid();
		check_do_run();

		(void)getegid();
		check_do_run();

		(void)getuid();
		check_do_run();

		(void)geteuid();
		check_do_run();

		ret = getgroups(GIDS_MAX, gids);
		if (verify && (ret < 0))
			pr_fail_err(name, "getgroups");
		check_do_run();

#if !defined(__minix__)
		(void)getpgrp();
		check_do_run();
#endif

#if !defined(__minix__)
		(void)getpgid(mypid);
		check_do_run();
#endif

		for (i = 0; i < SIZEOF_ARRAY(priorities); i++) {
			errno = 0;
			ret = getpriority(priorities[i], 0);
			if (verify && errno && (ret < 0))
				pr_fail_err(name, "getpriority");
			check_do_run();
		}

#if defined(__linux__)
		ret = getresgid(&rgid, &egid, &sgid);
		if (verify && (ret < 0))
			pr_fail_err(name, "getresgid");
		check_do_run();

		ret = getresuid(&ruid, &euid, &suid);
		if (verify && (ret < 0))
			pr_fail_err(name, "getresuid");
		check_do_run();
#endif

		for (i = 0; i < SIZEOF_ARRAY(rlimits); i++) {
			struct rlimit rlim;

			ret = getrlimit(rlimits[i], &rlim);
			if (verify && (ret < 0))
				pr_fail(stderr, "%s: getrlimit(%zu, ..) failed, errno=%d (%s)\n",
					name, i, errno, strerror(errno));
			check_do_run();
		}

#if defined(__linux__) && NEED_GLIBC(2,13,0)
		for (i = 0; i < SIZEOF_ARRAY(rlimits); i++) {
			struct rlimit rlim[2];

			ret = prlimit(mypid, rlimits[i], NULL, &rlim[0]);
			if (verify && (ret < 0))
				pr_fail(stderr, "%s: prlimit(%d, %zu, ..) failed, errno=%d (%s)\n",
					name, mypid, i, errno, strerror(errno));
			if (!ret) {
				prlimit(mypid, rlimits[i], &rlim[0], NULL);
				if (verify && (ret < 0))
					pr_fail(stderr, "%s: prlimit(%d, %zu, ..) failed, errno=%d (%s)\n",
						name, mypid, i, errno, strerror(errno));
				prlimit(mypid, rlimits[i], &rlim[0], &rlim[1]);
				if (verify && (ret < 0))
					pr_fail(stderr, "%s: prlimit(%d, %zu, ..) failed, errno=%d (%s)\n",
						name, mypid, i, errno, strerror(errno));
			}
			check_do_run();
		}
#endif

		for (i = 0; i < SIZEOF_ARRAY(rusages); i++) {
			struct rusage usage;

			ret = getrusage(rusages[i], &usage);
			if (verify && (ret < 0))
				pr_fail(stderr, "%s: getrusage(%zu, ..) failed, errno=%d (%s)\n",
					name, i, errno, strerror(errno));
			check_do_run();
		}

#if _XOPEN_SOURCE >= 500 || _XOPEN_SOURCE && _XOPEN_SOURCE_EXTENDED
		ret = getsid(mypid);
		if (verify && (ret < 0))
			pr_fail_err(name, "getsid");
		check_do_run();
#endif

		(void)shim_gettid();
		check_do_run();

		(void)shim_getcpu(&cpu, &node, NULL);
		(void)shim_getcpu(NULL, &node, NULL);
		(void)shim_getcpu(&cpu, NULL, NULL);
		(void)shim_getcpu(NULL, NULL, NULL);
		check_do_run();

		t = time(NULL);
		if (verify && (t == (time_t)-1))
			pr_fail_err(name, "time");

		ret = gettimeofday(&tv, NULL);
		if (verify && (ret < 0))
			pr_fail_err(name, "gettimeval");
#if defined(__linux__)
		ret = uname(&utsbuf);
		if (verify && (ret < 0))
			pr_fail_err(name, "uname");
#endif

#if defined(__linux__)
		timexbuf.modes = 0;
		ret = adjtimex(&timexbuf);
		if (verify && (ret < 0))
			pr_fail_err(name, "adjtimex");
#endif
		memset(&delta, 0, sizeof(delta));
		ret = adjtime(&delta, &tv);
		if (verify && (ret < 0))
			pr_fail_err(name, "adjtime");

		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}
