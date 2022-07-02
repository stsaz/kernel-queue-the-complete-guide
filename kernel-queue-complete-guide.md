# Kernel Queue: The Complete Guide On The Most Essential Technology For High-Performance I/O

Published originally at: https://habr.com/en/post/600123/

Author: Simon Zolin aka [stsaz](https://github.com/stsaz)

When talking about high-performance software we probably think of server software (such as nginx) which processes millions requests from thousands clients in parallel. Surely, what makes server software work so fast is high-end CPU running with huge amount of memory and a very fast network link. But even then, the software must utilize these hardware resources at maximum efficiency level, otherwise it will end up wasting the most of the valuable CPU power for unnecessary kernel-user context switching or while waiting for slow I/O operations to complete.

Thankfully, the Operating Systems have a solution to this problem, and it's called kernel event queue. Server software and OS kernel use this mechanism together to achieve minimum latency and maximum scalability (when serving a very large number of clients in parallel). In this article we are going to talk about **FreeBSD, macOS and kqueue, Linux and epoll, Windows and I/O Completion Ports**. They all have their similarities and differences which we're going to discuss here. The goal of this article is for you to understand the whole mechanism behind kernel queues and to understand how to work with each API.

_I assume you are already familiar with socket programming and with asynchronous operations, but anyway, in case you think there's something I should define or explain in more detail - send me a message, I'll try to update the article._

_Although I tried to keep this article clean of any unnecessary sentences (it's not a novel, after all), I sometimes can't stop myself from expressing my thoughts about something I like or dislike._

Contents

- [What is kernel queue?](#what-is-kernel-queue)
  - [API Principles](#api-principles)
- [FreeBSD/macOS and kqueue](#freebsdmacos-and-kqueue)
  - [Accepting socket connections with kqueue](#accepting-socket-connections-with-kqueue)
  - [Creating and closing kqueue object](#creating-and-closing-kqueue-object)
  - [Attaching socket descriptor to kqueue](#attaching-socket-descriptor-to-kqueue)
  - [Receiving events from kqueue](#receiving-events-from-kqueue)
  - [Processing received events from kqueue](#processing-received-events-from-kqueue)
  - [Establishing TCP connection with kqueue](#establishing-tcp-connection-with-kqueue)
  - [Processing stale cached events](#processing-stale-cached-events)
  - [User-triggered events with kqueue](#user-triggered-events-with-kqueue)
  - [System timer events with kqueue](#system-timer-events-with-kqueue)
  - [UNIX signals from kqueue](#unix-signals-from-kqueue)
  - [Asynchronous file I/O with kqueue](#asynchronous-file-io-with-kqueue)
- [Linux and epoll](#linux-and-epoll)
  - [Accepting socket connections with epoll](#accepting-socket-connections-with-epoll)
  - [Creating and closing epoll object](#creating-and-closing-epoll-object)
  - [Attaching socket descriptor to epoll](#attaching-socket-descriptor-to-epoll)
  - [Receiving events from epoll](#receiving-events-from-epoll)
  - [Processing received events from epoll](#processing-received-events-from-epoll)
  - [Establishing TCP connection with epoll](#establishing-tcp-connection-with-epoll)
  - [User-triggered events with epoll](#user-triggered-events-with-epoll)
  - [System timer events with epoll](#system-timer-events-with-epoll)
  - [UNIX signals from epoll](#unix-signals-from-epoll)
  - [Asynchronous file I/O with epoll](#asynchronous-file-io-with-epoll)
- [Windows and I/O Completion Ports](#windows-and-io-completion-ports)
  - [Accepting connections to a named pipe with IOCP](#accepting-connections-to-a-named-pipe-with-iocp)
  - [Creating and closing IOCP object](#creating-and-closing-iocp-object)
  - [Attaching file descriptor to IOCP](#attaching-file-descriptor-to-iocp)
  - [Receiving events from IOCP](#receiving-events-from-iocp)
  - [Processing received events from IOCP](#processing-received-events-from-iocp)
  - [User-triggered events with IOCP](#user-triggered-events-with-iocp)
  - [Establishing TCP connection with IOCP](#establishing-tcp-connection-with-iocp)
  - [Writing data to a TCP socket with IOCP](#writing-data-to-a-tcp-socket-with-iocp)
  - [Reading data from a TCP socket with IOCP](#reading-data-from-a-tcp-socket-with-iocp)
  - [Reading and writing data from/to a UDP socket with IOCP](#reading-and-writing-data-fromto-a-udp-socket-with-iocp)
  - [I/O Cancellation with IOCP](#io-cancellation-with-iocp)
  - [Accepting socket connections with IOCP](#accepting-socket-connections-with-iocp)
  - [System timer events with IOCP](#system-timer-events-with-iocp)
  - [Asynchronous file I/O with IOCP](#asynchronous-file-io-with-iocp)
- [Conclusion](#conclusion)


## What is kernel queue?

Kernel event queue (which I'm gonna call KQ from now on) is a **fast signal-delivery mechanism** which allows server software to process events from OS in a very effective way. KQ is a bunch of data living in kernel memory and a bunch of kernel code that operates with this data to notify a user-level application about various system events. A user app can't access KQ data directly (it's managed by kernel) and so it operates with KQ via the API that OS provides. There are 3 different API we're going to use here: kqueue, epoll, IOCP. However, this section describes kernel queues in general so the API doesn't matter for now.

Because the main purpose of KQ is to deliver notifications from network sockets, let me formulate the key idea in a different way:

> A user application wants to be notified when any of its sockets is ready to read or write some data, and the OS kernel serves this purpose by maintaining the list of all registered and signalled events.

**Use-case N1**
What an application achieves through KQ technology is that the app is notified about an I/O signal such as when a **network packet is received**. For example:

0. Suppose some user app created a UDP socket and registered it with a KQ along with some app-defined data (i.e. cookie).
   
1. At some point the last chunk of a UDP packet is received by network device.

2. OS now has a complete UDP packet and is ready to notify the user process as soon as it calls the KQ waiting function.

3. At some time the user app calls KQ waiting function which tells the kernel: Give me something new.

4. OS responds with Got a READ event from the socket associated with your cookie.

5. This cookie is the object pointer which the app then uses to handle the signal - read a message from UDP socket, in our case.

> Note that neither the opening of a socket, neither reading from a socket after the signal is received isn't normally the part of KQ mechanism. On UNIX we always use conventional socket functions and we use KQ functions to receive events associated with sockets. However, IOCP on Windows is different. There, I/O functions and their associated events are a part of a single mechanism. Anyway, we'll deal with IOCP later, so for now just don't bother with it - let us always think by default that KQ just delivers signals.

**Use-case N2**
Consider the next example where the user app receives a **notification after a TCP socket connects to its peer**:

0. User app creates a TCP socket and registers it with a KQ along with some app-defined data (i.e. cookie).

1. Now the app begins the procedure to connect to a remote host. Obviously, this operation can't finish immediately most of the time, because it takes some time to transmit 2 TCP packets needed for TCP connection. Moreover, what if the network link is very busy and the packets get dropped? Needless to say that TCP connection may take a long time to finish. Because of that, OS returns the control back to the app with the result Can't finish the operation immediately. While packets are being sent and received, the app keeps doing some other stuff, relying on OS to do its best to complete the connection procedure.

2. Finally, a SYN+ACK TCP packet is received from the remote host, which means it's willing to establish a TCP connection with our app. Now OS is ready to signal the app as soon as the latter becomes ready.

3. At some point the user app calls the KQ waiting function which tells the kernel: Give me something new.

4. OS responds with Got a WRITE event from the socket associated with your cookie.

5. This cookie is the object pointer which the app then uses to handle the signal - write some data to the TCP socket, in our case.

Although the primary use of KQ is I/O event notifications, it also can be used for other purposes, for example KQ can notify when a child process signals its parent (i.e. **UNIX signals delivery**), or KQ can be used to receive **notifications from a system timer**. I also explain these use-cases and show the example code in this article.

**Internal representation example**
Let's see a diagram with an example of how KQ may look like internally after a user app has registered 6 different events there (user-triggered event, I/O events, system timer), 3 of which have signalled already.

```
         KQ table example
=================================
Event    | Descriptor | Signalled?
---------+------------+-----------
USER     | #789       |
READ     | #1         |
READ     | #2         | yes
WRITE    | #2         | yes
WRITE    | #3         |
TIMER    | #456       | yes
```

In this example, both READ and WRITE events for socket #2 are in signalled state which means we can read and write data from/to this socket. And the timer event is in signalled state too which means the system timer interval has expired. The signalled flag also means that after a user app calls the function to receive events from KQ, it will receive an array of these 3 signalled events so it can process them. The kernel then may clear the signalled flag so that it won't deliver the same signals over and over again unless necessary.

Of course in reality KQ is much more complex but we don't need to know exactly how the KQ is implemented internally - we need just to understand what and when it delivers to us and how me may use it effectively. I'm not a kernel developer so I don't know much about how it's implemented inside - you have to read some Linux/FreeBSD kernel manuals and epoll/kqueue code if you are interested in this subject.

### API Principles
Now let's talk about what features all those API provide us with. In general, working with a KQ API consists of 4 steps:

1. **Create KQ object**. It's the easiest part, where we just call a function which returns the descriptor for our new KQ. We may create KQ objects as many as we want, but I don't see the point of creating more than 1 per process or thread.

2. **Attach file/socket descriptor** along with opaque user data to KQ. We have to tell the OS that we want it to notify us about any particular descriptor through a particular KQ object. How else the kernel should know what to notify us about? Here we also associate some data with the descriptor, which is usually a pointer to some kind of a structure object. How else are we going to handle the received signal? The attachment is needed only once for each descriptor, usually it's done right after the descriptor is configured and ready for I/O operations (though we can delay that until absolutely necessary to probably save a context switch). The detachment procedure usually is not needed (with the right design), so we won't even talk about it here.

3. **Wait for incoming events from KQ**. When the user app has nothing more important to do, it calls a KQ waiting function. We specify the output array of events and timeout value as parameters when calling this function. It fills our array with the information about which events signalled and how they signalled. By using an array of events rather than a single event we save CPU time on somewhat costly kernel-userspace context switches. By using timeout value we control how much time this KQ function can block internally. If we specify a positive value, then the function will block for this amount of time in case it has no events to give us. If we specify 0, it won't block at all and return immediately.

   > Some people use a small timeout value for KQ waiting functions so that they can check for some flags and variables and probably exit the waiting loop if some condition is met. But when using a small timeout value, like 50ms, they waste a lot of context switches unnecessarily. In this case OS periodically wakes up their process, even if it has nothing to do except calling the same KQ waiting function again in the next loop iteration. If you use this technique, it's most likely that there's something you do wrong. All normal software should use inifinite timeout, so the process wakes only when it is necessary.

4. **Destroy KQ object**. When we don't need a KQ object anymore, we close it so the OS can free all associated memory. Obviously, after KQ object is closed, you won't be able to receive any notifications for the file descriptors attached to it.

What I like the most about this whole KQ idea is that user code is very clear and straightforward. I think the OS must deliver a nice, clear and convenient API for their users - the API which everybody understands how it works. And the way I understand it, a canonical KQ mechanism shouldn't do or require users to do anything else except registering an event inside KQ and delivering this event from KQ to the user once it signals. There is one single promise to the user: `When an event you care about signals, I will notify you about it`. It allows the user code to be very flexible and free to do whatever it wants. Let's see an example with pseudo code which proves my point.

**Pseudo code example**

```cpp
// Pseudo code for an asynchronous HTTP/1 client
func do_logic(kq)
{
	conn := new
	conn.socket = socket(TCP, NONBLOCK)
	kq.attach(conn.socket, conn) // attach our socket along with the object pointer to KQ
	conn.connect_to_peer()
}

func connect_to_peer(conn)
{
	addr := "1.2.3.4:80"
	result := conn.socket.connect_async(addr) // initiate connection or get the result of the previously initiated connection procedure
	if result == EINPROGRESS {
		conn.write_handler = connect_to_peer
		return
	}

	print("connected to %1", addr)
	conn.write_data()
}

func write_data(conn)
{
	data[] := "GET / HTTP/1.1\r\nHost: hostname\r\n\r\n"
	result := conn.socket.send(data)
	if result == EAGAIN {
		conn.write_handler = write_data
		return
	}

	print("written %1 bytes to socket", result)
	conn.read_data()
}

func read_data(conn)
{
	data[], result := conn.socket.receive()
	if result == EAGAIN {
		conn.read_handler = read_data
		return
	}

	print("received %1 bytes from socket: %2", result, data)
}

func worker(kq)
{
	for {
		events[] := kq_wait(kq)

		for ev := events {

			conn := ev.user_data
			if ev.event == READ {
				conn.read_handler()
			}
			if ev.event == WRITE {
				conn.write_handler()
			}
		}
	}
}
```

Here we have 3 operations: connect, socket write, socket read. All 3 may block with a normal socket descriptor, so we set a non-blocking flag when creating the socket. Then we attach our socket to KQ along with the pointer to our object conn. Now we are ready to use the socket as we want, in our example we have a client socket which needs to be connected to a server.

We begin a socket connection procedure which may or may not complete immediately. In case it completes immediately - we continue with our program logic as usual, but in case it can't complete immediately it just returns `EINPROGRESS` error code. If it does so, we set `write_handler` function pointer to the name of our function we want to be called when connection is established. And then we just return from our function, because there's nothing else for us to do - we must wait. At this point our application is free to do whatever it wants - process something else or just wait until some events are received from the kernel. Which is why we have a `worker()` function. It receives events from KQ and processes them one by one, calling the appropriate handler function. In our case, `connect_to_peer()` function will be called after the TCP socket connection is established (or failed). Now we're inside this function the second time and we now get the result of our previous connect request. It may be a failure, but I don't check it here for the simplicity of our example.

In case the connection was successful, we continue by calling write_data function which sends an HTTP request to a server. Again, it may or may not complete immediately, so in case it returns with `EAGAIN` error, we set write_handler and return. As simple as that. After some time we are back inside our function again and we try to send the data once more. We may go back and forth with this logic until we have sent the complete data for our HTTP request. Once it's sent we start reading the HTTP response from server.

We are inside the `read_data()` function which starts reading data from socket. It may or may not complete immediately. If it returns with `EAGAIN`, we set `read_handler` and return. Why do we use a different name for function pointer depending on whether it's READ or WRITE event? For our example it doesn't matter, but in real life when we use full-duplex sockets, both events may fire at once, therefore we must be prepared to handle both READ and WRITE events in parallel.

Isn't it simple? The only thing we need inside our program logic is to check for return values and error codes and set handling function pointers, then after some time we're back **in the same function** to try once more **with the same code**. I love this approach - it makes everything seem very clear, even though we've just written the program logic that can easily handle thousands of connections in parallel.

## FreeBSD/macOS and kqueue
Now I think we're ready for some real code with a real API. kqueue API is the one I like the most for some reason, so let's start with it. **I strongly advise you to read this section and try to completely understand it, even if you won't use FreeBSD in your work**. There's just one syscall `kevent()` which we use with several different flags to control its behaviour.

### Accepting socket connections with kqueue
Let's see an easy example of a server which accepts a new connection by an event from kqueue.

```cpp
/* Kernel Queue The Complete Guide: kqueue-accept.c: Accept socket connection
Usage:
	$ ./kqueue-accept
	$ curl 127.0.0.1:64000/
*/
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/event.h>
#include <sys/ioctl.h>

int kq;

// the structure associated with a socket descriptor
struct context {
	int sk;
	void (*rhandler)(struct context *obj);
};

void accept_handler(struct context *obj)
{
	printf("Received socket READ event via kqueue\n");

	int csock = accept(obj->sk, NULL, 0);
	assert(csock != -1);
	close(csock);
}

void main()
{
	// create kqueue object
	kq = kqueue();
	assert(kq != -1);

	struct context obj = {};
	obj.rhandler = accept_handler;

	// create and prepare a socket
	obj.sk = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	assert(obj.sk != -1);
	int val = 1;
	setsockopt(obj.sk, SOL_SOCKET, SO_REUSEADDR, &val, 4);

	struct sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_port = ntohs(64000);
	assert(0 == bind(obj.sk, (struct sockaddr*)&addr, sizeof(addr)));
	assert(0 == listen(obj.sk, 0));

	// attach socket to kqueue
	struct kevent events[2];
	EV_SET(&events[0], obj.sk, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, &obj);
	EV_SET(&events[1], obj.sk, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, &obj);
	assert(0 == kevent(kq, events, 2, NULL, 0, NULL));

	// wait for incoming events from kqueue
	struct timespec *timeout = NULL; // wait indefinitely
	int n = kevent(kq, NULL, 0, events, 1, timeout);
	assert(n > 0);

	// process the received event
	struct context *o = events[0].udata;
	if (events[0].filter == EVFILT_READ)
		o->rhandler(o); // handle read event

	close(obj.sk);
	close(kq);
}
```

This code creates a TCP socket, attaches it to the newly created KQ, and waits for incoming connections. After a client is connected, it prints a message to stdout and sends an HTTP response. Inline comments explain in short form what each block is for. Now we're going to describe all this in detail.

### Creating and closing kqueue object
To create a new KQ object, we call `kqueue()` function which returns the descriptor or -1 on error. KQ object is usually stored in the global context (in our case - it's just a global variable) because we need it all the time while our app is running. We close KQ object with `close()`.

```cpp
	kq = kqueue();
	...
	close(kq);
```

### Attaching socket descriptor to kqueue
So how do we attach a file descriptor to our kqueue object? First, we prepare an object where we define:

* Which file descriptor we want to associate with KQ. This value can be a socket, UNIX signal, or an arbitrary user-defined ID, depending on the type of event we want to register.

* Which event we are interested in. For I/O events this value must be either `EVFILT_READ` or `EVFILT_WRITE`. For UNIX signals it's `EVFILT_SIGNAL`, for timers it's `EVFILT_TIMER`, for user events it's `EVFILT_USER`, but they will be explained later in separate sections.

* What we want `kevent()` to do: `EV_ADD` attaches descriptor to KQ. `EV_CLEAR` flag prevents the event from unnecessary signalling (it's explained later).

* What object pointer we associate with our file descriptor. Normally, this object contains at least 2 fields: file descriptor itself and the function pointer which handles the event.

To set the above parameters we use `EV_SET()` macro for convenience but you may also use the struct kevent fields directly.

Then, we call `kevent()` function which processes all our events we supplied to it and returns 0 on success.

```cpp
	struct kevent events[2];
	EV_SET(&events[0], sk, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, obj);
	EV_SET(&events[1], sk, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, obj);
	kevent(kq, events, 2, NULL, 0, NULL);
```

### Receiving events from kqueue
To receive events from KQ we must have a place to store them so the kernel can prepare the data for us - we need an array for signalled events, array of `struct kevent` objects. Note that it doesn't necessarily mean that events are stored in kernel memory in the same form - it's just what the kernel prepares for us. Normally, the events are stored in the chronological order, meaning that the event with index 0 has signalled before the event with a larger index, but this isn't important for us, because user software is prepared to handle the events in any order anyway. In our example we use an array for the maximum of 1 events. Only very busy software can benefit from using a large array here to minimize the number of context switching.

We call `kevent()` function and pass the array to it along with a timeout value defining how long it can block in case there are no signalled events. The function returns the number of signalled events, or 0 if timeout value has passed before anything signalled, or -1 on error. Normally, we call KQ waiting functions in a loop like so:

```cpp
	while (!quit) {
		struct timespec *timeout = NULL; // wait indefinitely
		struct kevent events[1];
		int n = kevent(kq, NULL, 0, events, 1, &timeout);
		if (n < 0 && errno == EINTR)
			continue; // kevent() interrupts when UNIX signal is received
	}
```

As you can see we also check the return value for error `EINTR` which means that while `kevent()` was waiting for events a UNIX signal has been received and processed by a UNIX signal handling function. This behaviour allows us to easily handle some important global flags that signal handlers may set. For example we may handle `SIGINT` signal which is sent after the user presses Ctrl+C within the terminal window. Then `SIGINT` signal handling function may set some kind of quit flag to indicate we should exit our app. `kevent()` then returns with `EINTR`, and we check for quit value and exit the loop in this case.

### Processing received events from kqueue
To process an event which we have received from KQ previously we have to know what to do with it. But all events look pretty much the same to us at this point. That's why we used an object pointer with `EV_SET()` to associate it with each event. Now we can simply call an event handling function. We get this pointer by accessing `struct kevent.udata` field. For full-duplex I/O we need either 2 different handling functions or a single handler which will check itself which filter has signalled. Since all KQ mechanisms have their different ways, I recommend you to go with 2-handlers approach and choose which handler to execute here, at the lowest level, to simplify the higher level code.

```cpp
	struct context *o = events[i].udata;
	if (events[i].filter == EVFILT_READ)
		o->rhandler(o); // handle read event
	else if (events[i].filter == EVFILT_WRITE)
		o->whandler(o); // handle write event
```

Do you remember the `EV_CLEAR` flag we supplied to KQ when we attached socket to it? Here's why we need to use it. For example, after KQ returns a READ event to us, it won't signal again until we drain all the data from this socket, i.e. until `recv()` returns with `EAGAIN` error. This mechanism prevents from signalling the same event over and over again each time we call KQ waiting function, thus improving overall performance. The software that can't deal with `EV_CLEAR` behaviour most probably has a design flaw.

### Establishing TCP connection with kqueue
Now let's see how to correctly use `connect()` on a TCP socket with kqueue. What makes this use-case special is that there is no function that could return the result of previous `connect()` operation. Instead, we must use `struct kevent` to get the error code. Here's an example.


```cpp
	int r = connect(sk, ...);
	if (r == 0) {
		... // connection completed successfully

	} else if (errno == EINPROGRESS) {
		// connection is in progress
		struct kevent events[1];
		int n = kevent(kq, NULL, 0, events, 1, &timeout);

		if (events[0].filter == EVFILT_WRITE) {
			errno = 0;
			if (events[i].flags & EV_EOF)
				errno = events[0].fflags;
			... // handle TCP connection result depending on `errno` value
		}

	} else {
		... // fatal error
	}
```

Suppose that we created a non-blocking TCP socket, attached it to KQ, and now we begin the connection procedure. If it completes successfully right away, then we can read or write data to it immediately, and it isn't what we are talking about here. But if it returns -1 with `EINPROGRESS` error, we should wait until OS notifies about with the result of the procedure. And here's the main thing: when `EVFILT_WRITE` event is received we test `struct kevent.flags` field for `EV_EOF` and if it's set, then it means that `connect()` has failed. In this case `struct kevent.fflags` field contains the error number - the same error that a blocking `connect()` call would set.

> I have to say that I don't like this whole logic with getting an error code from `struct kevent` because it forces me to multiply branches in my code. Another reason behind that is because it's kqueue-specific stuff - for example on Linux we have to call `getsockopt(..., SOL_SOCKET, SO_ERROR, ...)` to get error code. But on the other hand, on FreeBSD we don't need to perform another syscall which probably outweighs both of my points above, so in the end I think it's alright.

Let's see the complete example of a simple HTTP/1 client that connects, sends request and receives response - all via KQ. But of course you may notice that our code for handling WRITE event from KQ is useless here, because our request is very small and should always fit into the empty socket buffer, i.e. `send()` will always complete immediately. But I think it's OK for a sample program - the goal is to show you the general principle.

```cpp
/* Kernel Queue The Complete Guide: kqueue-connect.c: HTTP/1 client
Usage:
	$ nc -l 127.0.0.1 64000
	$ ./kqueue-connect
*/
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/event.h>

int kq;
int quit;

// the structure associated with a socket descriptor
struct context {
	int sk;
	void (*rhandler)(struct context *obj);
	void (*whandler)(struct context *obj);
	int data_offset;
};

void obj_write(struct context *obj);
void obj_read(struct context *obj);

void obj_prepare(struct context *obj)
{
	// create and prepare socket
	obj->sk = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	assert(obj->sk != -1);

	int val = 1;
	assert(0 == setsockopt(obj->sk, 0, TCP_NODELAY, (char*)&val, sizeof(int)));

	// attach socket to KQ
	struct kevent events[2];
	EV_SET(&events[0], obj->sk, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, obj);
	EV_SET(&events[1], obj->sk, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, obj);
	assert(0 == kevent(kq, events, 2, NULL, 0, NULL));
}

void obj_connect(struct context *obj)
{
	if (obj->whandler == NULL) {
		// begin asynchronous connection
		struct sockaddr_in addr = {};
		addr.sin_family = AF_INET;
		addr.sin_port = ntohs(64000);
		char ip4[] = {127,0,0,1};
		*(int*)&addr.sin_addr = *(int*)ip4;

		int r = connect(obj->sk, (struct sockaddr*)&addr, sizeof(struct sockaddr_in));
		if (r == 0) {
			// connection completed successfully
		} else if (errno == EINPROGRESS) {
			// connection is in progress
			obj->whandler = obj_connect;
			return;
		} else {
			assert(0); // fatal error
		}

	} else {
		assert(errno == 0); // connection is successful
		obj->whandler = NULL; // we don't want any more signals from KQ
	}

	printf("Connected\n");
	obj_write(obj);
}

void obj_write(struct context *obj)
{
	const char data[] = "GET / HTTP/1.1\r\nHost: hostname\r\nConnection: close\r\n\r\n";
	int r = send(obj->sk, data + obj->data_offset, sizeof(data)-1 - obj->data_offset, 0);
	if (r > 0) {
		// sent some data
		obj->data_offset += r;
		if (obj->data_offset != sizeof(data)-1) {
			// we need to send the complete request
			obj_write(obj);
			return;
		}
		obj->whandler = NULL;

	} else if (r < 0 && errno == EAGAIN) {
		// the socket's write buffer is full
		obj->whandler = obj_write;
		return;
	} else {
		assert(0); // fatal error
	}

	printf("Sent HTTP request.  Receiving HTTP response...\n");
	obj_read(obj);
}

void obj_read(struct context *obj)
{
	char data[64*1024];
	int r = recv(obj->sk, data, sizeof(data), 0);
	if (r > 0) {
		// received some data
		printf("%.*s", r, data);
		obj_read(obj);
		return;

	} else if (r == 0) {
		// server has finished sending data

	} else if (r < 0 && errno == EAGAIN) {
		// the socket's read buffer is empty
		obj->rhandler = obj_read;
		return;
	} else {
		assert(0); // fatal error
	}

	quit = 1;
}

void main()
{
	// create KQ object
	kq = kqueue();
	assert(kq != -1);

	struct context obj = {};
	obj_prepare(&obj);
	obj_connect(&obj);

	// wait for incoming events from KQ and process them
	while (!quit) {
		struct kevent events[1];
		struct timespec *timeout = NULL; // wait indefinitely
		int n = kevent(kq, NULL, 0, events, 1, timeout);
		if (n < 0 && errno == EINTR)
			continue; // kevent() interrupts when UNIX signal is received
		assert(n > 0);

		// now process each signalled event
		for (int i = 0;  i != n;  i++) {
			struct context *o = events[i].udata;

			errno = 0;
			if (events[i].flags & EV_EOF)
				errno = events[i].fflags;

			if (events[i].filter == EVFILT_READ
				&& o->rhandler != NULL)
				o->rhandler(o); // handle read event

			if (events[i].filter == EVFILT_WRITE
				&& o->whandler != NULL)
				o->whandler(o); // handle write event
		}
	}

	close(obj.sk);
	close(kq);
}
```

_Note that macOS doesn't support_ `SOCK_NONBLOCK` flag in `socket()` - you should set the socket as nonblocking manually.

### Processing stale cached events
One of the most interesting aspects of programming with KQ is handling the events in which we in fact are not interested anymore. Here's what may happen when we use KQ _carelessly_:

* We attach a socket to KQ for both READ and WRITE events.

* We keep performing normal operations on a socket, reading and writing to it occasionally.

* At some point both READ and WRITE events get signalled.

* We use an array of events for KQ waiting function and it **returns 2 events to us for the same socket**.

* We start handling the first event which happens to be a READ event.

* We call READ event handling function, it processes this event and comes to a conclusion that the client object should be closed, because it has sent some invalid data. We close the socket and destroy the object. And everything seems to be correct, because after the socket is closed KQ won't signal us with it anymore. But remember that we have called a KQ waiting function some time ago and it has already returned 2 events to us. We've handled the first event just now, but the second event is still in our array of event objects and is yet to be processed in the next iteration of the loop.

* We start handling the second event which is WRITE event in our case. We take our object data associated with the event and we try to call the handling function. BAM! We hit the memory region we have just destroyed while handling the READ event.

This can happen at any time while our app is running and we don't know anything in advance - we need to correctly determine such cases and handle them as they occur. Note that this situation isn't just limited to full-duplex sockets, but to any KQ event in general. Suppose a timer signal has fired and we decided to close a client connection, but its socket has signalled already, and there's an associated event already in our cache - we just don't know it yet, because the timer signal has occurred just before it. So unless we always limit the number of received events from KQ to 1, we must always be ready to handle this situation - we can't prevent it from happening. And of course, there's the same problem on Linux with epoll, so it's not just kqueue-only stuff.

So how are we going to solve this problem? Thankfully, it's already solved by Igor Sysoev (the great man who initially wrote nginx) long time ago. Here's the trick.

```cpp
struct context {
	int sk;
	void (*handler)(struct context *obj);
	int flag;
};

void handler_func(struct context *obj)
{
	if (...) {
		goto finish; // an error occurred
	}
	...

finish:
	close(obj->sk);
	obj->flag = !obj->flag; // turn over the safety flag
}

void main()
{
	...
	struct context *obj = ...;
	obj->flag = 0;
	struct kevent events[2];
	void *ptr = (void*)((size_t)obj | obj->flag); // pass additional flag along with the object pointer to KQ
	EV_SET(&events[0], obj->sk, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, ptr);
	EV_SET(&events[1], obj->sk, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, ptr);
	kevent(kq, events, 2, NULL, 0, NULL);

	...

	int n = kevent(kq, NULL, 0, events, 8, &timeout);
	for (...) {
		void *ptr = events[i].udata;
		struct context *obj = (void*)((size_t)ptr & ~1); // clear the lowest bit

		int flag = (size_t)ptr & 1; // check the lowest bit
		if (flag != obj->flag)
			continue; // don't process this event

		obj->handler(obj);
	}
}
```

Explanation:

* When we attach a socket to KQ, we also associate our object pointer with it. But we can actually **store some more information** there - not just the pointer. In our example here, we set the value of `struct context.flag` field (which is 0 at first) as the lowest bit value along with our object pointer. It works because all structure objects that contain a pointer are aligned to at least 4 or 8 bytes by default. In other words, the lowest bit for any object pointer is always 0, and we can use it for our own purposes.

* After we have received an event from KQ, we **clear the lowest bit** when converting the user data pointer to our object pointer. All event handlers are called as usual without any problem.

  ```cpp
    kevent(EV_ADD, 0x???????0)

    events[] = kevent()
    object = events[0].udata.bits[1..31]

    // object   | events[0]
    // ---------+-------------------
    // {flag=0} | {udata=0x???????0}

    if 0 == 0 // TRUE
    object.handler()
  ```      
* But if inside the event handling function we decided to close the socket, we **mark our object as unused** - in our case we set an internally stored safety flag to 1. We don't free the memory associated with our object so that we can access this value later.

  ```cpp
	read_handler() {
		close(object.socket)
		object.flag = 1
	}
  ```

* When we start the processing of the next (cached) event for the same socket, we **compare the lowest bit** from the associated data pointer with the flag stored within our object. In our case they don't match, which means that we don't want this event to be processed. Note that C doesn't allow logical bit operations on pointers, hence the somewhat ugly cast to integer type and back.

  ```cpp
	object = events[1].udata.bits[1..31]

	// object   | events[1]
	// ---------+-------------------
	// {flag=1} | {udata=0x???????0}

	if 1 == 0 // FALSE
  ```

* After we have processed all cached events we may free the memory allocated for our objects marked as unused. Or we may decide not to free the memory for our objects at all - because the next iteration may need to create a new object and we would need to allocate memory again. Instead, we may **store all unused objects in a list** (or array) and free them only when our app is closing the whole KQ subsystem.

* Next time we use the same object pointer (if we didn't free its memory), the flag is still set to 1 and so it is passed to KQ as the lowest bit of the user data pointer when we attach a new socket descriptor to KQ.

  ```cpp
	kevent(EV_ADD, 0x???????1)

	events[] = kevent()
	object = events[0].udata.bits[1..31]

	// object   | events[0]
	// ---------+-------------------
	// {flag=1} | {udata=0x???????1}

	if 1 == 1 // TRUE
	  object.handler()
  ```

* And again, when we have finished working with this object, we turn it over and set the flag to 0. After that, the values from KQ (the old bit value 1) and the flag value inside the object (now 0) don't match, therefore the handling function won't be called which is exactly what we want.

  ```cpp
	read_handler() {
		close(object.socket)
		object.flag = 0
	}

	object = events[1].udata.bits[1..31]

	// object   | events[1]
	// ---------+-------------------
	// {flag=0} | {udata=0x???????1}

	if 0 == 1 // FALSE
  ```

Some people don't use the lowest bit approach to handle the problem with stale events. They just use a list where they put the unused objects until they can free them (after each iteration or by a timer signal). Imagine how this can slow down the processing when **every cached event starting at index 1 should be checked against some data in a container** - a search must be performed which wastes CPU cycles. I don't know what's the reasoning for doing so, but I really don't see how it's too hard to use the lowest bit trick and turn it over once in a while. So I advise using a list of unused objects only to save on countless memory allocations and deallocations and not for deciding whether an event is stale or not.

### User-triggered events with kqueue
When we use an infinite timeout in KQ waiting function, it blocks forever until it can return an event. Similar to how it returns with `EINTR` after UNIX signal is processed, we can force it to return at any time by sending a user event to KQ. To do it we first register a `EVFILT_USER` event in KQ, then we can trigger this event via `NOTE_TRIGGER`. Here's an example.

```cpp
/* Kernel Queue The Complete Guide: kqueue-user.c: User-triggered events */
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/event.h>

int kq;

struct context {
	void (*handler)(struct context *obj);
};

struct context user_event_obj;

void user_event_obj_handler(struct context *obj)
{
	printf("Received user event via kqueue\n");
}

// application calls this function whenever it wants to add a new event to KQ
// which will execute user_event_obj_handler()
void trigger_user_event()
{
	user_event_obj.handler = user_event_obj_handler;

	struct kevent events[1];
	EV_SET(&events[0], 1234, EVFILT_USER, 0, NOTE_TRIGGER, 0, &user_event_obj);
	assert(0 == kevent(kq, events, 1, NULL, 0, NULL));
}

void main()
{
	// create kqueue object
	kq = kqueue();
	assert(kq != -1);

	// register user event with any random ID
	// note that user data is NULL here
	struct kevent events[1];
	EV_SET(&events[0], 1234, EVFILT_USER, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, NULL);
	assert(0 == kevent(kq, events, 1, NULL, 0, NULL));

	trigger_user_event();

	struct timespec *timeout = NULL; // wait indefinitely
	int n = kevent(kq, NULL, 0, events, 1, timeout);
	assert(n > 0);

	struct context *o = events[0].udata;
	if (events[0].filter == EVFILT_USER)
		o->handler(o); // handle user event

	close(kq);
}
```

To register a new user event in KQ we have to supply an arbitrary ID which we later will use to trigger it, I use `1234` just for example. Note also that contrary to when attaching socket descriptor, we don't set user data pointer at this point.

```cpp
	EV_SET(&events[0], 1234, EVFILT_USER, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, NULL);
```
Then at some point we decide to trigger the event with `NOTE_TRIGGER`. And now we can pass an object pointer which the waiting function will return back to us after the event signals.

```cpp
	EV_SET(&events[0], 1234, EVFILT_USER, 0, NOTE_TRIGGER, 0, obj);
```
After this event is returned from KQ and gets processed, it won't signal again until we trigger it next time - that's because we set `EV_CLEAR` flag when registering the event.

### System timer events with kqueue
Another facility that KQ offers us is system timers - we can order KQ to periodically send us a timer event. A timer is necessary when we want to close connections for the clients that are silent for too long, for example. In kqueue we register a timer with `EVFILT_TIMER` and process its events as usual. For example:

```cpp
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
```
Sometimes we don't need a periodic timer, but the timer which will signal us only once, i.e. a **one-shot timer**. It's simple with kqueue - we just use EV_ONESHOT flag:

```cpp
	EV_SET(&events[0], 1234, EVFILT_TIMER, EV_ADD | EV_ENABLE | EV_ONESHOT, 0, period_ms, obj);
```
Keep in mind that KQ timers are not designed so that you can use a million of them - you just need 1. Even if our software handles a million clients, we still need 1 system timer, because all we need is to just periodically wake up and process the oldest entries in our _timer queue_ which we handle ourselves with our own code. Timer queue mechanism isn't a part of KQ, it isn't in scope of this article, but it's just a linked-list or rbtree container where the first item is the oldest.

### UNIX signals from kqueue
Another convenient feature of KQ is handling UNIX signals. We register a UNIX signal handler with `EVFILT_SIGNAL` and pass the signal number we want to attach to. When processing an event, we get the signal number from `struct kevent.ident` field. Example:

```cpp
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
```

Note that KQ will return the event only after the signal has been processed with its normal mechanism, i.e. the handlers registered with `sigaction()` - KQ can't completely replace this mechanism. But KQ makes it easier to handle signals such as `SIGCHLD`, when a child process signals its parent about its closure.

### Asynchronous file I/O with kqueue
Trying to read or write data from/to files on disk is a little bit harder than performing I/O on sockets. One of the reasons behind this is because sockets don't have an offset to read at - we always read from one end, while with files we may issue several parallel operations at different offsets. And how can OS notify us about which particular operation has completed? That's why the kernel has to provide us with a new API for dealing with file AIO. And FreeBSD is the only OS that has a complete implementation of asynchronous file read/write operations. Sadly, it doesn't look anything like the rest of what we've talked about here so far. What we would have expected from kqueue is the mechanism which just signals us when some data is available to read or write from/to a file - exactly the same way we work with sockets. But no, what we have here instead is the mechanism of asynchronous file operations (only read/write operations are supported) which hold (or "lock") the user data buffer internally and don't signal at all unless the whole data chunk is transferred, depriving us from the flexibility we already got used to when working with sockets. _Now all this looks more like IOCP, which can't be a good sign_. But anyway, since I promised you to show everything I know about KQ, let's see how this mechanism works with files.

* First, we enable the AIO subsystem by loading the appropriate kernel module:

  ```cpp
	% kldload aio
  ```
* The next step is to prepare an AIO object of type struct aiocb and call the appropriate function, that is `aio_read()` or `aio_write()`.

* Then we immediately check for operation status with `aio_error()`, because it may have finished already before we even called KQ waiting function.

* When we know that everything is fine and the operation is in progress, we wait for a signal from KQ as usual.

* When we receive an event of type `EVFILT_AIO` from KQ we may read `struct kevent.ident` field to get `struct aiocb*` object pointer associated with the operation. This is how we can distinguish several parallel operations on the same file descriptor from each other.

Here's the minimal example of how to read from a file asynchronously:

```cpp
/* Kernel Queue The Complete Guide: kqueue-file.c: Asynchronous file reading
Usage:
	$ echo 'Hello file AIO' >./kqueue-file.txt
	$ ./kqueue-file
*/
#include <aio.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/event.h>

int kq;

struct context {
	int fd;
	struct aiocb acb;
	int (*handler)(struct context *obj, struct aiocb *acb);
};

void file_io_result(const char *via, int res)
{
	printf("Read from file via %s: %d\n", via, res);
}

int file_aio_handler(struct context *obj, struct aiocb *acb)
{
	int r = aio_error(acb);
	if (r == EINPROGRESS) {
		return 0; // AIO in progress
	} else if (r == -1) {
		file_io_result("kqueue", -1); // AIO completed with error
		return -1;
	}

	r = aio_return(acb);
	file_io_result("kqueue", r); // AIO completed successfully
	return 1;
}

void main()
{
	// create KQ object
	kq = kqueue();
	assert(kq != -1);

	// open file descriptor and prepare the associated object
	int fd = open("./kqueue-file.txt", O_RDONLY, 0);
	assert(fd != -1);
	struct context obj = {};
	obj.handler = file_aio_handler;

	// associate the AIO operation with KQ and user object pointer
	memset(&obj.acb, 0, sizeof(obj.acb));
	obj.acb.aio_sigevent.sigev_notify_kqueue = kq;
	obj.acb.aio_sigevent.sigev_notify = SIGEV_KEVENT;
	obj.acb.aio_sigevent.sigev_notify_kevent_flags = EV_CLEAR;
	obj.acb.aio_sigevent.sigev_value.sigval_ptr = &obj;

	void *buf = malloc(4*1024);

	// specify operation parameters
	obj.acb.aio_fildes = fd;
	obj.acb.aio_buf = buf; // destination buffer
	obj.acb.aio_nbytes = 4*1024; // max number of bytes to read
	obj.acb.aio_offset = 0; // offset to begin reading at

	// begin file AIO operation
	obj.acb.aio_lio_opcode = LIO_READ;
	if (0 != aio_read(&obj.acb)) {
		if (errno == EAGAIN || errno == ENOSYS || errno == EOPNOTSUPP) {
			// no resources to complete this I/O operation
			// or AIO module isn't loaded
			// or the system can't perform AIO on this file
		} else {
			file_io_result("aio_read", -1);
			return; // fatal error
		}

		// AIO doesn't work - perform synchronous reading at the specified offset
		int r = pread(fd, buf, obj.acb.aio_nbytes, obj.acb.aio_offset);
		file_io_result("pread", r);
		return;
	}

	// asynchronous file reading has started, but might be finished already
	if (0 != file_aio_handler(&obj, &obj.acb))
		return;

	// asynchronous file reading is in progress, now wait for the signal from KQ
	struct kevent events[1];
	struct timespec *timeout = NULL; // wait indefinitely
	int n = kevent(kq, NULL, 0, events, 1, timeout);

	struct context *o = events[0].udata;
	if (events[0].filter == EVFILT_AIO) {
		struct aiocb *acb = (void*)events[0].ident;
		o->handler(o, acb); // handle file AIO event
	}

	free(buf);
	close(fd);
	close(kq);
}
```

The main function here is `aio_read()`, which processes our request and starts the asynchronous operation. It returns 0 if the operation has started successfully.

```cpp
	struct aiocb acb = ...; // fill in `acb` object
	acb.aio_lio_opcode = LIO_READ;
	aio_read(&acb); // begin file AIO operation
```
However, if something is wrong, it returns with an error, and we must handle several cases here:

```cpp
	if (errno == EAGAIN || errno == ENOSYS || errno == EOPNOTSUPP) {
		// no resources to complete this I/O operation
		// or AIO module isn't loaded
		// or the system can't perform AIO on this file
	} else {
		// fatal error
	}
```
We can handle some types of errors by issuing a **synchronous file reading**:

```cpp
	// AIO doesn't work - perform synchronous reading at the specified offset
	int r = pread(fd, buf, size, off);
```
For **writing data to a file asynchronously** we use the same template, except the opcode and the function are different, while everything else is the same:

```cpp
	struct aiocb acb = ...; // fill in `acb` object
	acb.aio_lio_opcode = LIO_WRITE;
	aio_write(&acb); // begin file AIO operation
```
And of course we use a different function for the **synchronous file writing** in case file AIO doesn't work:

```cpp
	int r = pwrite(fd, buf, size, off);
```

> Though file AIO is good to have, it's still not enough for high-performance file servers, because such software needs not just file AIO in terms of reading/writing file data, but disk AIO in general. What for? In our example above we use asynchronous file reading only, but we know that the very first step in working with files in UNIX is opening a file descriptor. However, we just can't perform an asynchronous file open - it's not supported. And in the real life an `open()` syscall may take a whole second to complete on a busy machine - it may block our worker thread for a long time. In real life we also want to call `stat()` or `fstat()` on a file path or a file descriptor. And OS doesn't provide a way to call them asynchronously either, except calling them inside another thread. So even if file AIO can help sometimes, it's still somewhat lame and incomplete. And considering the fact that other OS don't have an appropriate file AIO implementation at all, it may be a better choice not to use any of those APIs at all. It may be better to use a thread pool with a file operations queue and dispatch operations to another thread. Inside a new thread the operations will be performed _synchronously_. And then it will signal the main thread when the operation is complete.

## Linux and epoll
epoll API is very similar to kqueue for socket I/O notifications, though it's quite different for other purposes. Here we use `epoll_ctl()` to attach file descriptors to KQ and we use `epoll_wait()` to receive events from KQ. There are several more syscalls we're going to use for user events, timers and UNIX signals: `eventfd()`, `timerfd_create()`, `timerfd_settime()`, `signalfd()`. Overall, the functionality of epoll is the same as kqueue but sometimes with a slightly different approach.

The key **differences of epoll and kqueue** are:

* `kevent()` supports attaching many file descriptors in a single syscall, `epoll_ctl()` does not - we must call it once for each fd. `kevent()` even allows us to attach fd's AND wait for new events in a single syscall, `epoll_wait()` can't do that.

* epoll may join 2 events (`EPOLLIN` and `EPOLLOUT`) into 1 event object in case both READ and WRITE events signal. This is contrary to kqueue which always returns 1 event object per 1 event (`EVFILT_READ` or `EVFILT_WRITE`). Be careful with epoll here, always check if it's alright to execute event handling functions, because otherwise you risk calling WRITE event handler after you have finalized the object in READ event handler.

* epoll makes it somewhat harder to use additional KQ functionality such as UNIX signals, system timers or user events - see below. In kqueue, however, it looks all the same.

What is different but also similar between epoll and kqueue:

* We attach a socket to epoll using `EPOLLIN` | `EPOLLOUT` flags which is the same as registering 2 separate events `EVFILT_READ` and `EVFILT_WRITE` with kqueue.

* `EPOLLET` flag in epoll is the same thing as `EV_CLEAR` flag in kqueue - it prevents epoll from signalling us about the same event more than once until we drain all data from the socket. And it's the same situation when writing to a streaming (e.g. TCP) socket - we must keep calling `write()` until it returns with `EAGAIN` error - only then we may expect epoll to signal us.

### Accepting socket connections with epoll
Here's a minimal example for accepting a socket connection.

```cpp
/* Kernel Queue The Complete Guide: epoll-accept.c: Accept socket connection
Usage:
	$ ./epoll-accept
	$ curl 127.0.0.1:64000/
*/
#include <assert.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <sys/socket.h>

int kq;

// the structure associated with a socket descriptor
struct context {
	int sk;
	void (*rhandler)(struct context *object);
};

void accept_handler(struct context *obj)
{
	printf("Received socket READ event via epoll\n");

	int csock = accept(obj->sk, NULL, 0);
	assert(csock != -1);
	close(csock);
}

void main()
{
	// create KQ object
	kq = epoll_create(1);
	assert(kq != -1);

	struct context obj = {};
	obj.rhandler = accept_handler;

	// create and prepare a socket
	obj.sk = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	assert(obj.sk != -1);
	int val = 1;
	setsockopt(obj.sk, SOL_SOCKET, SO_REUSEADDR, &val, 4);

	struct sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_port = ntohs(64000);
	assert(0 == bind(obj.sk, (struct sockaddr*)&addr, sizeof(addr)));
	assert(0 == listen(obj.sk, 0));

	// attach socket to KQ
	struct epoll_event event;
	event.events = EPOLLIN | EPOLLOUT | EPOLLET;
	event.data.ptr = &obj;
	assert(0 == epoll_ctl(kq, EPOLL_CTL_ADD, obj.sk, &event));

	// wait for incoming events from KQ
	struct epoll_event events[1];
	int timeout_ms = -1; // wait indefinitely
	int n = epoll_wait(kq, events, 1, timeout_ms);
	assert(n > 0);

	// process the received event
	struct context *o = events[0].data.ptr;
	if (events[0].events & (EPOLLIN | EPOLLERR))
		o->rhandler(o); // handle read event

	close(obj.sk);
	close(kq);
}
```

### Creating and closing epoll object
`epoll_create()` function returns new KQ object descriptor which we close as usual with `close()`.

```cpp
	kq = epoll_create(1);
	...
	close(kq);
```

### Attaching socket descriptor to epoll
`EPOLLIN` flag means that we want the kernel to notify us when a READ event signals, and `EPOLLOUT` flag is the same for a WRITE event. `EPOLLET` prevents epoll from returning to us the same signal unnecessarily. We set our object pointer with `struct epoll_event.data.ptr` field.

```cpp
	struct epoll_event event;
	event.events = EPOLLIN | EPOLLOUT | EPOLLET;
	event.data.ptr = obj;
	epoll_ctl(kq, EPOLL_CTL_ADD, sk, &event);
```

### Receiving events from epoll
`epoll_wait()` function blocks until it has something to return to us, or until the timeout value expires. The function returns the number of signalled events which may be 0 only in case of timeout. It also returns with `EINTR` exactly like `kevent()` after a UNIX signal has been received, so we must always handle this case.

```cpp
	while (!quit) {
		struct epoll_event events[1];
		int timeout_ms = -1; // wait indefinitely
		int n = epoll_wait(kq, events, 1, timeout_ms);
		if (n < 0 && errno == EINTR)
			continue; // epoll_wait() interrupts when UNIX signal is received
	}
```

### Processing received events from epoll
We get the events flags by reading struct epoll_event.events which we should always test for `EPOLLERR` too, because otherwise we can miss an event we're waiting for. To get the associated user data pointer, we read struct epoll_event.data.ptr value.

I emphasize once again that you should be careful not to invalidate user object memory region inside READ event handler, or the program may crash inside the WRITE event handler which is executed next. To handle this situation you may just always set event handler function pointers to NULL when you don't expect them to signal. An alternative solution may be to clear `EPOLLOUT | EPOLLERR` flags from struct epoll_event.events field from inside READ event handler.

```cpp
	struct context *o = events[i].data.ptr;

	if ((events[i].events & (EPOLLIN | EPOLLERR))
		&& o->rhandler != NULL)
		o->rhandler(o); // handle read event

	if ((events[i].events & (EPOLLOUT | EPOLLERR))
		&& o->whandler != NULL)
		o->whandler(o); // handle write event
```

### Establishing TCP connection with epoll
As with kqueue, epoll also has a way to notify us about the status of TCP connection. But unlike kqueue which sets the error number for us inside `struct kevent` object, epoll doesn't do that (it can't do that). Instead, we get the error number associated with our socket via `getsockopt(..., SOL_SOCKET, SO_ERROR, ...)`.

```cpp
	int err;
	socklen_t len = 4;
	getsockopt(obj->sk, SOL_SOCKET, SO_ERROR, &err, &len);
	errno = err;
	... // handle TCP connection result depending on `errno` value
```

### User-triggered events with epoll
There are slightly more things to do with epoll rather than with kqueue to handle user-triggered events:

First, we create a new file descriptor with `eventfd()` which we then attach to KQ.

* We trigger a user event at any time by writing an 8 byte value to our eventfd descriptor. In our case this value is an object pointer.

* After we receive an event from KQ, we read an 8 byte value from eventfd descriptor. We can convert this value to an object pointer. Remember that we need to keep reading data from eventfd descriptor until it returns with `EAGAIN` error, because we use `EPOLLET`.

```cpp
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
```

### System timer events with epoll
To receive the notifications from system timer with epoll we must use a timerfd object - a special file descriptor we can attach to epoll.

```cpp
/* Kernel Queue The Complete Guide: epoll-timer.c: System timer events */
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>

int kq;
int tfd;

struct context {
	void (*handler)(struct context *obj);
};

void timer_handler(struct context *obj)
{
	static int n;
	printf("Received timerfd event via epoll: %d\n", n++);

	unsigned long long val;
	read(tfd, &val, 8);
}

void main()
{
	// create kqueue object
	kq = epoll_create(1);
	assert(kq != -1);

	struct context obj = {};
	obj.handler = timer_handler;

	// prepare timerfd-descriptor
	tfd = timerfd_create(CLOCK_MONOTONIC, 0);
	assert(tfd != -1);

	// register timerfd in KQ
	struct epoll_event event;
	event.events = EPOLLIN | EPOLLET;
	event.data.ptr = &obj;
	assert(0 == epoll_ctl(kq, EPOLL_CTL_ADD, tfd, &event));

	// start periodic timer
	struct itimerspec its;
	its.it_value.tv_sec = 1;
	its.it_value.tv_nsec = 0;
	its.it_interval = its.it_value;
	assert(0 == timerfd_settime(tfd, 0, &its, NULL));

	for (;;) {
		struct epoll_event events[1];
		int timeout_ms = -1;
		int n = epoll_wait(kq, events, 1, timeout_ms);
		assert(n > 0);

		struct context *o = events[0].data.ptr;
		if (events[0].events & (EPOLLIN | EPOLLERR))
			o->handler(o); // handle timerfd event
	}

	close(tfd); // close timerfd descriptor
	close(kq);
}
```

To create a timerfd object we call `timerfd_create()` with `CLOCK_MONOTONIC` parameter which means the timer isn't affected by system time/date changes.

```cpp
	tfd = timerfd_create(CLOCK_MONOTONIC, 0);
```
To start a periodic timer, we set `struct itimerspec.it_interval` field which defines the timer interval.

```cpp
	// start periodict timer
	struct itimerspec its;
	its.it_value.tv_sec = 1;
	its.it_value.tv_nsec = 0;
	its.it_interval = its.it_value;
	timerfd_settime(tfd, 0, &its, NULL);
```
After we have received the timer event we always need to read the data from timerfd descriptor, otherwise we won't get any more events from KQ because we use `EPOLLET` flag.

```cpp
	unsigned long long val;
	read(tfd, &val, 8);
```
To create a **one-shot timer**, we set `struct itimerspec.it_interval` fields to 0:

```cpp
	// start one-shot timer
	struct itimerspec its;
	its.it_value.tv_sec = 1;
	its.it_value.tv_nsec = 0;
	its.it_interval.tv_sec = its.it_interval.tv_nsec = 0;
	timerfd_settime(tfd, 0, &its, NULL);
```

### UNIX signals from epoll
We can also receive UNIX signals with epoll. We must use a signalfd descriptor to be able to do that. To get the information about the UNIX signal we receive, we read some data from this signalfd object.

```cpp
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
```

We create a signalfd object by calling `signalfd()`.

```cpp
	sfd = signalfd(-1, &mask, SFD_NONBLOCK);
```
As with timerfd object, we also need to read from signalfd descriptor otherwise we won't get more events from KQ. But another reason why we need this is we want to know which signal has fired. We do it by reading `struct signalfd_siginfo` data from signalfd object and then reading `struct signalfd_siginfo.ssi_signo` field that contains UNIX signal number.

```cpp
	struct signalfd_siginfo si;
	read(sfd, &si, sizeof(si));
	int sig = si.ssi_signo;
```

### Asynchronous file I/O with epoll

It's even harder to work with files asynchronously when using epoll compared to the file AIO API we used earlier with kqueue. First of all, there's the requirement to use `O_DIRECT` flag when opening a file descriptor if we want it to support asynchronous operations. This flag alone creates 2 new restrictions for us: we must implement our own data caching mechanism and we must use buffers aligned to disk block size. We solve the first problem by not solving it - in our example we don't implement any data caching here. However, in the real life our performance may be very poor without it. Linux kernel has a very good disk data caching mechanism and it's a shame when we can't use it. In general, this means that there are not so many use-cases for using file AIO on Linux. The second restriction which is the requirement of using aligned buffers is just inconvenient, but at least we can handle it. We can't use a normal buffer pointer allocated by `malloc()` - we must use a pointer aligned to disk block size (in our example we use `posix_memalign()` function which returns an aligned pointer). Furthermore, we can't read or write an arbitrary amount of data, say, 1000 bytes, neither we can use an arbitrary file offset - we must also use an aligned number.

Another inconvenient thing is that GLIBC still doesn't have the wrappers for the syscalls we need to use here. We solve this problem by writing our own wrappers. It isn't hard, but just inconvenient. _Does this mean that only some crazy people use this functionality on Linux?_

The file AIO API on Linux sometimes looks similar to what we were using on FreeBSD (at least, submitting a new operation looks similar). However, the signal delivery mechanism is completely different. It uses eventfd descriptor we are already familiar with to notify us about that AIO subsystem has signalled. We then receive events from AIO and process the results of our operations. Though there's nothing here too hard to implement, the whole code just looks a little bit overcomplex and ugly. There are too many things we must control here, so the use of convenient wrappers is absolutely required in real life code.

OK, let's see the example already, or else I won't stop complaining how bad all this is.

```cpp
/* Kernel Queue The Complete Guide: epoll-file.c: Asynchronous file reading
Usage:
	$ echo 'Hello file AIO' >./epoll-file.txt
	$ ./epoll-file
*/
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/syscall.h>
#include <sys/eventfd.h>
#include <linux/aio_abi.h>

int kq;
int efd;
aio_context_t aioctx;

struct context {
	struct iocb acb;
	int (*handler)(struct context *obj);
};

void file_io_result(const char *via, int res)
{
	printf("Read from file via %s: %d\n", via, res);
}

// GLIBC doesn't have wrappers for these syscalls, so we make our own wrappers
static inline int io_setup(unsigned nr_events, aio_context_t *ctx_idp)
{
	return syscall(SYS_io_setup, nr_events, ctx_idp);
}
static inline int io_destroy(aio_context_t ctx_id)
{
	return syscall(SYS_io_destroy, ctx_id);
}
static inline int io_submit(aio_context_t ctx_id, long nr, struct iocb **iocbpp)
{
	return syscall(SYS_io_submit, ctx_id, nr, iocbpp);
}
static inline int io_getevents(aio_context_t ctx_id, long min_nr, long nr, struct io_event *events, struct timespec *timeout)
{
	return syscall(SYS_io_getevents, ctx_id, min_nr, nr, events, timeout);
}

int file_aio_handler(struct context *obj)
{
	unsigned long long n;
	for (;;) {
		int r = read(efd, &n, 8);
		if (r < 0 && errno == EAGAIN)
			break;
		assert(r == 8);
		// we've got `n` unprocessed events from file AIO

		for (;;) {

			struct io_event events[64];
			struct timespec timeout = {};
			r = io_getevents(aioctx, 1, 64, events, &timeout);
			if (r < 0 && errno == EINTR) {
				continue; // interrupted due to UNIX signal
			} else if (r == 0) {
				break; // no more events
			}
			assert(r > 0);

			// process result value for each event
			for (int i = 0;  i != r;  i++) {
				struct context *obj = (void*)(size_t)events[i].data;
				int result = events[i].res;
				if (result < 0) {
					errno = -result;
					result = -1;
				}
				file_io_result("epoll", result);
			}
		}
	}
	return 1;
}

void main()
{
	// create KQ object
	kq = epoll_create(1);
	assert(kq != -1);

	// prepare the associated object
	struct context obj = {};
	obj.handler = file_aio_handler;

	// open file descriptor, O_DIRECT is mandatory
	int fd = open("./epoll-file.txt", O_DIRECT | O_RDONLY, 0);
	assert(fd != -1);

	// initialize file AIO subsystem
	int aio_workers = 64;
	assert(0 == io_setup(aio_workers, &aioctx));

	// open eventfd descriptor which will pass signals from file AIO
	efd = eventfd(0, EFD_NONBLOCK);
	assert(efd != -1);

	// attach eventfd to KQ
	struct epoll_event event;
	event.events = EPOLLIN | EPOLLET;
	event.data.ptr = &obj;
	assert(0 == epoll_ctl(kq, EPOLL_CTL_ADD, efd, &event));

	// associate the AIO operation with KQ and user object pointer
	memset(&obj.acb, 0, sizeof(obj.acb));
	obj.acb.aio_data = (size_t)&obj;
	obj.acb.aio_flags = IOCB_FLAG_RESFD;
	obj.acb.aio_resfd = efd;

	void *buf;
	assert(0 == posix_memalign(&buf, 512, 4*1024)); // allocate 4k buffer aligned by 512

	// specify operation parameters
	obj.acb.aio_fildes = fd;
	obj.acb.aio_buf = (size_t)buf; // destination buffer
	obj.acb.aio_nbytes = 4*1024; // max number of bytes to read
	obj.acb.aio_offset = 0; // offset to begin reading at

	// begin file AIO operation
	obj.acb.aio_lio_opcode = IOCB_CMD_PREAD;
	struct iocb *cb = &obj.acb;
	if (1 != io_submit(aioctx, 1, &cb)) {
		if (errno == EAGAIN || errno == ENOSYS) {
			// no resources to complete this I/O operation
			// or the system can't perform AIO on this file
		} else {
			file_io_result("io_submit", -1);
			return; // fatal error
		}

		// AIO doesn't work - perform synchronous reading at the specified offset
		int r = pread(fd, buf, obj.acb.aio_nbytes, obj.acb.aio_offset);
		file_io_result("pread", r);
		return;
	}

	// asynchronous file reading is in progress, now wait for the signal from KQ
	struct epoll_event events[1];
	int timeout_ms = -1; // wait indefinitely
	int n = epoll_wait(kq, events, 1, timeout_ms);

	struct context *o = events[0].data.ptr;
	if (events[0].events & (EPOLLIN | EPOLLERR)) {
		o->handler(o); // handle file AIO event via eventfd
	}

	free(buf);
	close(fd);
	io_destroy(aioctx);
	close(kq);
}
```

First, we initialize AIO subsystem by calling `io_setup()` which returns a pointer to AIO context. We specify how many I/O workers the system should allocate for us. I don't have any good advice for you on how many workers you should allocate - the official documentation is very short - so I just use `64` for this example.

```cpp
	aio_context aioctx;
	int aio_workers = 64;
	io_setup(aio_workers, &aioctx);
```
We need an object of type struct iocb which will define the operation we want to perform. We start filling it by specifiyng how it should signal us about its completion. Here we order it to notify us via eventfd descriptor.

```cpp
	struct iocb acb = {};
	acb.aio_data = (size_t)obj; // user object pointer
	acb.aio_flags = IOCB_FLAG_RESFD;
	acb.aio_resfd = efd; // eventfd descriptor
```
Then we set the details on our operation: which file descriptor to use, where to put the data and so on.

```cpp
	acb.aio_fildes = fd;
	acb.aio_buf = (size_t)buf; // destination buffer
	acb.aio_nbytes = 4*1024; // max number of bytes to read
	acb.aio_offset = 0; // offset to begin reading at
```
Finally, we call the `io_submit()` function which will begin reading from a file and signal us via eventfd on completion.

```cpp
	acb.aio_lio_opcode = IOCB_CMD_PREAD;
	struct iocb *cb = &acb;
	io_submit(aioctx, 1, &cb);
```
Just like on FreeBSD, we also must check for error codes here on Linux and be ready to perform our I/O operation synchronously if we need to.

```cpp
	if (1 != io_submit(aioctx, 1, &cb)) {
		if (errno == EAGAIN || errno == ENOSYS) {
			// no resources to complete this I/O operation
			// or the system can't perform AIO on this file
		} else {
			file_io_result(-1);
			return; // fatal error
		}

		// AIO doesn't work - perform synchronous reading at the specified offset
		int r = pread(fd, buf, size, offset);
		file_io_result(r);
		return;
	}
```

When we receive a signal from epoll for our eventfd we've associated the file AIO operation with, we ask AIO about which operations are complete using `io_getevents()` function. It returns the number of completed operations and fills our array of `struct io_event` from where we can determine the results of each operation. Timeout value defines how long the function can block, but we don't want it to block at all, obviously, so we use a zero timeout value.

```cpp
	struct io_event events[64];
	struct timespec timeout = {};
	n = io_getevents(aioctx, 1, 64, events, &timeout);
```

And lastly, we process each event, get the user object associated with it by reading struct io_event.data field and the result of operation from struct io_event.res field. The result is the number of bytes read or written, but in case an error has occurred, this value is negative and contains the error code.

```cpp
	struct context *obj = (void*)(size_t)events[i].data;
	int result = events[i].res;
	if (result < 0) {
		errno = -result;
		result = -1;
	}
```

> Once again I'm gonna say that this whole system has a poor design and it may be better to use a thread-pool mechanism which performs file operations synchronously. A good file AIO system must have more configuration options such as how many workers should be used for each physical disk. And it shouldn't have restrictions such as buffer alignment. And it shouldn't force the user to disable in-kernel caching for a file descriptor. And it should support more file functions including `open()` and `fstat()`. But I think the main point is that the code for using file AIO should look similar to what we have with KQ and network sockets. That is, we want an API which allows us to perform a simple operation such as reading a 64k byte chunk from a file, and if it can't complete immediately, it should just return with `EINPROGRESS`. This function should also start a new background operation internally which will read at least 1 block of data from our file (GLIBC could handle that). Then we should receive a signal from KQ which tells us that now we may try again. And again we call the same code but now it returns, say, 4k bytes to us - and even so it isn't 64k we asked for, it is still fine, because we can start processing this data immediately. This is a very straightforward approach that works very well with sockets (except the poorly designed `connect()+EINPROGRESS+getsockopt()` logic). I just wish the file functions could work this way too. But I guess that since it isn't still implemented, no one needs this functionality, that's all.

## Windows and I/O Completion Ports

So are you ready for some more programming? I have to warn you that IOCP may be a little bit harder to use and understand. But even if you don't plan to use IOCP on Windows in the near future, I recommend you to at least walk through all these code samples just to learn and remember **how not to design a KQ API**. A bad example is still an example. I'm also very surprised to see that some developers even try to mimic the behaviour of IOCP in their own KQ/AIO libraries. They think that the API design of pending asynchronous operations that forces the user to have long living locked context data and buffer memory regions is better than the mechanism of simple and plain event signalling? Maybe for someone, but not for me. To prove my point, just see how the code for kqueue looks comparing to the same code for IOCP and judge for yourselves. But first, let's start with a minimal example which surprisingly looks quite clear, because we use pipes and not network sockets.

### Accepting connections to a named pipe with IOCP

```cpp
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
```

Note that some functions that return `HANDLE` type value on Windows return `NULL` on error while others return `INVALID_HANDLE_VALUE`. It's a typical design flaw from Microsoft.

### Creating and closing IOCP object

To create a new KQ object we call `CreateIoCompletionPort()` function and we pass `INVALID_HANDLE_VALUE` as the first parameter and `NULL` as the second, which means that we want to create a new KQ object and not use the already existing object. Remember that it returns `NULL` on error and not `INVALID_HANDLE_VALUE`. We close this object with `CloseHandle()` as usual.

```cpp
	HANDLE kq = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	...
	CloseHandle(kq);
```

### Attaching file descriptor to IOCP

To attach a file or socket descriptor to KQ we call the same function `CreateIoCompletionPort()` and pass file descriptor, KQ object and a user object pointer. Remember that the function name is deceiving in this case: it doesn't create a new "completion port" but only registers the file descriptor with it. And we must not close the file descriptor it returns - we only check if it's `NULL`, which means error. When registering a socket descriptor rather than a file, an explicit cast to HANDLE is required, because `SOCKET != HANDLE`, even though they are essentially of the same pointer type and can be safely cast to `void*`. Also, a cast to `ULONG_PTR` is required when passing a user object pointer, because `ULONG_PTR != void*`.

```cpp
	CreateIoCompletionPort(fd, kq, (ULONG_PTR)obj, 0); // attach file to KQ
	CreateIoCompletionPort((HANDLE)sk, kq, (ULONG_PTR)obj, 0); // attach socket to KQ
```

### Receiving events from IOCP

The new function for waiting and receiving events from IOCP is `GetQueuedCompletionStatusEx()` which is available since Windows 7. Older Windows versions don't have this function, so we must use `GetQueuedCompletionStatus()` there. I don't think it will benefit this article to resurrect old Windows, so we don't talk about old functions here. Thankfully, `GetQueuedCompletionStatusEx()` works exactly the same as in kqueue or epoll - we pass KQ object, array of events and timeout value for how long the function may block. It returns 1 on success and sets the number of signalled events.

```cpp
	OVERLAPPED_ENTRY events[1];
	ULONG n = 0;
	int timeout_ms = -1; // wait indefinitely
	BOOL ok = GetQueuedCompletionStatusEx(kq, events, 1, &n, timeout_ms, 0);
```

### Processing received events from IOCP

Now for each received event we read our object pointer from `OVERLAPPED_ENTRY.lpCompletionKey` field. Then we determine which event has signalled by reading `OVERLAPPED_ENTRY.lpOverlapped` field and comparing it with the appropriate `OVERLAPPED` object we use for our read and write operations.

```cpp
	struct context *o = events[i].lpCompletionKey;
	if (events[i].lpOverlapped == &o->read_context)
		// handle read event
	else if (events[i].lpOverlapped == &o->write_context)
		// handle write event
```

> If you are wondering about the term "overlapped" used by Windows, it is actually used as a synonym of [asynchronous](https://docs.microsoft.com/en-us/windows/win32/sync/synchronization-and-overlapped-input-and-output).

### User-triggered events with IOCP

Let's see how we can post a user event to KQ.

```cpp
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
```

There's no need to register or prepare a user event as in kqueue or epoll. We just trigger the event at any time by calling `PostQueuedCompletionStatus()` and we pass any user object pointer to it.

```cpp
	PostQueuedCompletionStatus(kq, 0, (ULONG_PTR)obj, NULL);
```

### Establishing TCP connection with IOCP

Now that we start talking about sockets in Windows, you can finally see the whole picture of how difficult it is. There are many things that seem unnecessary and overcomplicated, and the code looks ugly and bloated. Here's the code for our HTTP/1 client.

```cpp
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
```

When we're dealing with sockets on Windows, the first thing we need to do is initialize socket subsystem. We do it by calling `WSAStartup()` function as following:

```cpp
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
```

There are several Windows-specific socket functions that can be used for KQ, but they are not exported by any .dll library, so we must get their pointers at runtime using this method:

```cpp
	void *func = NULL;
	DWORD b;
	GUID guid = WSAID_CONNECTEX;
	WSAIoctl(sk, SIO_GET_EXTENSION_FUNCTION_POINTER, (void*)&guid, sizeof(GUID), &func, sizeof(void*), &b, 0, 0);
	LPFN_CONNECTEX KQConnectEx = func;
```

Here, `WSAID_CONNECTEX` is a predefined GUID value (array of bytes, to be specific). We pass it to `WSAIoctl()` function along with the valid socket descriptor to get the real function pointer, in our case `ConnectEx()` - the function that asynchronously initiates a new TCP connection.

In order for `ConnectEx()` to work, we must first bind our socket to a local address:

```cpp
	struct sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	char ip4[] = {127,0,0,1};
	*(int*)&addr.sin_addr = *(int*)ip4;
	bind(obj->sk, (struct sockaddr*)&addr, sizeof(struct sockaddr_in));
```

In this example we bind to 127.0.0.1 because we initiate connection to the loopback interface.

And then we can call the function and pass to it remote address and the `OVERLAPPED` object which of course must stay valid until the operation completes.

```cpp
	addr = ...;
	KQConnectEx(sk, (struct sockaddr*)&addr, sizeof(struct sockaddr_in), NULL, 0, NULL, &obj->write_ctx);
```

When IOCP signals about the completion of operation, we get the result with `GetOverlappedResult()`. Normally it sets the number of transferred bytes upon return, but in our case the number will be 0.

```cpp
	DWORD res;
	BOOL ok = GetOverlappedResult(NULL, &obj->write_ctx, &res, 0);
```

### Writing data to a TCP socket with IOCP

After the connection is established, we send the request data to server. And this block of code looks worse than with kqueue or epoll where we just try to read or write some data and return when the functions can't fulfil our request. It's not that easy with IOCP, because here we must initiate an ongoing I/O operation to make IOCP signal us when the state of our socket changes. These are the steps of the write-data algorithm:

1. The first thing to do is to check whether we already have a result from the previously initiated WRITE operation - we do it simply by checking the WRITE event handling function whandler.

2. On the first entry the function pointer is unset, so we enter the first branch and try to send some data using common `send()` function. Unlike on UNIX it doesn't return with `EAGAIN` error code if it can't complete the request immediately, but on Windows it returns with `WSAEWOULDBLOCK`.

3. Here's where the things get more interesting. Now we need to emulate the algorithm we have on UNIX to make IOCP signal us when the kernel is ready to accept some more data from us for this socket, but we can't do it exactly the UNIX-way. The solution is that we start an asynchronous WRITE operation which will send just 1 byte for us. It also means that the next time we are back in our function, `send()` will be able to copy some more data from us.

Why do we send just 1 byte via asynchronous operation and not the whole data we have for sending? Because the IOCP won't signal us at all until the whole data is sent. If the request is quite large, it's more likely that we'll hit our own timer signal which will kill the connection (we would think that it's stalled). That's why we send only 1 byte this way - it's much more flexible and it effectively emulates UNIX behaviour (which is canonical asynchronous I/O via KQ in my opinion). However, if you think you can benefit from sending larger chunk - go ahead and do it. Anyway, to send some data asynchronously we call `WriteFile()` and pass to it the data to send along with the `OVERLAPPED` object. Needless to say that the data buffer associated with the operation must stay valid until its completion.

4. Another interesting moment about asynchronous I/O with IOCP is that it seems like it always tries to deceive me. In our case, `WriteFile()` function may return `1` or it may return `0` with `ERROR_IO_PENDING` error, but in fact it's all the same to me. In the first case it says it has completed immediately, while in the second case it says it can't complete immediately. But in both cases IOCP will fire a signal anyway. I don't like the idea of increasing the complexity of my code while trying to support both cases correctly, therefore I just support 1 case - start an asynchronous operation then wait for its completion. Even if it's completed already - I still wait for the explicit signal. It's likely that you can adapt your own code to support both cases and save some context-switches and latency - I encourage you to try.

5. Now, the final step is handling the result from the previously initiated operation. We enter the else branch in this case and call `GetOverlappedResult()` with the same `OVERLAPPED` object. It returns the result of the operation and sets the number of bytes sent which is always 1 in our case.

```cpp
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
```

### Reading data from a TCP socket with IOCP

Reading data is very similar to writing the data - the process I described above. However, unlike what we did earlier with sending 1 byte of data to emulate UNIX behaviour, here we may achieve the exact UNIX behaviour by initiating an operation which reads 0 bytes. When we do so, IOCP will signal us at the moment the socket buffer becomes non-empty, and we can then read some data from it with `recv()`. I like this behaviour from IOCP very much - I wish everything in IOCP would work this way.

```cpp
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
```

Note that I call our `obj_read()` function again after receiving some data or after processing the event - I just think it looks clearer this way than a loop, although I don't recommend doing so in real life code, because there's a small possibility to crash the app with stack memory overuse.

### Reading and writing data from/to a UDP socket with IOCP

I have to explain the difference between reading data from a TCP and UDP sockets with IOCP. Although we used a beautiful way to get the signal from IOCP while using empty read buffer with `ReadFile()`, unfortunately it works only for TCP sockets. For UDP sockets we must provide the correct buffer with the maximum packet size, otherwise the message data will be cut (same as in UNIX, by the way). This buffer must stay valid until the operation completes. Then, after each asynchronous `ReadFile()` operation completes, I'm sure it's worth calling `recv()` in succession until it fails with `WSAEWOULDBLOCK`.

```cpp
	char buffer[64*1024];
	ReadFile((HANDLE)udp_socket, buffer, sizeof(buffer), &rd, &obj->read_ctx);
```

It's slightly different situation with writing data to UDP sockets. We must provide the whole buffer with UDP message to send() and we never need to issue asynchronous operations with WriteFile().

```cpp
	char data[] = ...;
	send(udp_socket, data, size, 0);
```

Someone once told me that he tried to force `WriteFile()` to return with `ERROR_IO_PENDING` with calling it relentlessly in a tight loop but he couldn't do it - it always succeeded. That's because sending data to a UDP socket just works this way - the kernel either sends the whole packet or the packet just gets dropped. After all, it's the user code that is responsible for UDP packet delivery - not the system.

### I/O Cancellation with IOCP

Now let me explain why this approach of IOCP with its `ReadFile`/`WriteFile` for socket I/O is worse than the UNIX approach with `recv`/`send` and KQ events. On UNIX, when the kernel can't complete my I/O request immediately, the functions just return with `EAGAIN` error and do nothing else. And then I must wait for a signal from KQ which means that now the same functions will give me some result. During the time I wait for a signal all kinds of stuff is happening in parallel, such as timer events, user interaction, signals from other processes, hardware errors, input data corruptions - all this stuff can easily change the whole picture for my app. And I want my code to be flexible in order to easily adapt to any changes.

The most basic example is the timer event. Imagine that while we're waiting for a specific socket event from KQ on UNIX, the timer has expired for this connection and I must now close the socket and destroy all data associated with this connection (i.e. destroy its context). It's very easy to do so with epoll or kqueue because I just go ahead and destroy everything immediately except the small portion which prevents from the stale cached events being handled by mistake. With IOCP, however, the OVERLAPPED object and the data associated with the ongoing asynchronous operation must both stay valid until I receive the notifications. And it's just very annoying because why would I want to receive unnecessary notifications from a thousand contexts when I have just initiated the closure of their associated sockets by myself? How does the notion of `receiving an event from a closed socket` sound in general? It sounds like nonsense to me. Maybe a schematic look can make my point clearer:

```
IOCP I/O Cancellation
====================
ReadFile(sk, rctx)
WriteFile(sk, wctx)
...
CloseHandle(sk)
...
GetQueuedCompletionStatusEx()
handle/skip wctx event
...
GetQueuedCompletionStatusEx()
handle/skip rctx event and destroy object data
```
while the same on UNIX is:

```
kqueue/epoll I/O Cancellation
=============================
recv(sk)
write(sk)
...
close(sk)
destroy object data and turn over the safety bit
```

In the end, IOCP's design is just different from that of kqueue and epoll. Maybe it's just me, but I have a feeling that there's not enough flexibility when I work with IOCP. It seems harder, it requires more code, more logical branches to support. But it is what it is.

### Accepting socket connections with IOCP

Now let's see how we can use IOCP to accept incoming socket connections.

```cpp
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
```

First, we call `accept()` as usual - we can always hope it will return immediately with a new client connetion and we won't need to go the long way with IOCP events.

```cpp
	csock = accept(lsock, NULL, 0);
```

But if it returns `INVALID_SOCKET` with `WSAEWOULDBLOCK`, it means that there are no clients at the moment and we must wait. Now we must use IOCP and a couple of new Windows-specific functions. `AcceptEx()` function begins an asynchronous operation which will signal us via IOCP when a new client connects to our listening socket. But since it's a real asynchronous operation and not just a signal like in kqueue or epoll, we must also provide it with a client socket descriptor created beforehand. Of course it must stay valid until the operation completes.

Another inconvenient thing is that we need to also supply it with the buffer where local and peer address will be stored. For some reason we must make 16 bytes more room in our buffer for each address. Please don't mind me allocating the space for IPv6 while using an IPv4 socket.

```cpp
	csock = socket(AF_INET, SOCK_STREAM, 0);

	unsigned char local_peer_addrs[(sizeof(struct sockaddr_in6) + 16) * 2];
	DWORD res;
	OVERLAPPED ctx = {};
	KQAcceptEx(lsock, csock, local_peer_addrs, 0, sizeof(struct sockaddr_in6) + 16, sizeof(struct sockaddr_in6) + 16, &res, &ctx);
```

After we receive a signal from IOCP, our client socket descriptor is a valid connected socket which we would get by calling `accept()`. The only difference now is that we must get the local and peer address by calling a new special function `GetAcceptExSockaddrs()` which finally returns us `struct sockaddr*` pointers.

```cpp
	int len_local = 0, len_peer = 0;
	struct sockaddr *addr_local, *addr_peer;
	KQGetAcceptExSockaddrs(local_peer_addrs, 0, sizeof(struct sockaddr_in6) + 16, sizeof(struct sockaddr_in6) + 16, &addr_local, &len_local, &addr_peer, &len_peer);
```

Don't forget that after processing each `AcceptEx()` signal we must proceed by calling `accept()` again, and if it fails - begin a new operation with `AcceptEx()`. We need so much more stuff to handle here in IOCP rather than very simple behaviour of the asynchronous `accept()` we have with kqueue and epoll. But this is the reality.

### System timer events with IOCP

Now let's see how we can work with timers and receive timer signals with IOCP.

```cpp
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
```

There are 3 things we need to do first - create timer object, create a thread that will process timer events and create an event object for controlling this timer thread. All 3 functions return `NULL` on error.

```cpp
	HANDLE tmr = CreateWaitableTimer(NULL, 0, NULL);
	HANDLE evt = CreateEvent(NULL, 0, 0, NULL);
	HANDLE thd = CreateThread(NULL, 0, (PTHREAD_START_ROUTINE)timer_thread, NULL, 0, NULL);
```

Inside the thread function we start the timer by calling `SetWaitableTimer()` and we pass to it the timer interval along with the timer function.

```cpp
	int period_ms = 1000;
	long long due_ns100 = (long long)period_ms * 1000 * -10;
	assert(SetWaitableTimer(htmr, (LARGE_INTEGER*)&due_ns100, period_ms, timer_func, param, 1));
```

The timer function looks this way:

```cpp
	void __stdcall timer_func(void *arg, DWORD dwTimerLowValue, DWORD dwTimerHighValue)
	{
	}
```

The function is called each time the timer interval expires. But it won't be called unless we wait for incoming timer events. We can't do it via IOCP directly - we need to use another waiting function, for example `WaitForSingleObjectEx()` with _alertable state_. We also wait for events on our event descriptor we created earlier, but in this example I don't have a code which processes it.

```cpp
	for (;;) {
		WaitForSingleObjectEx(evt, INFINITE, /*alertable*/ 1);
	}
```

But the call to our timer function alone won't wake up our main thread, so we have to post a user event to it. And only then we have a working system timer via KQ. The signal delivery mechanism for a timer can be best explained by this diagram:

```
	[Thread 2]                               [Thread 1]
	                                         main()
	                                         |- CreateThread(timer_thread)
	timer_thread()                           |
	|- SetWaitableTimer(timer_func)          |
	|- WaitForSingleObjectEx()               |
	   |- timer_func()                       |
	      |- PostQueuedCompletionStatus(obj) |
	         with obj.handler=timer_handler  |
	                                         |- GetQueuedCompletionStatusEx()
	                                            |- timer_handler()
```

But why do we need to post a user event into KQ while we can just process timer events in another thread? The reason for this is because in real world the timers are often used to close stalled connections. If we would try to do that directly from the timer thread, we will absolutely require a locking mechanism, because the KQ thread may operate with the same object at this time. So to simplify our code it's much better to do all the work from the same thread, thus eliminating the requirement to use per-object locks. The second reason for this is to emulate the behaviour of epoll and kqueue, thus making our code look similar everywhere. The third reason is just pure human logic which tells us that all we need is a single kernel queue mechanism for everything - we need a way to receive any event from the kernel and process it with the same code. We wouldn't need another thread for timer at all if Windows didn't force us.

### Asynchronous file I/O with IOCP

File I/O with IOCP looks similar to file AIO API we used with kqueue. But file AIO on FreeBSD is still better than anything else because it supports unaligned and bufferred I/O, while file AIO on Linux and Windows require us to switch off kernel disk caching and require us using aligned buffers and offsets. It's interesting that due to the nature of IOCP the code doesn't look "off-topic" as with kqueue or epoll (there, file AIO tries to glue asynchronous operations with a pure event delivery mechanism). So in this situation I can't tell anything bad about IOCP because it delivers what it's designed for - asynchronous operations. Here's the code that reads from a file asynchronously.

```cpp
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
```

File descriptor must be opened with `FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED` flags, otherwise asynchronous operations won't work.

To initiate an asynchronous file reading operation we call `ReadFile()` on a file descriptor and supply the parameters: aligned buffer pointer, aligned maximum number of bytes to read, aligned file offset. Remember that if the function fails immediately or asynchronously with `ERROR_HANDLE_EOF` error it simply means that we've reached file end. It's always interesting to see how Windows functions are designed: here we pass buffer and size as function parameters, but we pass file offset via `OVERLAPPED` object even though all 3 parameters are bound together for a single operation.

```cpp
	void *buf = ...;
	unsigned int size = ...;
	unsigned long long off = ...;
	OVERLAPPED ctx = {};
	ctx.Offset = (unsigned int)off;
	ctx.OffsetHigh = (unsigned int)(off >> 32);
	ReadFile(fd, buf, size, NULL, &ctx);
```

Writing asynchronously to a file is the same as reading, except we would call `WriteFile()`. However, writing into a new file (into an unallocated disk space) on NTFS is **always blocking** and it isn't in fact asynchronous at all. So you should first allocate enough disk space before writing new data to a file using this method to achieve truely asynchronous behaviour.

## Conclusion

So I've written here nearly everything that I know about kernel queues and I hope that this guide helped you at least with something. I've made a new repository on GitHub so that you can easily download all examples and so that we all can improve the code together: https://github.com/stsaz/kernel-queue-the-complete-guide.

I understand that the low-level KQ APIs aren't very popular because the developers would rather use some library which handles all these complexities inside than waste their time trying to understand how it all works... But come on, since when did we start to ignore the true nature of the things? If you would sit down and write your own function wrappers for all KQs you will have a small library where you will understand every single line. You will have a complete experience which will absolutely help you with your future work. It will make you smarter, it will make you better. After all, isn't it what we are here in this world for? To make ourselves better with each passing day, to grow in confidence, to grow in experience, in knowledge?
