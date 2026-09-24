/*
 * Copyright (c) 2009-2014 Kazuho Oku, Tokuhiro Matsuno, Daisuke Murase,
 *                         Shigeo Mitsunari
 *
 * The software is licensed under either the MIT License (below) or the Perl
 * license.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <assert.h>
#include <stdio.h>
#include <chrono>

#include "picohttpparser.hpp"


static constexpr char REQ[] =
"GET /wp-content/uploads/2010/03/hello-kitty-darth-vader-pink.jpg HTTP/1.1\r\n"
"Host: www.kittyhell.com\r\n"
"User-Agent: Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.6; ja-JP-mac; rv:1.9.2.3) Gecko/20100401 Firefox/3.6.3 "
"Pathtraq/0.9\r\n"
"Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
"Accept-Language: ja,en-us;q=0.7,en;q=0.3\r\n"
"Accept-Encoding: gzip,deflate\r\n"
"Accept-Charset: Shift_JIS,utf-8;q=0.7,*;q=0.7\r\n"
"Keep-Alive: 115\r\n"
"Connection: keep-alive\r\n"
"Cookie: wp_ozh_wsa_visits=2; wp_ozh_wsa_visit_lasttime=xxxxxxxxxx; "
"__utma=xxxxxxxxx.xxxxxxxxxx.xxxxxxxxxx.xxxxxxxxxx.xxxxxxxxxx.x; "
"__utmz=xxxxxxxxx.xxxxxxxxxx.x.x.utmccn=(referral)|utmcsr=reader.livedoor.com|utmcct=/reader/|utmcmd=referral\r\n"
"\r\n"
;

int main(void)
{
    struct phr_header headers[32];
    
    request_result ret;
    
    auto start_time = std::chrono::high_resolution_clock::now();

    constexpr int LOOP = 10'000'000;

    for (int i = 0; i < LOOP; i++) 
    {
      
        std::span<const char> req(REQ, sizeof(REQ) - 1);
        
        std::span<phr_header> hsp(headers, std::size(headers));

        ret = phr_parse_request(req,  headers,  0);
        assert(ret.ec == parse_ec::ok);
        assert(ret.bsz == sizeof(REQ) - 1);
    }

    auto end_time = std::chrono::high_resolution_clock::now();

    double elapsed_time = std::chrono::duration<double>(end_time - start_time).count();
    printf("LOOP: %d  elapsed: %.7f seconds\n", LOOP, elapsed_time);

    return 0;
}