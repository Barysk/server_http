#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

#define HTTP_MAX_HEADERS 64

enum http_parse_result {
	HTTP_PARSE_OK = 0,
	HTTP_PARSE_INCOMPLETE,
	HTTP_PARSE_BAD_REQUEST,
	HTTP_PARSE_TOO_MANY_HEADERS
};

struct http_header {
	char *name;
	char *value;
};

struct http_request {
	char *method;
	char *target;
	char *version;

	struct http_header headers[HTTP_MAX_HEADERS];
	size_t header_count;

	char *body;
	size_t body_length;
};

enum http_parse_result http_parse_request(
	char *buffer,
	size_t length,
	struct http_request *request
);

const char *http_header_get(const struct http_request *request, const char *name);

int http_get_content_length_from_headers(const char *buffer, size_t header_length, size_t *length);

int http_get_content_length(const struct http_request *request, size_t *length);

#endif
