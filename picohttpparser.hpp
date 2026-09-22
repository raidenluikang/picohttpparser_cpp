#ifndef PICOHTTPPARSER_HPP
#define PICOHTTPPARSER_HPP

#include <stdint.h>
#include <stddef.h>
#include <compare>

#ifdef _MSC_VER
using ssize_t = intptr_t;
#endif


    /* Users of the library are recommended to use the up-to-date master branch. But for those who prefer using versions, the release
     * branch is also updated each time a new commit is pushed to the master branch; incrementing the minor version by one. */
#define PICOHTTPPARSER_VERSION "1.dev"
#define PICOHTTPPARSER_VERSION_MAJOR 1
#define PICOHTTPPARSER_VERSION_MINOR 99999999 /* master is treated as newer than any 1.x snapshot */

    /* contains name and value of a header (name == NULL if is a continuing line
    * of a multiline header */
struct phr_header {
    const char* name;
    size_t name_len;
    const char* value;
    size_t value_len;
};

/* returns number of bytes consumed if successful, -2 if request is partial,
    * -1 if failed */
int phr_parse_request(const char* buf, size_t len, const char** method, size_t* method_len, const char** path, size_t* path_len,
    int* minor_version, struct phr_header* headers, size_t* num_headers, size_t last_len);

/* ditto */
int phr_parse_response(const char* _buf, size_t len, int* minor_version, int* status, const char** msg, size_t* msg_len,
    struct phr_header* headers, size_t* num_headers, size_t last_len);

/* ditto */
int phr_parse_headers(const char* buf, size_t len, struct phr_header* headers, size_t* num_headers, size_t last_len);


enum class ChunkedState : unsigned char
{
    chunk_size,
    chunk_ext,
    chunk_header_expect_lf,
    chunk_data,
    chunk_data_expect_cr,
    chunk_data_expect_lf,
    trailers_line_head,
    trailers_line_middle
    //CHUNKED_IN_CHUNK_SIZE,
    //CHUNKED_IN_CHUNK_EXT,
    //CHUNKED_IN_CHUNK_HEADER_EXPECT_LF,
    //CHUNKED_IN_CHUNK_DATA,
    //CHUNKED_IN_CHUNK_DATA_EXPECT_CR,
    //CHUNKED_IN_CHUNK_DATA_EXPECT_LF,
    //CHUNKED_IN_TRAILERS_LINE_HEAD,
    //CHUNKED_IN_TRAILERS_LINE_MIDDLE
};

/* should be zero-filled before start */
struct phr_chunked_decoder {
    size_t bytes_left_in_chunk; /* number of bytes left in current chunk */
    char consume_trailer;       /* if trailing headers should be consumed */
    char _hex_count;
    ChunkedState _state;
    uint64_t _total_read;
    uint64_t _total_overhead;
};

/* the function rewrites the buffer given as (buf, bufsz) removing the chunked-
    * encoding headers.  When the function returns without an error, bufsz is
    * updated to the length of the decoded data available.  Applications should
    * repeatedly call the function while it returns -2 (incomplete) every time
    * supplying newly arrived data.  If the end of the chunked-encoded data is
    * found, the function returns a non-negative number indicating the number of
    * octets left undecoded, that starts from the offset returned by `*bufsz`.
    * Returns -1 on error.
    */
enum class chunked_errc { error_occur = -1, incomplete = -2 };
struct phr_decode_chunked_result
{
    ptrdiff_t bufsz;
    chunked_errc ec;

    auto operator <=> (const phr_decode_chunked_result& other) const = default;
};

phr_decode_chunked_result phr_decode_chunked(struct phr_chunked_decoder* decoder, char* buf, size_t* bufsz);

/* returns if the chunked decoder is in middle of chunked data */
bool phr_decode_chunked_is_in_data(const struct phr_chunked_decoder& decoder);



#endif //!PICOHTTPPARSER_HPP