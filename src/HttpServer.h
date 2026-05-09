//
// Created by RedonXharja on 5/8/2026.
//

#ifndef HTTPSERVER_HTTPSERVER_H
#define HTTPSERVER_HTTPSERVER_H
#include "HttpResponse.h"

int run_server(const char * port, const Route routes[], size_t count, size_t backlog);

#endif //HTTPSERVER_HTTPSERVER_H
