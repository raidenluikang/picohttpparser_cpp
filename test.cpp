
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif 

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "picohttpparser.hpp"


#include <format>
#include <source_location>
#include <string>
#include <cstdio>
#include <iostream>


namespace picotest
{
    struct test_t
    {
        int num_tests = 0;
        int level = 0;
        bool failed = false;



        void indent(void) const
        {
            for (int i = 0; i != level; ++i)
            {
                std::cout << "    ";
            }
        }

        void note(const std::string& text) const
        {
            indent();
            std::cout << "# " << text << "\n";
            //printf("# %s\n", text.c_str());
        }

        void _OK(bool cond, const std::string& text) {
            if (!cond) {
                failed = true;
            }
            indent();

            //printf("%s %d - %s\n", cond ? "ok" : "not ok", ++num_tests, text.c_str());
            
            const char* cond_str[ 2 ] = { "not ok", "ok" };
            
            std::cout << std::format("{} {} - {}\n", cond_str[ cond ], num_tests, text);
            
            num_tests++;
        }

        void OK(bool cond, const std::source_location location =
            std::source_location::current()) 
        {
            _OK(cond, std::format("{} {}", location.file_name(), location.line()));
        }

        bool done(void) const
        {
            indent();
            //printf("1..%d\n", num_tests);
            std::cout << "1.." << num_tests << "\n";
            return failed;
        }

        template <typename Callback>
        void subtest(const char* name, Callback cb)
        {
            //copy current test
            struct test_t test {}; // num_test = 0 and failed = false.
            test.level = this->level + 1; // level increased.

            test.note(std::format("Subtest: {}", name));

            cb(test);

            test.done();

            if (test.failed)
                this->failed = true;
            _OK(!test.failed, std::format("{}", name));
        }
    };

} // end namespace picotest

static int bufis(const char* s, size_t l, const char* t)
{
    return strlen(t) == l && memcmp(s, t, l) == 0;
}

static char* inputbuf; /* point to the end of the buffer */

