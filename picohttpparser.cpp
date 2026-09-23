
#include <assert.h>
#include <stddef.h>
#include <string.h>

#include <string_view>


#include "picohttpparser.hpp"
// anonymous namespace
namespace
{

    constexpr bool is_printable_ascii(int c) noexcept
    {
        return c >= ' ' && c <= '~';
    }

    constexpr bool is_ascii_digit(int c) noexcept
    {
        return c >= '0' and c <= '9';
    }

constexpr char token_char_map[] =
"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
"\0\1\0\1\1\1\1\1\0\0\1\1\0\1\1\0\1\1\1\1\1\1\1\1\1\1\0\0\0\0\0\0"
"\0\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\0\0\0\1\1"
"\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\0\1\0\1\0"
"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";



std::string_view advance_token(const char* buf, const char* buf_end, int* ret)
{
    const char* tok_start = buf;
    
    

    if (buf == buf_end) 
    {
        *ret = -2;
    
        return {};
    }

    while (1)
    {
        if (*buf == ' ')
        {
            break;
        }
        else if (!is_printable_ascii(*buf)) [[unlikely]]
        {
            if ((unsigned char)*buf < '\040' || *buf == '\177')
            {
                *ret = -1;
                return {};
            }
        }
        ++buf;
        if (buf == buf_end) {
            *ret = -2;
            return {};
        };
    }

    *ret = 0;
    size_t len = buf - tok_start;
    return std::string_view(tok_start, len);
}

static const char* get_token_to_eol(const char* buf, const char* buf_end, const char** token, size_t* token_len, int* ret)
{
    const char* token_start = buf;

 
    /* find non-printable char within the next 8 bytes, this is the hottest code; manually inlined */
    while (buf_end - buf >= 8) [[likely]]
    {


        
        if (!is_printable_ascii(*buf)) [[unlikely]] 
            goto NonPrintable; 
        ++buf;
        
        
        if (!is_printable_ascii(*buf)) [[unlikely]] 
            goto NonPrintable; 
        ++buf;
        
        
        if (!is_printable_ascii(*buf)) [[unlikely]] 
            goto NonPrintable; 
        ++buf;
        
        
        if (!is_printable_ascii(*buf)) [[unlikely]] 
            goto NonPrintable; 
        ++buf;
        
        
        if (!is_printable_ascii(*buf)) [[unlikely]] 
            goto NonPrintable; 
        ++buf;
        
        
        if (!is_printable_ascii(*buf)) [[unlikely]] 
            goto NonPrintable; 
        ++buf;
        
        
        if (!is_printable_ascii(*buf)) [[unlikely]] 
            goto NonPrintable; 
        ++buf;
        
        
        if (!is_printable_ascii(*buf)) [[unlikely]] 
            goto NonPrintable; 
        ++buf;
        
 
        continue;
    NonPrintable:
        if (( ((unsigned char)*buf < '\040') && (*buf != '\011')) || (*buf == '\177')) [[likely]]{
            goto FOUND_CTL;
        }
        ++buf;
    }
 
    
    for (;; ++buf) {
        if (buf == buf_end) {
            *ret = -2; return 0;
        };
        if ((!is_printable_ascii(*buf))) [[unlikely]]
        {
            if ((((unsigned char)*buf < '\040') && (*buf != '\011')) || (*buf == '\177')) [[likely]] {
                goto FOUND_CTL;
            }
        }
    }
FOUND_CTL:
    if ((*buf == '\015')) [[likely]]
    {
        ++buf;
        if (buf == buf_end) {
            *ret = -2; return 0;
        } 
        if (*buf++ != '\012') {
            *ret = -1; return 0;
        }
        *token_len = buf - 2 - token_start;
    }
    else if (*buf == '\012') {
        *token_len = buf - token_start;
        ++buf;
    }
    else {
        *ret = -1;
        return NULL;
    }
    *token = token_start;

    return buf;
}


static const char* is_complete(const char* buf, const char* buf_end, size_t last_len, int* ret)
{
    int ret_cnt = 0;
    buf = last_len < 3 ? buf : buf + last_len - 3;

    while (buf != buf_end) 
    {
        if (*buf == '\015') 
        {
            ++buf;
            if (buf == buf_end)
            {
                *ret = -2;
                return NULL;
            }

            //EXPECT_CHAR('\012');
            if (*buf++ != '\012') 
            {
               * ret = -1;                                                                                                                 
                return NULL;                                                                                                               
            }
            ++ret_cnt;
        }
        else if (*buf == '\012') {
            ++buf;
            ++ret_cnt;
        }
        else 
        {
            ++buf;
            ret_cnt = 0;
        }
        
        if (ret_cnt == 2) 
        {
            return buf;
        }
    }

    *ret = -2;
    return NULL;
}


/* returned pointer is always within [buf, buf_end), or null */
static const char* parse_token(const char* buf, const char* buf_end, const char** token, size_t* token_len, char next_char,
    int* ret)
{
    /* We use pcmpestri to detect non-token characters. This instruction can take no more than eight character ranges (8*2*8=128
     * bits that is the size of a SSE register). Due to this restriction, characters `|` and `~` are handled in the slow loop. */
    alignas(16) static const char ranges[] = 
        "\x00 "  /* control chars and up to SP */
        "\"\""   /* 0x22 */
        "()"     /* 0x28,0x29 */
        ",,"     /* 0x2c */
        "//"     /* 0x2f */
        ":@"     /* 0x3a-0x40 */
        "[]"     /* 0x5b-0x5d */
        "{\xff"; /* 0x7b-0xff */
    const char* buf_start = buf;
    
    int found = 0;
    
    //buf = findchar_fast(buf, buf_end, ranges, sizeof(ranges) - 1, &found);
    
    
    if (buf == buf_end) {
        *ret = -2; return 0;
    }
     
    
    while (1) 
    {
        if (*buf == next_char) {
            break;
        }
        else if (!token_char_map[(unsigned char)*buf]) {
            *ret = -1;
            return NULL;
        }
        ++buf;
        if (buf == buf_end) {
            *ret = -2; return 0;
        };
    }
    *token = buf_start;
    *token_len = buf - buf_start;
    return buf;
}

/* returned pointer is always within [buf, buf_end), or null */
static const char* parse_http_version(const char* buf, const char* buf_end, int* minor_version, int* ret)
{
    /* we want at least [HTTP/1.<two chars>] to try to parse */
    if (buf_end - buf < 9) 
    {
        *ret = -2;
        return NULL;
    }
    if (*buf++ != 'H') {
        *ret = -1; 
        return 0;
    }

    if (*buf++ != 'T') {
        *ret = -1; 
        return 0;
    }
    
    if (*buf++ != 'T') {
        *ret = -1; 
        return 0;
    }
    
    if (*buf++ != 'P') {
        *ret = -1; 
        return 0;
    }
    
    if (*buf++ != '/') {
        *ret = -1; 
        return 0;
    }
    
    if (*buf++ != '1') {
        *ret = -1; 
        return 0;
    }
    
    if (*buf++ != '.') {
        *ret = -1; 
        return 0;
    }
    
    
    if (*buf < '0' || '9' < *buf) 
    {
        buf++; 
        *ret = -1; 
        return 0;
    } 
    
    *(minor_version) = (1) * (*buf++ - '0');

    return buf;
}

static const char* parse_headers(const char* buf, const char* buf_end, struct phr_header* headers, size_t* num_headers,
    size_t max_headers, int* ret)
{
    for (;; ++*num_headers) {
        if (buf == buf_end) {
            *ret = -2; return 0;
        };
        if (*buf == '\015') {
            ++buf;
            if (buf == buf_end) {
                *ret = -2; return 0;
            }; if (*buf++ != '\012') {
                *ret = -1; return 0;
            };;
            break;
        }
        else if (*buf == '\012') {
            ++buf;
            break;
        }
        if (*num_headers == max_headers) {
            *ret = -1;
            return NULL;
        }
        if (!(*num_headers != 0 && (*buf == ' ' || *buf == '\t'))) {
            /* parsing name, but do not discard SP before colon, see
             * http://www.mozilla.org/security/announce/2006/mfsa2006-33.html */
            const char* name  = headers[*num_headers].name.data();
            size_t len = headers[*num_headers].name.length();
            buf = parse_token(buf, buf_end, &name, &len, ':', ret);
            
            headers[*num_headers].name = std::string_view(name, len);

            if (buf == NULL) {
                return NULL;
            }
            if (len == 0) {
                *ret = -1;
                return NULL;
            }
            ++buf;
            for (;; ++buf) {
                if (buf == buf_end) {
                    *ret = -2; return 0;
                };
                if (!(*buf == ' ' || *buf == '\t')) {
                    break;
                }
            }
        }
        else {
            headers[*num_headers].name = std::string_view{};
            
        }
        const char* value;
        size_t value_len;
        if ((buf = get_token_to_eol(buf, buf_end, &value, &value_len, ret)) == NULL) {
            return NULL;
        }
        /* remove trailing SPs and HTABs */
        const char* value_end = value + value_len;
        for (; value_end != value; --value_end) {
            const char c = *(value_end - 1);
            if (!(c == ' ' || c == '\t')) {
                break;
            }
        }
        
        headers[*num_headers].value = std::string_view{ value, (size_t)(value_end - value) };
        //headers[*num_headers].value_len = value_end - value;
    }
    return buf;
}

static const char* parse_request(const char* buf, const char* buf_end, const char** method, size_t* method_len, const char** path,
    size_t* path_len, int* minor_version, struct phr_header* headers, size_t* num_headers,
    size_t max_headers, int* ret)
{
    /* skip first empty line (some clients add CRLF after POST content) */
    if (buf == buf_end) {
        *ret = -2; 
        return 0;
    };
    if (*buf == '\015') {
        ++buf;
        if (buf == buf_end) {
            *ret = -2; return 0;
        }; 
        if (*buf++ != '\012') {
            *ret = -1; return 0;
        };;
    }
    else if (*buf == '\012') {
        ++buf;
    }

    /* parse request line */
    if ((buf = parse_token(buf, buf_end, method, method_len, ' ', ret)) == NULL) {
        return NULL;
    }
    do {
        ++buf;
        if (buf == buf_end) {
            *ret = -2; return 0;
        };
    } while (*buf == ' ');
   
    std::string_view path_vw = advance_token(buf, buf_end, ret);
    
    if (*ret != 0) 
    {
        return NULL;
    }
    *path = path_vw.data();
    *path_len = path_vw.length();

    do {
        ++buf;
        if (buf == buf_end) {
            *ret = -2; return 0;
        };
    } while (*buf == ' ');
    if (*method_len == 0 || *path_len == 0) {
        *ret = -1;
        return NULL;
    }
    if ((buf = parse_http_version(buf, buf_end, minor_version, ret)) == NULL) {
        return NULL;
    }
    
    if (*buf == '\015') 
    {
        ++buf;
        
        if (buf == buf_end) 
        {
            *ret = -2; 
            return 0;
        } 
        if (*buf++ != '\012') {
            *ret = -1; 
            return 0;
        }
    }
    else if (*buf == '\012') {
        ++buf;
    }
    else {
        *ret = -1;
        return NULL;
    }

    return parse_headers(buf, buf_end, headers, num_headers, max_headers, ret);
}


static const char* parse_response(const char* buf, const char* buf_end, int* minor_version, int* status, const char** msg,
    size_t* msg_len, struct phr_header* headers, size_t* num_headers, size_t max_headers, int* ret)
{
    /* parse "HTTP/1.x" */
    if ((buf = parse_http_version(buf, buf_end, minor_version, ret)) == NULL) {
        return NULL;
    }
    /* skip space */
    if (*buf != ' ') {
        *ret = -1;
        return NULL;
    }
    do {
        ++buf;
        if (buf == buf_end) 
        {
            *ret = -2; 
            return 0;
        }
    } while (*buf == ' ');
    /* parse status code, we want at least [:digit:][:digit:][:digit:]<other char> to try to parse */
    if (buf_end - buf < 4) {
        *ret = -2;
        return NULL;
    }
    do {
        int res_ = 0; 
        if (*buf < '0' || '9' < *buf) {
            buf++; 
            *ret = -1; 
            return 0;
        } 
        
        *(&res_) = (100) * (*buf++ - '0'); 
        *status = res_; 
        
        if (*buf < '0' || '9' < *buf) {
            buf++; *ret = -1; 
            return 0;
        } 
        
        *(&res_) = (10) * (*buf++ - '0'); 
        *status += res_; 
        
        if (*buf < '0' || '9' < *buf) 
        {
            buf++; *ret = -1; return 0;
        } 
        
        *(&res_) = (1) * (*buf++ - '0'); 
        *status += res_;
    } while (0);

    /* get message including preceding space */
    if ((buf = get_token_to_eol(buf, buf_end, msg, msg_len, ret)) == NULL) {
        return NULL;
    }
    if (*msg_len == 0) {
        /* ok */
    }
    else if (**msg == ' ') {
        /* Remove preceding space. Successful return from `get_token_to_eol` guarantees that we would hit something other than SP
         * before running past the end of the given buffer. */
        do {
            ++*msg;
            --*msg_len;
        } while (**msg == ' ');
    }
    else {
        /* garbage found after status code */
        *ret = -1;
        return NULL;
    }

    return parse_headers(buf, buf_end, headers, num_headers, max_headers, ret);
}







static constexpr int decode_hex(const int ch) noexcept
{
    if ('0' <= ch && ch <= '9') {
        return ch - '0';
    }
    else if ('A' <= ch && ch <= 'F') {
        return ch - 'A' + 0xa;
    }
    else if ('a' <= ch && ch <= 'f') {
        return ch - 'a' + 0xa;
    }
    else {
        return -1;
    }
}

static constexpr bool allowed_after_hex(const int c) noexcept 
{
    switch (c)
    {
    case ' ':
    case '\011':
    case ';':
    case '\012':
    case '\015':
        return true;
    default:
        return false;
    }
}

struct hex_result
{
    size_t value;
    size_t length;

