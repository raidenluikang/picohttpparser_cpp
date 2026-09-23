
#include <assert.h>
#include <stddef.h>
#include <string.h>

#include <string_view>
#include <algorithm>
#include <numeric>
#include <array>
#include <ranges>

#include "picohttpparser.hpp"

namespace // anonymous namespace
{

    constexpr bool is_printable_ascii(char c) noexcept
    {
        return c >= ' ' && c <= '~';
    }

    constexpr bool is_ascii_digit(char c) noexcept
    {
        return c >= '0' && c <= '9';
    }
    constexpr bool is_ascii_control(char c) noexcept
    {
        return static_cast<unsigned char>(c) < 32 || c == 127; /*DEL = 127 code*/
    }
    
    constexpr bool is_ascii_control_except_tab(char c) noexcept
    {
        return c != '\t' && is_ascii_control(c);
    }

    constexpr bool space_or_tab(char c) noexcept
    {
        return (c == ' ') || (c == '\t');
    }

    constexpr std::string_view remove_last_spaces_and_tabs(const std::string_view value) noexcept
    {
        
        auto rv = value | std::views::reverse;
        const auto it = std::ranges::find_if_not(rv, space_or_tab);
        size_t counter = std::ranges::distance(rv.begin(), it);
        return value.substr(0, value.length() - counter);
    }

//constexpr char token_char_map[] =
//"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
//"\0\1\0\1\1\1\1\1\0\0\1\1\0\1\1\0\1\1\1\1\1\1\1\1\1\1\0\0\0\0\0\0"
//"\0\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\0\0\0\1\1"
//"\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\0\1\0\1\0"
//"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
//"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
//"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
//"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";

// bit i в mask[k] соответствует символу с кодом (64*k + i)

constexpr size_t char_value_bits   = 256;
constexpr size_t uint64_bits_count = 64;
constexpr size_t mask_size = char_value_bits / uint64_bits_count;

constexpr std::array<uint64_t, mask_size> token_char_mask = 
{
    0x03FF6CFA00000000ULL,  // символы 0-63    (0-31: control, 32-63: !#$%&'*+-.0-9)
    0x57FFFFFFC7FFFFFEULL,  // символы 64-127  (64-90 A-Z^_, 96-122 `a-z|~)
    0x0000000000000000ULL,  // символы 128-191 — всё 0
    0x0000000000000000ULL,  // символы 192-255 — всё 0
};

constexpr bool is_token_char(unsigned char c) noexcept
{
    return (token_char_mask[c >> 6] >> (c & 63)) & 1ULL;
}

constexpr char CR = '\015';   // 0x0D, Carriage Return
constexpr char LF = '\012';   // 0x0A, Line Feed

struct advance_result
{
    std::string_view token;
    parse_ec ec;

    constexpr advance_result  unexpected(parse_ec ec) noexcept
    {
        this->ec = ec;
        return *this;
    }
};

constexpr advance_result advance_token(const std::string_view buf) noexcept
{
    advance_result result{};
    const auto it = std::find_if(buf.cbegin(), buf.cend(), [](const char c) {
        return c == ' ' || is_ascii_control(c);
    });
    if (it == buf.cend())
        return result.unexpected(parse_ec::partial);
    if (*it != ' ')   // остановились на control-символе, не на пробеле
        return result.unexpected(parse_ec::failed);
    result.token = std::string_view(buf.cbegin(), it);
    return result ;
}

struct token_to_eol_result
{
    ptrdiff_t processed;
    std::string_view token;
    parse_ec ec;
    
