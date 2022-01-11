/* Kernel Queue The Complete Guide: kqueue-signal.c: UNIX signal handler
Usage:
	$ ./kqueue-signal
	$ killall -SIGUSR1 kqueue-signal
*/
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/event.h>

int kq;

struct context {
	void (*handler)(int sig);
};

void unix_signal_handler(int sig)
{
	printf("Received UNIX signal via kqueue: %d\n", sig);
}

void main()
{
	kq = kqueue();
	assert(kq != -1);

	struct context obj = {};
	obj.handler = unix_signal_handler;

	// block default signal handler
	int sig = SIGUSR1;
	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, sig);
	sigprocmask(SIG_BLOCK, &mask, NULL);

	// register UNIX signal handler
	struct kevent events[1];
	EV_SET(&events[0], sig, EVFILT_SIGNAL, EV_ADD | EV_ENABLE, 0, 0, &obj);
	assert(0 == kevent(kq, events, 1, NULL, 0, NULL));

	struct timespec *timeout = NULL; // wait indefinitely
	int n = kevent(kq, NULL, 0, events, 1, timeout);
	assert(n > 0);

	struct context *o = events[0].udata;
	if (events[0].filter == EVFILT_SIGNAL) {
		int sig = events[0].ident;
		obj.handler(sig); // handle UNIX signal
	}

	close(kq);
}
