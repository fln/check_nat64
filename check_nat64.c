#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/time.h>

static int	server_port = 46464;
static char	*server_address = NULL;
static double	warning_time = -1.;
static double	critical_time = -1.;

static int	server_socket = -1;
static int	socket_timeout = 10;
static pid_t	pid = -1;


long deltime (struct timeval tv)
{
        struct timeval now;
        gettimeofday (&now, NULL);
        return (now.tv_sec - tv.tv_sec)*1000000 + now.tv_usec - tv.tv_usec;
}

int start_server(int port)
{
	struct sockaddr_in serveraddr;
	int	serversock;
	int	optval = 1;

	serversock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (serversock < 0) {
		printf("check_nat64: socket failed: %s\n", strerror(errno));
		exit(3);
	}

	memset(&serveraddr, 0, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = INADDR_ANY;
	serveraddr.sin_port = htons(port);

	setsockopt(serversock, SOL_SOCKET, SO_REUSEADDR, &optval,
							sizeof(optval));

	if (bind(serversock, (struct sockaddr *) &serveraddr,
				sizeof(serveraddr)) < 0) {
		printf("check_nat64: bind failed: %s\n", strerror(errno));
		exit(3);
	}

	if (listen(serversock, 1) < 0) {
		printf("check_nat64: listen failed: %s\n", strerror(errno));
		exit(3);
	}

	return serversock;
}

void listen_server(int serversock)
{
	int                clientsock;
	struct sockaddr_in clientaddr;
	socklen_t          addrlen = sizeof(clientaddr);
	char               msg[256] = "";

	while (1) {
		clientsock = accept(serversock, (struct sockaddr *) &clientaddr, &addrlen);
		if (clientsock < 0) {
			continue;
		}
		sprintf(msg, "%s,%d\n",
			inet_ntoa(clientaddr.sin_addr),
			ntohs(clientaddr.sin_port));
		write(clientsock, msg, strlen(msg));

		shutdown(clientsock, SHUT_RDWR);
		close(clientsock);
        }

}

void sighandler_server(int sig)
{
	close(server_socket);
	exit(0);
}

void signal_sigalrm(int sig)
{
	kill(pid, SIGTERM);

	printf("check_nat64: time out %d sec", socket_timeout);
	exit(2);
}

int register_signal(int sig, void (*handler)(int))
{
	struct sigaction act;

	bzero(&act, sizeof(act));
	act.sa_handler = handler;

	return sigaction(sig, &act, NULL);
}

int connect_client(char *address, int port)
{
	int            clientsock;
	struct sockaddr_in6 serveraddr, clientaddr;
	socklen_t      addr_len;
	char           buffer[BUFSIZ];
	char           client_address[INET6_ADDRSTRLEN];
	int            len;
	int            result = 0;
	struct timeval tv;
	double         elapsed_time;
	static char   *status_codes[] = {"OK", "WARNING", "CRITICAL"};

	clientsock = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
	if (clientsock < 0) {
		printf("check_nat64: socket failed: %s\n", strerror(errno));
		return 3;
	}

	memset(&serveraddr, 0, sizeof(serveraddr));
	serveraddr.sin6_family = AF_INET6;
	serveraddr.sin6_port = htons(port);
	if(inet_pton(AF_INET6, address, &serveraddr.sin6_addr) != 1) {
		printf("check_nat64: inet_pton failed: %s\n", strerror(errno));
		return 3;
	}

	gettimeofday (&tv, NULL);

	if (connect(clientsock, (struct sockaddr *) &serveraddr,
				sizeof(serveraddr)) < 0) {
		printf("check_nat64: connect failed: %s\n", strerror(errno));
		return 2;
	}

	addr_len = sizeof(clientaddr);
	if (getsockname(clientsock, (struct sockaddr *) &clientaddr,
						&addr_len) != 0) {
		printf("check_nat64: getsockname failed: %s\n", strerror(errno));
		return 3;
	}

	inet_ntop(AF_INET6, &clientaddr.sin6_addr, client_address,
						sizeof(client_address));

	bzero(buffer, sizeof(buffer));
	len = read(clientsock, buffer, sizeof(buffer)-1);
	if(buffer[len-1] == '\n')
		buffer[len-1] = '\0';

	elapsed_time = ((double) deltime(tv)) / 1.0e3;
	if (critical_time > 0 && elapsed_time > critical_time)
                result = 2;
        else if (warning_time > 0 && elapsed_time > warning_time)
                result = 1;

	printf("NAT64 %s - (%s,%d) -> (%s)|time=%.6fms\n", status_codes[result],
			client_address, ntohs(clientaddr.sin6_port),
			buffer, elapsed_time);

	shutdown(clientsock, SHUT_RDWR);
	close(clientsock);

	return result;
}

static int process_arguments(int argc, char *argv[]);
int main(int argc, char *argv[])
{
	int	retval;

	process_arguments(argc, argv);

	server_socket = start_server(server_port);

	pid = fork();
	if (pid < 0) {
		printf("check_nat64: fork failed: %s\n", strerror(errno));
		return 3;
	}
	
	if (pid == 0) {
		/* Child */
		register_signal(SIGTERM, sighandler_server);
		listen_server(server_socket);
		/* Never returns */
	}

	/* Parent */
	close(server_socket);
	register_signal(SIGALRM, signal_sigalrm);
	alarm(socket_timeout);

	retval = connect_client(server_address, server_port);

	kill(pid, SIGTERM);
	wait(0);

	return retval;
}

void print_usage(void)
{
	printf("Usage: check_nat64 -H host [-p <port>] [-w <warning time ms>] [-c <critical time ms>]\n");
}

void print_help(void)
{
	print_usage();

	printf("\n");
	printf(	"Checks if NAT64 service is working properly. It is done by\n"
		"listening on the IPv4 socket and connecting to it using IPv6 via\n"
		"NAT64. This test must be started from a dual-stack host.\n\n");

	printf(	"    -H, --hostname=ADDRESS  IPv6 address to connect to, usually nat64_prefix::your_ipv4\n");
	printf(	"    -p, --port=INTEGER      Port number to listen on IPv4 socket (default: 46464)\n");
	printf(	"    -t, --timeout=INTEGER   Seconds before connection times out (default: 10 sec)\n");
	printf(	"    -w, --warning=DOUBLE    Response time warning threshold (ms)\n");
	printf(	"    -c, --critical=DOUBLE   Response time critical threshold (ms)\n");

	printf("\nReport bugs to <julius@kriukas.lt>.\n");
}

static int process_arguments(int argc, char *argv[])
{
	int c;
	int option_index = 0;

	static struct option longopts[] = {
		{"hostname", required_argument, 0, 'H'},
		{"port", required_argument, 0, 'p'},
		{"timeout", required_argument, 0, 't'},
		{"warning", required_argument, 0, 'w'},
		{"critical", required_argument, 0, 'c'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	while(1) {
		c = getopt_long (argc, argv, "H:p:w:c:t:Vh",
				longopts, &option_index);

		if (c == -1)
			break;

		switch (c) {
		case 'H':
			server_address = optarg;
			break;
		case 'p':
			server_port = atoi(optarg);
			if (server_port < 0 || server_port > 65535) {
				printf("Invalid port\n");
				exit(3);
			}
			break;
		case 't':
			socket_timeout = atoi(optarg);
			if (socket_timeout <= 0) {
				printf("Timeout interval must be a positive integer\n");
				exit(3);
			}
			break;
		case 'w':
			warning_time = strtod (optarg, NULL);
			break;
		case 'c':
			critical_time = strtod (optarg, NULL);
			break;
		case 'h':
			print_help();
			exit(0);
			break;
		case 'V':
			printf("check_nat64 0.0.1");
			break;
		}
	}

	if (server_address == NULL) {
		print_usage();
		exit(3);
	}

	return 0;
}

