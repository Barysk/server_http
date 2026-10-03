#include "router.h"

#include <stdio.h>
#include <string.h>

size_t request_path_length(const char *target) {
	const char *query = strchr(target, '?');

	if (query == NULL) {
		return strlen(target);
	}

	return (size_t)(query - target);
}

int route_matches_path(const char *route_path, const char *target) {
	size_t target_length = request_path_length(target);
	size_t route_length = strlen(route_path);
	return target_length == route_length && strncmp(route_path, target, route_length) == 0;
}

int method_in_list(const char *list, const char *method) {
	const char *start = list;

	while (*start != '\0') {
		const char *end = strchr(start, ',');
		size_t length;

		if (end == NULL) {
			length = strlen(start);
		} else {
			length = (size_t)(end - start);
		}

		if (strlen(method) == length && strncmp(start, method, length) == 0) {
			return 1;
		}

		if (end == NULL) {
			break;
		}

		start = end + 1;

		while (*start == ' ') {
			start++;
		}
	}
	return 0;
}

int append_allowed_method(char *buffer, size_t capacity, const char *method) {
	if (method_in_list(buffer, method)) {
		return 0;
	}

	size_t current_length = strlen(buffer);
	int written = snprintf(buffer + current_length, capacity - current_length, current_length == 0 ? "%s" : ", %s", method);

	if (written < 0 || (size_t)written >= capacity - current_length) {
		return -1;
	}
	return 0;
}

void http_router_init(struct http_router *router) {
	memset(router, 0, sizeof(*router));
}

int http_router_add(struct http_router *router, const char *method, const char *path, http_route_handler handler) {
	if (router == NULL || method == NULL || path == NULL || handler == NULL) {
		return -1;
	}

	if (router->route_count >= HTTP_MAX_ROUTES) {
		return -1;
	}

	struct http_route *route = &router->routes[router->route_count];

	int method_length = snprintf(route->method, sizeof(route->method), "%s", method);
	int path_length = snprintf(route->path, sizeof(route->path), "%s", path);

	if (method_length < 0 || path_length < 0 || method_length >= (int)sizeof(route->method) || path_length >= (int)sizeof(route->path)) {
		return -1;
	}

	route->handler = handler;
	router->route_count++;
	return 0;
}

enum http_router_result http_router_dispatch(const struct http_router *router, const struct http_request *request, struct http_response *response) {
	char allowed_methods[256] = "";

	int path_found = 0;

	for (size_t i = 0; i < router->route_count; i++) {
		const struct http_route *route = &router->routes[i];

		if (!route_matches_path(route->path, request->target)) {
			continue;
		}

		path_found = 1;

		if (strcmp(route->method, request->method) != 0) {
			if (append_allowed_method(allowed_methods, sizeof(allowed_methods), route->method ) == -1) {
				return HTTP_ROUTER_ERROR;
			}
			continue;
		}

		if (route->handler(request, response) != 0) {
			return HTTP_ROUTER_ERROR;
		}
		return HTTP_ROUTER_HANDLED;
	}

	if (!path_found) {
		return HTTP_ROUTER_NOT_FOUND;
	}

	http_response_init(response, 405, "Method Not Allowed");

	if (allowed_methods[0] != '\0') {
		if (http_response_add_header(response, "Allow", allowed_methods) != 0) {
			return HTTP_ROUTER_ERROR;
		}
	}

	const char body[] = "Method Not Allowed\n";
	http_response_set_body(response, body, sizeof(body) - 1);
	http_response_add_header(response, "Content-Type", "text/plain; charset=utf-8");
	return HTTP_ROUTER_METHOD_NOT_ALLOWED;
}
