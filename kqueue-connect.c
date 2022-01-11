/* Kernel Queue The Complete Guide: kqueue-connect.c: HTTP/1 client
Usage:
	$ nc -l 127.0.0.1 64000
	$ ./kqueue-connect
*/
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/event.h>

int kq;
int quit;

// the structure associated with a socket descriptor
struct context {
	int sk;
	void (*rhandler)(struct context *obj);
	void (*whandler)(struct context *obj);
	int data_offset;
};

void obj_write(struct context *obj);
void obj_read(struct context *obj);

void obj_prepare(struct context *obj)
{
	// create and prepare socket
	obj->sk = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	assert(obj->sk != -1);

	int val = 1;
	assert(0 == setsockopt(obj->sk, 0, TCP_NODELAY, (char*)&val, sizeof(int)));

	// attach socket to KQ
	struct kevent events[2];
	EV_SET(&events[0], obj->sk, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, obj);
	EV_SET(&events[1], obj->sk, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, obj);
	assert(0 == kevent(kq, events, 2, NULL, 0, NULL));
}

void obj_connect(struct context *obj)
{
	if (obj->whandler == NULL) {
		// begin asynchronous connection
		struct sockaddr_in addr = {};
		addr.sin_family = AF_INET;
		addr.sin_port = ntohs(64000);
		char ip4[] = {127,0,0,1};
		*(int*)&addr.sin_addr = *(int*)ip4;

		int r = connect(obj->sk, (struct sockaddr*)&addr, sizeof(struct sockaddr_in));
		if (r == 0) {
			// connection completed successfully
		} else if (errno == EINPROGRESS) {
			// connection is in progress
			obj->whandler = obj_connect;
			return;
		} else {
			assert(0); // fatal error
		}

	} else {
		assert(errno == 0); // connection is successful
		obj->whandler = NULL; // we don't want any more signals from KQ
	}

	printf("Connected\n");
	obj_write(obj);
}

void obj_write(struct context *obj)
{
	const char data[] = "GET / HTTP/1.1\r\nHost: hostname\r\nConnection: close\r\n\r\n";
	int r = send(obj->sk, data + obj->data_offset, sizeof(data)-1 - obj->data_offset, 0);
	if (r > 0) {
		// sent some data
		obj->data_offset += r;
		if (obj->data_offset != sizeof(data)-1) {
			// we need to send the complete request
			obj_write(obj);
			return;
		}
		obj->whandler = NULL;

	} else if (r < 0 && errno == EAGAIN) {
		// the socket's write buffer is full
		obj->whandler = obj_write;
		return;
	} else {
		assert(0); // fatal error
	}

	printf("Sent HTTP request.  Receiving HTTP response...\n");
	obj_read(obj);
}

void obj_read(struct context *obj)
{
	char data[64*1024];
	int r = recv(obj->sk, data, sizeof(data), 0);
	if (r > 0) {
		// received some data
		printf("%.*s", r, data);
		obj_read(obj);
		return;

	} else if (r == 0) {
		// server has finished sending data

	} else if (r < 0 && errno == EAGAIN) {
		// the socket's read buffer is empty
		obj->rhandler = obj_read;
		return;
	} else {
		assert(0); // fatal error
	}

	quit = 1;
}

void main()
{
	// create KQ object
	kq = kqueue();
	assert(kq != -1);

	struct context obj = {};
	obj_prepare(&obj);
	obj_connect(&obj);

	// wait for incoming events from KQ and process them
	while (!quit) {
		struct kevent events[1];
		struct timespec *timeout = NULL; // wait indefinitely
		int n = kevent(kq, NULL, 0, events, 1, timeout);
		if (n < 0 && errno == EINTR)
			continue; // kevent() interrupts when UNIX signal is received
		assert(n > 0);

		// now process each signalled event
		for (int i = 0;  i != n;  i++) {
			struct context *o = events[i].udata;

			errno = 0;
			if (events[i].flags & EV_EOF)
				errno = events[i].fflags;

			if (events[i].filter == EVFILT_READ
				&& o->rhandler != NULL)
				o->rhandler(o); // handle read event

			if (events[i].filter == EVFILT_WRITE
				&& o->whandler != NULL)
				o->whandler(o); // handle write event
		}
	}

	close(obj.sk);
	close(kq);
}