    size_t parse_hex(const std::string_view buf)
    {
        size_t ix = 0;
        while (ix < buf.size())
        {
            const int hex_digit = decode_hex(buf[ix]);

            if (hex_digit == -1)
                break;

            
            length++;

            if (length > 2 * sizeof(size_t))
                break;

            value = value * 16 + hex_digit;

            ix++;
        }
        return ix;
    }
};

static constexpr size_t find_first_of(const std::string_view buf, size_t off, char ca, char cb)
{
    while (off < buf.size()) {
        if (buf[off] == ca || buf[off] == cb)
            return off;
        ++off;
    }

    return off;
}



    struct phr_chunked_decoder_msm
    {
        phr_chunked_decoder& decoder;
        const std::span<char> buf;
        size_t dst, src;
        phr_decode_chunked_result result;

        const std::string_view buf_view;


        enum class SwitchState
        {
            do_continue,
            do_exit,
            do_complete
        };


        explicit phr_chunked_decoder_msm(phr_chunked_decoder& decoder, const std::span<char> buf)
            : decoder(decoder)
            , buf(buf)
            , dst(0)
            , src(0)
            , result{ .left_sz = 0, .ec = chunked_errc::incomplete }
            , buf_view(buf.data(), buf.size())
        {
        }


        void Complete()
        {
            result.left_sz = buf.size() - src;
            result.ec = chunked_errc{};

            return Exit();
        }