   constexpr token_to_eol_result  unexpected(parse_ec ec) noexcept 
   {
        this->ec = ec;
        return *this;
    }
};

constexpr token_to_eol_result get_token_to_eol(const std::string_view buf) noexcept
{
    token_to_eol_result result{};

    const auto ctl_it = std::find_if(buf.cbegin(), buf.cend(), is_ascii_control_except_tab);
    if (ctl_it == buf.cend())
        return result.unexpected(parse_ec::partial);

    result.token = std::string_view(buf.cbegin(), ctl_it);   // общее для обеих веток
    switch (*ctl_it)
    {
    case LF:
        result.processed = std::distance(buf.cbegin(), std::next(ctl_it));  //std::string_view(std::next(ctl_it), buf.cend());
        return result;
    case CR:
    {
        const auto lf_it = std::next(ctl_it);
        if (lf_it == buf.cend())
            return result.unexpected(parse_ec::partial);
        if (*lf_it != LF)
            return result.unexpected(parse_ec::failed);

        result.processed = std::distance(buf.cbegin(), std::next(lf_it)); //std::string_view( std::next(lf_it), buf.cend());
        return result;
    }
    default:
        return result.unexpected(parse_ec::failed);
    }
}



constexpr parse_ec is_complete(const std::string_view buf, size_t last_len) noexcept
{
    const size_t start_pos = last_len < 3 ? 0 : last_len - 3;
    for (size_t index = start_pos, ret_cnt = 0; index < buf.size(); ++index)
    {
        switch (buf[index]) 
        {
        case CR:
            if (++index == buf.size())
                return parse_ec::partial;

            if (buf[index] != LF)
                return parse_ec::failed;
            [[fallthrough]];
        case LF:
            ++ret_cnt;
        break;
        default:
            ret_cnt = 0;
        break;
        }
        if (ret_cnt == 2) 
            return parse_ec::ok;
    }
    return parse_ec::partial;
}


struct token_result
{
    std::string_view token;
    parse_ec ec;

    constexpr token_result unexpected(parse_ec ec) noexcept
    {
        this->ec = ec;
        return *this;
    }
};

constexpr token_result parse_token(const std::string_view buf, char next_char) noexcept
{
    token_result result{};
    const auto iter = std::find_if(buf.cbegin(), buf.cend(), [next_char](char c) {
        return c == next_char || !is_token_char(c);
        });

    if (iter == buf.cend())
        return result.unexpected(parse_ec::partial);

    if (*iter != next_char) //non token char
        return result.unexpected(parse_ec::failed);

    result.token = std::string_view(buf.cbegin(), iter);
   
    return result;
}

struct http_version_result
{
    std::string_view left;
    int minor_version;
    parse_ec ec;

    constexpr http_version_result unexpected(parse_ec ec) noexcept
    {
        this->ec = ec;
        return *this;
    }
};


constexpr http_version_result parse_http_version(const std::string_view buf) noexcept
{
    using namespace std::literals::string_view_literals;

    constexpr std::string_view http_prefix = "HTTP/1."sv;

    http_version_result result{};

    /* we want at least [HTTP/1.<two chars>] to try to parse */
    if (buf.length() < http_prefix.length() + 2) //+2 chars
        return result.unexpected(parse_ec::partial);
    
    if (!buf.starts_with(http_prefix))
        return result.unexpected(parse_ec::failed);

    if (!is_ascii_digit(buf[http_prefix.length()]))
        return result.unexpected(parse_ec::failed);
    
    result.minor_version =  buf[http_prefix.length()] - '0';
    result.left = buf.substr(http_prefix.length() + 1);

    return result;
}

struct headers_result
{
    size_t processed;
    parse_ec ec;
    size_t num_headers;

