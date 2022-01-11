/* Kernel Queue The Complete Guide: iocp-user.c: User-triggered events */
#include <windows.h>
#include <assert.h>
#include <stdio.h>

HANDLE kq;

struct context {
	void (*handler)(struct context *obj);
};

void user_event_handler(struct context *obj)
{
	printf("Received user event via IOCP\n");
}

void main()
{
	// create KQ object
	kq = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	assert(kq != NULL);

	struct context obj = {};
	obj.handler = user_event_handler;

	assert(0 != PostQueuedCompletionStatus(kq, 0, (ULONG_PTR)&obj, NULL));

	// wait for incoming events from KQ and process them
	OVERLAPPED_ENTRY events[1];
	ULONG n = 0;
	int timeout_ms = -1; // wait indefinitely
	BOOL ok = GetQueuedCompletionStatusEx(kq, events, 1, &n, timeout_ms, 0);
	assert(ok);
	assert(n == 1);

	struct context *o = (void*)events[0].lpCompletionKey;
	o->handler(o); // handle the event

	CloseHandle(kq);
}
