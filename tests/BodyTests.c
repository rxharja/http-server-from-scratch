//
// Body-side tests: content-length, transfer-encoding, and the chunked decode
// (chunk_advance) + encode (chunk_frame) state machines. Split out of the
// former ParserTests.c.
//
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <http_server/HttpRequest.h>
#include "http_server/HttpBody.h"
#include "http_server/HttpResponse.h"
#include "Connection.h"
#include "test_harness.h"

// parse_content_length helper.
// Assumed signature: ParseStatus parse_content_length(const char *val, size_t *out);
// On PARSE_OK the parsed length is written to *out; otherwise *out is left zero.
static void expect_content_length(const char *label, const char *input,
                                  const ParseStatus want_status, const size_t want_value) {
    size_t out = 0;
    const ParseStatus got = content_length_parse(input, &out);
    check(label, got == want_status, "content_length status mismatch");
    if (want_status == PARSE_OK) {
        check(label, out == want_value, "content_length value mismatch");
    }
}

// parse_transfer_encoding helper.
// Assumed signature: ParseStatus parse_transfer_encoding(const char *val, TransferCoding *out);
// Where TransferCoding has at least: TE_NONE, TE_CHUNKED, TE_UNSUPPORTED.
//   PARSE_OK + TE_CHUNKED      → list valid, chunked is the only coding.
//   PARSE_OK + TE_UNSUPPORTED  → list valid, contains codings other than chunked
//                                (server should respond 501 at the orchestrator level).
//   PARSE_BAD_REQUEST          → list malformed: chunked not last, bad token, empty value.
static void expect_transfer_encoding(const char *label, const char *input,
                                     const ParseStatus want_status,
                                     const TransferCoding want_te) {
    TransferCoding out = TE_NONE;
    const ParseStatus got = transfer_encoding_parse(input, &out);
    check(label, got == want_status, "transfer_encoding status mismatch");
    if (want_status == PARSE_OK || want_status == PARSE_NOT_IMPLEMENTED) {
        check(label, out == want_te, "transfer_encoding output mismatch");
    }
}

// Drive chunk_advance over `input` until it either reaches CHUNK_DONE or stops with
// a non-OK status. Returns the final ChunkResult with `bytes_written` accumulated
// across all calls; out_phase / out_consumed report the decoder's final state.
static ChunkResult dechunk_drive(const char *input, const size_t input_len,
                                 char *dest, const size_t dest_cap,
                                 ChunkedPhase *out_phase, size_t *out_consumed) {
    ChunkDecoder dec = {0};
    size_t consumed = 0;
    size_t written  = 0;
    ChunkResult cr  = {0};
    while (1) {
        cr = chunk_advance(&dec,
                           input + consumed, input_len - consumed,
                           dest + written,   dest_cap - written);
        if (cr.parse_result.next) {
            consumed += (size_t)(cr.parse_result.next - (input + consumed));
        }
        written += cr.bytes_written;
        if (dec.phase == CHUNK_DONE) break;
        if (cr.parse_result.status != PARSE_OK) break;
    }
    if (out_phase)    *out_phase    = dec.phase;
    if (out_consumed) *out_consumed = consumed;
    cr.bytes_written = written;
    return cr;
}

// chunk_advance happy path — full-buffer feed must drive the decoder to CHUNK_DONE,
// consume every input byte, and produce the expected decoded payload.
// want_decoded may be NULL when want_decoded_len is 0 (empty body).
static void expect_dechunk_ok(const char *label,
                              const char *input, const size_t input_len,
                              const char *want_decoded, const size_t want_decoded_len) {
    char dest[4096] = {0};
    ChunkedPhase final_phase = CHUNK_SIZE;
    size_t consumed = 0;
    const ChunkResult cr = dechunk_drive(input, input_len, dest, sizeof(dest),
                                         &final_phase, &consumed);
    check(label, cr.parse_result.status == PARSE_OK,        "dechunk: not OK");
    check(label, final_phase == CHUNK_DONE,                 "dechunk: phase != CHUNK_DONE");
    check(label, consumed == input_len,                     "dechunk: did not consume all input");
    check(label, cr.bytes_written == want_decoded_len,      "dechunk: decoded length mismatch");
    if (want_decoded_len > 0 && want_decoded) {
        check(label, memcmp(dest, want_decoded, want_decoded_len) == 0,
              "dechunk: decoded data mismatch");
    }
}