static void test_request(struct picotest::test_t & test)
{
    const char* method;
    size_t method_len;
    const char* path;
    size_t path_len;
    int minor_version;
    struct phr_header headers[4];
    size_t num_headers;

//#define PARSE(s, last_len, exp, comment)                                                                                           \
//    do {                                                                                                                           \
//        size_t slen = sizeof(s) - 1;                                                                                               \
//        test.note(comment);                                                                                                        \
//        num_headers = sizeof(headers) / sizeof(headers[0]);                                                                        \
//        memcpy(inputbuf - slen, s, slen);                                                                                          \
//        test.OK(phr_parse_request(inputbuf - slen, slen, &method, &method_len, &path, &path_len, &minor_version, headers, &num_headers, \
//                             last_len) == (exp == 0 ? (int)slen : exp));                                                           \
//    } while (0)

    const auto PARSE = [&]<size_t N>(const char(&s)[N], size_t last_len, int exp, const char* comment)
    {
        size_t slen = N - 1; // sizeof(s) - 1
        test.note(comment);
        num_headers = sizeof(headers) / sizeof(headers[0]);
        memcpy(inputbuf - slen, s, slen);
        test.OK(phr_parse_request(inputbuf - slen, slen, &method, &method_len, &path, &path_len, &minor_version, headers, &num_headers, 
                                   last_len) == (exp == 0 ? (int)slen : exp));
                
    };

    PARSE("GET / HTTP/1.0\r\n\r\n", 0, 0, "simple");
    test.OK(num_headers == 0);
    test.OK(bufis(method, method_len, "GET"));
    test.OK(bufis(path, path_len, "/"));
    test.OK(minor_version == 0);

    PARSE("GET / HTTP/1.0\r\n\r", 0, -2, "partial");

    PARSE("GET /hoge HTTP/1.1\r\nHost: example.com\r\nCookie: \r\n\r\n", 0, 0, "parse headers");
    test.OK(num_headers == 2);
    test.OK(bufis(method, method_len, "GET"));
    test.OK(bufis(path, path_len, "/hoge"));
    test.OK(minor_version == 1);
    test.OK(bufis(headers[0].name, headers[0].name_len, "Host"));
    test.OK(bufis(headers[0].value, headers[0].value_len, "example.com"));
    test.OK(bufis(headers[1].name, headers[1].name_len, "Cookie"));
    test.OK(bufis(headers[1].value, headers[1].value_len, ""));

    PARSE("GET /hoge HTTP/1.1\r\nHost: example.com\r\nUser-Agent: \343\201\262\343/1.0\r\n\r\n", 0, 0, "multibyte included");
    test.OK(num_headers == 2);
    test.OK(bufis(method, method_len, "GET"));
    test.OK(bufis(path, path_len, "/hoge"));
    test.OK(minor_version == 1);
    test.OK(bufis(headers[0].name, headers[0].name_len, "Host"));
    test.OK(bufis(headers[0].value, headers[0].value_len, "example.com"));
    test.OK(bufis(headers[1].name, headers[1].name_len, "User-Agent"));
    test.OK(bufis(headers[1].value, headers[1].value_len, "\343\201\262\343/1.0"));

    PARSE("GET / HTTP/1.0\r\nfoo: \r\nfoo: b\r\n  \tc\r\n\r\n", 0, 0, "parse multiline");
    test.OK(num_headers == 3);
    test.OK(bufis(method, method_len, "GET"));
    test.OK(bufis(path, path_len, "/"));
    test.OK(minor_version == 0);
    test.OK(bufis(headers[0].name, headers[0].name_len, "foo"));
    test.OK(bufis(headers[0].value, headers[0].value_len, ""));
    test.OK(bufis(headers[1].name, headers[1].name_len, "foo"));
    test.OK(bufis(headers[1].value, headers[1].value_len, "b"));
    test.OK(headers[2].name == NULL);
    test.OK(bufis(headers[2].value, headers[2].value_len, "  \tc"));

    PARSE("GET / HTTP/1.0\r\nfoo : ab\r\n\r\n", 0, -1, "parse header name with trailing space");

    PARSE("GET", 0, -2, "incomplete 1");
    test.OK(method == NULL);
    PARSE("GET ", 0, -2, "incomplete 2");
    test.OK(bufis(method, method_len, "GET"));
    PARSE("GET /", 0, -2, "incomplete 3");
    test.OK(path == NULL);
    PARSE("GET / ", 0, -2, "incomplete 4");
    test.OK(bufis(path, path_len, "/"));
    PARSE("GET / H", 0, -2, "incomplete 5");
    PARSE("GET / HTTP/1.", 0, -2, "incomplete 6");
    PARSE("GET / HTTP/1.0", 0, -2, "incomplete 7");
    test.OK(minor_version == -1);
    PARSE("GET / HTTP/1.0\r", 0, -2, "incomplete 8");
    test.OK(minor_version == 0);

    PARSE("GET /hoge HTTP/1.0\r\n\r", strlen("GET /hoge HTTP/1.0\r\n\r") - 1, -2, "slowloris (incomplete)");
    PARSE("GET /hoge HTTP/1.0\r\n\r\n", strlen("GET /hoge HTTP/1.0\r\n\r\n") - 1, 0, "slowloris (complete)");

    PARSE(" / HTTP/1.0\r\n\r\n", 0, -1, "empty method");
    PARSE("GET  HTTP/1.0\r\n\r\n", 0, -1, "empty request-target");

    PARSE("GET / HTTP/1.0\r\n:a\r\n\r\n", 0, -1, "empty header name");
    PARSE("GET / HTTP/1.0\r\n :a\r\n\r\n", 0, -1, "header name (space only)");

    PARSE("G\0T / HTTP/1.0\r\n\r\n", 0, -1, "NUL in method");
    PARSE("G\tT / HTTP/1.0\r\n\r\n", 0, -1, "tab in method");
    PARSE(":GET / HTTP/1.0\r\n\r\n", 0, -1, "invalid method");
    PARSE("GET /\x7fhello HTTP/1.0\r\n\r\n", 0, -1, "DEL in uri-path");
    PARSE("GET / HTTP/1.0\r\na\0b: c\r\n\r\n", 0, -1, "NUL in header name");
    PARSE("GET / HTTP/1.0\r\nab: c\0d\r\n\r\n", 0, -1, "NUL in header value");
    PARSE("GET / HTTP/1.0\r\na\033b: c\r\n\r\n", 0, -1, "CTL in header name");
    PARSE("GET / HTTP/1.0\r\nab: c\033\r\n\r\n", 0, -1, "CTL in header value");
    PARSE("GET / HTTP/1.0\r\n/: 1\r\n\r\n", 0, -1, "invalid char in header value");
    PARSE("GET /\xa0 HTTP/1.0\r\nh: c\xa2y\r\n\r\n", 0, 0, "accept MSB chars");
    test.OK(num_headers == 1);
    test.OK(bufis(method, method_len, "GET"));
    test.OK(bufis(path, path_len, "/\xa0"));
    test.OK(minor_version == 0);
    test.OK(bufis(headers[0].name, headers[0].name_len, "h"));
    test.OK(bufis(headers[0].value, headers[0].value_len, "c\xa2y"));

    PARSE("GET / HTTP/1.0\r\n\x7c\x7e: 1\r\n\r\n", 0, 0, "accept |~ (though forbidden by SSE)");
    test.OK(num_headers == 1);
    test.OK(bufis(headers[0].name, headers[0].name_len, "\x7c\x7e"));
    test.OK(bufis(headers[0].value, headers[0].value_len, "1"));

    PARSE("GET / HTTP/1.0\r\n\x7b: 1\r\n\r\n", 0, -1, "disallow {");

    PARSE("GET / HTTP/1.0\r\nfoo: a \t \r\n\r\n", 0, 0, "exclude leading and trailing spaces in header value");
    test.OK(bufis(headers[0].value, headers[0].value_len, "a"));

    PARSE("GET   /   HTTP/1.0\r\n\r\n", 0, 0, "accept multiple spaces between tokens");


}

