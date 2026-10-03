#include <stddef.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <stdbool.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include "http.h"
#include "router.h"
#include "static.h"

#define DEFAULT_PORT 8080
#define BACKLOG 16
#define REQUEST_BUFFER 16384
#define FILE_BUFFER 8192

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

int send_error(int client_fd, int status_code, const char *status_text, const char *message) {
	char body[1024];

	int body_length = snprintf(
		body,
		sizeof(body),
		"<html>"
		"<head><title>%d %s</title></head>"
		"<body><h1>%d %s</h1><p>%s</p></body>"
		"</html>\n",
		status_code,
		status_text,
		status_code,
		status_text,
		message
	);

	if (body_length < 0 || (size_t)body_length >= sizeof(body)) {
		return -1;
	}

	char header[512];

	int header_length = snprintf(
		header,
		sizeof(header),
		"HTTP/1.1 %d %s\r\n"
		"Content-Type: text/html; charset=utf-8\r\n"
		"Content-Length: %d\r\n"
		"Connection: close\r\n"
		"\r\n",
		status_code,
		status_text,
		body_length
	);

	if (header_length < 0 || (size_t)header_length >= sizeof(header)) {
		return -1;
	}

	if (send_all(client_fd, header, (size_t)header_length) == -1) {
		return -1;
	}

	return send_all(client_fd, body, (size_t)body_length);
}

int find_header_end(const char *buffer, size_t length, size_t *header_length) {
	if (length < 4) {
		return 0;
	}

	for (size_t i = 0; i <= length - 4; i++) {
		if (memcmp(buffer + i, "\r\n\r\n", 4) == 0) {
			/* four bytes of "\r\n\r\n" */
			*header_length = i + 4;
			return 1;
		}
	}

	return 0;
}

int receive_headers(int client_fd, char *buffer, size_t capacity, size_t *length, size_t *header_length) {
	*length = 0;
	*header_length = 0;

	for (;;) {
		if (*length >= capacity - 1) {
			return -2;
		}

		ssize_t received = recv(client_fd, buffer + *length, capacity - 1 - *length, 0);

		if (received < 0) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}

		if (received == 0) {
			return -3;
		}

		*length += (size_t)received;

		if (find_header_end(buffer, *length, header_length)) {
			buffer[*length] = '\0';
			return 0;
		}
	}
}

int receive_body(int client_fd, char *buffer, size_t capacity, size_t *length, size_t header_length, size_t body_length) {
	if (body_length > capacity - 1 - header_length) {
		return -2;
	}

	size_t expected_length = header_length + body_length;

	/* NOTE: the first recv() may already have contained some or all of the body.
	 * this server doesn't support multiple requests on one connection. */
	if (*length > expected_length) {
		return -4;
	}

	while (*length < expected_length) {
		ssize_t received = recv(client_fd, buffer + *length, expected_length - *length, 0);

		if (received < 0) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}

		if (received == 0) {
			/* Client closed the connection before sending the complete body. */
			return -3;
		}
		*length += (size_t)received;
	}
	buffer[*length] = '\0';
	return 0;
}

int handle_request(int client_fd, const struct http_router *router, char *buffer, size_t length) {
	struct http_request request;

	enum http_parse_result parse_result = http_parse_request(buffer, length, &request);

	if (parse_result != HTTP_PARSE_OK) {
		return -1;
	}

	struct http_response response;

	enum http_router_result route_result = http_router_dispatch(router, &request, &response);

	if (route_result == HTTP_ROUTER_NOT_FOUND) {
		if (strcmp(request.method, "GET") == 0) {
			static_file_handler(&request, &response);
		} else {
			static const char body[] = "Not Found\n";

			http_response_init(&response, 404, "Not Found");
			http_response_add_header(&response, "Content-Type", "text/plain; charset=utf-8");
			http_response_set_body(&response, body, sizeof(body) - 1);
		}
	} else if (route_result == HTTP_ROUTER_ERROR) {
		static const char body[] = "Internal Server Error\n";

		http_response_init(&response, 500, "Internal Server Error");
		http_response_add_header(&response, "Content-Type", "text/plain; charset=utf-8");
		http_response_set_body(&response, body, sizeof(body) - 1);
	}

	int result = http_response_send(client_fd, &response);
	http_response_destroy(&response);
	return result;
}

int route_hello(const struct http_request *request, struct http_response *response) {
	(void)request;

	static const char body[] = "{\"message\":\"hello from C\"}\n";

	http_response_init(response, 200, "OK");
	http_response_add_header(response, "Content-Type", "application/json; charset=utf-8");
	http_response_set_body(response, body, sizeof(body) - 1);
	return 0;
}

int route_echo(const struct http_request *request, struct http_response *response) {
	http_response_init(response, 200, "OK");
	http_response_add_header(response, "Content-Type", "text/plain; charset=utf-8");
	http_response_set_body(response, request->body, request->body_length);
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

	/* Time vars */
	time_t now;
	struct tm *t;

	bool should_close = false;
	while (!should_close) {
		struct http_router router;
		http_router_init(&router);

		if (http_router_add(&router, "GET", "/api/hello", route_hello) != 0) {
			return EXIT_FAILURE;
		}

		if (http_router_add(&router, "POST", "/api/echo", route_echo) != 0) {
			return EXIT_FAILURE;
		}

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

		now = time(NULL);
		t = localtime(&now);
		printf("%04d-%02d-%02d %02d:%02d:%02d -- Client connected\n", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);

		char request[REQUEST_BUFFER];

		size_t request_length;
		size_t header_length;

		int result = receive_headers(client_fd, request, sizeof(request), &request_length, &header_length);

		if (result == -1) {
			perror("recv");
			close(client_fd);
			continue;
		}

		if (result == -2) {
			send_error(client_fd, 431, "Request Header Fields Too Large", "The HTTP headers are too large.");
			close(client_fd);
			continue;
		}

		if (result == -3) {
			close(client_fd);
			continue;
		}

		size_t content_length;

		if (http_get_content_length_from_headers(request, header_length, &content_length) != 0) {
			send_error(client_fd, 400, "Bad Request", "Invalid Content-Length.");
			close(client_fd);
			continue;
		}

		if (content_length > sizeof(request) - 1 - header_length) {
			send_error(client_fd, 413, "Payload Too Large", "The request body is too large.");
			close(client_fd);
			continue;
		}

		result = receive_body(client_fd, request, sizeof(request), &request_length, header_length, content_length);

		if (result == -1) {
			perror("recv");
			close(client_fd);
			continue;
		}

		if (result == -2) {
			send_error(client_fd, 413, "Payload Too Large", "The request body is too large.");
			close(client_fd);
			continue;
		}

		if (result == -3) {
			send_error(client_fd, 400, "Bad Request", "The request body was incomplete.");
			close(client_fd);
			continue;
		}

		if (result == -4) {
			send_error(client_fd, 400, "Bad Request", "Extra data was received after the request body.");
			close(client_fd);
			continue;
		}

		handle_request(client_fd, &router, request, request_length);

		close(client_fd);

		now = time(NULL);
		t = localtime(&now);
		printf("%04d-%02d-%02d %02d:%02d:%02d -- Client disconnected\n", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
	}
	close(server_fd);
	return EXIT_SUCCESS;
}
