//
// Created by redonxharja on 5/3/26.
//

#ifndef HTTPSERVER_PARSERESULT_H
#define HTTPSERVER_PARSERESULT_H
#include <stddef.h>
#include <time.h>

#define HTTP_DATE_FMT "%a, %d %b %Y %H:%M:%S GMT"

typedef enum {
    PARSE_OK,
    PARSE_BAD_REQUEST,
    PARSE_URI_TOO_LONG,
    PARSE_HEADER_KEY_TOO_LONG,
    PARSE_HEADER_VALUE_TOO_LONG,
    PARSE_HEADER_TOO_LONG,
    PARSE_VERSION_NOT_SUPPORTED,
    PARSE_PAYLOAD_TOO_LARGE,
    PARSE_NOT_IMPLEMENTED,
    PARSE_INCOMPLETE,
    PARSE_NOT_FOUND,
    PARSE_NOT_ALLOWED,
    PARSE_SERVER_ERROR
  } ParseStatus;

typedef struct {
    ParseStatus status;
    const char * error_position;
    const char * next;
} ParseResult;


/**
 * Strict unsigned-integer parser with explicit base and overflow guard. No leading
 * sign, no whitespace, no base prefix; all `len` bytes must be valid digits.
 *
 * @param s     digit bytes (not NUL-terminated)
 * @param len   number of bytes to consume from `s`
 * @param base  numeric base (2..16); typically 10 or 16
 * @param max   maximum permitted value; values above this yield PAYLOAD_TOO_LARGE
 * @param out   parsed value on PARSE_OK; untouched otherwise
 * @return      PARSE_OK / PARSE_BAD_REQUEST / PARSE_PAYLOAD_TOO_LARGE
 */
ParseStatus uint_parse(const char *s, size_t len, int base, size_t max, size_t *out);

/**
 * @param c     candidate digit byte
 * @param base  numeric base (2..16)
 * @return      value in [0, base) on a valid digit, -1 otherwise
 */
int digit_value(unsigned char c, int base);

/**
 * Convenience setter for the error path: sets `status` and points `next` at `pos`
 * so callers can resume / report where they stopped.
 *
 * @param res     result to populate
 * @param status  non-OK status code
 * @param pos     cursor position the parser stopped at
 */
void parse_error_set(ParseResult *res, ParseStatus status, const char * pos);

/**
 * Parse an HTTP date per RFC 7231 §7.1.1.1 (IMF-fixdate preferred; obsolete formats
 * accepted for tolerance).
 *
 * @param s    NUL-terminated date string
 * @param out  UTC timestamp on success
 * @return     0 on success, non-zero on malformed input
 */
int http_date_parse(const char *s, time_t *out);

/**
 * Format `t` as an IMF-fixdate string (e.g. "Sun, 06 Nov 1994 08:49:37 GMT").
 *
 * @param t        UTC timestamp to format
 * @param buf      output buffer
 * @param buf_len  capacity of `buf`
 * @return         bytes written excluding NUL, or 0 on overflow
 */
size_t http_date_format(time_t t, char *buf, size_t buf_len);

#endif //HTTPSERVER_PARSERESULT_H