static void test_response(struct picotest::test_t& test)
{
    int minor_version;
    int status;
    const char* msg;
    size_t msg_len;
    struct phr_header headers[4];
    size_t num_headers;

//#define PARSE(s, last_len, exp, comment)                                                                                           \
//    do {                                                                                                                           \
//        size_t slen = sizeof(s) - 1;                                                                                               \
//        test.note(comment);                                                                                                             \
//        num_headers = sizeof(headers) / sizeof(headers[0]);                                                                        \
//        memcpy(inputbuf - slen, s, slen);                                                                                          \
//        test.OK(phr_parse_response(inputbuf - slen, slen, &minor_version, &status, &msg, &msg_len, headers, &num_headers, last_len) ==  \
//           (exp == 0 ? (int)slen : exp));                                                                                          \
//    } while (0)

    const auto PARSE = [&]<size_t N>(const char(&s)[N], size_t last_len, int exp, const char* comment)
    {
        size_t slen = N - 1; // sizeof(s) - 1
        test.note(comment);
        num_headers = sizeof(headers) / sizeof(headers[0]);
        memcpy(inputbuf - slen, s, slen);
        test.OK(phr_parse_response(inputbuf - slen, slen, &minor_version, &status, &msg, &msg_len, 
            headers, &num_headers, last_len) == (exp == 0 ? (int)slen : exp));
    };

    PARSE("HTTP/1.0 200 OK\r\n\r\n", 0, 0, "simple");
    test.OK(num_headers == 0);
    test.OK(status == 200);
    test.OK(minor_version == 0);
    test.OK(bufis(msg, msg_len, "OK"));

    PARSE("HTTP/1.0 200 OK\r\n\r", 0, -2, "partial");

    PARSE("HTTP/1.1 200 OK\r\nHost: example.com\r\nCookie: \r\n\r\n", 0, 0, "parse headers");
    test.OK(num_headers == 2);
    test.OK(minor_version == 1);
    test.OK(status == 200);
    test.OK(bufis(msg, msg_len, "OK"));
    test.OK(bufis(headers[0].name, headers[0].name_len, "Host"));
    test.OK(bufis(headers[0].value, headers[0].value_len, "example.com"));
    test.OK(bufis(headers[1].name, headers[1].name_len, "Cookie"));
    test.OK(bufis(headers[1].value, headers[1].value_len, ""));

    PARSE("HTTP/1.0 200 OK\r\nfoo: \r\nfoo: b\r\n  \tc\r\n\r\n", 0, 0, "parse multiline");
    test.OK(num_headers == 3);
    test.OK(minor_version == 0);
    test.OK(status == 200);
    test.OK(bufis(msg, msg_len, "OK"));
    test.OK(bufis(headers[0].name, headers[0].name_len, "foo"));
    test.OK(bufis(headers[0].value, headers[0].value_len, ""));
    test.OK(bufis(headers[1].name, headers[1].name_len, "foo"));
    test.OK(bufis(headers[1].value, headers[1].value_len, "b"));
    test.OK(headers[2].name == NULL);
    test.OK(bufis(headers[2].value, headers[2].value_len, "  \tc"));

    PARSE("HTTP/1.0 500 Internal Server Error\r\n\r\n", 0, 0, "internal server error");
    test.OK(num_headers == 0);
    test.OK(minor_version == 0);
    test.OK(status == 500);
    test.OK(bufis(msg, msg_len, "Internal Server Error"));
    test.OK(msg_len == sizeof("Internal Server Error") - 1);

    PARSE("H", 0, -2, "incomplete 1");
    PARSE("HTTP/1.", 0, -2, "incomplete 2");
    PARSE("HTTP/1.1", 0, -2, "incomplete 3");
    test.OK(minor_version == -1);
    PARSE("HTTP/1.1 ", 0, -2, "incomplete 4");
    test.OK(minor_version == 1);
    PARSE("HTTP/1.1 2", 0, -2, "incomplete 5");
    PARSE("HTTP/1.1 200", 0, -2, "incomplete 6");
    test.OK(status == 0);
    PARSE("HTTP/1.1 200 ", 0, -2, "incomplete 7");
    test.OK(status == 200);
    PARSE("HTTP/1.1 200 O", 0, -2, "incomplete 8");
    PARSE("HTTP/1.1 200 OK\r", 0, -2, "incomplete 9");
    test.OK(msg == NULL);
    PARSE("HTTP/1.1 200 OK\r\n", 0, -2, "incomplete 10");
    test.OK(bufis(msg, msg_len, "OK"));
    PARSE("HTTP/1.1 200 OK\n", 0, -2, "incomplete 11");
    test.OK(bufis(msg, msg_len, "OK"));

    PARSE("HTTP/1.1 200 OK\r\nA: 1\r", 0, -2, "incomplete 11");
    test.OK(num_headers == 0);
    PARSE("HTTP/1.1 200 OK\r\nA: 1\r\n", 0, -2, "incomplete 12");
    test.OK(num_headers == 1);
    test.OK(bufis(headers[0].name, headers[0].name_len, "A"));
    test.OK(bufis(headers[0].value, headers[0].value_len, "1"));

    PARSE("HTTP/1.0 200 OK\r\n\r", strlen("HTTP/1.0 200 OK\r\n\r") - 1, -2, "slowloris (incomplete)");
    PARSE("HTTP/1.0 200 OK\r\n\r\n", strlen("HTTP/1.0 200 OK\r\n\r\n") - 1, 0, "slowloris (complete)");

    PARSE("HTTP/1. 200 OK\r\n\r\n", 0, -1, "invalid http version");
    PARSE("HTTP/1.2z 200 OK\r\n\r\n", 0, -1, "invalid http version 2");
    PARSE("HTTP/1.1  OK\r\n\r\n", 0, -1, "no status code");

    PARSE("HTTP/1.1 200\r\n\r\n", 0, 0, "accept missing trailing whitespace in status-line");
    test.OK(bufis(msg, msg_len, ""));
    PARSE("HTTP/1.1 200X\r\n\r\n", 0, -1, "garbage after status 1");
    PARSE("HTTP/1.1 200X \r\n\r\n", 0, -1, "garbage after status 2");
    PARSE("HTTP/1.1 200X OK\r\n\r\n", 0, -1, "garbage after status 3");

    PARSE("HTTP/1.1 200 OK\r\nbar: \t b\t \t\r\n\r\n", 0, 0, "exclude leading and trailing spaces in header value");
    test.OK(bufis(headers[0].value, headers[0].value_len, "b"));

    PARSE("HTTP/1.1   200   OK\r\n\r\n", 0, 0, "accept multiple spaces between tokens");

 
}

