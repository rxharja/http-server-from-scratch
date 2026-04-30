cmake_minimum_required(VERSION 3.31)
project(HttpServer C)

set(CMAKE_C_STANDARD 11)

add_executable(HttpServer src/main.c
        src/HttpRequest.c src/HttpRequest.h
        src/HttpResponse.c src/HttpResponse.h
        lib/linked_node_macro.h lib/Dictionary.c lib/Dictionary.h
        lib/parser.c lib/parser.h)

add_executable(HttpServerTests tests/ParserTests.c
        src/HttpRequest.c src/HttpRequest.h
        lib/parser.c lib/parser.h)
