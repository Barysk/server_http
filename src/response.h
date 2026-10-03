#ifndef RESPONSE_H
#define RESPONSE_H

#include <stddef.h>
#include <stdio.h>

#define HTTP_MAX_RESPONSE_HEADERS 32
#define HTTP_RESPONSE_HEADER_NAME_MAX 64
#define HTTP_RESPONSE_HEADER_VALUE_MAX 1024

struct http_response_header {
	char name[HTTP_RESPONSE_HEADER_NAME_MAX];
	char value[HTTP_RESPONSE_HEADER_VALUE_MAX];
};

struct http_response {
	int status_code;
	char reason[64];

	struct http_response_header headers[HTTP_MAX_RESPONSE_HEADERS];
	size_t header_count;

	const void *body;
	size_t body_length;

	char *owned_body;
	FILE *file;
};

int send_file(int client_fd, FILE *file);
int send_all(int fd, const void *buffer, size_t length);
int http_response_init(struct http_response *response, int status_code, const char *reason);
int http_response_add_header(struct http_response *response, const char *name, const char *value);
int http_response_set_body(struct http_response *response, const void *body, size_t body_length);
int http_response_set_body_copy(struct http_response *response, const void *body, size_t body_length);
int http_response_set_file(struct http_response *response, FILE *file, size_t file_length);
int http_response_send(int client_fd, const struct http_response *response);
void http_response_destroy(struct http_response *response);

#endif