static void test_headers(struct picotest::test_t& test)
{
    /* only test the interface; the core parser is tested by the tests above */

    struct phr_header headers[4];
    size_t num_headers;

//#define PARSE(s, last_len, exp, comment)                                                                                           \
//    do {                                                                                                                           \
//        test.note(comment);                                                                                                             \
//        num_headers = sizeof(headers) / sizeof(headers[0]);                                                                        \
//        test.OK(phr_parse_headers(s, strlen(s), headers, &num_headers, last_len) == (exp == 0 ? (int)strlen(s) : exp));                 \
//    } while (0)

    
    auto const PARSE = [&]<size_t N>(const char(&s)[N], int last_len, int exp, const char* comment)
    {
        test.note(comment);
        num_headers = sizeof(headers) / sizeof(headers[0]);
        test.OK(phr_parse_headers(s, strlen(s), headers, &num_headers, last_len) == (exp == 0 ? (int)strlen(s) : exp));
    };

    PARSE("Host: example.com\r\nCookie: \r\n\r\n", 0, 0, "simple");
    test.OK(num_headers == 2);
    test.OK(bufis(headers[0].name, headers[0].name_len, "Host"));
    test.OK(bufis(headers[0].value, headers[0].value_len, "example.com"));
    test.OK(bufis(headers[1].name, headers[1].name_len, "Cookie"));
    test.OK(bufis(headers[1].value, headers[1].value_len, ""));

    PARSE("Host: example.com\r\nCookie: \r\n\r\n", 1, 0, "slowloris");
    test.OK(num_headers == 2);
    test.OK(bufis(headers[0].name, headers[0].name_len, "Host"));
    test.OK(bufis(headers[0].value, headers[0].value_len, "example.com"));
    test.OK(bufis(headers[1].name, headers[1].name_len, "Cookie"));
    test.OK(bufis(headers[1].value, headers[1].value_len, ""));

    PARSE("Host: example.com\r\nCookie: \r\n\r", 0, -2, "partial");

    PARSE("Host: e\7fample.com\r\nCookie: \r\n\r", 0, -1, "error");

 
}

