#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <unistd.h>

#define DEFAULT_PORT 8080
#define BACKLOG 16
#define REQUEST_BUFFER 4096

/* parses text into int. retruns 0 if success and -1 if failure. */
int port_parse (const char *text, int *port) {
	char *p_end;
	errno = 0;
	long value = strtol(text, &p_end, 10);

	if (errno == ERANGE || value < 1 || value > 65535 || p_end == text || *p_end != '\0') {
		return -1;
	}

	*port = (int)value;

	return 0;
}

int send_all(int fd, const char *buffer, size_t length) {
	size_t sent = 0;

	while (sent < length) {
		ssize_t n = send(fd, buffer + sent, length - sent, 0);

		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}

		if (n == 0) {
			return -1;
		}

		sent += (size_t)n;
	}
	return 0;
}

int main(int argc, char** argv) {
	int port = DEFAULT_PORT;

	/* Parsing arguments */
	if (argc >= 2) {
		if (port_parse(argv[1], &port) != 0) {
			fprintf(stderr, "Provided port '%s' is invalid.\n", argv[1]);
			return EXIT_FAILURE;
		}
	}

	/* If client disconnects before respond is sent. */
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		perror("signal");
		return EXIT_FAILURE;
	}

	/* Creating TCP socket. Getting a file descriptor we are going to interact with. */
	int server_fd = socket(AF_INET, SOCK_STREAM, 0);

	if (server_fd == -1) {
		perror("socket");
		return EXIT_FAILURE;
	}

	/* Setting a quick restart after stopping server. */
	int reuse = 1;

	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
		perror("setsockopt");
		close(server_fd);
		return EXIT_FAILURE;
	}

	/* Where listen */
	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;

	/* [TODO] Only on localhost [for now] */
	server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	/* Byte ordering */
	server_addr.sin_port = htons((uint16_t)port);

	/* Binding address and port */
	if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
		perror("bind");
		close(server_fd);
		return EXIT_FAILURE;
	}

	/* Start listening */
	if (listen(server_fd, BACKLOG) == -1) {
		perror("listen");
		close(server_fd);
		return EXIT_FAILURE;
	}

	printf("HTTP server listening on http://127.0.0.1:%d\n", port);

	while (1) {
		struct sockaddr_in client_addr;
		socklen_t client_len = sizeof(client_addr);

		/* waiting for client */
		int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

		if (client_fd == -1) {
			if (errno == EINTR) {
				continue;
			}
			perror("accept");
			break;
		}

		printf("Client connected.\n");

		/* read request */
		char request[REQUEST_BUFFER];

		ssize_t received = recv(client_fd, request, sizeof(request) - 1, 0);

		if (received == -1) {
			perror("recv");
			close(client_fd);
			continue;
		}

		request[received] = '\0';

		printf("___ request ___\n");
		printf("%s", request);
		printf("___ _______ ___\n");

		const char response[] =
			"HTTP/1.1 200 OK\r\n"
			"Content-Type: text/plain\r\n"
			"Content-Length: 14\r\n"
			"Connection: close\r\n"
			"\r\n"
			"This is http!\n";

		if (send_all(client_fd, response, strlen(response)) == -1) {
			perror("send");
		}

		close(client_fd);
		printf("Client disconnected.\n");
	}
	close(server_fd);
	return EXIT_SUCCESS;
}
