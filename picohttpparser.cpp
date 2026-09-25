
#include <cassert>
#include <cstddef>
#include <algorithm>

#ifdef __SSE4_2__
#ifdef _MSC_VER
#include <nmmintrin.h>
#else
#include <x86intrin.h>
#endif
#endif

#include "picohttpparser.hpp"

namespace // anonymous namespace
{
    constexpr char CR    = 0x0D ;   // '\r', Carriage Return
    constexpr char LF    = 0x0A ;   // '\n', Line Feed
    constexpr char SP    = 0x20 ;   // ' ', Space Character
    constexpr char TAB   = 0x09 ;   // '\t', Tab space
    constexpr char COLON = 0x3A ;   // ':'

    [[maybe_unused]]
    [[nodiscard]] constexpr bool is_printable_ascii(char c) noexcept
    {
        //return ((unsigned char)(c)-040u < 0137u);
        return c >= 0x20 && c <= 0x7E;
    }

    [[nodiscard]] constexpr bool is_ascii_digit(char c) noexcept
    {
        return c >= 0x30 && c <= 0x39;
    }

    [[nodiscard]] constexpr bool is_ascii_control(char c) noexcept
    {
        return static_cast<unsigned char>(c) < 0x20 || c == 0x7F; /*DEL = 127 code*/
    }
    
    [[nodiscard]] constexpr bool is_ascii_control_except_tab(char c) noexcept
    {
        return c != TAB && is_ascii_control(c);
    }

