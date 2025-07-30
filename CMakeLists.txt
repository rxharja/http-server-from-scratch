cmake_minimum_required(VERSION 3.31)
project(HttpServer C)

set(CMAKE_C_STANDARD 11)

add_executable(HttpServer main.c HttpRequest.c HttpRequest.h)