    constexpr headers_result unexpected(parse_ec ec) noexcept {
        this->ec = ec;
        return *this;
    }
};

struct header_list
{
    std::span<phr_header> list;
    size_t index;
};

headers_result parse_headers(const std::string_view buf, header_list headers)
{
    headers_result result{};
    result.num_headers = headers.index;

    size_t& index = result.processed;
    size_t& num_headers = result.num_headers;

    for (;; ++num_headers) {
         
        if (index == buf.size())
            return result.unexpected(parse_ec::partial);

        if (buf[index] == CR) {
            ++index;
            
            if (index == buf.size())
                return result.unexpected(parse_ec::partial);
            
            if (buf[index++] != LF)
                return result.unexpected(parse_ec::failed);
            
            break;
        }
        else if (buf[index] == LF) 
        {
            ++index;
            break;
        }
        
        if (num_headers == headers.list.size())
            return result.unexpected(parse_ec::failed);

        if (!(num_headers != 0 && space_or_tab(buf[index]) ) )
        {
            /* parsing name, but do not discard SP before colon, see
             * http://www.mozilla.org/security/announce/2006/mfsa2006-33.html */

            token_result tk_res = parse_token(buf.substr(index), ':');
           
            if (tk_res.ec != parse_ec::ok)
                return result.unexpected(tk_res.ec);

            headers.list[num_headers].name = tk_res.token;
            index += tk_res.token.length();

            if (tk_res.token.empty())
                return result.unexpected(parse_ec::failed);

            assert(buf[index] == ':');
            ++index; // skip the ':'

            // find non space or tab
            const auto nst_iter = std::find_if_not(buf.cbegin() + index, buf.cend(), space_or_tab);
            if (nst_iter == buf.cend())
                return result.unexpected(parse_ec::partial);

            index = nst_iter - buf.cbegin();
        }
        else 
        {
            headers.list[num_headers].name = std::string_view{};
            
        }

        token_to_eol_result eol_res = get_token_to_eol(buf.substr(index));
        if (eol_res.ec != parse_ec::ok)
            return result.unexpected(eol_res.ec);
        
        index += eol_res.processed;
        headers.list[num_headers].value  = remove_last_spaces_and_tabs( eol_res.token );
    }
   
    return result;
}

static request_result parse_request(const std::string_view buf,  header_list headers)
{
    request_result result{};
     

    /* skip first empty line (some clients add CRLF after POST content) */
    if (buf.empty())
        return result.unexpected(parse_ec::partial);
    
    size_t& index = result.bsz; // consumed bytes.

    if (buf[index] == CR)
    {
        ++index;
        
        if (index == buf.size())
            return result.unexpected(parse_ec::partial);
        
        if (buf[index++] != LF)
            return result.unexpected(parse_ec::failed);
    }
    else if (buf[index] == LF)
    {
        ++index;
    }

    /* parse request line */
    {
        token_result tk_res = parse_token(buf.substr(index), ' ');
        if (tk_res.ec != parse_ec::ok)
            return result.unexpected(tk_res.ec);

        index += tk_res.token.length();

        result.method = tk_res.token;
        
    }

    size_t non_space = buf.find_first_not_of(' ', index + 1);
    if (non_space == buf.npos)
        return result.unexpected(parse_ec::partial);
    index = non_space;

   
    advance_result adv_res = advance_token(buf.substr(index));
    if (adv_res.ec != parse_ec::ok)
        return result.unexpected(adv_res.ec);

    result.path = adv_res.token;
    
    index += adv_res.token.length();

    size_t non_space2 = buf.find_first_not_of(' ', index + 1);

    if (non_space2 == buf.npos)
        return result.unexpected(parse_ec::partial);

    index = non_space2;

    if (result.method.empty() || result.path.empty())
        return result.unexpected(parse_ec::failed);

    http_version_result http_version = parse_http_version(buf.substr(index));

    if (http_version.ec != parse_ec::ok)
        return result.unexpected(http_version.ec);
    
    result.minor_version = http_version.minor_version;
    
    index = http_version.left.data() - buf.data(); //@TODO fix it.
    
    if (buf[index] == CR)
    {
        ++index;
        
        if (index == buf.size())
            return result.unexpected(parse_ec::partial);
        if (buf[index++] != LF)
            return result.unexpected(parse_ec::failed);
    }
    else if (buf[index] == LF ) {
        ++index;
    }
    else {
        return result.unexpected(parse_ec::failed);
    }

    headers_result hd_res =  parse_headers( buf.substr(index), headers);

    result.ec = hd_res.ec;
    result.num_headers = hd_res.num_headers;

    index += hd_res.processed;

    return result;
}


static const char* parse_response(const char* buf, const char* buf_end, int* minor_version, int* status, const char** msg,
    size_t* msg_len, struct phr_header* headers, size_t* num_headers, size_t max_headers, int* ret)
{
    /* parse "HTTP/1.x" */
    std::string_view buf_vw { buf, static_cast<size_t>(buf_end - buf) };
    http_version_result http_version = parse_http_version(buf_vw);

    if ( http_version.ec != parse_ec::ok) {
        *ret = static_cast<int>(http_version.ec);
        return NULL;
    }
    
    buf = http_version.left.data();
    *minor_version = http_version.minor_version;

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
    std::string_view buf_vw2{ buf, static_cast<size_t>(buf_end - buf) };
    //if ((buf = get_token_to_eol(buf_vw2, msg, msg_len, ret)) == NULL) 
    token_to_eol_result eol_res = get_token_to_eol(buf_vw2);
    if (eol_res.ec != parse_ec::ok)
    {
        *ret = static_cast<int>(eol_res.ec);
        return NULL;
    }

    buf += eol_res.processed;
    *msg = eol_res.token.data();
    *msg_len = eol_res.token.length();

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
    header_list hlist;
    hlist.list = std::span<phr_header>(headers, max_headers);
    hlist.index = *num_headers;

    headers_result hd_res =  parse_headers(std::string_view{ buf, buf_end }, hlist);
    *ret = (int)hd_res.ec;
    *num_headers = hd_res.num_headers;

    return buf + hd_res.processed;
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
    

    if (last_len != 0 ) {
        std::string_view buf_vw{ buf, static_cast<size_t>(buf_end - buf) };
        parse_ec cpl_res = is_complete(buf_vw, last_len);
        if (cpl_res != parse_ec::ok) {
            result.ec = cpl_res;
            return result;
        }
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

    if (r != 0) 
    {
        result.ec = static_cast<parse_ec>(r);
        return result;
    }

    result.bsz = (buf - buf_start.data());
    
    return result;
}

parse_result phr_parse_headers(const std::span<const char> buf_start, std::span<phr_header> headers, size_t last_len)
{
    //const char* buf = buf_start.data(), * buf_end = buf + buf_start.size();
    const std::string_view buf(buf_start.data(), buf_start.size());
    size_t max_headers = headers.size();
    
    parse_result result{};

    /* if last_len != 0, check if the response is complete (a fast countermeasure
       against slowloris */
    if (last_len != 0 and (result.ec = is_complete(buf, last_len)) != parse_ec::ok)
    {
            return result;
    }

    
    header_list hlist;
    hlist.list = headers;
    hlist.index = 0;

    headers_result hd_res = parse_headers(buf, hlist);
    result.num_headers = hd_res.num_headers;
    result.ec  = hd_res.ec;
    result.bsz = hd_res.processed; 
    
    return result;
}


request_result phr_parse_request(const std::span<const char> buf_start, std::span<phr_header> headers, size_t last_len)
{
    //const char* buf = buf_start.data(), * buf_end = buf_start.data() + buf_start.size();
    //size_t max_headers = headers.size();
    
    request_result result{};
    std::string_view buf(buf_start.data(), buf_start.size());

    result.minor_version = -1;

    /* if last_len != 0, check if the request is complete (a fast countermeasure
       againt slowloris */
    
    if (last_len != 0 ) {
        
        parse_ec cpl_res = is_complete(buf, last_len);
        if (cpl_res != parse_ec::ok) {
            result.ec = cpl_res;
            return result;
        }
    }
    header_list hdr_list;
    hdr_list.list = headers;
    hdr_list.index = 0;
    return parse_request(buf, hdr_list );

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

