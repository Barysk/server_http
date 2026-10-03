#include "response.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>

int send_all(int fd, const void *buffer, size_t length) {
	const char *data = buffer;
	size_t sent = 0;

	while (sent < length) {
		ssize_t n = send(fd, data + sent, length - sent, 0);

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

int send_string(int fd, const char *text) {
	return send_all(fd, text, strlen(text));
}

int header_is_reserved(const char *name) {
	return strcasecmp(name, "Content-Length") == 0 || strcasecmp(name, "Connection") == 0;
}

int http_response_init(struct http_response *response, int status_code, const char *reason) {
	if (response == NULL || reason == NULL) {
		return -1;
	}

	memset(response, 0, sizeof(*response));

	response->status_code = status_code;

	if (snprintf(response->reason, sizeof(response->reason), "%s", reason ) >= (int)sizeof(response->reason)) {
		return -1;
	}
	return 0;
}

int http_response_add_header(struct http_response *response, const char *name, const char *value) {
	if (response == NULL || name == NULL || value == NULL) {
		return -1;
	}

	if (header_is_reserved(name)) {
		return -1;
	}

	if (response->header_count >= HTTP_MAX_RESPONSE_HEADERS) {
		return -1;
	}

	struct http_response_header *header = &response->headers[response->header_count];

	int name_length = snprintf(header->name, sizeof(header->name), "%s", name);

	int value_length = snprintf( header->value, sizeof(header->value), "%s", value);

	if (name_length < 0 || value_length < 0 || name_length >= (int)sizeof(header->name) || value_length >= (int)sizeof(header->value)) {
		return -1;
	}

	response->header_count++;

	return 0;
}

int http_response_set_body(struct http_response *response, const void *body, size_t body_length) {
	if (response == NULL) {
		return -1;
	}

	if (response->owned_body != NULL) {
		free(response->owned_body);
		response->owned_body = NULL;
	}

	if (response->file != NULL) {
		fclose(response->file);
		response->file = NULL;
	}

	response->body = body;
	response->body_length = body_length;

	return 0;
}

int http_response_set_body_copy(struct http_response *response, const void *body, size_t body_length) {
	if (response == NULL) {
		return -1;
	}

	char *copy = malloc(body_length + 1);

	if (copy == NULL) {
		return -1;
	}

	memcpy(copy, body, body_length);
	copy[body_length] = '\0';

	if (response->owned_body != NULL) {
		free(response->owned_body);
	}

	if (response->file != NULL) {
		fclose(response->file);
		response->file = NULL;
	}

	response->owned_body = copy;
	response->body = copy;
	response->body_length = body_length;

	return 0;
}

int http_response_set_file(struct http_response *response, FILE *file, size_t file_length) {
	if (response == NULL || file == NULL) {
		return -1;
	}

	if (response->owned_body != NULL) {
		free(response->owned_body);
		response->owned_body = NULL;
	}

	if (response->file != NULL) {
		fclose(response->file);
	}

	response->file = file;
	response->body = NULL;
	response->body_length = file_length;

	return 0;
}

int send_file(int client_fd, FILE *file) {
	char buffer[8192];

	for (;;) {
		size_t bytes_read = fread(buffer, 1, sizeof(buffer), file);

		if (bytes_read > 0) {
			if (send_all(client_fd, buffer, bytes_read) == -1) {
				return -1;
			}
		}

		if (bytes_read < sizeof(buffer)) {
			if (feof(file)) {
				return 0;
			}

			if (ferror(file)) {
				return -1;
			}
		}
	}
}

int http_response_send(int client_fd, const struct http_response *response) {
	if (response == NULL) {
		return -1;
	}

	char status_line[256];

	int status_length = snprintf(status_line, sizeof(status_line), "HTTP/1.1 %d %s\r\n", response->status_code, response->reason);

	if (status_length < 0 || (size_t)status_length >= sizeof(status_line)) {
		return -1;
	}

	if (send_all(client_fd, status_line, (size_t)status_length) == -1) {
		return -1;
	}

	for (size_t i = 0; i < response->header_count; i++) {
		char header_line[HTTP_RESPONSE_HEADER_NAME_MAX + HTTP_RESPONSE_HEADER_VALUE_MAX + 8];

		int header_length = snprintf(header_line, sizeof(header_line), "%s: %s\r\n", response->headers[i].name, response->headers[i].value);

		if (header_length < 0 || (size_t)header_length >= sizeof(header_line)) {
			return -1;
		}

		if (send_all(client_fd, header_line, (size_t)header_length) == -1) {
			return -1;
		}
	}

	char content_length[64];

	int content_length_size = snprintf(content_length, sizeof(content_length), "Content-Length: %zu\r\n", response->body_length);

	if (content_length_size < 0 || (size_t)content_length_size >= sizeof(content_length)) {
		return -1;
	}

	if (send_all(client_fd, content_length, (size_t)content_length_size) == -1) {
		return -1;
	}

	if (send_string(client_fd, "Connection: close\r\n") == -1) {
		return -1;
	}

	if (send_string(client_fd, "\r\n") == -1) {
		return -1;
	}

	if (response->file != NULL) {
		return send_file(client_fd, response->file);
	}

	if (response->body_length == 0) {
		return 0;
	}

	return send_all(client_fd, response->body, response->body_length);
}

void http_response_destroy(struct http_response *response) {
	if (response == NULL) {
		return;
	}

	if (response->owned_body != NULL) {
		free(response->owned_body);
		response->owned_body = NULL;
	}

	if (response->file != NULL) {
		fclose(response->file);
		response->file = NULL;
	}

	response->body = NULL;
	response->body_length = 0;
}
