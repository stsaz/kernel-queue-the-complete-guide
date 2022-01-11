/* Kernel Queue The Complete Guide: kqueue-accept.c: Accept socket connection
Usage:
	$ ./kqueue-accept
	$ curl 127.0.0.1:64000/
*/
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/event.h>
#include <sys/ioctl.h>

int kq;

// the structure associated with a socket descriptor
struct context {
	int sk;
	void (*rhandler)(struct context *obj);
};

void accept_handler(struct context *obj)
{
	printf("Received socket READ event via kqueue\n");

	int csock = accept(obj->sk, NULL, 0);
	assert(csock != -1);

	// make socket as blocking
	int nonblock = 0;
	ioctl(csock, FIONBIO, (unsigned long*)&nonblock);

	char buf[1000];
	int r = recv(csock, buf, 1000, 0);
	assert(r >= 0);

	char data[] = "HTTP/1.1 200 OK\r\n\r\nHello";
	assert(sizeof(data)-1 == send(csock, data, sizeof(data)-1, 0));

	close(csock);
}

void main()
{
	// create kqueue object
	kq = kqueue();
	assert(kq != -1);

	struct context obj = {};
	obj.rhandler = accept_handler;

	// create and prepare a socket
	obj.sk = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	assert(obj.sk != -1);
	int val = 1;
	setsockopt(obj.sk, SOL_SOCKET, SO_REUSEADDR, &val, 4);

	struct sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_port = ntohs(64000);
	assert(0 == bind(obj.sk, (struct sockaddr*)&addr, sizeof(addr)));
	assert(0 == listen(obj.sk, 0));

	// attach socket to kqueue
	struct kevent events[2];
	EV_SET(&events[0], obj.sk, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, &obj);
	EV_SET(&events[1], obj.sk, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, &obj);
	assert(0 == kevent(kq, events, 2, NULL, 0, NULL));

	// wait for incoming events from kqueue
	struct timespec *timeout = NULL; // wait indefinitely
	int n = kevent(kq, NULL, 0, events, 1, timeout);
	assert(n > 0);

	// process the received event
	struct context *o = events[0].udata;
	if (events[0].filter == EVFILT_READ)
		o->rhandler(o); // handle read event

	close(obj.sk);
	close(kq);
}
