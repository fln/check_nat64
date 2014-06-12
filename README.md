check_nat64
===========

This is Icinga/Nagios plugin for stateful NAT64 service monitoring. It
creates a TCP session from monitoring station IPv6 address to its own IPv4
address.


How does it work?
-----------------

This plugin forks into two processes:

* Child process waits for incoming TCP connection over IPv4.
* Parent process connects to the child process over IPv6 by using NAT64 service.

For example, monitoring station have dual-stack IP configuration with IP
addresses **192.0.2.94** and **2001:db8::94**. NAT64 uses IPv6 prefix
**64:ff9b::**. Following illustration shows connection path over NAT64 service.

	+--------------+ 
	| parent       |------IPv6------------+
	| 2001:db8::94 |                      V
	|              |           +---------------------+
	|              |           | 64:ff9b::192.0.2.94 |
	+--------------+           |        NAT64        |
	| child        |           |    198.51.100.34    |
	| 192.0.2.94   |           +---------------------+
	|              |                      |
	|              |<-----IPv4------------+
	+--------------+

Requirements for monitoring server:

* It must have IPv4 and IPv6 connectivity.
* It must be reachable over IPv4 from the NAT64 service.

Installation
------------

There are no library dependencies. To compile the plug-in use the make file
included with the source code:

	$ make

Plugin binary is called 'check_nat64' you should manually copy it to your
plugins directory.


Usage
-----

Command line arguments:

    -H, --hostname=ADDRESS  IPv6 address to connect to, usually nat64_prefix::your_ipv4
    -p, --port=INTEGER      Port number to listen on IPv4 socket (default: 46464)
    -t, --timeout=INTEGER   Seconds before connection times out (default: 10 sec)
    -w, --warning=DOUBLE    Response time warning threshold (ms)
    -c, --critical=DOUBLE   Response time critical threshold (ms)

Plugin execution example:

	./check_nat64 -H 64:ff9b::192.0.2.94
	NAT64 OK - (2001:db8::94,51039) -> (198.51.100.34,51039)|time=0.901000ms

In the plugin output thre is (source_ip,source_port) pair before and after NAT64
service.

Icinga/Nagios command configuration example:

	define command {
		command_name    check_nat64
		command_line    $USER1$/check_nat64 -H $ARG1$ -p $ARG2$
	}

Icinga/Nagios service configuration example:

	define service {
		use                  generic-service
		host_name            nat64
		service_description  NAT64
		check_command        check_nat64!64:ff9b::192.0.2.94!46464
	}

Replace **64:ff9b::** with your NAT64 service prefix, and **192.0.2.94** with
your monitoring server IPv4 address (not NAT64 server address!).
