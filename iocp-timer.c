/* Kernel Queue The Complete Guide: iocp-timer.c: System timer events */
#include <windows.h>
#include <assert.h>
#include <stdio.h>

HANDLE kq;
HANDLE tmr;
HANDLE evt;

struct context {
	void (*handler)(struct context *obj);
};

void timer_handler(struct context *obj)
{
	static int n;
	printf("Received timer event via IOCP: %d\n", n++);
}

void __stdcall timer_func(void *arg, DWORD dwTimerLowValue, DWORD dwTimerHighValue)
{
	assert(0 != PostQueuedCompletionStatus(kq, 0, (ULONG_PTR)arg, NULL));
}

int __stdcall timer_thread(void *param)
{
	int period_ms = 1000;
	long long due_ns100 = (long long)period_ms * 1000 * -10;
	assert(SetWaitableTimer(tmr, (LARGE_INTEGER*)&due_ns100, period_ms, timer_func, param, 1));

	for (;;) {
		int r = WaitForSingleObjectEx(evt, INFINITE, /*alertable*/ 1);

		if (r == WAIT_IO_COMPLETION) {

		} else if (r == WAIT_OBJECT_0) {

		} else {
			assert(0);
			break;
		}
	}
	return 0;
}

void main()
{
	// create KQ object
	kq = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	assert(kq != NULL);

	struct context obj = {};
	obj.handler = timer_handler;

	// create timer object
	tmr = CreateWaitableTimer(NULL, 0, NULL);
	assert(tmr != NULL);

	// create event object to control the timer thread
	evt = CreateEvent(NULL, 0, 0, NULL);
	assert(evt != NULL);

	// start a new thread which will receive timer notifications
	HANDLE thd = CreateThread(NULL, 0, (PTHREAD_START_ROUTINE)timer_thread, &obj, 0, NULL);
	assert(thd != NULL);

	// wait for incoming events from KQ and process them
	for (;;) {
		OVERLAPPED_ENTRY events[1];
		ULONG n = 0;
		int timeout_ms = -1; // wait indefinitely
		BOOL ok = GetQueuedCompletionStatusEx(kq, events, 1, &n, timeout_ms, 0);
		assert(ok);

		struct context *o = (void*)events[0].lpCompletionKey;
		o->handler(o); // handle event
	}

	CloseHandle(tmr);
	// Note that we should correctly exit the thread with WaitForSingleObject(), but it's OK for our example
	CloseHandle(thd);
	CloseHandle(evt);
	CloseHandle(kq);
}
