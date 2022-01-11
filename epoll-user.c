/* Kernel Queue The Complete Guide: epoll-user.c: User-triggered events */
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>

int kq;
int efd;

struct context {
	void (*handler)(struct context *obj);
};

struct context eventfd_obj;
struct context user_event_obj;

void user_event_obj_handler(struct context *obj)
{
	printf("Received user event via epoll\n");
}

// application calls this function whenever it wants to add a new event to KQ
// which will execute user_event_obj_handler()
void trigger_user_event()
{
	struct context *obj = &user_event_obj;
	obj->handler = user_event_obj_handler;

	unsigned long long val = (size_t)obj;
	int r = write(efd, &val, 8);
	assert(r == 8);
}

// handle event from eventfd-descriptor
void handle_eventfd(struct context *obj)
{
	unsigned long long val;
	for (;;) {
		int r = read(efd, &val, 8);
		if (r < 0 && errno == EAGAIN)
			break;
		assert(r == 8);

		struct context *o = (void*)(size_t)val;
		o->handler(o);
	}
}

void main()
{
	// create kqueue object
	kq = epoll_create(1);
	assert(kq != -1);

	struct context obj = {};
	obj.handler = handle_eventfd;

	// prepare eventfd-descriptor for user events
	efd = eventfd(0, EFD_NONBLOCK);
	assert(efd != -1);

	// register eventfd in KQ
	struct epoll_event event;
	event.events = EPOLLIN | EPOLLET;
	event.data.ptr = &obj;
	assert(0 == epoll_ctl(kq, EPOLL_CTL_ADD, efd, &event));

	trigger_user_event();

	struct epoll_event events[1];
	int timeout_ms = -1;
	int n = epoll_wait(kq, events, 1, timeout_ms);
	assert(n > 0);

	struct context *o = events[0].data.ptr;
	if (events[0].events & (EPOLLIN | EPOLLERR))
		o->handler(o); // handle eventfd event

	close(efd); // close eventfd descriptor
	close(kq);
}
