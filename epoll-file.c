/* Kernel Queue The Complete Guide: epoll-file.c: Asynchronous file reading
Usage:
	$ echo 'Hello file AIO' >./epoll-file.txt
	$ ./epoll-file
*/
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/syscall.h>
#include <sys/eventfd.h>
#include <linux/aio_abi.h>

int kq;
int efd;
aio_context_t aioctx;

struct context {
	struct iocb acb;
	int (*handler)(struct context *obj);
};

void file_io_result(const char *via, int res)
{
	printf("Read from file via %s: %d\n", via, res);
}

// GLIBC doesn't have wrappers for these syscalls, so we make our own wrappers
static inline int io_setup(unsigned nr_events, aio_context_t *ctx_idp)
{
	return syscall(SYS_io_setup, nr_events, ctx_idp);
}
static inline int io_destroy(aio_context_t ctx_id)
{
	return syscall(SYS_io_destroy, ctx_id);
}
static inline int io_submit(aio_context_t ctx_id, long nr, struct iocb **iocbpp)
{
	return syscall(SYS_io_submit, ctx_id, nr, iocbpp);
}
static inline int io_getevents(aio_context_t ctx_id, long min_nr, long nr, struct io_event *events, struct timespec *timeout)
{
	return syscall(SYS_io_getevents, ctx_id, min_nr, nr, events, timeout);
}

int file_aio_handler(struct context *obj)
{
	unsigned long long n;
	for (;;) {
		int r = read(efd, &n, 8);
		if (r < 0 && errno == EAGAIN)
			break;
		assert(r == 8);
		// we've got `n` unprocessed events from file AIO

		for (;;) {

			struct io_event events[64];
			struct timespec timeout = {};
			r = io_getevents(aioctx, 1, 64, events, &timeout);
			if (r < 0 && errno == EINTR) {
				continue; // interrupted due to UNIX signal
			} else if (r == 0) {
				break; // no more events
			}
			assert(r > 0);

			// process result value for each event
			for (int i = 0;  i != r;  i++) {
				struct context *obj = (void*)(size_t)events[i].data;
				int result = events[i].res;
				if (result < 0) {
					errno = -result;
					result = -1;
				}
				file_io_result("epoll", result);
			}
		}
	}
	return 1;
}

void main()
{
	// create KQ object
	kq = epoll_create(1);
	assert(kq != -1);

	// prepare the associated object
	struct context obj = {};
	obj.handler = file_aio_handler;

	// open file descriptor, O_DIRECT is mandatory
	int fd = open("./epoll-file.txt", O_DIRECT | O_RDONLY, 0);
	assert(fd != -1);

	// initialize file AIO subsystem
	int aio_workers = 64;
	assert(0 == io_setup(aio_workers, &aioctx));

	// open eventfd descriptor which will pass signals from file AIO
	efd = eventfd(0, EFD_NONBLOCK);
	assert(efd != -1);

	// attach eventfd to KQ
	struct epoll_event event;
	event.events = EPOLLIN | EPOLLET;
	event.data.ptr = &obj;
	assert(0 == epoll_ctl(kq, EPOLL_CTL_ADD, efd, &event));

	// associate the AIO operation with KQ and user object pointer
	memset(&obj.acb, 0, sizeof(obj.acb));
	obj.acb.aio_data = (size_t)&obj;
	obj.acb.aio_flags = IOCB_FLAG_RESFD;
	obj.acb.aio_resfd = efd;

	void *buf;
	assert(0 == posix_memalign(&buf, 512, 4*1024)); // allocate 4k buffer aligned by 512

	// specify operation parameters
	obj.acb.aio_fildes = fd;
	obj.acb.aio_buf = (size_t)buf; // destination buffer
	obj.acb.aio_nbytes = 4*1024; // max number of bytes to read
	obj.acb.aio_offset = 0; // offset to begin reading at

	// begin file AIO operation
	obj.acb.aio_lio_opcode = IOCB_CMD_PREAD;
	struct iocb *cb = &obj.acb;
	if (1 != io_submit(aioctx, 1, &cb)) {
		if (errno == EAGAIN || errno == ENOSYS) {
			// no resources to complete this I/O operation
			// or the system can't perform AIO on this file
		} else {
			file_io_result("io_submit", -1);
			return; // fatal error
		}

		// AIO doesn't work - perform synchronous reading at the specified offset
		int r = pread(fd, buf, obj.acb.aio_nbytes, obj.acb.aio_offset);
		file_io_result("pread", r);
		return;
	}

	// asynchronous file reading is in progress, now wait for the signal from KQ
	struct epoll_event events[1];
	int timeout_ms = -1; // wait indefinitely
	int n = epoll_wait(kq, events, 1, timeout_ms);

	struct context *o = events[0].data.ptr;
	if (events[0].events & (EPOLLIN | EPOLLERR)) {
		o->handler(o); // handle file AIO event via eventfd
	}

	free(buf);
	close(fd);
	io_destroy(aioctx);
	close(kq);
}
