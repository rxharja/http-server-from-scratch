cmake_minimum_required(VERSION 3.30)
project(HttpServer C)

set(CMAKE_C_STANDARD 11)

add_compile_definitions(_XOPEN_SOURCE=700 _GNU_SOURCE)

# === Library ===
add_library(HttpServer STATIC
        src/HttpServer.c
        src/HttpRouter.c
        src/HttpRequest.c
        src/HttpRequestLine.c
        src/HttpResponse.c
        src/HttpHeaders.c
        src/HttpBody.c
        src/ParseResult.c
        src/Connection.c
        src/parser.c
        src/Dictionary.c
        src/Networking.c
        src/Networking.h
)

target_include_directories(HttpServer
        PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# === Example app ===
add_executable(example_server examples/main.c)
target_link_libraries(example_server PRIVATE HttpServer)

# === Tests ===
# Tests need private headers (Connection.h, parser.h, etc.), so we grant
# them access to src/ explicitly. This is a deliberate exception, only
# in-tree tests get to peek behind the library's public API.
add_executable(HttpServerTests
        tests/test_harness.c
        tests/RequestTests.c
        tests/BodyTests.c
        tests/ResponseTests.c
        tests/StreamTests.c
)
target_link_libraries(HttpServerTests PRIVATE HttpServer)
target_include_directories(HttpServerTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
