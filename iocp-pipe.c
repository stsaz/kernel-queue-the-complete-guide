/* Kernel Queue The Complete Guide: iocp-pipe.c: Accept connections to a named pipe */
#include <windows.h>
#include <assert.h>
#include <stdio.h>

HANDLE kq;

struct context {
	HANDLE p;
	void (*handler)(struct context *obj);
	OVERLAPPED accept_ctx;
};

void pipe_handler(struct context *obj)
{
	printf("Accepted pipe connection via IOCP\n");
}

void main()
{
	// create KQ object
	kq = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	assert(kq != NULL);

	struct context obj = {};
	obj.handler = pipe_handler;

	// create a named pipe
	obj.p = CreateNamedPipeW(L"\\\\.\\pipe\\iocp-pipe"
		, PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED
		, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT
		, PIPE_UNLIMITED_INSTANCES, 512, 512, 0, NULL);
	assert(obj.p != INVALID_HANDLE_VALUE);

	// attach pipe to KQ
	assert(NULL != CreateIoCompletionPort(obj.p, kq, (ULONG_PTR)&obj, 0));

	memset(&obj.accept_ctx, 0, sizeof(obj.accept_ctx));
	BOOL ok = ConnectNamedPipe(obj.p, &obj.accept_ctx);
	assert(ok || GetLastError() == ERROR_IO_PENDING);

	// wait for incoming events from KQ and process them
	for (;;) {
		OVERLAPPED_ENTRY events[1];
		ULONG n = 0;
		int timeout_ms = -1; // wait indefinitely
		BOOL ok = GetQueuedCompletionStatusEx(kq, events, 1, &n, timeout_ms, 0);
		assert(ok);

		// now process each signalled event
		for (int i = 0;  i != (int)n;  i++) {
			struct context *o = (void*)events[i].lpCompletionKey;
			o->handler(o); // handle event
		}
	}

	DisconnectNamedPipe(obj.p); // close accepted pipe
	CloseHandle(obj.p); // close listening pipe
	CloseHandle(kq);
}
