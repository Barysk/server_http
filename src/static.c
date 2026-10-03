#define _POSIX_C_SOURCE 200809L
#include "static.h"
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

void set_error_response(struct http_response *response, int status_code, const char *reason, const char *message) {
	char body[1024];

	int body_length = snprintf(
		body,
		sizeof(body),
		"<html>"
		"<head><title>%d %s</title></head>"
		"<body>"
		"<h1>%d %s</h1>"
		"<p>%s</p>"
		"</body>"
		"</html>\n",
		status_code,
		reason,
		status_code,
		reason,
		message
	);

	if (body_length < 0 || (size_t)body_length >= sizeof(body)) {
		return;
	}

	http_response_init(response, status_code, reason);
	http_response_add_header(response, "Content-Type", "text/html; charset=utf-8");
	http_response_set_body_copy(response, body, (size_t)body_length);
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

int is_safe_path(const char *path) {
	const char *segment_start = path;

	while (*segment_start != '\0') {
		const char *slash =
			strchr(segment_start, '/');

		size_t length;

		if (slash != NULL) {
			length =
				(size_t)(slash - segment_start);
		} else {
			length =
				strlen(segment_start);
		}

		if (length == 2 &&
		    segment_start[0] == '.' &&
		    segment_start[1] == '.') {
			return 0;
		}

		if (slash == NULL) {
			break;
		}

		segment_start = slash + 1;
	}

	return 1;
}

const char *content_type(const char *path) {
	const char *extension =
		strrchr(path, '.');

	if (extension == NULL) {
		return "application/octet-stream";
	}

	if (strcmp(extension, ".html") == 0 ||
	    strcmp(extension, ".htm") == 0) {
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

	if (strcmp(extension, ".jpg") == 0 ||
	    strcmp(extension, ".jpeg") == 0) {
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

int static_file_handler(const struct http_request *request, struct http_response *response) {
	char path[PATH_MAX];

	const char *question_mark = strchr(request->target, '?');

	size_t target_length;

	if (question_mark != NULL) {
		target_length = (size_t)(question_mark - request->target);
	} else {
		target_length = strlen(request->target);
	}

	if (target_length == 0 || request->target[0] != '/') {
		set_error_response(response, 400, "Bad Request", "Invalid request target.");
		return 0;
	}

	if (target_length >= sizeof(path)) {
		set_error_response(response, 414, "URI Too Long", "The requested path is too long.");
		return 0;
	}

	memcpy(path, request->target, target_length);
	path[target_length] = '\0';

	if (url_decode(path) != 0) {
		set_error_response(response, 400, "Bad Request", "Invalid URL encoding.");
		return 0;
	}

	if (!is_safe_path(path)) {
		set_error_response(response, 403, "Forbidden", "The requested path is not allowed.");
		return 0;
	}

	char file_path[PATH_MAX];

	if (strcmp(path, "/") == 0) {
		snprintf(file_path, sizeof(file_path), "./index.html");
	} else {
		int written = snprintf(file_path, sizeof(file_path), ".%s", path);

		if (written < 0 || (size_t)written >= sizeof(file_path)) {
			set_error_response(response, 414, "URI Too Long", "The requested path is too long.");
			return 0;
		}
	}

	struct stat info;

	if (stat(file_path, &info) == -1) {
		if (errno == ENOENT) {
			set_error_response(response, 404, "Not Found", "The requested file was not found.");
		} else {
			set_error_response(response, 500, "Internal Server Error", "Could not inspect the requested file.");
		}
		return 0;
	}

	if (S_ISDIR(info.st_mode)) {
		const char suffix[] = "/index.html";

		if (strlen(file_path) + strlen(suffix) >= sizeof(file_path)) {
			set_error_response(response, 414, "URI Too Long", "The requested path is too long.");
			return 0;
		}

		strcat(file_path, suffix);

		if (stat(file_path, &info) == -1 || !S_ISREG(info.st_mode)) {
			set_error_response(response, 404, "Not Found", "The directory does not contain index.html.");
			return 0;
		}
	}

	if (!S_ISREG(info.st_mode)) {
		set_error_response(response, 404, "Not Found", "The requested resource is not a regular file.");
		return 0;
	}

	FILE *file = fopen(file_path, "rb");

	if (file == NULL) {
		set_error_response(response, 500, "Internal Server Error", "Could not open the requested file.");
		return 0;
	}

	if (info.st_size < 0 || (uintmax_t)info.st_size > SIZE_MAX) {
		fclose(file);

		set_error_response(response, 500, "Internal Server Error", "The file is too large.");
		return 0;
	}

	http_response_init(response, 200, "OK");

	if (http_response_add_header(response, "Content-Type", content_type(file_path)) != 0) {
		fclose(file);

		set_error_response(response, 500, "Internal Server Error", "Could not build the response.");
		return 0;
	}

	if (http_response_set_file(response, file, (size_t)info.st_size) != 0) {
		fclose(file);

		set_error_response(response, 500, "Internal Server Error", "Could not prepare the file response.");
		return 0;
	}

	return 0;
}
