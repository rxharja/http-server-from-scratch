//
// Created by RedonXharja on 5/8/2026.
//

#ifndef HTTPSERVER_HTTPSERVER_H
#define HTTPSERVER_HTTPSERVER_H
#include "HttpRouter.h"

#define MAX_REQUESTS 100

/*
 * Known HTTP/1.1 compliance gaps:
 *   - Absolute-form request targets (GET http://host/path) not parsed; affects proxy use
 *   - Chunked trailer fields not supported (RFC 9112 §7.1.2)
 */

/**
 * @param str  candidate port string (NUL-terminated)
 * @return     0 if `str` is a valid TCP port, non-zero otherwise
 */
int server_port_valid(const char * str);

/**
 * @param port     TCP port to bind, validated by server_port_valid
 * @param router   route + cache configuration; not retained beyond the call
 * @param backlog  listen() backlog passed straight to the kernel
 * @return         0 on clean shutdown, non-zero on fatal startup error
 */
int server_run(const char * port, const Router * router, size_t backlog);

#endif //HTTPSERVER_HTTPSERVER_H