// chunk_advance error path — final status only.
static void expect_dechunk_err(const char *label,
                               const char *input, const size_t input_len,
                               const ParseStatus want_status) {
    char dest[4096] = {0};
    ChunkedPhase final_phase = CHUNK_SIZE;
    size_t consumed = 0;
    const ChunkResult cr = dechunk_drive(input, input_len, dest, sizeof(dest),
                                         &final_phase, &consumed);
    check(label, cr.parse_result.status == want_status, "dechunk: status mismatch");
}

// Feed `input` to chunk_advance one byte at a time, growing the readable window
// each step. Exercises the persistent decoder across artificial split boundaries —
// the property the connection-level loop relies on across poll wake-ups.
static void expect_dechunk_split_ok(const char *label,
                                    const char *input, const size_t input_len,
                                    const char *want_decoded, const size_t want_decoded_len) {
    char dest[4096] = {0};
    ChunkDecoder dec = {0};
    size_t fed = 0, consumed = 0, written = 0;
    while (fed < input_len && dec.phase != CHUNK_DONE) {
        fed++;
        while (1) {
            const ChunkResult cr = chunk_advance(&dec,
                                                 input + consumed, fed - consumed,
                                                 dest + written,   sizeof(dest) - written);
            if (cr.parse_result.next) {
                consumed += (size_t)(cr.parse_result.next - (input + consumed));
            }
            written += cr.bytes_written;
            if (cr.parse_result.status == PARSE_INCOMPLETE) break;
            if (cr.parse_result.status != PARSE_OK) {
                check(label, 0, "split dechunk: unexpected hard error");
                return;
            }
            if (dec.phase == CHUNK_DONE) break;
        }
    }
    check(label, dec.phase == CHUNK_DONE,            "split dechunk: phase != CHUNK_DONE");
    check(label, consumed == input_len,              "split dechunk: did not consume all input");
    check(label, written == want_decoded_len,        "split dechunk: decoded length mismatch");
    if (want_decoded_len > 0 && want_decoded) {
        check(label, memcmp(dest, want_decoded, want_decoded_len) == 0,
              "split dechunk: decoded data mismatch");
    }
}

// ── chunk_frame / chunk_frame_last — the encoder, mirror of chunk_advance ─────
// The framer wraps `len` raw payload bytes as `<hexsize>\r\n<payload>\r\n`. These
// assert against the exact wire bytes (memcmp, never strcmp) so they catch the
// two classic chunked-encoding traps:
//   - the size MUST be hexadecimal: a decimal size silently breaks every chunk
//     larger than 9 bytes, because the decoder reads the size as hex.
//   - the payload is binary: it must be copied by length, never string-formatted.
//     An embedded NUL would truncate a %s-based encoder mid-chunk.
// NOTE: a zero-length data chunk *is* the terminator, so chunk_frame is never
// called with len == 0 — that case is chunk_frame_last's job.

// Frame `payload`/`len` into a generous buffer; assert the produced wire bytes
// equal `want`/`want_len` and that the return value equals want_len.
static void expect_frame(const char *label,
                         const char *payload, const size_t len,
                         const char *want, const size_t want_len) {
    char out[4096];
    memset(out, 0xAA, sizeof(out)); // poison so a short write or stray NUL shows up
    const ssize_t n = chunk_frame(payload, len, out, sizeof(out));
    check(label, n == (ssize_t)want_len, "frame: returned length mismatch");
    if (n == (ssize_t)want_len) {
        check(label, memcmp(out, want, want_len) == 0, "frame: wire bytes mismatch");
    }
}

// Frame into a buffer of exactly `cap` bytes; assert the return value is
// `want_ret` (the framed length on an exact fit, or -1 when it can't fit).
// The framer must refuse to overflow rather than write past `cap`.
static void expect_frame_cap(const char *label,
                             const char *payload, const size_t len,
                             const size_t cap, const ssize_t want_ret) {
    char out[4096];
    assert(cap <= sizeof(out));
    const ssize_t n = chunk_frame(payload, len, out, cap);
    check(label, n == want_ret, "frame: capacity-boundary return mismatch");
}

