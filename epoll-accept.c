/* Kernel Queue The Complete Guide: epoll-accept.c: Accept socket connection
Usage:
	$ ./epoll-accept
	$ curl 127.0.0.1:64000/
*/
#include <assert.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <sys/socket.h>

int kq;

// the structure associated with a socket descriptor
struct context {
	int sk;
	void (*rhandler)(struct context *object);
};

void accept_handler(struct context *obj)
{
	printf("Received socket READ event via epoll\n");

	int csock = accept(obj->sk, NULL, 0);
	assert(csock != -1);

	char buf[1000];
	int r = recv(csock, buf, 1000, 0);
	assert(r >= 0);

	char data[] = "HTTP/1.1 200 OK\r\n\r\nHello";
	assert(sizeof(data)-1 == send(csock, data, sizeof(data)-1, 0));

	close(csock);
}

void main()
{
	// create KQ object
	kq = epoll_create(1);
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

	// attach socket to KQ
	struct epoll_event event;
	event.events = EPOLLIN | EPOLLOUT | EPOLLET;
	event.data.ptr = &obj;
	assert(0 == epoll_ctl(kq, EPOLL_CTL_ADD, obj.sk, &event));

	// wait for incoming events from KQ
	struct epoll_event events[1];
	int timeout_ms = -1; // wait indefinitely
	int n = epoll_wait(kq, events, 1, timeout_ms);
	assert(n > 0);

	// process the received event
	struct context *o = events[0].data.ptr;
	if (events[0].events & (EPOLLIN | EPOLLERR))
		o->rhandler(o); // handle read event

	close(obj.sk);
	close(kq);
}