        void Exit()
        {
            if (dst != src && src < buf.size())
            {
                memmove(buf.data() + dst, buf.data() + src, buf.size() - src);
            }

            result.buf_len = dst;

            /* if incomplete but the overhead of the chunked encoding is >=100KB and >80%, signal an error */
            if (result.ec == chunked_errc::incomplete)
            {
                decoder._total_overhead += buf.size() - dst;

                if (decoder._total_overhead >= 100 * 1024 && decoder._total_read - decoder._total_overhead < decoder._total_read / 4)
                {
                    result.ec = chunked_errc::error_occur;
                }
            }
        }

        enum SwitchState chunkSize()
        {
            assert(src < buf.size());
            //if (src == buf.size())
           // {
           //     return SwitchState::do_exit;
           // }

            hex_result hrs = { .value = decoder.bytes_left_in_chunk, .length = decoder._hex_count };

            size_t read_count = hrs.parse_hex(buf_view.substr(src));

            src += read_count;

            decoder.bytes_left_in_chunk = hrs.value;
            decoder._hex_count = hrs.length;

            if (decoder._hex_count == 0 || decoder._hex_count > 2 * sizeof(size_t)) {
                result.ec = chunked_errc::error_occur;
                return SwitchState::do_exit;
            }

            if (src == buf.size()) {
                return SwitchState::do_exit;
            }

