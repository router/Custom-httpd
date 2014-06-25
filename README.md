Custom-httpd
============

Emulation of a web server by way of an interface (using C sockets) for receiving HTTP/1.0 requests and a thread-pool to serve the requests based on a couple of scheduling algorithms (FCFS and SJF).


Compilation: Type 'make' on the terminal.

Executable name: myhttpd

Notes:

1) A file named "sys.log" will be created in the current directory. It contains the trace of every operation performed by the web server.
2) Compilation may display some warnings on the terminal.
