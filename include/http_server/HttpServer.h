//
// Created by RedonXharja on 5/8/2026.
//

#ifndef HTTPSERVER_HTTPSERVER_H
#define HTTPSERVER_HTTPSERVER_H
#include "HttpRouter.h"

#define MAX_REQUESTS 100

/*
 * Known HTTP/1.1 compliance gaps:
 *   - Expect: 100-continue not handled; clients may stall before sending large bodies
 *   - Chunked Transfer-Encoding only supported in requests, not responses
 *   - Absolute-form request targets (GET http://host/path) not parsed; affects proxy use
 *   - Chunked trailer fields not supported (RFC 9112 §7.1.2)
 */
int server_port_valid(const char * str);

int server_run(const char * port, const Router * router, size_t backlog);

#endif //HTTPSERVER_HTTPSERVER_H
