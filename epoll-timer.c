/* Kernel Queue The Complete Guide: epoll-timer.c: System timer events */
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>

int kq;
int tfd;

struct context {
	void (*handler)(struct context *obj);
};

void timer_handler(struct context *obj)
{
	static int n;
	printf("Received timerfd event via epoll: %d\n", n++);

	unsigned long long val;
	read(tfd, &val, 8);
}

void main()
{
	// create kqueue object
	kq = epoll_create(1);
	assert(kq != -1);

	struct context obj = {};
	obj.handler = timer_handler;

	// prepare timerfd-descriptor
	tfd = timerfd_create(CLOCK_MONOTONIC, 0);
	assert(tfd != -1);

	// register timerfd in KQ
	struct epoll_event event;
	event.events = EPOLLIN | EPOLLET;
	event.data.ptr = &obj;
	assert(0 == epoll_ctl(kq, EPOLL_CTL_ADD, tfd, &event));

	// start periodic timer
	struct itimerspec its;
	its.it_value.tv_sec = 1;
	its.it_value.tv_nsec = 0;
	its.it_interval = its.it_value;
	assert(0 == timerfd_settime(tfd, 0, &its, NULL));

	for (;;) {
		struct epoll_event events[1];
		int timeout_ms = -1;
		int n = epoll_wait(kq, events, 1, timeout_ms);
		assert(n > 0);

		struct context *o = events[0].data.ptr;
		if (events[0].events & (EPOLLIN | EPOLLERR))
			o->handler(o); // handle timerfd event
	}

	close(tfd); // close timerfd descriptor
	close(kq);
}
