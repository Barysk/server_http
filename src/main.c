#include <stddef.h>
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <limits.h>
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

int hex_to_int(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}

	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}

	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}

	return -1;
}

int url_decode(char *path) {
	char *src = path;
	char *dst = path;

	while (*src != '\0') {
		if (*src == '%') {
			if (src[1] == '\0' || src[2] == '\0') {
				return -1;
			}

			int high = hex_to_int(src[1]);
			int low = hex_to_int(src[2]);

			if (high < 0 || low < 0) {
				return -1;
			}

			char decoded = (char)((high << 4) | low);

			if (decoded == '\0') {
				return -1;
			}

			*dst++ = decoded;
			src += 3;
			continue;
		}
		*dst++ = *src++;
	}
	*dst = '\0';
	return 0;
}

bool is_safe_path(const char *path) {
	const char *segment_start = path;

	while (*segment_start != '\0') {
		if (*segment_start == '\\') {
			return false;
		}

		const char *slash = strchr(segment_start, '/');

		size_t length;

		if (slash != NULL) {
			length = (size_t)(slash - segment_start);
		} else {
			length = strlen(segment_start);
		}

		if (length == 2 && segment_start[0] == '.' && segment_start[1] == '.') {
			return false;
		}

		if (slash == NULL) {
			break;
		}

		segment_start = slash + 1;
	}
	return true;
}

const char *content_type(const char *path) {
	const char *extension = strrchr(path, '.');

	if (extension == NULL) {
		return "application/octet-stream";
	}

	if (strcmp(extension, ".html") == 0 || strcmp(extension, ".htm") == 0) {
		return "text/html; charset=utf-8";
	}

	if (strcmp(extension, ".css") == 0) {
		return "text/css; charset=utf-8";
	}

	if (strcmp(extension, ".js") == 0) {
		return "text/javascript; charset=utf-8";
	}

	if (strcmp(extension, ".txt") == 0) {
		return "text/plain; charset=utf-8";
	}

	if (strcmp(extension, ".json") == 0) {
		return "application/json";
	}

	if (strcmp(extension, ".png") == 0) {
		return "image/png";
	}

	if (strcmp(extension, ".jpg") == 0 || strcmp(extension, ".jpeg") == 0) {
		return "image/jpeg";
	}

	if (strcmp(extension, ".gif") == 0) {
		return "image/gif";
	}

	if (strcmp(extension, ".svg") == 0) {
		return "image/svg+xml";
	}

	if (strcmp(extension, ".ico") == 0) {
		return "image/x-icon";
	}

	return "application/octet-stream";
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

int send_file(int client_fd, const char *path) {
	struct stat file_info;

	if (stat(path, &file_info) == -1) {
		if (errno == ENOENT) {
			send_error(client_fd, 404, "Not Found", "The requested file was not found.");
		} else {
			send_error(client_fd, 500, "Internal Server Error", "Could not inspect the requested file.");
		}
		return -1;
	}

	if (!S_ISREG(file_info.st_mode)) {
		send_error(client_fd, 404, "Not Found", "The requested resource is not a regular file.");
		return -1;
	}

	FILE *file = fopen(path, "rb");

	if (file == NULL) {
		send_error(client_fd, 500, "Internal Server Error", "Could not open the requested file.");
		return -1;
	}

	char header[1024];

	int header_length = snprintf(
		header,
		sizeof(header),
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: %s\r\n"
		"Content-Length: %lld\r\n"
		"Connection: close\r\n"
		"\r\n",
		content_type(path),
		(long long)file_info.st_size
	);

	if (header_length < 0 || (size_t)header_length >= sizeof(header)) {
		fclose(file);
		return -1;
	}

	if (send_all(client_fd, header, (size_t)header_length) == -1) {
		fclose(file);
		return -1;
	}

	char buffer[FILE_BUFFER];

	for (;;) {
		size_t bytes_read = fread(buffer, 1, sizeof(buffer), file);

		if (bytes_read > 0) {
			if (send_all(client_fd, buffer, bytes_read) == -1) {
				fclose(file);
				return -1;
			}
		}

		if (bytes_read < sizeof(buffer)) {
			if (feof(file)) {
				break;
			}

			if (ferror(file)) {
				fclose(file);
				return -1;
			}
		}
	}
	fclose(file);
	return 0;
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

int handle_request(int client_fd, char *buffer, size_t length) {
	struct http_request request;

	enum http_parse_result result = http_parse_request(buffer, length, &request);

	if (result != HTTP_PARSE_OK) {
		const char *message = "Malformed HTTP request.";

		if (result == HTTP_PARSE_INCOMPLETE) {
			message = "Incomplete HTTP request.";
		} else if (result == HTTP_PARSE_TOO_MANY_HEADERS) {
			message = "Too many HTTP headers.";
		}

		send_error(client_fd, 400, "Bad Request", message);
		return -1;
	}

	printf("%s %s %s\n", request.method, request.target, request.version);

	for (size_t i = 0; i < request.header_count; i++) {
		printf("  %s: %s\n", request.headers[i].name, request.headers[i].value);
	}

	printf("Body length: %zu\n", request.body_length);

	/* currently only support GET. */
	if (strcmp(request.method, "GET") != 0) {
		send_error(client_fd, 405, "Method Not Allowed", "Only GET is supported right now.");
		return -1;
	}

	char *target = request.target;

	/* Removing query string from the filesystem path. */
	char *query = strchr(target, '?');

	if (query != NULL) {
		*query = '\0';
	}

	if (url_decode(target) != 0) {
		send_error(client_fd, 400, "Bad Request", "The URL contains invalid percent encoding.");
		return -1;
	}

	if (!is_safe_path(target)) {
		send_error(client_fd, 403, "Forbidden", "The requested path is not allowed.");
		return -1;
	}

	char file_path[PATH_MAX];

	if (strcmp(target, "/") == 0) {
		snprintf(file_path, sizeof(file_path), "./index.html");
	} else {
		int written = snprintf(file_path, sizeof(file_path), ".%s", target);

		if (written < 0 || (size_t)written >= sizeof(file_path)) {
			send_error(client_fd, 414, "URI Too Long", "The requested path is too long.");
			return -1;
		}
	}

	struct stat info;

	if (stat(file_path, &info) == 0 && S_ISDIR(info.st_mode)) {
		size_t path_length = strlen(file_path);

		if (path_length + strlen("/index.html") >= sizeof(file_path)) {
			send_error(client_fd, 414, "URI Too Long", "The requested path is too long.");
			return -1;
		}
		strcat(file_path, "/index.html");
	}
	return send_file(client_fd, file_path);
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

	bool should_close = false;
	while (!should_close) {
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

		handle_request(client_fd, request, request_length);

		close(client_fd);
		printf("Client disconnected.\n");
	}
	close(server_fd);
	return EXIT_SUCCESS;
}
