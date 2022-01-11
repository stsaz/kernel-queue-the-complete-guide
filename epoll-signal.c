/* Kernel Queue The Complete Guide: epoll-signal.c: UNIX signal handler
Usage:
	$ ./epoll-signal
	$ killall -SIGUSR1 epoll-signal
*/
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>

int kq;
int sfd;

struct context {
	void (*handler)(struct context *obj);
};

void unix_signal_handler(struct context *obj)
{
	struct signalfd_siginfo si;
	int r = read(sfd, &si, sizeof(si));
	assert(r == sizeof(si));

	int sig = si.ssi_signo;
	printf("Received UNIX signal via epoll: %d\n", sig);
}

void main()
{
	// create kqueue object
	kq = epoll_create(1);
	assert(kq != -1);

	struct context obj = {};
	obj.handler = unix_signal_handler;

	// block default signal handler
	int sig = SIGUSR1;
	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, sig);
	sigprocmask(SIG_BLOCK, &mask, NULL);

	// prepare signalfd-descriptor
	sfd = signalfd(-1, &mask, SFD_NONBLOCK);
	assert(sfd != -1);

	// register signalfd in KQ
	struct epoll_event event;
	event.events = EPOLLIN | EPOLLET;
	event.data.ptr = &obj;
	assert(0 == epoll_ctl(kq, EPOLL_CTL_ADD, sfd, &event));

	struct epoll_event events[1];
	int timeout_ms = -1;
	int n = epoll_wait(kq, events, 1, timeout_ms);
	assert(n > 0);

	struct context *o = events[0].data.ptr;
	if (events[0].events & (EPOLLIN | EPOLLERR))
		o->handler(o); // handle signalfd event

	close(sfd); // close signalfd descriptor
	close(kq);
}
