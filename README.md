# CS2106 Lab 2 Exercise 6

_Zhu Hanming | A0196737L_

> This file was written as a Markdown file. Please feel free to rename this file to README.md for a better viewing experience.

## Contents

- [Directory Structure](#directory-structure)
- [Getting Started](#getting-started)
- [IPC Mechanism](#ipc-mechanism)
- [Limitations](#limitations)
- [Commands](#commands)

## Directory Structure

The current directory structure is as shown below:

```bash
ex6/
├─main.c
├─sm.h
├─sm.c
├─smc.c
└─README.md
```

### `main.c`

Contains the server-side code, i.e. this is the main program that runs in the background and waits for "requests" from the client `smc` program.

In addition to the above, it also contains the original user command-handling code.

### `sm.h`

Contains `struct` declarations and function declarations. No modification was made.

### `sm.c`

Contains the service manager functions from ex 1-5. No modification was made.

### `smc.c`

Contains the client-side code. This is the program that launches into a prompt, allowing users to send commands to the `sm` service manager running in the background.

## Getting Started

To run this program, first run

```bash
make
gcc -std=c99 -Wall -Wextra smc.c -o smc
```

to compile the files into executables. Thereafter, run

```bash
./sm    ## to start the service manager
./smc   ## to launch the prompt
```

The first command should return control to the terminal after starting the service manager.

### I got an error: `Error binding socket in start_server: 48`

What this error message means is that you have already binded a socket to the socket name we will be using, i.e. likely you ran `./sm` once before, but did not terminate it.

You should be able to run `./sm` again and it will bind correctly. However, you will need to clean up the previous `sm` process yourself, via:

```bash
ps x | grep sm
kill -9 <PID of sm process>
```

The next time you need to terminate your service manager background process, you can use the client smc program, as such:

```bash
./smc
sm> shutdown
```

Alternatively, to just unbind the current socket from the socket name, you can run `unbind sm_socket` in your command line.

### Commands Available

To view commands available for execution, you can refer to the [Commands](#commands) section below.

## Mechanisms

The server process first daemonises itself, before creating a socket and binding itself to a socket name. We will run through each step in greater detail below.

### Daemonising

> Acknowledgements: The code was adapted from [The Geek Stuff](https://www.thegeekstuff.com/2012/02/c-daemon-process/)

In the initial daemonising step, we want to create a child process and kill our parent process so that control can return to the terminal.

To do so, we first need to `fork`, then for `pid > 0`, we do a `exit(0)`. The child process will then become an orphan and be taken over by the init process.

Subsequently, we want to create a new session for this child process since the child process is not a process group leader. [This will result in a it becoming the leader of the new session, the process group leader of the new process group, and it will have no controlling terminal.](https://linux.die.net/man/2/setsid)

We then close the `stdin` of this daemonised process, since we will be reading all inputs via a socket afterwards.

### IPC

For IPC, we are using UNIX domain sockets. Unlike Internet sockets, which I am currently learning about in CS2105 and uses ports and an underlying network protocol, a UNIX domain socket uses the file system as its address name space.

For our socket, we are using the path name `"sm_socket"` as the socket name.

We thus perform the following five steps:

1. Create one end of the socket. We will be using `AF_UNIX`, i.e. UNIX communication protocol, and `SOCK_STREAM`, i.e. a stream socket. We thus now have the socket descriptor, `sock`.
2. We then set the options for this socket. This was actually done in an attempt to resolve errno 48, i.e. `address already in use`. However, this unfortunately does not resolve it, and I am not sure why.
3. We then `bind` the socket descriptor to an address. This address uses the `sockaddr_un` struct (`un` is for UNIX), though it is casted to the general `sockaddr` struct when binding. Now, the system knows the name of the host socket.
4. We then get the server to `listen` for connection requests from a client process. We can set an arbitrary number of requests that can be backlogged, which is 5 in our case.
5. Lastly, we use `accept` to set up an actual connection with our client process. What `accept` returns us is a new socket descriptor, specifically for this client. Potentially, we can `fork` at this point so that our parent can continue accepting new connections, but as there is no explicit need for concurrency for this exercise, this has not been done.

And the server is now ready! We can now use `read` and `write` once a connection with the client process is established.

For client, we would want to perform a similar set-up process for the socket descriptor. Then, instead of `bind`ing and `listen`ing, we would instead `connect`, and use `read` and `write` to communicate with the server.

> Note: For my implementation, the services executed are maintained across connections. It will only reset upon `shutdown`. This can be easily changed by shifting `sm_init` and `sm_free` into the for loop instead.

## Strengths

For this section, we will quickly run through some of the strengths of this current implementation.

### Minimal changes to `main.c` and no changes to `sm.c`

One strength of this socket implementation is that not much changes were needed to the existing code. This is because by setting up the socket and redirecting stdout and stderr to the socket descriptor, the rest of the programs can continue on without noticing any difference. Abstraction is well-maintained using this implementation.

### Use of UNIX domain sockets

I think the number one strength of sockets (personally) is the ease of bi-directional communication. It's also relatively simpler to use (especially familiarisation through CS2105).

However, there are certain limitations of UNIX domain sockets. It seems like their performance is not as good as that of pipes and shared memory: [Benchmarking done on StackOverflow](https://stackoverflow.com/a/54164058). But since we are not too worried about performance for this exercise, and that I was also trying to explore new ways to facilitate IPC, I feel that UNIX domain sockets was a good choice.

## Limitations

There are, however, some limitations to my implementation, some due to the lack of time, and many due to the lack of experience and required knowledge.

### Buffer size of 8192

I am currently using a buffer of size 8192 for reading from socket. This means two things:

1. I am not handling the reading carefully enough.
2. I cannot handle responses and messages larger than 8192.

For the second one, especially, I have yet to implement any mechanism that takes care of it. One possible way is to have the server send back some unique string at the end to denote that the full message has been conveyed. But this has its own complexities as well.

### Potential timeout due to `poll`

Another possible pitfall is the `poll`ing I do prior to reading. As some commands, e.g. `start /bin/sleep 10` do not result in a response, the blocking `read` on the socket will cause the client to simply wait forever. As such, I've implemented a polling mechanism using `poll` to check if there's something to read within a given timeout (in this case, 0.1s), and if so, read it, else I will skip the reading.

This also means that if there's a command that takes extremely long to respond, its response may be dropped if let's say no more `read`ing occurs afterwards, or it may be printed out along with the next command's output, which is less than ideal.

> Alternative solution I tried but decided not to use:
>
> One other solution I explored was the forking of a child process that will simply loop in the background and print out anything it reads immediately. Though it worked, this resulted in output being printed very sporadically, and the `sm>` prompt was often displaced.

### Reliance on socket name

There is also some reliance on the socket name being unbinded. This is a bug that I have yet to resolve, and has to be remedied using `unbind` or `kill`.

## Commands

### `start`

Issues a command for the service manager to run.

```bash
## Examples
sm> start /bin/echo hello
hello
sm> start /bin/sleep 20
sm> start /bin/echo hello | /bin/echo world | /bin/cat
world
```

### `status`

Shows the statuses of commands executed

```bash
## Examples
sm> status
0. /bin/echo (PID 88419): Exited
1. /bin/sleep (PID 88488): Running
2. /bin/cat (PID 88557): Exited
```

The output is of the folloing format:

```bash
<service_no>. <last process path> (PID <last process PID>): <last process status>
```

### `stop`

Stops all processes in a specified service. The service can be specified using its index seen from `status`.

```bash
sm> stop 1
sm> status
0. /bin/echo (PID 88419): Exited
1. /bin/sleep (PID 88488): Exited
2. /bin/cat (PID 88557): Exited
```

### `wait`

Waits on all processes in a specified service. The service can be specified using its index seen from `status`.

```bash
sm> start /bin/sleep 20
sm> wait 3 ## will wait for ~20 seconds
sm>
```

### `startlog`

The same as `start`, except that the final output is redirected to a log file `serviceN.log`, where `N` is the index of the service seen from `status`.

```bash
sm> startlog /bin/echo hello | /bin/echo world | /bin/cat ## creates service4.log and outputs to it
sm> status
0. /bin/echo (PID 88419): Exited
1. /bin/sleep (PID 88488): Exited
2. /bin/cat (PID 88557): Exited
3. /bin/sleep (PID 89963): Exited
4. /bin/cat (PID 90699): Exited
```

### `showlog`

Reads and prints the log file created from `startlog`. The service for which you want to see the log file for can be specified using its index seen from `status`.

```bash
sm> showlog 4
world
```

Note that if the log file does not exist, it will output `service has no log file`. This may occur if the service was not started with `startlog`, or some other program deleted the log file.

### `exit`

This command terminates the client (prompt) process, but leaves the background service manager process running.

### `shutdown`

This command terminates both the background service manager process as well as the client (prompt) process.
