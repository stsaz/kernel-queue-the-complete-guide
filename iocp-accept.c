/* Kernel Queue The Complete Guide: iocp-accept.c: Accept socket connection
Link with -lws2_32
*/
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#include <assert.h>
#include <stdio.h>

HANDLE kq;
LPFN_ACCEPTEX KQAcceptEx;
LPFN_GETACCEPTEXSOCKADDRS KQGetAcceptExSockaddrs;

struct context {
	void (*handler)(struct context *obj);
	OVERLAPPED accept_ctx;
	unsigned char local_peer_addrs[(sizeof(struct sockaddr_in6) + 16) * 2];
	SOCKET client_sock;
};

void accept_handler(struct context *obj)
{
	DWORD res;
	BOOL ok = GetOverlappedResult(NULL, &obj->accept_ctx, &res, 0);
	assert(ok);

	printf("Accepted socket connection via IOCP\n");

	// get local and peer network address of the accepted socket
	int len_local = 0, len_peer = 0;
	struct sockaddr *addr_local, *addr_peer;
	KQGetAcceptExSockaddrs(obj->local_peer_addrs, 0, sizeof(struct sockaddr_in6) + 16, sizeof(struct sockaddr_in6) + 16, &addr_local, &len_local, &addr_peer, &len_peer);

	char buf[1000];
	int r = recv(obj->client_sock, buf, 1000, 0);
	assert(r >= 0);

	char data[] = "HTTP/1.1 200 OK\r\n\r\nHello";
	assert(sizeof(data)-1 == send(obj->client_sock, data, sizeof(data)-1, 0));
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
	obj.handler = accept_handler;

	// create the listening socket
	SOCKET lsock = socket(AF_INET, SOCK_STREAM, 0);
	assert(lsock != INVALID_SOCKET);

	// make socket as non-blocking
	int nonblock = 1;
	ioctlsocket(lsock, FIONBIO, (unsigned long*)&nonblock);

	struct sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_port = ntohs(64000);
	assert(0 == bind(lsock, (struct sockaddr*)&addr, sizeof(struct sockaddr_in)));
	assert(0 == listen(lsock, SOMAXCONN));

	assert(NULL != CreateIoCompletionPort((HANDLE)lsock, kq, (ULONG_PTR)&obj, 0));

	// get extended socket function pointers
	void *func = NULL;
	DWORD res;
	GUID guid = WSAID_ACCEPTEX;
	WSAIoctl(lsock, SIO_GET_EXTENSION_FUNCTION_POINTER, (void*)&guid, sizeof(GUID), &func, sizeof(void*), &res, 0, 0);
	KQAcceptEx = func;
	func = NULL;
	GUID guid2 = WSAID_GETACCEPTEXSOCKADDRS;
	WSAIoctl(lsock, SIO_GET_EXTENSION_FUNCTION_POINTER, (void*)&guid2, sizeof(GUID), &func, sizeof(void*), &res, 0, 0);
	KQGetAcceptExSockaddrs = func;
	assert(KQAcceptEx != NULL && KQGetAcceptExSockaddrs != NULL);

	// try to accept a connection synchronously
	obj.client_sock = accept(lsock, NULL, 0);
	assert(obj.client_sock == INVALID_SOCKET && GetLastError() == WSAEWOULDBLOCK); // we require this for our example

	// begin asynchronous operation
	obj.client_sock = socket(AF_INET, SOCK_STREAM, 0);
	assert(obj.client_sock != INVALID_SOCKET);
	memset(&obj.accept_ctx, 0, sizeof(obj.accept_ctx));
	BOOL ok = KQAcceptEx(lsock, obj.client_sock, obj.local_peer_addrs, 0, sizeof(struct sockaddr_in6) + 16, sizeof(struct sockaddr_in6) + 16, &res, &obj.accept_ctx);
	assert(ok || GetLastError() == ERROR_IO_PENDING);

	// wait for incoming events from KQ
	OVERLAPPED_ENTRY events[1];
	ULONG n = 0;
	int timeout_ms = -1; // wait indefinitely
	ok = GetQueuedCompletionStatusEx(kq, events, 1, &n, timeout_ms, 0);
	assert(ok);
	assert(n == 1);

	struct context *o = (void*)events[0].lpCompletionKey;
	o->handler(o); // handle socket accept event

	closesocket(obj.client_sock);
	closesocket(lsock);
	CloseHandle(kq);
}
