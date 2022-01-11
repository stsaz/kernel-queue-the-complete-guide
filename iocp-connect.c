/* Kernel Queue The Complete Guide: iocp-connect.c: HTTP/1 client
Link with -lws2_32
*/
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#include <assert.h>
#include <stdio.h>

HANDLE kq;
int quit;
LPFN_CONNECTEX KQConnectEx;

// the structure associated with a socket descriptor
struct context {
	SOCKET sk;
	void (*rhandler)(struct context *obj);
	void (*whandler)(struct context *obj);
	OVERLAPPED read_ctx;
	OVERLAPPED write_ctx;
	char sendbuf[1];
	int data_offset;
};

// some forward declarations
void obj_write(struct context *obj);
void obj_read(struct context *obj);

void obj_prepare(struct context *obj)
{
	// create and prepare socket
	obj->sk = socket(AF_INET, SOCK_STREAM, 0);
	assert(obj->sk != INVALID_SOCKET);

	// make socket as non-blocking
	int nonblock = 1;
	ioctlsocket(obj->sk, FIONBIO, (unsigned long*)&nonblock);

	int val = 1;
	assert(0 == setsockopt(obj->sk, 0, TCP_NODELAY, (char*)&val, sizeof(int)));

	// attach socket to KQ
	assert(NULL != CreateIoCompletionPort((HANDLE)obj->sk, kq, (ULONG_PTR)obj, 0));

	// get extended socket function pointers
	void *func = NULL;
	DWORD res;
	GUID guid = WSAID_CONNECTEX;
	WSAIoctl(obj->sk, SIO_GET_EXTENSION_FUNCTION_POINTER, (void*)&guid, sizeof(GUID), &func, sizeof(void*), &res, 0, 0);
	KQConnectEx = func;
	assert(KQConnectEx != NULL);
}

void obj_connect(struct context *obj)
{
	if (obj->whandler == NULL) {
		struct sockaddr_in baddr = {};
		baddr.sin_family = AF_INET;
		char ip4[] = {127,0,0,1};
		*(int*)&baddr.sin_addr = *(int*)ip4;
		assert(0 == bind(obj->sk, (struct sockaddr*)&baddr, sizeof(struct sockaddr_in)));

		// begin asynchronous connection
		struct sockaddr_in addr = {};
		addr.sin_family = AF_INET;
		addr.sin_port = ntohs(64000);
		*(int*)&addr.sin_addr = *(int*)ip4;
		BOOL ok = KQConnectEx(obj->sk, (struct sockaddr*)&addr, sizeof(struct sockaddr_in), NULL, 0, NULL, &obj->write_ctx);
		assert(ok || GetLastError() == ERROR_IO_PENDING);
		obj->whandler = obj_connect;
		return;

	} else {
		DWORD res;
		BOOL ok = GetOverlappedResult(NULL, &obj->write_ctx, &res, 0);
		assert(ok);
	}

	printf("Connected\n");
	obj_write(obj);
}

void obj_write(struct context *obj)
{
	const char data[] = "GET / HTTP/1.1\r\nHost: hostname\r\nConnection: close\r\n\r\n";
	int r;
	if (obj->whandler == NULL) {
		r = send(obj->sk, data + obj->data_offset, sizeof(data)-1 - obj->data_offset, 0);
		if (r > 0) {
			// sent some data

		} else if (r < 0 && GetLastError() == WSAEWOULDBLOCK) {
			// the socket's write buffer is full
			memset(&obj->write_ctx, 0, sizeof(obj->write_ctx));
			obj->sendbuf[0] = data[obj->data_offset];
			DWORD wr;
			BOOL ok = WriteFile((HANDLE)obj->sk, obj->sendbuf, 1, &wr, &obj->write_ctx);
			assert(ok || GetLastError() == ERROR_IO_PENDING);
			obj->whandler = obj_write;
			return;

		} else {
			assert(0); // fatal error
		}

	} else {
		DWORD res;
		BOOL ok = GetOverlappedResult(NULL, &obj->write_ctx, &res, 0);
		assert(ok);
		r = res;
		obj->whandler = NULL;
	}

	// sent some data
	obj->data_offset += r;
	if (obj->data_offset != sizeof(data)-1) {
		// we need to send the complete request
		obj_write(obj);
		return;
	}

	printf("Sent HTTP request.  Receiving HTTP response...\n");
	obj_read(obj);
}

void obj_read(struct context *obj)
{
	char data[64*1024];
	if (obj->rhandler == NULL) {
		int r = recv(obj->sk, data, sizeof(data), 0);
		if (r > 0) {
			// received some data
			printf("%.*s", r, data);
			obj_read(obj);
			return;

		} else if (r == 0) {
			// server has finished sending data

		} else if (r < 0 && GetLastError() == WSAEWOULDBLOCK) {
			// the socket's read buffer is empty
			memset(&obj->read_ctx, 0, sizeof(obj->read_ctx));
			DWORD rd;
			BOOL ok = ReadFile((HANDLE)obj->sk, NULL, 0, &rd, &obj->read_ctx);
			assert(ok || GetLastError() == ERROR_IO_PENDING);
			obj->rhandler = obj_read;
			return;
		} else {
			assert(0); // fatal error
		}

	} else {
		DWORD res;
		BOOL ok = GetOverlappedResult(NULL, &obj->read_ctx, &res, 0);
		assert(ok);
		obj->rhandler = NULL;
		obj_read(obj);
		return;
	}

	quit = 1;
}

void main()
{
	// initialize sockets
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);

	// create KQ object
	kq = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	assert(kq != NULL);

	struct context obj = {};
	obj_prepare(&obj);
	obj_connect(&obj);

	// wait for incoming events from KQ and process them
	while (!quit) {
		OVERLAPPED_ENTRY events[1];
		ULONG n = 0;
		int timeout_ms = -1; // wait indefinitely
		BOOL ok = GetQueuedCompletionStatusEx(kq, events, 1, &n, timeout_ms, 0);
		assert(ok);
		assert(n == 1);

		// now process each signalled event
		for (int i = 0;  i != n;  i++) {
			struct context *o = (void*)events[i].lpCompletionKey;
			if (events[i].lpOverlapped == &o->read_ctx)
				o->rhandler(o); // handle read event
			else if (events[i].lpOverlapped == &o->write_ctx)
				o->whandler(o); // handle connect/write event
		}
	}

	closesocket(obj.sk);
	CloseHandle(kq);
}