// chunk_frame_last must emit exactly "0\r\n\r\n" (5 bytes) and refuse a buffer
// that can't hold it.
static void expect_frame_last(const char *label) {
    char out[16];
    memset(out, 0xAA, sizeof(out));
    const ssize_t n = chunk_frame_last(out, sizeof(out));
    check(label, n == 5, "frame_last: length != 5");
    if (n == 5) check(label, memcmp(out, "0\r\n\r\n", 5) == 0, "frame_last: bytes != 0CRLFCRLF");
    check(label, chunk_frame_last(out, 4) == -1, "frame_last: expected -1 when cap < 5");
}

// Round-trip: frame a payload + terminator, then run the bytes back through
// chunk_advance. Proves the encoder and decoder are inverses — the strongest
// single check, and it ties the new framer to the existing decoder.
static void expect_frame_roundtrip(const char *label,
                                   const char *payload, const size_t len) {
    char wire[8192];
    const ssize_t a = chunk_frame(payload, len, wire, sizeof(wire));
    check(label, a > 0, "roundtrip: data frame failed");
    if (a <= 0) return;
    const ssize_t b = chunk_frame_last(wire + a, sizeof(wire) - (size_t)a);
    check(label, b > 0, "roundtrip: terminator frame failed");
    if (b <= 0) return;

    char dest[4096] = {0};
    ChunkedPhase phase = CHUNK_SIZE;
    size_t consumed = 0;
    const ChunkResult cr = dechunk_drive(wire, (size_t)(a + b), dest, sizeof(dest),
                                         &phase, &consumed);
    check(label, cr.parse_result.status == PARSE_OK, "roundtrip: decode not OK");
    check(label, phase == CHUNK_DONE,                "roundtrip: decode not DONE");
    check(label, cr.bytes_written == len,            "roundtrip: decoded length mismatch");
    if (len > 0) check(label, memcmp(dest, payload, len) == 0, "roundtrip: payload mismatch");
}