static void test_chunked_at_once(int line, int consume_trailer, const char* encoded, const char* decoded, struct phr_decode_chunked_result expected, struct picotest::test_t& test)
{
    struct phr_chunked_decoder dec = { 0 };
    char* buf;
    size_t bufsz;
    //ssize_t ret;

    dec.consume_trailer = consume_trailer;

    test.note(std::format("testing at-once, source at line {}", line));

    buf = _strdup(encoded);
    bufsz = strlen(buf);

    auto ret = phr_decode_chunked(&dec, buf, &bufsz);

    test.OK(ret == expected);
    test.OK(bufsz == strlen(decoded));
    test.OK(bufis(buf, bufsz, decoded));
    
    if (expected.ec == chunked_errc{}) 
    {
        if (ret == expected)
            test.OK(bufis(buf + bufsz, ret.bufsz, encoded + strlen(encoded) - ret.bufsz));
        else
            test.OK(false);
    }

    free(buf);
}

static void test_chunked_per_byte(int line, int consume_trailer, const char* encoded, const char* decoded, struct phr_decode_chunked_result expected, struct picotest::test_t& test)
{
    struct phr_chunked_decoder dec = { 0 };
    char* buf = (char*) malloc(strlen(encoded) + 1);
    size_t bytes_to_consume = strlen(encoded) - (expected.ec == chunked_errc{} ? expected.bufsz : 0), bytes_ready = 0, bufsz, i;
    struct phr_decode_chunked_result ret;

    dec.consume_trailer = consume_trailer;

    test.note(std::format("testing per-byte, source at line {}", line));

    for (i = 0; i < bytes_to_consume - 1; ++i) {
        buf[bytes_ready] = encoded[i];
        bufsz = 1;
        ret = phr_decode_chunked(&dec, buf + bytes_ready, &bufsz);
        if (ret.ec != chunked_errc::incomplete) {
            test.OK(false);
            goto cleanup;
        }
        bytes_ready += bufsz;
    }
    strcpy(buf + bytes_ready, encoded + bytes_to_consume - 1);
    bufsz = strlen(buf + bytes_ready);
    ret = phr_decode_chunked(&dec, buf + bytes_ready, &bufsz);
    test.OK(ret == expected);
    bytes_ready += bufsz;
    test.OK(bytes_ready == strlen(decoded));
    test.OK(bufis(buf, bytes_ready, decoded));
    
    if (expected.ec == chunked_errc{}) 
    {
        if (ret == expected)
            test.OK(bufis(buf + bytes_ready, expected.bufsz, encoded + bytes_to_consume));
        else
            test.OK(false);
    }

cleanup:
    free(buf);
}