            if (!allowed_after_hex(buf[src])) {
                result.ec = chunked_errc::error_occur;
                return SwitchState::do_exit;
            }

            decoder._hex_count = 0;
            decoder._state = ChunkedState::chunk_ext;

            return SwitchState::do_continue;
        }

        enum SwitchState chunkExt()
        {
            /* RFC 7230 A.2 "Line folding in chunk extensions is disallowed" */
           // assert(src < buf.size());

            src = find_first_of(buf_view, src, '\015', '\012');

            if (src == buf_view.size()) {
                return SwitchState::do_exit;
            }
            if (buf_view[src] == '\012') {
                result.ec = chunked_errc::error_occur;
                return SwitchState::do_exit;
            }
            src++;
            decoder._state = ChunkedState::chunk_header_expect_lf;
            return SwitchState::do_continue;
        }

        enum SwitchState chunkHeaderExpectLF()
        {
            assert(src < buf.size());
            //if (src == buf.size())
            //    return SwitchState::do_exit;

            if (buf[src] != '\012')
            {
                result.ec = chunked_errc::error_occur;
                return SwitchState::do_exit;
            }
            ++src;

            if (decoder.bytes_left_in_chunk == 0)
            {
                if (decoder.consume_trailer)
                {
                    decoder._state = ChunkedState::trailers_line_head;
                    return SwitchState::do_continue;
                }
                else
                {
                    //goto Complete;
                    return SwitchState::do_complete;
                }
            }
            decoder._state = ChunkedState::chunk_data;
            return SwitchState::do_continue;
        }

