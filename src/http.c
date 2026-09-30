#include "http.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>

char *trim_ows(char *text) {
	while (*text == ' ' || *text == '\t') {
		text++;
	}

	char *end = text + strlen(text);

	while (end > text && (end[-1] == ' ' || end[-1] == '\t')) {
		end--;
	}

	*end = '\0';

	return text;
}

int valid_header_name(const char *name) {
	if (*name == '\0') {
		return 0;
	}

	for (; *name != '\0'; name++) {
		unsigned char c = (unsigned char)*name;

		if (isalnum(c)) {
			continue;
		}

		switch (c) {
			case '!':
			case '#':
			case '$':
			case '%':
			case '&':
			case '\'':
			case '*':
			case '+':
			case '-':
			case '.':
			case '^':
			case '_':
			case '`':
			case '|':
			case '~':
				continue;
			default:
				return 0;
		}
	}
	return 1;
}

enum http_parse_result http_parse_request(char *buffer, size_t length, struct http_request *request) {
	memset(request, 0, sizeof(*request));

	if (length == 0) {
		return HTTP_PARSE_INCOMPLETE;
	}

	/* HTTP headers ends are: \r\n\r\n */
	char *header_end = strstr(buffer, "\r\n\r\n");

	if (header_end == NULL) {
		return HTTP_PARSE_INCOMPLETE;
	}

	char *request_line_end = strstr(buffer, "\r\n");

	if (request_line_end == NULL || request_line_end > header_end) {
		return HTTP_PARSE_BAD_REQUEST;
	}

	/* temp. replacing CR with '\0', so it is c-string. */
	*request_line_end = '\0';

	char *method = buffer;
	char *space1 = strchr(method, ' ');

	if (space1 == NULL || space1 == method) {
		return HTTP_PARSE_BAD_REQUEST;
	}

	*space1 = '\0';
	char *target = space1 + 1;
	char *space2 = strchr(target, ' ');

	if (space2 == NULL || space2 == target) {
		return HTTP_PARSE_BAD_REQUEST;
	}

	*space2 = '\0';
	char *version = space2 + 1;

	if (*version == '\0') {
		return HTTP_PARSE_BAD_REQUEST;
	}

	if (strcmp(version, "HTTP/1.0") != 0 && strcmp(version, "HTTP/1.1") != 0) {
		return HTTP_PARSE_BAD_REQUEST;
	}

	request->method = method;
	request->target = target;
	request->version = version;

	/* start after the request line CRLF */
	char *line = request_line_end + 2;

	while (line < header_end) {
		char *line_end = strstr(line, "\r\n");

		if (line_end == NULL || line_end > header_end) {
			return HTTP_PARSE_BAD_REQUEST;
		}

		*line_end = '\0';

		/* header format -- name: value */
		char *colon = strchr(line, ':');

		if (colon == NULL || colon == line) {
			return HTTP_PARSE_BAD_REQUEST;
		}

		*colon = '\0';

		if (!valid_header_name(line)) {
			return HTTP_PARSE_BAD_REQUEST;
		}

		if (request->header_count >= HTTP_MAX_HEADERS) {
			return HTTP_PARSE_TOO_MANY_HEADERS;
		}

		request->headers[request->header_count].name = line;
		request->headers[request->header_count].value = trim_ows(colon + 1);
		request->header_count++;

		line = line_end + 2;
	}

	/* things after \r\n\r\n are the body. for GET requests this will normally be empty. */
	request->body = header_end + 4;
	request->body_length = length - (size_t)(request->body - buffer);

	return HTTP_PARSE_OK;
}

const char *http_header_get(const struct http_request *request, const char *name) {
	for (size_t i = 0; i < request->header_count; i++) {
		if (strcasecmp(request->headers[i].name, name) == 0) {
			return request->headers[i].value;
		}
	}
	return NULL;
}
