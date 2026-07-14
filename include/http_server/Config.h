//
// Created by redonxharja on 7/1/26.
//
// Compile-time tunables for buffer sizes and protocol limits.
//
// Every knob below is guarded with #ifndef, so you can override any of them
// without editing this file:
//
//   1. On the compiler command line:
//        cc -DHTTP_MAX_HEADERS=16 -DHTTP_MAX_BODY_LEN=8192 ...
//      (CMake: add_compile_definitions(HTTP_MAX_HEADERS=16))
//   2. Define before including any http_server header (e.g. a prefix header).
//
// Defaults target POSIX systems with generous memory. Constrained targets
// (e.g. ESP32-class hardware) will want to shrink most of these, either macro by
// macro or in one shot with -DHTTP_PROFILE_ESP32 (see the profile block below).

#ifndef HTTPSERVER_CONFIG_H
#define HTTPSERVER_CONFIG_H

/* ---- ESP32 profile ------------------------------------------------------- */
// Selected with -DHTTP_PROFILE_ESP32. Shrinks the working set for ~300 KiB-SRAM
// class targets. Each value is still #ifndef-guarded, so precedence runs
// explicit -D (wins) > profile > POSIX default (below). At MAX_CONNECTIONS=4
// this is a ~14 KiB arena per connection (req 4K + resp 4K + dechunk 2K +
// slack 4K), so ~56 KiB of static arena pool plus a few KiB of Connection state.
#ifdef HTTP_PROFILE_ESP32
  #ifndef HTTP_MAX_PATH_LEN
  #define HTTP_MAX_PATH_LEN 256
  #endif
  #ifndef HTTP_MAX_QUERY_LEN
  #define HTTP_MAX_QUERY_LEN 128
  #endif
  #ifndef HTTP_MAX_REQUEST_LEN
  #define HTTP_MAX_REQUEST_LEN (4 * 1024)
  #endif
  #ifndef HTTP_MAX_HEADERS
  #define HTTP_MAX_HEADERS 16
  #endif
  #ifndef HTTP_MAX_HEADER_KEY_LEN
  #define HTTP_MAX_HEADER_KEY_LEN 48
  #endif
  #ifndef HTTP_MAX_HEADER_VALUE_LEN
  #define HTTP_MAX_HEADER_VALUE_LEN 128
  #endif
  #ifndef HTTP_MAX_BODY_LEN
  #define HTTP_MAX_BODY_LEN (4 * 1024)
  #endif
  #ifndef HTTP_MAX_DECHUNK_SIZE
  #define HTTP_MAX_DECHUNK_SIZE (2 * 1024)
  #endif
  #ifndef HTTP_RESPONSE_BUFFER_SIZE
  #define HTTP_RESPONSE_BUFFER_SIZE (4 * 1024)
  #endif
  #ifndef HTTP_STREAM_CHUNK_SIZE
  #define HTTP_STREAM_CHUNK_SIZE (1 * 1024)
  #endif
  #ifndef HTTP_MAX_CONNECTIONS
  #define HTTP_MAX_CONNECTIONS 4
  #endif
  #ifndef HTTP_ARENA_SLACK
  #define HTTP_ARENA_SLACK (4 * 1024)
  #endif
  #ifndef HTTP_BUCKET
  #define HTTP_BUCKET 64
  #endif
#endif // HTTP_PROFILE_ESP32

/* ---- Request line -------------------------------------------------------- */
#ifndef HTTP_MAX_PATH_LEN
#define HTTP_MAX_PATH_LEN 2048
#endif

#ifndef HTTP_MAX_QUERY_LEN
#define HTTP_MAX_QUERY_LEN 512
#endif

#ifndef HTTP_MAX_REQUEST_LEN
#define HTTP_MAX_REQUEST_LEN (8 * 1024 * 1024)
#endif

// Length of the "HTTP/x.y" version token. Protocol-fixed; not really a knob.
#ifndef HTTP_VERSION_LEN
#define HTTP_VERSION_LEN 8
#endif

/* ---- Headers ------------------------------------------------------------- */
#ifndef HTTP_MAX_HEADERS
#define HTTP_MAX_HEADERS 32
#endif

#ifndef HTTP_MAX_HEADER_KEY_LEN
#define HTTP_MAX_HEADER_KEY_LEN 64
#endif

#ifndef HTTP_MAX_HEADER_VALUE_LEN
#define HTTP_MAX_HEADER_VALUE_LEN 256
#endif

/* ---- Body ---------------------------------------------------------------- */
#ifndef HTTP_MAX_BODY_LEN
#define HTTP_MAX_BODY_LEN (1 * 1024 * 1000)
#endif

#ifndef HTTP_MAX_DECHUNK_SIZE
#define HTTP_MAX_DECHUNK_SIZE (16 * 1024)
#endif

// Scratch for a single chunk-size line. Protocol-fixed; not really a knob.
#ifndef HTTP_CHUNK_LINE_SIZE
#define HTTP_CHUNK_LINE_SIZE 64
#endif

/* ---- Response / streaming ------------------------------------------------ */
#ifndef HTTP_RESPONSE_BUFFER_SIZE
#define HTTP_RESPONSE_BUFFER_SIZE (8 * 1024)
#endif

#ifndef HTTP_STREAM_CHUNK_SIZE
#define HTTP_STREAM_CHUNK_SIZE (4 * 1024)
#endif

/* ---- Server -------------------------------------------------------------- */
#ifndef HTTP_MAX_REQUESTS
#define HTTP_MAX_REQUESTS 100
#endif

#ifndef HTTP_MAX_CONNECTIONS
#define HTTP_MAX_CONNECTIONS 4
#endif

/* ---- Arena --------------------------------------------------------------- */
#ifndef HTTP_ARENA_SLACK
#define HTTP_ARENA_SLACK (8 * 1024)
#endif

#ifndef HTTP_CONN_ARENA_SIZE
#define HTTP_CONN_ARENA_SIZE \
    (HTTP_MAX_REQUEST_LEN + HTTP_RESPONSE_BUFFER_SIZE + HTTP_MAX_DECHUNK_SIZE + HTTP_ARENA_SLACK)
#endif

/* ---- Content registry ---------------------------------------------------- */
#ifndef HTTP_BUCKET
#define HTTP_BUCKET 1024
#endif

#endif //HTTPSERVER_CONFIG_H
