/* Kernel Queue The Complete Guide: iocp-file.c: Asynchronous file reading
Usage:
	echo Hello file AIO >iocp-file.txt
	iocp-file
*/
#include <windows.h>
#include <assert.h>
#include <stdio.h>

HANDLE kq;

struct context {
	void (*handler)(struct context *obj);
	HANDLE fd;
	OVERLAPPED rctx;
};

void file_io_result(struct context *obj)
{
	DWORD res;
	BOOL ok = GetOverlappedResult(NULL, &obj->rctx, &res, 0);
	if (ok)
		;
	else if (GetLastError() == ERROR_HANDLE_EOF)
		res = 0;
	else
		assert(0);

	printf("Read from file with IOCP: %d\n", res);
}

void main()
{
	// create KQ object
	kq = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	assert(kq != NULL);

	struct context obj = {};
	obj.handler = file_io_result;

	// create a named pipe, FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED are mandatory
	obj.fd = CreateFileW(L"iocp-file.txt"
		, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING
		, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED, NULL);
	assert(obj.fd != INVALID_HANDLE_VALUE);

	// attach file to KQ
	assert(NULL != CreateIoCompletionPort(obj.fd, kq, (ULONG_PTR)&obj, 0));

	void *buf = HeapAlloc(GetProcessHeap(), 0, 4*1024);
	assert(buf != NULL);

	// begin asynchronous file read operation
	memset(&obj.rctx, 0, sizeof(obj.rctx));
	unsigned int size = 4*1024;
	unsigned long long off = 0;
	obj.rctx.Offset = (unsigned int)off;
	obj.rctx.OffsetHigh = (unsigned int)(off >> 32);
	BOOL ok = ReadFile(obj.fd, buf, size, NULL, &obj.rctx);
	assert(GetLastError() != ERROR_HANDLE_EOF);
	assert(ok || GetLastError() == ERROR_IO_PENDING);

	// asynchronous file reading is in progress, now wait for the signal from KQ
	OVERLAPPED_ENTRY events[1];
	ULONG n = 0;
	int timeout_ms = -1; // wait indefinitely
	ok = GetQueuedCompletionStatusEx(kq, events, 1, &n, timeout_ms, 0);
	assert(ok);

	struct context *o = (void*)events[0].lpCompletionKey;
	o->handler(o); // handle read event

	HeapFree(GetProcessHeap(), 0, buf);
	CloseHandle(obj.fd);
	CloseHandle(kq);
}