static void test_chunked_failure(int line, const char* encoded, struct phr_decode_chunked_result expected, struct picotest::test_t& test)
{
    struct phr_chunked_decoder dec = { 0 };
    char* buf = _strdup(encoded);
    size_t bufsz, i;
    struct phr_decode_chunked_result ret;

    test.note(std::format("testing failure at-once, source at line {}", line));
    bufsz = strlen(buf);
    ret = phr_decode_chunked(&dec, buf, &bufsz);
    test.OK(ret == expected);

    test.note(std::format("testing failure per-byte, source at line {}", line));

    memset(&dec, 0, sizeof(dec));
    
    for (i = 0; encoded[i] != '\0'; ++i) 
    {
        buf[0] = encoded[i];
        bufsz = 1;
        ret = phr_decode_chunked(&dec, buf, &bufsz);
        if (ret.ec == chunked_errc::error_occur) {
            test.OK(ret == expected);
            goto cleanup;
        }
        else if (ret.ec == chunked_errc::incomplete) {
            /* continue */
        }
        else {
            test.OK(false);
            goto cleanup;
        }
    }
    test.OK(ret == expected);

cleanup:
    free(buf);
}

static void (*chunked_test_runners[])(int, int, const char*, const char*, struct phr_decode_chunked_result, struct picotest::test_t&) =
{ 
    test_chunked_at_once, 
    test_chunked_per_byte,
    nullptr 
};

static void test_chunked(struct picotest::test_t& test)
{
    size_t i;

    for (i = 0; chunked_test_runners[i] != NULL; ++i) 
    {
        chunked_test_runners[i](__LINE__, 0, "b\r\nhello world\r\n0\r\n", "hello world", phr_decode_chunked_result{}, test);
        chunked_test_runners[i](__LINE__, 0, "6\r\nhello \r\n5\r\nworld\r\n0\r\n", "hello world", phr_decode_chunked_result{}, test);
        chunked_test_runners[i](__LINE__, 0, "6;comment=hi\r\nhello \r\n5\r\nworld\r\n0\r\n", "hello world", phr_decode_chunked_result{}, test);
        chunked_test_runners[i](__LINE__, 0, "6 ; comment\r\nhello \r\n5\r\nworld\r\n0\r\n", "hello world", phr_decode_chunked_result{}, test);
        chunked_test_runners[i](__LINE__, 0, "6\r\nhello \r\n5\r\nworld\r\n0\r\na: b\r\nc: d\r\n\r\n", "hello world",
            phr_decode_chunked_result{ .bufsz = sizeof("a: b\r\nc: d\r\n\r\n") - 1 }, test);
        chunked_test_runners[i](__LINE__, 0, "b\r\nhello world\r\n0\r\n", "hello world", phr_decode_chunked_result{}, test);
    }

    test.note("failures");
    test_chunked_failure(__LINE__, "z\r\nabcdefg", phr_decode_chunked_result{.bufsz = 0, .ec = chunked_errc::error_occur}, test);
    if (sizeof(size_t) == 8) {
        test_chunked_failure(__LINE__, "6\r\nhello \r\nffffffffffffffff\r\nabcdefg", phr_decode_chunked_result{ .bufsz = 0, .ec = chunked_errc::incomplete }, test);
        test_chunked_failure(__LINE__, "6\r\nhello \r\nfffffffffffffffff\r\nabcdefg", phr_decode_chunked_result{ .bufsz = 0, .ec = chunked_errc::error_occur }, test);
    }
    test_chunked_failure(__LINE__, "1x\r\na\r\n0\r\n", phr_decode_chunked_result{ .bufsz = 0, .ec = chunked_errc::error_occur }, test);

    /* bare lf cannot be used in chunk header */
    test_chunked_failure(__LINE__, "6\nhello \r\n5\r\nworld\r\n0\r\n", phr_decode_chunked_result{ .bufsz = 0, .ec = chunked_errc::error_occur },  test);
    test_chunked_failure(__LINE__, "6\r\nhello \n5\r\nworld\r\n0\r\n", phr_decode_chunked_result{ .bufsz = 0, .ec = chunked_errc::error_occur }, test);
    test_chunked_failure(__LINE__, "6\r\nhello \r\n5\r\nworld\n0\r\n", phr_decode_chunked_result{ .bufsz = 0, .ec = chunked_errc::error_occur }, test);
    test_chunked_failure(__LINE__, "6\r\nhello \r\n5\r\nworld\n0\r\n", phr_decode_chunked_result{ .bufsz = 0, .ec = chunked_errc::error_occur }, test);
    test_chunked_failure(__LINE__, "6\r\nhello \r\n5\r\nworld\r\n0\n", phr_decode_chunked_result{ .bufsz = 0, .ec = chunked_errc::error_occur }, test);
    test_chunked_failure(__LINE__, "6\rX\nhello \n5\r\nworld\r\n0\r\n", phr_decode_chunked_result{ .bufsz = 0, .ec = chunked_errc::error_occur }, test);
}

