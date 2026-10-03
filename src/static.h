#ifndef STATIC_H
#define STATIC_H

#include "http.h"
#include "response.h"

int url_decode(char *path);
int is_safe_path(const char *path);
int hex_to_int(char c);
const char *content_type(const char *path);
int static_file_handler(const struct http_request *request, struct http_response *response);

#endif
