#ifndef PICOHTTPPARSER_HPP
#define PICOHTTPPARSER_HPP

#include <cstdint>
#include <cstddef>
#include <span>
#include <string_view>



    /* Users of the library are recommended to use the up-to-date master branch. But for those who prefer using versions, the release
     * branch is also updated each time a new commit is pushed to the master branch; incrementing the minor version by one. */
#define PICOHTTPPARSER_VERSION "1.dev"
#define PICOHTTPPARSER_VERSION_MAJOR 1
#define PICOHTTPPARSER_VERSION_MINOR 99999999 /* master is treated as newer than any 1.x snapshot */

    /* contains name and value of a header (name == NULL if is a continuing line
    * of a multiline header */
struct phr_header 
{
    std::string_view name;
    std::string_view value;
};

enum class parse_ec
{
    ok,
    failed = -1,
    partial = -2,
};

struct parse_result
{
    parse_ec ec;
    size_t bsz;// number of bytes consumed.

    size_t num_headers;
};

struct response_result : public parse_result
{
    int minor_version = -1;
    int status = 0;
    std::string_view msg;

    constexpr response_result unexpected(parse_ec ec) noexcept {
        this->ec = ec;
        return *this;
    }
};

struct request_result : public parse_result
{
    std::string_view method;
    std::string_view path;
    int minor_version = -1;

    constexpr request_result unexpected(parse_ec ec) noexcept {
        this->ec = ec;
        return *this;
    }
};

/* returns number of bytes consumed if successful, -2 if request is partial,
    * -1 if failed */
request_result phr_parse_request(const std::span<const char> buf, std::span<phr_header> headers, size_t last_len);

/* ditto */
response_result phr_parse_response(const std::span<const char> buf,   std::span<phr_header> headers, size_t last_len);

/* ditto */
parse_result phr_parse_headers(const std::span<const char> buf, std::span<phr_header> headers, size_t last_len);


enum class ChunkedState 
{
    chunk_size,
    chunk_ext,
    chunk_header_expect_lf,
    chunk_data,
    chunk_data_expect_cr,
    chunk_data_expect_lf,
    trailers_line_head,
    trailers_line_middle
};

/* should be zero-filled before start */
struct phr_chunked_decoder 
{
    size_t bytes_left_in_chunk; /* number of bytes left in current chunk */
    bool consume_trailer;       /* if trailing headers should be consumed */
    size_t _hex_count;
    ChunkedState _state;
    uint64_t _total_read;
    uint64_t _total_overhead;
};

/* the function rewrites the buffer given as (buf) removing the chunked-
    * encoding headers.  When the function returns without an error, buf_len is
    * updated to the length of the decoded data available.  Applications should
    * repeatedly call the function while it returns -2 (incomplete) every time
    * supplying newly arrived data.  If the end of the chunked-encoded data is
    * found, the function returns a non-negative number indicating the number of
    * octets left undecoded, that starts from the offset returned by left_sz.
    * Returns -1 on error.
    */

enum class chunked_errc { error_occur = -1, incomplete = -2 };

struct phr_decode_chunked_result
{
    ptrdiff_t left_sz; // number of octet left undecoded.
    chunked_errc ec;

    size_t buf_len;//available decoded data length

    //compare only left_sz and ec.
    friend 
    constexpr bool operator == (const phr_decode_chunked_result& lhs, const phr_decode_chunked_result& rhs) noexcept
    {
        return lhs.left_sz == rhs.left_sz && lhs.ec == rhs.ec;
    }
};

phr_decode_chunked_result phr_decode_chunked(struct phr_chunked_decoder& decoder, const std::span<char> buf);

/* returns if the chunked decoder is in middle of chunked data */
bool phr_decode_chunked_is_in_data(const struct phr_chunked_decoder& decoder);



#endif //!PICOHTTPPARSER_HPP