static void test_chunked_consume_trailer(struct picotest::test_t& test)
{
    size_t i;

    for (i = 0; chunked_test_runners[i] != NULL; ++i) {
        chunked_test_runners[i](__LINE__, 1, "b\r\nhello world\r\n0\r\n", "hello world", phr_decode_chunked_result{ .bufsz = 0, .ec = chunked_errc::incomplete }, test);
        chunked_test_runners[i](__LINE__, 1, "6\r\nhello \r\n5\r\nworld\r\n0\r\n", "hello world", phr_decode_chunked_result{ .bufsz = 0, .ec = chunked_errc::incomplete }, test);
        chunked_test_runners[i](__LINE__, 1, "6;comment=hi\r\nhello \r\n5\r\nworld\r\n0\r\n", "hello world", phr_decode_chunked_result{ .bufsz = 0, .ec = chunked_errc::incomplete }, test);
        chunked_test_runners[i](__LINE__, 1, "b\r\nhello world\r\n0\r\n\r\n", "hello world", phr_decode_chunked_result{ }, test);
        chunked_test_runners[i](__LINE__, 1, "6\r\nhello \r\n5\r\nworld\r\n0\r\na: b\r\nc: d\r\n\r\n", "hello world", phr_decode_chunked_result{ }, test);
        /* bare lf is allowed in trailers, for consistency to when they are parsed using phr_parse_headers */
        chunked_test_runners[i](__LINE__, 1, "b\r\nhello world\r\n0\r\n\n", "hello world", phr_decode_chunked_result{ }, test);
        chunked_test_runners[i](__LINE__, 1, "6\r\nhello \r\n5\r\nworld\r\n0\r\na: b\nc: d\n\n", "hello world", phr_decode_chunked_result{ }, test);
    }
}

static void test_chunked_leftdata(struct picotest::test_t& test)
{

    
    const char NEXT_REQ[] = "GET / HTTP/1.1\r\n\r\n";

    struct phr_chunked_decoder dec = { 0 };
    dec.consume_trailer = 1;
    char buf[] = "5\r\nabcde\r\n0\r\n\r\n"  "GET / HTTP/1.1\r\n\r\n";
    size_t bufsz = sizeof(buf) - 1;

    phr_decode_chunked_result ret = phr_decode_chunked(&dec, buf, &bufsz);
    test.OK(ret.ec != chunked_errc::error_occur );
    test.OK(bufsz == 5);
    test.OK(memcmp(buf, "abcde", 5) == 0);
    test.OK(ret.bufsz == sizeof(NEXT_REQ) - 1);
    test.OK(memcmp(buf + bufsz, NEXT_REQ, sizeof(NEXT_REQ) - 1) == 0);


}