    [[nodiscard]] constexpr bool space_or_tab(char c) noexcept
    {
        return (c == SP) || (c == TAB);
    }


//constexpr char token_char_map[] = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
//        "\0\1\0\1\1\1\1\1\0\0\1\1\0\1\1\0\1\1\1\1\1\1\1\1\1\1\0\0\0\0\0\0"
//        "\0\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\0\0\0\1\1"
//        "\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\0\1\0\1\0"
//        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
//        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
//        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
//        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";

constexpr bool token_char_map[256] = { 
    false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false,
    false, true, false, true, true, true, true, true, false, false, true, true, false, true, true, false,
    true, true, true, true, true, true, true, true, true, true, false, false, false, false, false, false,
    false, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true,
    true, true, true, true, true, true, true, true, true, true, true, false, false, false, true, true,
    true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true,
    true, true, true, true, true, true, true, true, true, true, true, false, true, false, true, false,
    false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false,
};

[[nodiscard]] constexpr bool is_token_char(unsigned char c) noexcept
{
    return  token_char_map[c];
}


template <typename Iterator>
[[nodiscard]] constexpr Iterator remove_last_spaces_and_tabs(Iterator first, Iterator last) noexcept
{
    while (first != last && space_or_tab(*std::prev(last)))
    {
        --last;
    }

    return last;
}

template<size_t ranges_size, typename Iterator>
static Iterator findchar_fast([[maybe_unused]] Iterator buf, 
                            [[maybe_unused]] Iterator buf_end, 
                            [[maybe_unused]] const char *ranges, 
                            [[maybe_unused]]  int& found)
{
    
#if __SSE4_2__
    if (buf_end - buf >= 16) [[likely]]
    {
        __m128i ranges16 = _mm_loadu_si128((const __m128i*)ranges);

        size_t left = (buf_end - buf) & ~15;
        do {
            __m128i b16 = _mm_loadu_si128((const __m128i*)&*buf);
            int r = _mm_cmpestri(ranges16, ranges_size, b16, 16, _SIDD_LEAST_SIGNIFICANT | _SIDD_CMP_RANGES | _SIDD_UBYTE_OPS);
            if (r != 16) [[unlikely]]
            {
                buf += r;
                found = 1;
                break;
            }
            buf += 16;
            left -= 16;
        } while (left != 0);
    }
#endif
    return buf;
}


template <typename Iterator>
[[nodiscard]] constexpr Iterator advance_token(Iterator first, Iterator last, parse_ec& ec) noexcept
{
    const auto iter = std::find_if(first, last, [](const char c) 
    {
        return c == SP || is_ascii_control(c);
    });

    if (iter == last)
        ec = parse_ec::partial;
    else
    if (*iter != SP)   // остановились на control-символе, не на пробеле
        ec = parse_ec::failed;
    else
        ec = parse_ec::ok;
    
    
    return iter ;
}


template <typename Iterator>
[[nodiscard]]  Iterator get_token_to_eol(Iterator first, Iterator last, parse_ec & ec) noexcept
{

#ifdef __SSE4_2__
    alignas(16) static  const char  ranges1[16] = "\0\010"    /* allow HT */
        "\012\037"  /* allow SP and up to but not including DEL */
        "\177\177"; /* allow chars w. MSB set */
    int found = 0;
    first = findchar_fast<6, Iterator>(first, last, ranges1, found);
    if (found)
        goto FOUND_CTL;
#else

    while (last - first >= 8) 
    {
        if (!is_printable_ascii(*first)) [[unlikely]]
            goto NonPrintable;
        ++first;


        if (!is_printable_ascii(*first)) [[unlikely]]
            goto NonPrintable;
        ++first;


        if (!is_printable_ascii(*first)) [[unlikely]]
            goto NonPrintable;
        ++first;


        if (!is_printable_ascii(*first)) [[unlikely]]
            goto NonPrintable;
        ++first;


        if (!is_printable_ascii(*first)) [[unlikely]]
            goto NonPrintable;
        ++first;


        if (!is_printable_ascii(*first)) [[unlikely]]
            goto NonPrintable;
        ++first;


        if (!is_printable_ascii(*first)) [[unlikely]]
            goto NonPrintable;
        ++first;


        if (!is_printable_ascii(*first)) [[unlikely]]
            goto NonPrintable;
        ++first;
        
        continue;

    NonPrintable:
        if ( is_ascii_control_except_tab(*first) ) 
            goto FOUND_CTL;
        ++first;

    }
#endif //! __SSE4_2__

    for (;; ++first) {
        if (first == last)
        {
            ec = parse_ec::partial;
            return first;
        }

        if (!is_printable_ascii(*first))  
        {
            if (is_ascii_control_except_tab(*first)) 
                goto FOUND_CTL;
        }
    }
FOUND_CTL:
    if (*first == CR ) 
    {
        ++first;
        if (first == last)  
        {
            ec = parse_ec::partial;
        }
        else if (*first != LF) {
            ec = parse_ec::failed;
        } else {
            ++first;
            ec = static_cast<parse_ec>(+2);
        }
    }
    else if (*first == LF ) {
        ec = static_cast<parse_ec>(+1);
        ++first;
    }
    else {
        ec = parse_ec::failed;
    }
    return first;
}

template <typename Iterator>
[[nodiscard]] constexpr Iterator last_iter_cmlt(Iterator first, size_t last_len) noexcept
{
    return last_len < 3 ? first : first + (last_len - 3);
}

template <typename Iterator>
[[nodiscard]] constexpr parse_ec is_complete(Iterator first, Iterator last) noexcept
{
    int ret_cnt = 0;
    for (; first != last; ++first)
    {
        switch (*first) 
        {
        case CR:
            if (++first == last)
                return parse_ec::partial;

            if (*first != LF)
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

template <typename Iterator>
[[nodiscard]] constexpr Iterator parse_token(Iterator first, Iterator last, char next_char, parse_ec& ec) noexcept
{
    auto iter = std::find_if(first, last, [next_char](char c) {
        return c == next_char || !is_token_char(c);
        });

    if (iter == last)  
        ec = parse_ec::partial;
    else
    if (*iter != next_char)  
        ec = parse_ec::failed;
    else
        ec = parse_ec::ok;

    return iter;
}

//save minor version to ec.
template <typename Iterator>
[[nodiscard]] constexpr Iterator parse_http_version(Iterator first, Iterator last, parse_ec& ec) noexcept
{
    //                                          H     T     T     P     /     1    .
    constexpr unsigned char http_prefix[] = { 0x48, 0x54, 0x54, 0x50, 0x2F, 0x31, 0x2E };

    if (std::distance(first, last) < 9)
    {
        ec = parse_ec::partial;
    }
    else if (!std::equal(http_prefix, http_prefix + 7, first))
    {
        ec = parse_ec::failed;
    }
    else
    {
        first += 7;
        if (!is_ascii_digit(*first))
            ec = parse_ec::failed;
        else
            ec = static_cast<parse_ec>(*first++ - 0x30);
    }
    
    return first;
}



template <typename Iterator>
[[nodiscard]] Iterator parse_headers(Iterator first, Iterator last, std::span<phr_header> headers, size_t& num_headers, parse_ec& ec)
{
    ec = parse_ec::ok;
    size_t num = num_headers;
    for (; ; ++num)
    {
        if (first == last)
        {
            ec = parse_ec::partial;
            num_headers = num;
            return first;
        }

        if (*first == CR) {
            ++first;
            
            if (first == last)
            {
                ec = parse_ec::partial; 
                num_headers = num;
                return first;
            }
            
            if (*first++ != LF)
            {
                ec = parse_ec::failed; 
                num_headers = num;
                return first;
            }
            break;
        }
        else if (*first == LF) 
        {
            ++first;
            break;
        }
        
        if (num >= headers.size())
        {
            ec = parse_ec::failed;
            num_headers = num;
            return first;
        }

        if (!(num != 0 && space_or_tab(*first) ) )
        {
            /* parsing name, but do not discard SP before colon, see
             * http://www.mozilla.org/security/announce/2006/mfsa2006-33.html */

            
            auto token_begin = first;
            first = parse_token(token_begin, last, COLON, ec);

            if (ec != parse_ec::ok)
            {
                num_headers = num;
                return first;
            }

            headers[num].name = std::string_view(token_begin, first);
            
            

            if (headers[num].name.empty())
            {
                ec = parse_ec::failed;
                num_headers = num;
                return first;
            }

            assert(*first == COLON);
            ++first; // skip the ':'

            // find non space or tab
            first = std::find_if_not(first, last, space_or_tab);
            if (first == last)
            {
                ec = parse_ec::partial;
                num_headers = num;
                return first;
            }
        }
        else 
        {
            headers[num].name = std::string_view{};
            
        }

        auto token_begin = first;
        first = get_token_to_eol(token_begin, last, ec);
        
        ptrdiff_t ec_skip = static_cast<ptrdiff_t>(ec);
        
        if (ec_skip < 0) {
            num_headers = num;
            return first;
        }
        
        

        auto token_last = remove_last_spaces_and_tabs(token_begin, first - ec_skip);

        headers[num].value = std::string_view(token_begin, token_last);
        
        ec = parse_ec::ok;//clear status
         
    }
    
    num_headers = num;
    return first;
}



request_result parse_request(std::span<const char> buf,  std::span<phr_header> headers)
{
    request_result result{};
     
    const auto start_buf = buf.begin();

    auto first = buf.begin();
    
    const auto last = buf.end();

    /* skip first empty line (some clients add CRLF after POST content) */
    if (first == last)
        return result.unexpected(parse_ec::partial, first - start_buf);
    
    
    if (*first == CR)
    {
        ++first;
        
        if (first == last)
            return result.unexpected(parse_ec::partial, first - start_buf);
        
        if (*first++ != LF)
            return result.unexpected(parse_ec::failed, first - start_buf);
    }
    else if (*first == LF)
    {
        ++first;
    }

    /* parse request line */
    {
        parse_ec ec{};
        auto token_begin = first;
        first = parse_token(token_begin, last, SP, ec);

        if (ec != parse_ec::ok)
        {
            return result.unexpected(ec, first - start_buf);
        }

        result.method = std::string_view(token_begin, first);
    }

    ++first;
    first = std::find_if(first, last, [](char c) { return c != SP;});

    
    if (first == last)
        return result.unexpected(parse_ec::partial, first - start_buf);
    

   
    {
        parse_ec ec{};
        auto token_begin = first;
        first = advance_token(token_begin, last, ec);
        if (ec != parse_ec::ok)
            return result.unexpected(ec, first  - start_buf);

        result.path = std::string_view(token_begin, first); 
    }


    ++first;
    first = std::find_if(first, last, [](char c) { return c != SP;});


    if (first == last)
        return result.unexpected(parse_ec::partial, first - start_buf);

    if (result.method.empty() || result.path.empty())
        return result.unexpected(parse_ec::failed, first - start_buf);

    {
        parse_ec ec{};

        first = parse_http_version(first, last, ec);

        if ((ptrdiff_t)ec < 0) {
            return result.unexpected(ec, first - start_buf);
        }

        result.minor_version = static_cast<int>(ec); 
    }

    
    if (*first == CR)
    {
        ++first;
        
        if (first == last)
            return result.unexpected(parse_ec::partial, first - start_buf);
        if (*first++ != LF)
            return result.unexpected(parse_ec::failed, first - start_buf);
    }
    else if (*first == LF ) {
        ++first;
    }
    else {
        return result.unexpected(parse_ec::failed, first - start_buf);
    }

    
        
    first = parse_headers(first, last, headers, result.num_headers, result.ec);
    

    result.bsz = first - start_buf;
    
    return result;
}


response_result parse_response(const std::span<const char> buf, std::span<phr_header> headers)
{
    response_result result{};

    const auto start_buf = buf.begin();

    const auto last = buf.end();

    auto first = buf.begin();

    /* parse "HTTP/1.x" */
    parse_ec ec{};
    first = parse_http_version(first, last, ec);

    if ((ptrdiff_t)ec < 0) {
        return result.unexpected(ec, first - start_buf);
    }
    result.minor_version = static_cast<int>(ec); //http_version.minor_version;

    /* skip space */
    if (*first != SP)
        return result.unexpected(parse_ec::failed, first - start_buf);
    
    first = std::find_if(first, last, [](char c) { return c != SP;});
    if (first == last)
        return result.unexpected(parse_ec::partial, first - start_buf);

    
    /* parse status code, we want at least [:digit:][:digit:][:digit:]<other char> to try to parse */
    if (last - first < 4)
        return result.unexpected(parse_ec::partial, first - start_buf);
    
    int dig_1 = 0, dig_2 = 0, dig_3 = 0;
    if (!is_ascii_digit(*first))
        return result.unexpected(parse_ec::failed, first - start_buf);
    
    dig_1 = *first++  - 0x30;

    if (!is_ascii_digit(*first))
        return result.unexpected(parse_ec::failed, first - start_buf);
    dig_2 = *first++ - 0x30;

    if (!is_ascii_digit(*first))
        return result.unexpected(parse_ec::failed, first - start_buf);
    dig_3 = *first++ - 0x30;

     
    result.status = 100 * dig_1 + 10 * dig_2 + 1 * dig_3;
    
    
    /* get message including preceding space */
    {
        parse_ec ec{};
        auto token_begin = first;
        first = get_token_to_eol(token_begin, last, ec);
        ptrdiff_t ec_skip = static_cast<ptrdiff_t>(ec);
        if (ec_skip < 0)
            return result.unexpected(ec, first - start_buf);
        
        result.msg = std::string_view(token_begin, first - ec_skip);
    }

    if (result.msg.empty()) {
        /* ok */
    }
    else if (result.msg[0] == SP) {
        /* Remove preceding space. Successful return from `get_token_to_eol` guarantees that we would hit something other than SP
         * before running past the end of the given buffer. */
        size_t non_space_pos = result.msg.find_first_not_of(SP);
        if (non_space_pos == result.msg.npos) {
            //all are spaces
            result.msg.remove_prefix(result.msg.size()); // remove whole string.
        }
        else {
            // 0 1 2 3 ... 4 -> non_space_pos
            result.msg.remove_prefix(non_space_pos);
        }
    }
    else {
        /* garbage found after status code */
        return result.unexpected(parse_ec::failed, first - start_buf);
    }

    
    first = parse_headers(first, last, headers, result.num_headers, result.ec);
    
    result.bsz = static_cast<size_t>( first - start_buf);

    return result;
}


constexpr int decode_hex(const int ch) noexcept
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

constexpr bool allowed_after_hex(const int c) noexcept 
{
    switch (c)
    {
    case SP:
    case '\011':
    case ';':
    case LF:
    case CR:
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
                //memmove(buf.data() + dst, buf.data() + src, buf.size() - src);
                std::shift_left(buf.begin() + dst, buf.end(), src - dst);
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
            auto const cr_lf_iter = std::find_if(buf_view.cbegin() + src, buf_view.cend(), [](char c) {return c == CR || c == LF;});
            
            src = std::distance(buf_view.cbegin(), cr_lf_iter);
                

            if (src == buf_view.size()) {
                return SwitchState::do_exit;
            }
            if (buf_view[src] == LF) {
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

            if (buf[src] != LF)
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
            size_t avail = buf.size() - src;
            if (avail < decoder.bytes_left_in_chunk)
            {
                if (dst != src)
                {
                    //memmove(buf.data() + dst, buf.data() + src, avail);
                    std::shift_left(buf.begin() + dst, buf.end(), src - dst);
                }

                src += avail;
                dst += avail;

                decoder.bytes_left_in_chunk -= avail;

                return SwitchState::do_exit;
            }

            if (dst != src)
            {
                //memmove(buf.data() + dst, buf.data() + src, decoder.bytes_left_in_chunk);
                std::shift_left(buf.begin() + dst, buf.begin() + src + decoder.bytes_left_in_chunk, src - dst);
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

            if (buf[src] != CR ) {
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

            if (buf[src] != LF)
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
            const size_t pos = buf_view.find_first_not_of(CR, src);
            if (pos == buf_view.npos) {
                //all symbols are '\015'
                src = buf.size();
                return SwitchState::do_exit;
            }
            src = pos;

            if (buf[src++] == LF)
                return SwitchState::do_complete;

            decoder._state = ChunkedState::trailers_line_middle;
            return SwitchState::do_continue;
        }

        enum SwitchState trailerLineMiddle()
        {
            //  assert(src < buf.size());
            const size_t pos = buf_view.find(LF, src);
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

response_result phr_parse_response(const std::span<const char> buf_start, std::span<phr_header> headers, size_t last_len)
{
    response_result result{};
    
    const std::string_view buf(buf_start.data(), buf_start.size());

    /* if last_len != 0, check if the response is complete (a fast countermeasure against slowloris */
    if (last_len != 0 && (result.ec = is_complete( last_iter_cmlt(buf_start.begin(), last_len), buf_start.end())) != parse_ec::ok)
        return result;

    return parse_response(buf_start, headers);
}

parse_result phr_parse_headers(const std::span<const char> buf_start, std::span<phr_header> headers, size_t last_len)
{
    parse_result result{};

    /* if last_len != 0, check if the response is complete (a fast countermeasure against slowloris */
    if (last_len != 0 && (result.ec = is_complete(last_iter_cmlt(buf_start.begin(), last_len), buf_start.end())) != parse_ec::ok)
    {
       return result;
    }

    auto p_end = parse_headers(buf_start.begin(), buf_start.end(), headers, result.num_headers, result.ec);
    result.bsz = p_end - buf_start.begin(); 
    return result;
}


request_result phr_parse_request(const std::span<const char> buf_start, std::span<phr_header> headers, size_t last_len)
{
    request_result result{};

    /* if last_len != 0, check if the request is complete (a fast countermeasure againt slowloris */
    if (last_len != 0 && (result.ec = is_complete( last_iter_cmlt(buf_start.begin(), last_len), buf_start.end())) != parse_ec::ok)
    {
       return result;
    }
    return parse_request(buf_start, headers );
}

phr_decode_chunked_result phr_decode_chunked(struct phr_chunked_decoder& decoder, const std::span<char> buf)
{
    phr_chunked_decoder_msm msm(decoder, buf);
    msm.process();
    return msm.result;
}


bool phr_decode_chunked_is_in_data(const struct phr_chunked_decoder& decoder)
{
    return decoder._state == ChunkedState::chunk_data; 
}