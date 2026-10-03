#ifndef ROUTER_H
#define ROUTER_H

#include <stddef.h>

#include "http.h"
#include "response.h"

#define HTTP_MAX_ROUTES 64
#define HTTP_ROUTE_METHOD_MAX 16
#define HTTP_ROUTE_PATH_MAX 512

enum http_router_result {
	HTTP_ROUTER_HANDLED = 0,
	HTTP_ROUTER_NOT_FOUND,
	HTTP_ROUTER_METHOD_NOT_ALLOWED,
	HTTP_ROUTER_ERROR
};

typedef int (*http_route_handler)(
	const struct http_request *request,
	struct http_response *response
);

struct http_route {
	char method[HTTP_ROUTE_METHOD_MAX];
	char path[HTTP_ROUTE_PATH_MAX];

	http_route_handler handler;
};

struct http_router {
	struct http_route routes[HTTP_MAX_ROUTES];
	size_t route_count;
};

void http_router_init(struct http_router *router);
int http_router_add(struct http_router *router, const char *method, const char *path, http_route_handler handler);

enum http_router_result http_router_dispatch(
	const struct http_router *router,
	const struct http_request *request,
	struct http_response *response
);

#endif