static phr_decode_chunked_result do_test_chunked_overhead(size_t chunk_len, size_t chunk_count, const char* extra)
{
    struct phr_chunked_decoder dec = { 0 };
    char buf[1024];
    size_t bufsz;
    phr_decode_chunked_result ret{};

    for (size_t i = 0; i < chunk_count; ++i) {
        /* build and feed the chunk header */
        bufsz = (size_t)snprintf(buf, sizeof(buf), "%zx%s\r\n", chunk_len, extra);
        if ((ret = phr_decode_chunked(&dec, buf, &bufsz)).ec != chunked_errc::incomplete)
            goto Exit;
        assert(bufsz == 0);
        /* build and feed the chunk boby */
        memset(buf, 'A', chunk_len);
        bufsz = chunk_len;
        if ((ret = phr_decode_chunked(&dec, buf, &bufsz)).ec != chunked_errc::incomplete)
            goto Exit;
        assert(bufsz == chunk_len);
        /* build and feed the chunk end (CRLF) */
        strcpy(buf, "\r\n");
        bufsz = 2;
        if ((ret = phr_decode_chunked(&dec, buf, &bufsz)).ec != chunked_errc::incomplete)
            goto Exit;
        assert(bufsz == 0);
    }

    /* build and feed the end chunk */
    strcpy(buf, "0\r\n\r\n");
    bufsz = 5;
    ret = phr_decode_chunked(&dec, buf, &bufsz);
    assert(bufsz == 0);

Exit:
    return ret;
}

static void test_chunked_overhead(struct picotest::test_t& test)
{
    test.OK(do_test_chunked_overhead(100, 10000, "") == phr_decode_chunked_result{ .bufsz = 2 } /* consume trailer is not set */);
    test.OK(do_test_chunked_overhead(10, 100000, "") == phr_decode_chunked_result{ .bufsz = 2 } /* consume trailer is not set */);
    test.OK(do_test_chunked_overhead(1, 1000000, "") == phr_decode_chunked_result{ .bufsz = 0, .ec = chunked_errc::error_occur });

    test.OK(do_test_chunked_overhead(10, 100000, "; tiny=1") == phr_decode_chunked_result{ .bufsz = 2 } /* consume trailer is not set */);
    test.OK(do_test_chunked_overhead(10, 100000, "; large=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") == phr_decode_chunked_result{ .bufsz = 0, .ec = chunked_errc::error_occur });
}

static constexpr size_t INPUTBUF_SIZE = 65536;   /* с запасом, оригинал давал одну страницу (обычно 4096) */

int main(void)
{
    //long pagesize = sysconf(_SC_PAGESIZE);
    //assert(pagesize >= 1);

    //inputbuf = mmap(NULL, pagesize * 3, PROT_NONE, MAP_ANON | MAP_PRIVATE, -1, 0);
    //assert(inputbuf != MAP_FAILED);
    //inputbuf += pagesize * 2;
    //ok(mprotect(inputbuf - pagesize, pagesize, PROT_READ | PROT_WRITE) == 0);

    char* inputbase = (char*)malloc(INPUTBUF_SIZE);
    assert(inputbase != NULL);
    inputbuf = inputbase + INPUTBUF_SIZE;   /* конец буфера, как раньше конец страницы */

    struct picotest::test_t main_test;

    main_test.subtest("request", test_request);
    main_test.subtest("response", test_response);
    main_test.subtest("headers", test_headers);
    main_test.subtest("chunked", test_chunked);
    main_test.subtest("chunked-consume-trailer", test_chunked_consume_trailer);
    main_test.subtest("chunked-leftdata", test_chunked_leftdata);
    main_test.subtest("chunked-overhead", test_chunked_overhead);

    //munmap(inputbuf - pagesize * 2, pagesize * 3);

    free(inputbase);

    main_test.done();
    
    return main_test.failed ? -1 : 0;
}