/* Kernel Queue The Complete Guide: kqueue-file.c: Asynchronous file reading
Usage:
	$ echo 'Hello file AIO' >./kqueue-file.txt
	$ ./kqueue-file
*/
#include <aio.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/event.h>

int kq;

struct context {
	int fd;
	struct aiocb acb;
	int (*handler)(struct context *obj, struct aiocb *acb);
};

void file_io_result(const char *via, int res)
{
	printf("Read from file via %s: %d\n", via, res);
}

int file_aio_handler(struct context *obj, struct aiocb *acb)
{
	int r = aio_error(acb);
	if (r == EINPROGRESS) {
		return 0; // AIO in progress
	} else if (r == -1) {
		file_io_result("kqueue", -1); // AIO completed with error
		return -1;
	}

	r = aio_return(acb);
	file_io_result("kqueue", r); // AIO completed successfully
	return 1;
}

void main()
{
	// create KQ object
	kq = kqueue();
	assert(kq != -1);

	// open file descriptor and prepare the associated object
	int fd = open("./kqueue-file.txt", O_RDONLY, 0);
	assert(fd != -1);
	struct context obj = {};
	obj.handler = file_aio_handler;

	// associate the AIO operation with KQ and user object pointer
	memset(&obj.acb, 0, sizeof(obj.acb));
	obj.acb.aio_sigevent.sigev_notify_kqueue = kq;
	obj.acb.aio_sigevent.sigev_notify = SIGEV_KEVENT;
	obj.acb.aio_sigevent.sigev_notify_kevent_flags = EV_CLEAR;
	obj.acb.aio_sigevent.sigev_value.sigval_ptr = &obj;

	void *buf = malloc(4*1024);

	// specify operation parameters
	obj.acb.aio_fildes = fd;
	obj.acb.aio_buf = buf; // destination buffer
	obj.acb.aio_nbytes = 4*1024; // max number of bytes to read
	obj.acb.aio_offset = 0; // offset to begin reading at

	// begin file AIO operation
	obj.acb.aio_lio_opcode = LIO_READ;
	if (0 != aio_read(&obj.acb)) {
		if (errno == EAGAIN || errno == ENOSYS || errno == EOPNOTSUPP) {
			// no resources to complete this I/O operation
			// or AIO module isn't loaded
			// or the system can't perform AIO on this file
		} else {
			file_io_result("aio_read", -1);
			return; // fatal error
		}

		// AIO doesn't work - perform synchronous reading at the specified offset
		int r = pread(fd, buf, obj.acb.aio_nbytes, obj.acb.aio_offset);
		file_io_result("pread", r);
		return;
	}

	// asynchronous file reading has started, but might be finished already
	if (0 != file_aio_handler(&obj, &obj.acb))
		return;

	// asynchronous file reading is in progress, now wait for the signal from KQ
	struct kevent events[1];
	struct timespec *timeout = NULL; // wait indefinitely
	int n = kevent(kq, NULL, 0, events, 1, timeout);

	struct context *o = events[0].udata;
	if (events[0].filter == EVFILT_AIO) {
		struct aiocb *acb = (void*)events[0].ident;
		o->handler(o, acb); // handle file AIO event
	}

	free(buf);
	close(fd);
	close(kq);
}