        enum SwitchState chunkData()
        {
            // assert(src < buf.size());

            size_t avail = buf.size() - src;
            if (avail < decoder.bytes_left_in_chunk)
            {
                if (dst != src)
                {
                    memmove(buf.data() + dst, buf.data() + src, avail);
                }

                src += avail;
                dst += avail;

                decoder.bytes_left_in_chunk -= avail;

                return SwitchState::do_exit;
            }

            if (dst != src)
            {
                memmove(buf.data() + dst, buf.data() + src, decoder.bytes_left_in_chunk);
            }

            src += decoder.bytes_left_in_chunk;
            dst += decoder.bytes_left_in_chunk;

            decoder.bytes_left_in_chunk = 0;
            decoder._state = ChunkedState::chunk_data_expect_cr;

            return SwitchState::do_continue;
        }

        enum SwitchState chunkDataExpectCR()
        {
            assert(src < buf.size());
            //if (src == buf.size())
            //    return SwitchState::do_exit;

            if (buf[src] != '\015') {
                result.ec = chunked_errc::error_occur;
                return SwitchState::do_exit;
            }
            ++src;
            decoder._state = ChunkedState::chunk_data_expect_lf;
            return SwitchState::do_continue;
        }

        enum SwitchState chunkDataExpectLF()
        {
             assert(src < buf.size());
            //if (src == buf.size())
            //    return SwitchState::do_exit;

            if (buf[src] != '\012')
            {
                result.ec = chunked_errc::error_occur;
                return SwitchState::do_exit;
            }
            ++src;
            decoder._state = ChunkedState::chunk_size;
            return SwitchState::do_continue;
        }

        enum SwitchState trailerLineHead()
        {
            // assert(src < buf.size());
            const size_t pos = buf_view.find_first_not_of('\015', src);
            if (pos == buf_view.npos) {
                //all symbols are '\015'
                src = buf.size();
                return SwitchState::do_exit;
            }
            src = pos;

            if (buf[src++] == '\012')
                return SwitchState::do_complete;

            decoder._state = ChunkedState::trailers_line_middle;
            return SwitchState::do_continue;
        }

        enum SwitchState trailerLineMiddle()
        {
            //  assert(src < buf.size());

            const size_t pos = buf_view.find('\012', src);
            if (pos == buf_view.npos) {
                src = buf.size();
                return SwitchState::do_exit;
            }
            src = pos + 1;
            decoder._state = ChunkedState::trailers_line_head;
            return SwitchState::do_continue;
        }

        enum SwitchState  doSwitch()
        {
            switch (decoder._state)
            {
            case ChunkedState::chunk_size: return chunkSize();
            case ChunkedState::chunk_ext: return chunkExt();
            case ChunkedState::chunk_header_expect_lf: return chunkHeaderExpectLF();
            case ChunkedState::chunk_data: return chunkData();
            case ChunkedState::chunk_data_expect_cr: return chunkDataExpectCR();
            case ChunkedState::chunk_data_expect_lf: return chunkDataExpectLF();
            case ChunkedState::trailers_line_head: return trailerLineHead();
            case ChunkedState::trailers_line_middle: return trailerLineMiddle();
            default:
                assert(!"decoder is corrupt");
                return SwitchState::do_continue;
            }
        }

        void process()
        {
            decoder._total_read += buf.size();
            while (src < buf.size())
            {
                SwitchState state = doSwitch();
                switch (state) 
                {
                case SwitchState::do_continue: break;//continue
                case SwitchState::do_exit: return Exit();
                case SwitchState::do_complete: return Complete();
                }
            }
            return Exit();
        }
    };
} // end namespace anonymous

