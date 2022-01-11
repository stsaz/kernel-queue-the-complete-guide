/* Kernel Queue The Complete Guide: kqueue-timer.c: System timer events */
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/event.h>

int kq;

struct context {
	void (*handler)(struct context *obj);
};

void timer_handler(struct context *obj)
{
	static int n;
	printf("Received timer event via kqueue: %d\n", n++);
}

void main()
{
	kq = kqueue();
	assert(kq != -1);

	struct context obj = {};
	obj.handler = timer_handler;

	// start system timer
	int period_ms = 1000;
	struct kevent events[1];
	EV_SET(&events[0], 1234, EVFILT_TIMER, EV_ADD | EV_ENABLE, 0, period_ms, &obj);
	assert(0 == kevent(kq, events, 1, NULL, 0, NULL));

	for (;;) {
		struct timespec *timeout = NULL; // wait indefinitely
		int n = kevent(kq, NULL, 0, events, 1, timeout);
		assert(n > 0);

		struct context *o = events[0].udata;
		if (events[0].filter == EVFILT_TIMER)
			o->handler(o); // handle timer event
	}

	close(kq);
}