void run_body_tests(void) {
    // parse_content_length — see helper comment for the assumed signature.
    // (You'll need to declare it in HttpRequest.h once the implementation lands.)
    expect_content_length("CL - zero",           "0",       PARSE_OK,           0);
    expect_content_length("CL - small",          "100",     PARSE_OK,           100);
    expect_content_length("CL - larger",         "12345",   PARSE_OK,           12345);
    expect_content_length("CL - empty",          "",        PARSE_BAD_REQUEST,  0);
    expect_content_length("CL - non-digit",      "abc",     PARSE_BAD_REQUEST,  0);
    expect_content_length("CL - mixed",          "12abc",   PARSE_BAD_REQUEST,  0);
    expect_content_length("CL - leading SP",     " 100",    PARSE_BAD_REQUEST,  0);
    expect_content_length("CL - trailing SP",    "100 ",    PARSE_BAD_REQUEST,  0);
    expect_content_length("CL - leading HTAB",   "\t100",   PARSE_BAD_REQUEST,  0);
    expect_content_length("CL - plus sign",      "+100",    PARSE_BAD_REQUEST,  0);
    expect_content_length("CL - negative",       "-100",    PARSE_BAD_REQUEST,  0);
    expect_content_length("CL - hex prefix",     "0x10",    PARSE_BAD_REQUEST,  0);
    expect_content_length("CL - float",          "1.5",     PARSE_BAD_REQUEST,  0);
    // size_t-overflow case — number bigger than any platform's size_t.
    // Should map to PARSE_PAYLOAD_TOO_LARGE (or whatever name you settle on).
    expect_content_length("CL - overflow size_t","999999999999999999999999999",
                                                            PARSE_PAYLOAD_TOO_LARGE, 0);

    // parse_transfer_encoding — see helper comment for assumed signature.
    // Recognition (case-insensitive per RFC 9110 §10.1.4).
    expect_transfer_encoding("TE - chunked",            "chunked",          PARSE_OK,           TE_CHUNKED);
    expect_transfer_encoding("TE - case Chunked",       "Chunked",          PARSE_OK,           TE_CHUNKED);
    expect_transfer_encoding("TE - case CHUNKED",       "CHUNKED",          PARSE_OK,           TE_CHUNKED);
    expect_transfer_encoding("TE - case mixed",         "cHuNkEd",          PARSE_OK,           TE_CHUNKED);

    // Single non-chunked coding → unsupported (server returns 501 at orchestrator).
    expect_transfer_encoding("TE - gzip alone",         "gzip",             PARSE_NOT_IMPLEMENTED,           TE_UNSUPPORTED);
    expect_transfer_encoding("TE - deflate alone",      "deflate",          PARSE_NOT_IMPLEMENTED,           TE_UNSUPPORTED);
    expect_transfer_encoding("TE - identity alone",     "identity",         PARSE_NOT_IMPLEMENTED,           TE_UNSUPPORTED);
    expect_transfer_encoding("TE - compress alone",     "compress",         PARSE_NOT_IMPLEMENTED,           TE_UNSUPPORTED);

    // Chunked last in a stacked list → spec-valid framing, but body content unsupported → 501.
    expect_transfer_encoding("TE - gzip then chunked",  "gzip, chunked",    PARSE_NOT_IMPLEMENTED, TE_UNSUPPORTED);
    expect_transfer_encoding("TE - 2 stacks chunked",   "gzip, deflate, chunked", PARSE_NOT_IMPLEMENTED, TE_UNSUPPORTED);

    // Chunked NOT last → malformed framing per RFC 9112 §6.1 ("MUST apply chunked as the final transfer coding").
    // Spec-strict: this is 400, distinct from "unsupported coding" → 501. If your current parser short-circuits
    // on the first unsupported token regardless of position, these will fail until you scan the whole list
    // before deciding malformed-vs-unsupported.
    expect_transfer_encoding("TE - chunked then gzip",  "chunked, gzip",    PARSE_BAD_REQUEST,  TE_NONE);
    expect_transfer_encoding("TE - chunked mid-list",   "chunked, gzip, deflate", PARSE_BAD_REQUEST, TE_NONE);

    // RFC 9110 §5.6.1 — empty list elements MUST be ignored.
    expect_transfer_encoding("TE - leading comma",      ",chunked",         PARSE_OK,           TE_CHUNKED);
    expect_transfer_encoding("TE - trailing comma",     "chunked,",         PARSE_OK,           TE_CHUNKED);
    expect_transfer_encoding("TE - double comma",       "chunked,,",        PARSE_OK,           TE_CHUNKED);
    expect_transfer_encoding("TE - sparse list",        ", , ,chunked",     PARSE_OK,           TE_CHUNKED);

    // OWS handling around commas.
    expect_transfer_encoding("TE - SP after comma",     "gzip, chunked",    PARSE_NOT_IMPLEMENTED,           TE_UNSUPPORTED);
    expect_transfer_encoding("TE - HTAB after comma",   "gzip,\tchunked",   PARSE_NOT_IMPLEMENTED,           TE_UNSUPPORTED);
    expect_transfer_encoding("TE - SP before comma",    "gzip ,chunked",    PARSE_NOT_IMPLEMENTED,           TE_UNSUPPORTED);
    expect_transfer_encoding("TE - SP both sides",      "gzip , chunked",   PARSE_NOT_IMPLEMENTED,           TE_UNSUPPORTED);

    // Parameters (chunked carries none in practice, but the grammar allows them — must strip).
    expect_transfer_encoding("TE - chunked w/ params",  "chunked;ext=val",  PARSE_OK,           TE_CHUNKED);
    expect_transfer_encoding("TE - chunked OWS+params", "chunked ;ext=val", PARSE_OK,           TE_CHUNKED);
    expect_transfer_encoding("TE - gzip w/ params last","gzip;q=0.5, chunked", PARSE_NOT_IMPLEMENTED, TE_UNSUPPORTED);

    // Malformed inputs.
    expect_transfer_encoding("TE - empty value",        "",                 PARSE_BAD_REQUEST,  TE_NONE);
    expect_transfer_encoding("TE - all OWS",            "   ",              PARSE_BAD_REQUEST,  TE_NONE);
    expect_transfer_encoding("TE - only commas",        ",,,",              PARSE_BAD_REQUEST,  TE_NONE);
    expect_transfer_encoding("TE - bad token paren",    "(invalid)",        PARSE_BAD_REQUEST,  TE_NONE);
    expect_transfer_encoding("TE - bad token slash",    "weird/coding",     PARSE_BAD_REQUEST,  TE_NONE);

    // chunk_advance — drives the chunked-decoding state machine end-to-end.
    // Grammar: *chunk last-chunk trailer-section CRLF
    //   chunk          = chunk-size [chunk-ext] CRLF chunk-data CRLF
    //   last-chunk     = 1*"0" [chunk-ext] CRLF
    //   trailer-section= *( field-line CRLF )    (rejected — see README compliance gaps)

    // Happy paths (full-buffer feed).
    expect_dechunk_ok("Dechunk - empty body",
        "0\r\n\r\n",                                            5, NULL,        0);
    expect_dechunk_ok("Dechunk - single chunk",
        "5\r\nABCDE\r\n0\r\n\r\n",                             15, "ABCDE",     5);
    expect_dechunk_ok("Dechunk - single byte chunk",
        "1\r\nA\r\n0\r\n\r\n",                                 11, "A",         1);
    expect_dechunk_ok("Dechunk - two chunks",
        "5\r\nABCDE\r\n3\r\nXYZ\r\n0\r\n\r\n",                 23, "ABCDEXYZ",  8);
    expect_dechunk_ok("Dechunk - hex size lowercase",
        "1a\r\n12345678901234567890123456\r\n0\r\n\r\n",       37, "12345678901234567890123456", 26);
    expect_dechunk_ok("Dechunk - hex size uppercase",
        "1A\r\n12345678901234567890123456\r\n0\r\n\r\n",       37, "12345678901234567890123456", 26);
    expect_dechunk_ok("Dechunk - last-chunk multi-zero",
        "00\r\n\r\n",                                           6, NULL,        0);
    expect_dechunk_ok("Dechunk - chunk-ext on data",
        "5;name=val\r\nABCDE\r\n0\r\n\r\n",                    24, "ABCDE",     5);
    expect_dechunk_ok("Dechunk - chunk-ext on last",
        "5\r\nABCDE\r\n0;name=val\r\n\r\n",                    24, "ABCDE",     5);
    expect_dechunk_ok("Dechunk - chunk-ext throughout",
        "5;a=b\r\nABCDE\r\n0;c=d\r\n\r\n",                     23, "ABCDE",     5);
    expect_dechunk_ok("Dechunk - embedded CRLF in data",
        "5\r\nA\r\nXY\r\n0\r\n\r\n",                           15, "A\r\nXY",   5);

    // Trailer fields are not yet supported — must be rejected (README compliance gaps).
    expect_dechunk_err("Dechunk - one trailer rejected",
        "5\r\nABCDE\r\n0\r\nFoo: bar\r\n\r\n",                 25, PARSE_BAD_REQUEST);
    expect_dechunk_err("Dechunk - multi trailers rejected",
        "0\r\nA: 1\r\nB: 2\r\n\r\n",                           17, PARSE_BAD_REQUEST);
    expect_dechunk_err("Dechunk - trailer line no CRLF",
        "0\r\nFoo: bar",                                       11, PARSE_BAD_REQUEST);

    // Hard-error paths (malformed grammar).
    expect_dechunk_err("Dechunk - non-hex size first",
        "G\r\nABC\r\n",                                         8, PARSE_BAD_REQUEST);
    expect_dechunk_err("Dechunk - empty size",
        "\r\n",                                                 2, PARSE_BAD_REQUEST);
    expect_dechunk_err("Dechunk - ext only no size",
        ";ext=v\r\n",                                           8, PARSE_BAD_REQUEST);
    expect_dechunk_err("Dechunk - non-hex size mid-body",
        "5\r\nABCDE\r\nG\r\nXYZ\r\n0\r\n\r\n",                 23, PARSE_BAD_REQUEST);
    expect_dechunk_err("Dechunk - wrong byte after data",
        "5\r\nABCDEXX",                                        10, PARSE_BAD_REQUEST);

    // Incomplete-input paths (would resume cleanly given more bytes).
    expect_dechunk_err("Dechunk - missing size CRLF",
        "5",                                                    1, PARSE_INCOMPLETE);
    expect_dechunk_err("Dechunk - data shorter",
        "5\r\nABC",                                             6, PARSE_INCOMPLETE);
    expect_dechunk_err("Dechunk - missing trail CRLF after data",
        "5\r\nABCDE",                                           8, PARSE_INCOMPLETE);
    expect_dechunk_err("Dechunk - missing LF after CR",
        "5\r\nABCDE\r",                                         9, PARSE_INCOMPLETE);
    expect_dechunk_err("Dechunk - missing body terminator",
        "5\r\nABCDE\r\n0\r\n",                                 13, PARSE_INCOMPLETE);

    // Split-feed — byte-by-byte resumption across artificial poll boundaries.
    // Verifies the persistent ChunkDecoder state survives PARSE_INCOMPLETE returns
    // and continues from exactly where it left off when more bytes arrive.
    expect_dechunk_split_ok("Split - empty body",
        "0\r\n\r\n",                                            5, NULL,        0);
    expect_dechunk_split_ok("Split - single chunk",
        "5\r\nABCDE\r\n0\r\n\r\n",                             15, "ABCDE",     5);
    expect_dechunk_split_ok("Split - two chunks",
        "5\r\nABCDE\r\n3\r\nXYZ\r\n0\r\n\r\n",                 23, "ABCDEXYZ",  8);
    expect_dechunk_split_ok("Split - hex size",
        "1a\r\n12345678901234567890123456\r\n0\r\n\r\n",       37, "12345678901234567890123456", 26);
    expect_dechunk_split_ok("Split - chunk-ext on data",
        "5;name=val\r\nABCDE\r\n0\r\n\r\n",                    24, "ABCDE",     5);
    expect_dechunk_split_ok("Split - embedded CRLF in data",
        "5\r\nA\r\nXY\r\n0\r\n\r\n",                           15, "A\r\nXY",   5);

    // chunk_frame — the encoder. Exact-wire assertions; the hex-size and
    // embedded-NUL cases are the ones that catch %zu/%s bugs.
    expect_frame("Frame - small chunk", "ABCDE", 5, "5\r\nABCDE\r\n", 10);
    expect_frame("Frame - single byte", "Q", 1, "1\r\nQ\r\n", 6);

    // Hex size, not decimal: 16 bytes -> "10", 26 bytes -> "1a".
    expect_frame("Frame - hex size (16 -> 10)",
        "0123456789ABCDEF", 16, "10\r\n0123456789ABCDEF\r\n", 22);

    expect_frame("Frame - hex size (26 -> 1a)",
        "abcdefghijklmnopqrstuvwxyz", 26,
        "1a\r\nabcdefghijklmnopqrstuvwxyz\r\n", 32);

    // Binary payload with embedded NULs: must be copied by length, not %s.
    // strcmp/%s would stop at the first 0x00 and corrupt the chunk.
    const char bin[]      = { 'A', 0x00, 'B', 0x00, 0x00, 'C' };
    const char bin_wire[] = { '6','\r','\n', 'A',0x00,'B',0x00,0x00,'C', '\r','\n' };
    expect_frame("Frame - binary payload with NULs",
        bin, sizeof(bin), bin_wire, sizeof(bin_wire));

    // Capacity boundary: "ABCDE" frames to 10 bytes. One short -> -1; exact -> 10.
    expect_frame_cap("Frame - one byte short refuses",  "ABCDE", 5,  9, -1);
    expect_frame_cap("Frame - exact fit succeeds",      "ABCDE", 5, 10, 10);

    // Terminator.
    expect_frame_last("Frame - terminator 0CRLFCRLF");

    // Encoder/decoder are inverses — including binary and embedded CRLF payloads.
    expect_frame_roundtrip("Roundtrip - text",          "ABCDE", 5);
    expect_frame_roundtrip("Roundtrip - single byte",   "Q", 1);
    expect_frame_roundtrip("Roundtrip - binary w/ NULs", bin, sizeof(bin));
    expect_frame_roundtrip("Roundtrip - embedded CRLF",  "A\r\nB", 4);
    expect_frame_roundtrip("Roundtrip - 256 bytes (hex 100)",
        "................................................................"
        "................................................................"
        "................................................................"
        "................................................................", 256);

    // parse_content_length now thin-wraps parse_uint with base=10 — quick sanity tie-in.
    expect_content_length("CL via PU - small",  "100", PARSE_OK, 100);
    expect_content_length("CL via PU - empty",  "",    PARSE_BAD_REQUEST, 0);
}