//int phr_parse_response(const char* buf_start, size_t len, int* minor_version, int* status, const char** msg, size_t* msg_len,
 //   struct phr_header* headers, size_t* num_headers, size_t last_len)
response_result phr_parse_response(const std::span<const char> buf_start, std::span<phr_header> headers, size_t last_len)
{
    const char* buf = buf_start.data(), * buf_end = buf + buf_start.size();
    size_t max_headers = headers.size();
    
    response_result result{};
    int r;

    result.minor_version = -1;
    result.num_headers = 0;

    /* if last_len != 0, check if the response is complete (a fast countermeasure
       against slowloris */
    if (last_len != 0 && is_complete(buf, buf_end, last_len, &r) == NULL) {
        result.ec = static_cast<parse_ec>(r);
        return result;
    }
    int minor_version = -1;
    int status = 0;
    const char* msg = NULL;
    size_t msg_len = 0;
    size_t num_headers = 0;

    buf = parse_response(buf, buf_end, &minor_version, &status, &msg, &msg_len, headers.data(), &num_headers, max_headers, &r);
    
    result.minor_version = minor_version;
    result.status = status;
    result.msg = msg == NULL ? std::string_view{} : std::string_view{ msg, msg_len };
    result.num_headers = num_headers;

    if (buf == NULL) {
        result.ec = static_cast<parse_ec>(r);
        return result;
    }

    result.bsz = (buf - buf_start.data());
    
    return result;
}

parse_result phr_parse_headers(const std::span<const char> buf_start, std::span<phr_header> headers, size_t last_len)
{
    const char* buf = buf_start.data(), * buf_end = buf + buf_start.size();
    size_t max_headers = headers.size();
    
    parse_result result{};

    int r = 0;

    /* if last_len != 0, check if the response is complete (a fast countermeasure
       against slowloris */
    if (last_len != 0 && is_complete(buf, buf_end, last_len, &r) == NULL) {
        result.ec = static_cast<parse_ec>(r);
        return result;
        //return r;
    }
    size_t num_headers = 0;

    if ((buf = parse_headers(buf, buf_end, headers.data(), &num_headers, max_headers, &r)) == NULL) {
        result.num_headers = num_headers;
        result.ec = static_cast<parse_ec>(r);
        return result;
    }

    result.num_headers = num_headers;
    result.ec = parse_ec::ok;
    result.bsz = (size_t)(buf - buf_start.data());
    return result;
}

//int phr_parse_request(const char* buf_start, size_t len, const char** method, size_t* method_len, const char** path,
//    size_t* path_len, int* minor_version, struct phr_header* headers, size_t* num_headers, size_t last_len)
request_result phr_parse_request(const std::span<const char> buf_start, std::span<phr_header> headers, size_t last_len)
{
    const char* buf = buf_start.data(), * buf_end = buf_start.data() + buf_start.size();
    size_t max_headers = headers.size();
    int r;
    request_result result{};
    

    result.minor_version = -1;

    /* if last_len != 0, check if the request is complete (a fast countermeasure
       againt slowloris */
    if (last_len != 0 && is_complete(buf, buf_end, last_len, &r) == NULL) {
        result.ec = static_cast<parse_ec>(r);
        return result;
    }

    const char* method = nullptr;
    size_t method_len = 0;

    const char* path = nullptr;
    size_t path_len = 0;

    int minor_version = -1;
    size_t num_headers = 0;
    buf = parse_request(buf, buf_end, &method, &method_len, &path, &path_len, &minor_version, headers.data(), &num_headers, max_headers,
        &r);

    result.minor_version = minor_version;
    result.num_headers = num_headers;
    result.method = (method != nullptr ? std::string_view{ method, method_len } : std::string_view{});
    result.path = (path != nullptr ? std::string_view(path, path_len) : std::string_view{});

    if (buf == NULL) {
        result.ec = static_cast<parse_ec>(r);
        return result;
        
    }
    result.ec = parse_ec::ok;
    result.bsz = (buf - buf_start.data());
    return result;
}

phr_decode_chunked_result phr_decode_chunked(struct phr_chunked_decoder& decoder, const std::span<char> buf)
{
    phr_chunked_decoder_msm msm(decoder, buf);
    msm.process();
    return msm.result;
}


bool phr_decode_chunked_is_in_data(const struct phr_chunked_decoder& decoder)
{
    return decoder._state == ChunkedState::chunk_data; //CHUNKED_IN_CHUNK_DATA;
}

