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

#if defined(HAVE_LIB_RT) && defined(__linux__) && NEED_GLIBC(2,1,0)

#include <aio.h>

#define BUFFER_SZ	(16)

/* per request async I/O data */
typedef struct {
	int		request;		/* Request slot */
	int		status;			/* AIO error status */
	struct aiocb	aiocb;			/* AIO cb */
	uint8_t		buffer[BUFFER_SZ];	/* Associated read/write buffer */
	volatile uint64_t count;		/* Signal handled count */
} io_req_t;

static volatile bool do_accounting = true;
#endif

static int opt_aio_requests = DEFAULT_AIO_REQUESTS;
static bool set_aio_requests = false;

void stress_set_aio_requests(const char *optarg)
{
	uint64_t aio_requests;

	set_aio_requests = true;
	aio_requests = get_uint64(optarg);
	check_range("aio-requests", aio_requests,
		MIN_AIO_REQUESTS, MAX_AIO_REQUESTS);
	opt_aio_requests = (int)aio_requests;
}

#if defined(HAVE_LIB_RT) && defined(__linux__) && NEED_GLIBC(2,1,0)
/*
 *  aio_fill_buffer()
 *	fill buffer with some known pattern
 */
static inline void aio_fill_buffer(
	const int request,
	uint8_t *const buffer,
	const size_t size)
{
	register size_t i;

	for (i = 0; i < size; i++)
		buffer[i] = (uint8_t)(request + i);
}

/*
 *  aio_signal_handler()
 *	handle an async I/O signal
 */
static void MLOCKED aio_signal_handler(int sig, siginfo_t *si, void *ucontext)
{
	io_req_t *io_req = (io_req_t *)si->si_value.sival_ptr;

	(void)sig;
	(void)si;
	(void)ucontext;

	if (do_accounting && io_req)
		io_req->count++;
}

/*
 *  aio_issue_cancel()
 *	cancel an in-progress async I/O request
 */
static void aio_issue_cancel(const char *name, io_req_t *io_req)
{
	int ret;

	if (io_req->status != EINPROGRESS)
		return;

	ret = aio_cancel(io_req->aiocb.aio_fildes,
		&io_req->aiocb);
	switch (ret) {
	case AIO_CANCELED:
	case AIO_ALLDONE:
		break;
	case AIO_NOTCANCELED:
		pr_dbg(stderr, "%s: async I/O request %d not cancelled\n",
			name, io_req->request);
		break;
	default:
		pr_err(stderr, "%s: %d error: %d %s\n",
			name, io_req->request,
			errno, strerror(errno));
	}
}

/*
 *  issue_aio_request()
 *	construct an AIO request and action it
 */
static int issue_aio_request(
	const char *name,
	const int fd,
	const off_t offset,
	io_req_t *const io_req,
	const int request,
	int (*aio_func)(struct aiocb *aiocbp) )
{
	while (opt_do_run) {
		int ret;

		io_req->request = request;
		io_req->status = EINPROGRESS;
		io_req->aiocb.aio_fildes = fd;
		io_req->aiocb.aio_buf = io_req->buffer;
		io_req->aiocb.aio_nbytes = BUFFER_SZ;
		io_req->aiocb.aio_reqprio = 0;
		io_req->aiocb.aio_offset = offset;
		io_req->aiocb.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
		io_req->aiocb.aio_sigevent.sigev_signo = SIGUSR1;
		io_req->aiocb.aio_sigevent.sigev_value.sival_ptr = io_req;

		ret = aio_func(&io_req->aiocb);
		if (ret < 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			pr_err(stderr, "%s: failed to issue aio request: %d (%s)\n",
				name, errno, strerror(errno));
		}
		return ret;
	}
	/* Given up */
	return 1;
}

/*
 *  stress_aio
 *	stress asynchronous I/O
 */
int stress_aio(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int ret, fd, rc = EXIT_FAILURE;
	io_req_t *io_reqs;
	struct sigaction sa, sa_old;
	int i;
	uint64_t total = 0;
	char filename[PATH_MAX];
	const pid_t pid = getpid();

	if (!set_aio_requests) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_aio_requests = MAX_AIO_REQUESTS;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_aio_requests = MIN_AIO_REQUESTS;
	}

	if ((io_reqs = calloc(opt_aio_requests, sizeof(io_req_t))) == NULL) {
		pr_err(stderr, "%s: cannot allocate io request structures\n", name);
		return EXIT_NO_RESOURCE;
	}

	ret = stress_temp_dir_mk(name, pid, instance);
	if (ret < 0) {
		free(io_reqs);
		return exit_status(-ret);
	}

	(void)stress_temp_filename(filename, sizeof(filename),
		name, pid, instance, mwc32());

	(void)umask(0077);
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		rc = exit_status(errno);
		pr_fail_err(name, "open");
		goto finish;
	}
	(void)unlink(filename);

	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART | SA_SIGINFO;
	sa.sa_sigaction = aio_signal_handler;
	if (sigaction(SIGUSR1, &sa, &sa_old) < 0)
		pr_fail_err(name, "sigaction");

	/* Kick off requests */
	for (i = 0; i < opt_aio_requests; i++) {
		aio_fill_buffer(i, io_reqs[i].buffer, BUFFER_SZ);
		ret = issue_aio_request(name, fd, (off_t)i * BUFFER_SZ,
			&io_reqs[i], i, aio_write);
		if (ret < 0)
			goto cancel;
		if (ret > 0) {
			rc = EXIT_SUCCESS;
			goto cancel;
		}
	}

	do {
		(void)shim_usleep(250000); /* wait until a signal occurs */

		for (i = 0; opt_do_run && (i < opt_aio_requests); i++) {
			if (io_reqs[i].status != EINPROGRESS)
				continue;

			io_reqs[i].status = aio_error(&io_reqs[i].aiocb);
			switch (io_reqs[i].status) {
			case ECANCELED:
			case 0:
				/* Succeeded or cancelled, so redo another */
				(*counter)++;
				if (issue_aio_request(name, fd,
					(off_t)i * BUFFER_SZ, &io_reqs[i], i,
					(mwc32() & 0x8) ? aio_read : aio_write) < 0)
					goto cancel;
				break;
			case EINPROGRESS:
				break;
			default:
				/* Something went wrong */
				pr_fail_errno(name, "aio_error",
					io_reqs[i].status);
				goto cancel;
			}
		}
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	rc = EXIT_SUCCESS;

cancel:
	/* Stop accounting */
	do_accounting = false;
	/* Cancel pending AIO requests */
	for (i = 0; i < opt_aio_requests; i++) {
		aio_issue_cancel(name, &io_reqs[i]);
		total += io_reqs[i].count;
	}
	(void)close(fd);
finish:
	free(io_reqs);

	pr_dbg(stderr, "%s: total of %" PRIu64 " async I/O signals "
		"caught (instance %d)\n",
		name, total, instance);
	(void)stress_temp_dir_rm(name, pid, instance);
	return rc;
}
#else
int stress_aio(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	return stress_not_implemented(counter, instance, max_ops, name);
}
#endif
