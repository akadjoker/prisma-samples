#include <ct/http.hpp>
#include <ct/http_client.hpp>
#include <ct/http_server.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif
#if defined(__linux__)
#include <dirent.h>
#include <sys/resource.h>
#endif

TEST(HttpParser, IncrementalRequest)
{
    const char first[] = "POST /users?id=7 HTTP/1.1\r\nHost: example\r\nContent-Length: 5\r\n\r\nhe";
    ct::HttpParser parser; ct::HttpRequest request;
    EXPECT_EQ(parser.feed(first, sizeof(first) - 1, request), ct::HttpParser::NeedMore);
    EXPECT_EQ(parser.feed("llo", 3, request), ct::HttpParser::Done);
    EXPECT_EQ(request.method, "POST"); EXPECT_EQ(request.path, "/users"); EXPECT_EQ(request.query, "id=7");
    EXPECT_EQ(request.header("host"), "example"); EXPECT_EQ(request.body, "hello");
}

TEST(HttpParser, ChunkedResponse)
{
    const char message[] = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n";
    ct::HttpParser parser; ct::HttpResponse response;
    EXPECT_EQ(parser.feed(message, sizeof(message) - 1, response), ct::HttpParser::Done);
    EXPECT_EQ(response.status, 200); EXPECT_EQ(response.reason, "OK"); EXPECT_EQ(response.body, "Wikipedia");
}

TEST(HttpParser, ResponseBodyDelimitedByClose)
{
    const char message[] = "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\nhello";
    ct::HttpParser parser; ct::HttpResponse response;
    EXPECT_EQ(parser.feed(message, sizeof(message) - 1, response), ct::HttpParser::NeedMore);
    EXPECT_EQ(parser.finish(response), ct::HttpParser::Done);
    EXPECT_EQ(response.body, "hello");
}

TEST(HttpParser, RejectsMalformedAndOversized)
{
    const char malformed[] = "GET / HTTP/1.1\r\nBroken\r\n\r\n";
    ct::HttpRequest request; ct::HttpParser parser;
    EXPECT_EQ(parser.feed(malformed, sizeof(malformed) - 1, request), ct::HttpParser::Error);
    const char oversized[] = "POST / HTTP/1.1\r\nContent-Length: 4\r\n\r\ntest";
    ct::HttpParser small(3);
    EXPECT_EQ(small.feed(oversized, sizeof(oversized) - 1, request), ct::HttpParser::Error);
}

TEST(HttpParser, PreservesPipelinedBytesOnReset)
{
    const char messages[] = "GET /one HTTP/1.1\r\n\r\nGET /two HTTP/1.1\r\n\r\n";
    ct::HttpParser parser; ct::HttpRequest request;
    ASSERT_EQ(parser.feed(messages, sizeof(messages) - 1, request), ct::HttpParser::Done); EXPECT_EQ(request.path, "/one");
    parser.reset(); ASSERT_EQ(parser.feed(nullptr, 0, request), ct::HttpParser::Done); EXPECT_EQ(request.path, "/two");
}

TEST(HttpClient, GetsFromLoopbackServer)
{
    ct::Address address; ASSERT_TRUE(ct::Address::parse("127.0.0.1", 0, address));
    ct::TcpListener listener; if (!listener.bind(address)) GTEST_SKIP() << "HTTP test port unavailable"; ASSERT_TRUE(listener.listen());
    address = listener.local_address();
    std::thread server([&] {
        ct::TcpStream peer; ASSERT_TRUE(listener.accept(peer));
        char request[1024]; ASSERT_GT(peer.recv(request, sizeof(request)), 0);
        ASSERT_TRUE(peer.send_all("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Type: text/plain\r\n\r\nhello"));
    });
    ct::HttpResponse response; ct::NetError error = {"", 0};
    ct::String url("http://127.0.0.1:"); url.append_number(address.port()).append("/test?q=1");
    EXPECT_TRUE(ct::HttpClient::get(url, response, &error));
    EXPECT_EQ(response.status, 200); EXPECT_EQ(response.body, "hello"); EXPECT_EQ(response.header("content-type"), "text/plain");
    server.join();
}

TEST(HttpServer, RoutesParamsAndKeepAlive)
{
    ct::Address address; ASSERT_TRUE(ct::Address::parse("127.0.0.1", 0, address));
    ct::HttpServer server;
    server.route("GET", "/users/:id", [](const ct::HttpRequest &request, ct::HttpResponse &response) {
        response.text(request.param("id"));
    });
    ct::NetError error = {"", 0};
    if (!server.listen(address, &error)) GTEST_SKIP() << "HTTP server test port unavailable";
    address = server.local_address();
    std::thread loop([&] { server.run(); });
    ct::HttpResponse response;
    ct::String base("http://127.0.0.1:"); base.append_number(address.port());
    EXPECT_TRUE(ct::HttpClient::get(base + "/users/42", response, &error));
    EXPECT_EQ(response.status, 200); EXPECT_EQ(response.body, "42");
    EXPECT_TRUE(ct::HttpClient::get(base + "/missing", response, &error));
    EXPECT_EQ(response.status, 404);
    server.stop(); loop.join();
}

namespace
{
    ct::String raw_exchange(const ct::Address &address, ct::StringView request)
    {
        ct::TcpStream stream;
        if (!stream.connect(address, 2000)) return ct::String("connect failed");
        stream.set_timeout_ms(2000, 2000);
        if (!stream.send_all(request)) return ct::String("send failed");
        ct::String reply;
        char buffer[4096];
        for (;;)
        {
            long n = stream.recv(buffer, sizeof(buffer));
            if (n <= 0) break;
            reply.append(buffer, static_cast<std::size_t>(n));
        }
        return reply;
    }
}

TEST(HttpParser, RejectsHeaderSmugglingAndInjection)
{
    ct::HttpRequest request;
    const char space_before_colon[] = "POST / HTTP/1.1\r\nContent-Length : 27\r\n\r\n";
    ct::HttpParser a; EXPECT_EQ(a.feed(space_before_colon, sizeof(space_before_colon) - 1, request), ct::HttpParser::Error);
    const char bare_lf[] = "GET / HTTP/1.1\r\nX-In: v\nSet-Cookie: pwned=1\r\n\r\n";
    ct::HttpParser b; EXPECT_EQ(b.feed(bare_lf, sizeof(bare_lf) - 1, request), ct::HttpParser::Error);
    const char both[] = "POST / HTTP/1.1\r\nContent-Length: 5\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n";
    ct::HttpParser c; EXPECT_EQ(c.feed(both, sizeof(both) - 1, request), ct::HttpParser::Error);
    const char gzip[] = "POST / HTTP/1.1\r\nTransfer-Encoding: gzip\r\n\r\n";
    ct::HttpParser d; EXPECT_EQ(d.feed(gzip, sizeof(gzip) - 1, request), ct::HttpParser::Error);
    const char chunked_first[] = "POST / HTTP/1.1\r\nTransfer-Encoding: chunked, gzip\r\n\r\n";
    ct::HttpParser e; EXPECT_EQ(e.feed(chunked_first, sizeof(chunked_first) - 1, request), ct::HttpParser::Error);
    const char chunked_last[] = "POST / HTTP/1.1\r\nTransfer-Encoding: gzip, chunked\r\n\r\n2\r\nhi\r\n0\r\n\r\n";
    ct::HttpParser f; EXPECT_EQ(f.feed(chunked_last, sizeof(chunked_last) - 1, request), ct::HttpParser::Done);
    EXPECT_EQ(request.body, "hi");
    const char tab_in_target[] = "GET /a\tb HTTP/1.1\r\n\r\n";
    ct::HttpParser g; EXPECT_EQ(g.feed(tab_in_target, sizeof(tab_in_target) - 1, request), ct::HttpParser::Error);
}

TEST(HttpParser, FeedAfterDoneIsAnError)
{
    const char one[] = "GET /one HTTP/1.1\r\n\r\n";
    ct::HttpParser parser; ct::HttpRequest request;
    ASSERT_EQ(parser.feed(one, sizeof(one) - 1, request), ct::HttpParser::Done);
    EXPECT_EQ(parser.feed("GET /two HTTP/1.1\r\n\r\n", 21, request), ct::HttpParser::Error);
    EXPECT_NE(parser.error(), nullptr);
}

TEST(HttpParser, DecodesPercentEncodedPathButKeepsQueryAndEncodedSlash)
{
    ct::HttpRequest request;
    const char ok[] = "GET /a%20b/%2e%2e/c%2Fd?x=%20y HTTP/1.1\r\n\r\n";
    ct::HttpParser a; ASSERT_EQ(a.feed(ok, sizeof(ok) - 1, request), ct::HttpParser::Done);
    EXPECT_EQ(request.path, "/a b/../c%2Fd");
    EXPECT_EQ(request.query, "x=%20y");
    const char lower[] = "GET /a%2fb%252Fc HTTP/1.1\r\n\r\n";
    ct::HttpParser lo; ASSERT_EQ(lo.feed(lower, sizeof(lower) - 1, request), ct::HttpParser::Done);
    EXPECT_EQ(request.path, "/a%2Fb%25" "2Fc");
    const char ctl[] = "GET /a%0d%0aSet-Cookie:%20x%1b HTTP/1.1\r\n\r\n";
    ct::HttpParser cc; EXPECT_EQ(cc.feed(ctl, sizeof(ctl) - 1, request), ct::HttpParser::Error);
    const char del[] = "GET /a%7f HTTP/1.1\r\n\r\n";
    ct::HttpParser dd; EXPECT_EQ(dd.feed(del, sizeof(del) - 1, request), ct::HttpParser::Error);
    const char bad_hex[] = "GET /a%zz HTTP/1.1\r\n\r\n";
    ct::HttpParser b; EXPECT_EQ(b.feed(bad_hex, sizeof(bad_hex) - 1, request), ct::HttpParser::Error);
    const char nul[] = "GET /a%00b HTTP/1.1\r\n\r\n";
    ct::HttpParser c; EXPECT_EQ(c.feed(nul, sizeof(nul) - 1, request), ct::HttpParser::Error);
    const char truncated[] = "GET /a%2 HTTP/1.1\r\n\r\n";
    ct::HttpParser d; EXPECT_EQ(d.feed(truncated, sizeof(truncated) - 1, request), ct::HttpParser::Error);
}

TEST(HttpParser, HeadResponseHasNoBodyAndInformationalIsComplete)
{
    const char head[] = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n";
    ct::HttpParser parser; ct::HttpResponse response;
    parser.expect_head_response(true);
    EXPECT_EQ(parser.feed(head, sizeof(head) - 1, response), ct::HttpParser::Done);
    EXPECT_TRUE(response.body.empty());
    EXPECT_EQ(parser.consumed(), sizeof(head) - 1);
    const char cont[] = "HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
    ct::HttpParser second; ct::HttpResponse first_response;
    EXPECT_EQ(second.feed(cont, sizeof(cont) - 1, first_response), ct::HttpParser::Done);
    EXPECT_EQ(first_response.status, 100);
    second.reset();
    EXPECT_EQ(second.feed(nullptr, 0, first_response), ct::HttpParser::Done);
    EXPECT_EQ(first_response.status, 200);
    EXPECT_EQ(first_response.body, "ok");
}

TEST(HttpClient, RejectsControlCharactersBeforeConnecting)
{
    ct::Address address; ASSERT_TRUE(ct::Address::parse("127.0.0.1", 9, address));
    ct::HttpResponse response; ct::NetError error = {"", 0};
    ct::HttpRequest crlf; crlf.method = "GET"; crlf.path = "/a\r\nX-Injected: yes";
    EXPECT_FALSE(ct::HttpClient::request(crlf, address, response, &error, 200));
    EXPECT_STREQ(error.message, "invalid characters in HTTP request line");
    ct::HttpRequest header; header.method = "GET"; header.path = "/";
    header.headers.push_back(ct::HttpHeader{ct::String("X-A"), ct::String("v\nSet-Cookie: x")});
    EXPECT_FALSE(ct::HttpClient::request(header, address, response, &error, 200));
    EXPECT_STREQ(error.message, "invalid characters in HTTP header");
    EXPECT_FALSE(ct::HttpClient::get("http://127.0.0.1:9/a\r\nHost: evil", response, &error, 200));
}

TEST(HttpClient, SkipsInformationalResponsesAndHandlesHead)
{
    ct::Address address; ASSERT_TRUE(ct::Address::parse("127.0.0.1", 0, address));
    ct::TcpListener listener; if (!listener.bind(address)) GTEST_SKIP() << "HTTP test port unavailable"; ASSERT_TRUE(listener.listen());
    address = listener.local_address();
    std::thread server([&] {
        for (int round = 0; round < 2; ++round)
        {
            ct::TcpStream peer; ASSERT_TRUE(listener.accept(peer));
            char request[1024]; long n = peer.recv(request, sizeof(request) - 1); ASSERT_GT(n, 0); request[n] = '\0';
            if (std::strncmp(request, "HEAD", 4) == 0)
                ASSERT_TRUE(peer.send_all("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Type: text/plain\r\n\r\n"));
            else
                ASSERT_TRUE(peer.send_all("HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello"));
        }
    });
    ct::HttpResponse response; ct::NetError error = {"", 0};
    ct::HttpRequest head; head.method = "HEAD"; head.path = "/";
    EXPECT_TRUE(ct::HttpClient::request(head, address, response, &error));
    EXPECT_EQ(response.status, 200); EXPECT_TRUE(response.body.empty()); EXPECT_EQ(response.header("content-length"), "5");
    ct::HttpRequest get; get.method = "GET"; get.path = "/";
    EXPECT_TRUE(ct::HttpClient::request(get, address, response, &error));
    EXPECT_EQ(response.status, 200); EXPECT_EQ(response.body, "hello");
    server.join();
}

TEST(HttpServer, HeadUsesGetRoutesWithoutBodyAndBadInputGets400)
{
    ct::Address address; ASSERT_TRUE(ct::Address::parse("127.0.0.1", 0, address));
    ct::HttpServer server;
    server.route("GET", "/users/:id", [](const ct::HttpRequest &request, ct::HttpResponse &response) { response.text(request.param("id")); });
    server.route("GET", "/inject", [](const ct::HttpRequest &, ct::HttpResponse &response) { response.set("X-Bad", "a\r\nSet-Cookie: pwned=1"); response.text("x"); });
    server.route("GET", "/empty", [](const ct::HttpRequest &, ct::HttpResponse &response) { response.status = 204; });
    ct::NetError error = {"", 0};
    if (!server.listen(address, &error)) GTEST_SKIP() << "HTTP server test port unavailable";
    address = server.local_address();
    std::thread loop([&] { server.run(); });
    ct::String head = raw_exchange(address, "HEAD /users/42 HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    EXPECT_TRUE(head.starts_with("HTTP/1.1 200 ")) << head.c_str();
    EXPECT_TRUE(head.contains("Content-Length: 2\r\n")) << head.c_str();
    EXPECT_TRUE(head.ends_with("\r\n\r\n")) << head.c_str();
    ct::String missing = raw_exchange(address, "HEAD /missing HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    EXPECT_TRUE(missing.starts_with("HTTP/1.1 404 ")) << missing.c_str();
    EXPECT_TRUE(missing.ends_with("\r\n\r\n")) << missing.c_str();
    ct::String bad = raw_exchange(address, "GET / HTTP/1.1\r\nBroken header\r\n\r\n");
    EXPECT_TRUE(bad.starts_with("HTTP/1.1 400 ")) << bad.c_str();
    ct::String smuggle = raw_exchange(address, "POST /users/1 HTTP/1.1\r\nHost: x\r\nContent-Length : 27\r\n\r\nGET /users/2 HTTP/1.1\r\n\r\n");
    EXPECT_TRUE(smuggle.starts_with("HTTP/1.1 400 ")) << smuggle.c_str();
    EXPECT_EQ(smuggle.find("HTTP/1.1 200"), ct::String::npos) << smuggle.c_str();
    ct::String inject = raw_exchange(address, "GET /inject HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    EXPECT_TRUE(inject.starts_with("HTTP/1.1 500 ")) << inject.c_str();
    EXPECT_FALSE(inject.contains("Set-Cookie")) << inject.c_str();
    ct::String empty = raw_exchange(address, "GET /empty HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    EXPECT_TRUE(empty.starts_with("HTTP/1.1 204 ")) << empty.c_str();
    EXPECT_FALSE(empty.contains("Content-Length")) << empty.c_str();
    EXPECT_TRUE(empty.ends_with("\r\n\r\n")) << empty.c_str();
    ct::String pipelined = raw_exchange(address, "HEAD /users/7 HTTP/1.1\r\nHost: x\r\n\r\nGET /users/8 HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    EXPECT_NE(pipelined.find("HTTP/1.1 200 OK\r\n"), ct::String::npos) << pipelined.c_str();
    EXPECT_TRUE(pipelined.ends_with("\r\n\r\n8")) << pipelined.c_str();
    server.stop(); loop.join();
}

#if !defined(_WIN32)
TEST(HttpServer, StaticFilesUseDecodedPathsAndStayInsideTheDirectory)
{
    const char *dir = "/tmp/ct_http_static_test";
    ::mkdir(dir, 0755);
    ASSERT_TRUE(ct::File::write_all("/tmp/ct_http_static_test/my file.txt", "conteudo", 8));
    ASSERT_TRUE(ct::File::write_all("/tmp/ct_http_secret.txt", "segredo", 7));
    ct::Address address; ASSERT_TRUE(ct::Address::parse("127.0.0.1", 0, address));
    ct::HttpServer server;
    server.serve_files("/static", dir);
    ct::NetError error = {"", 0};
    if (!server.listen(address, &error)) GTEST_SKIP() << "HTTP server test port unavailable";
    address = server.local_address();
    std::thread loop([&] { server.run(); });
    ct::String ok = raw_exchange(address, "GET /static/my%20file.txt HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    EXPECT_TRUE(ok.starts_with("HTTP/1.1 200 ")) << ok.c_str();
    EXPECT_TRUE(ok.ends_with("conteudo")) << ok.c_str();
    ct::String escape = raw_exchange(address, "GET /static/%2e%2e/ct_http_secret.txt HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    EXPECT_TRUE(escape.starts_with("HTTP/1.1 404 ")) << escape.c_str();
    ct::String head = raw_exchange(address, "HEAD /static/my%20file.txt HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    EXPECT_TRUE(head.starts_with("HTTP/1.1 200 ")) << head.c_str();
    EXPECT_TRUE(head.contains("Content-Length: 8\r\n")) << head.c_str();
    EXPECT_TRUE(head.ends_with("\r\n\r\n")) << head.c_str();
    server.stop(); loop.join();
    ct::File::remove("/tmp/ct_http_static_test/my file.txt");
    ct::File::remove("/tmp/ct_http_secret.txt");
    ::rmdir(dir);
}
#endif

#if defined(__linux__)
TEST(HttpServer, AcceptFailureDoesNotSpinThePollLoop)
{
    ct::Address address; ASSERT_TRUE(ct::Address::parse("127.0.0.1", 0, address));
    ct::HttpServer server;
    ct::NetError error = {"", 0};
    if (!server.listen(address, &error)) GTEST_SKIP() << "HTTP server test port unavailable";
    address = server.local_address();
    ct::TcpStream client;
    ASSERT_TRUE(client.connect(address, 1000));
    rlimit saved; ASSERT_EQ(getrlimit(RLIMIT_NOFILE, &saved), 0);
    unsigned open_fds = 0;
    if (DIR *d = opendir("/proc/self/fd")) { while (readdir(d)) ++open_fds; closedir(d); }
    ASSERT_GT(open_fds, 3u);
    rlimit low = saved; low.rlim_cur = open_fds - 3;
    ASSERT_EQ(setrlimit(RLIMIT_NOFILE, &low), 0);
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; ++i) server.poll(10);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    ASSERT_EQ(setrlimit(RLIMIT_NOFILE, &saved), 0);
    EXPECT_GE(ms, 60.0) << "20 polls de 10 ms terminaram em " << ms << " ms: accept a falhar em busy-loop";
    server.stop();
}
#endif

TEST(HttpParser, ChunkedBodyFedByteByByteIsLinear)
{
    const std::size_t total = 256 * 1024;
    ct::String wire("POST /up HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n");
    ct::String expected;
    for (std::size_t sent = 0; sent < total;)
    {
        const std::size_t part = total - sent < 1000 ? total - sent : 1000;
        char size_line[16]; std::snprintf(size_line, sizeof(size_line), "%zx\r\n", part);
        wire.append(size_line);
        for (std::size_t i = 0; i < part; ++i) { const char c = static_cast<char>('a' + (sent + i) % 26); wire.push_back(c); expected.push_back(c); }
        wire.append("\r\n");
        sent += part;
    }
    wire.append("0\r\n\r\n");
    ct::HttpParser parser; ct::HttpRequest request;
    const clock_t start = std::clock();
    ct::HttpParser::State state = ct::HttpParser::NeedMore;
    for (std::size_t i = 0; i < wire.size() && state == ct::HttpParser::NeedMore; ++i)
        state = parser.feed(wire.data() + i, 1, request);
    const double seconds = double(std::clock() - start) / CLOCKS_PER_SEC;
    ASSERT_EQ(state, ct::HttpParser::Done);
    EXPECT_EQ(request.body.size(), total);
    EXPECT_TRUE(request.body == expected);
    EXPECT_LT(parser.buffered(), 4096u);
    EXPECT_LT(seconds, 5.0) << "256 KiB chunked a 1 byte por feed demorou " << seconds << " s";
}

TEST(HttpParser, ChunkedBodyAtTheBodyLimitIsAccepted)
{
    const std::size_t limit = 64 * 1024;
    ct::String wire("POST /up HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n");
    for (std::size_t sent = 0; sent < limit; sent += 1024)
    {
        wire.append("400\r\n");
        wire.append(1024, 'z');
        wire.append("\r\n");
    }
    wire.append("0\r\n\r\nGET /next HTTP/1.1\r\n\r\n");
    ct::HttpParser parser(limit, 1024); ct::HttpRequest request;
    ct::HttpParser::State state = ct::HttpParser::NeedMore;
    for (std::size_t at = 0; at < wire.size() && state == ct::HttpParser::NeedMore; at += 8192)
    {
        const std::size_t n = wire.size() - at < 8192 ? wire.size() - at : 8192;
        state = parser.feed(wire.data() + at, n, request);
    }
    ASSERT_EQ(state, ct::HttpParser::Done);
    EXPECT_EQ(request.body.size(), limit);
    parser.reset();
    ASSERT_EQ(parser.feed(nullptr, 0, request), ct::HttpParser::Done);
    EXPECT_EQ(request.path, "/next");
    ct::String over("POST /up HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n400\r\n");
    over.append(1024, 'z'); over.append("\r\n1\r\nq\r\n0\r\n\r\n");
    ct::HttpParser small(1024, 1024);
    EXPECT_EQ(small.feed(over.data(), over.size(), request), ct::HttpParser::Error);
    ct::String long_line("POST /up HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n");
    long_line.append(2048, '1');
    ct::HttpParser lines;
    EXPECT_EQ(lines.feed(long_line.data(), long_line.size(), request), ct::HttpParser::Error);
}

TEST(HttpParser, HeadersFedByteByByteStayCheap)
{
    ct::String wire("GET / HTTP/1.1\r\n");
    for (int i = 0; i < 100; ++i) { wire.append("X-H"); wire.append_number(i); wire.append(": "); wire.append(100, 'v'); wire.append("\r\n"); }
    wire.append("\r\n");
    ct::HttpParser parser; ct::HttpRequest request;
    ct::HttpParser::State state = ct::HttpParser::NeedMore;
    for (std::size_t i = 0; i < wire.size() && state == ct::HttpParser::NeedMore; ++i)
        state = parser.feed(wire.data() + i, 1, request);
    ASSERT_EQ(state, ct::HttpParser::Done);
    EXPECT_EQ(request.headers.size(), 100u);
    EXPECT_EQ(request.header("x-h99").size(), 100u);
}

TEST(HttpServer, IdleConnectionsAreClosedAndConnectionCountIsCapped)
{
    ct::Address address; ASSERT_TRUE(ct::Address::parse("127.0.0.1", 0, address));
    ct::HttpServer server;
    server.set_idle_timeout_ms(150);
    server.set_max_connections(1);
    server.route("GET", "/ping", [](const ct::HttpRequest &, ct::HttpResponse &response) { response.text("pong"); });
    ct::NetError error = {"", 0};
    if (!server.listen(address, &error)) GTEST_SKIP() << "HTTP server test port unavailable";
    address = server.local_address();
    std::thread loop([&] { server.run(); });
    ct::TcpStream first; ASSERT_TRUE(first.connect(address, 1000)); first.set_timeout_ms(2000, 2000);
    ct::Thread::sleep_ms(50);
    ct::TcpStream second; ASSERT_TRUE(second.connect(address, 1000)); second.set_timeout_ms(2000, 2000);
    char buffer[256];
    EXPECT_EQ(second.recv(buffer, sizeof(buffer)), 0) << "a segunda ligacao devia ser fechada pelo limite";
    ASSERT_TRUE(first.send_all("GET /ping HTTP/1.1\r\nHost: x\r\n\r\n"));
    long n = first.recv(buffer, sizeof(buffer));
    ASSERT_GT(n, 0);
    EXPECT_TRUE(ct::String(buffer, static_cast<std::size_t>(n)).ends_with("pong"));
    EXPECT_EQ(first.recv(buffer, sizeof(buffer)), 0) << "a ligacao inactiva devia ser fechada pelo idle timeout";
    server.stop(); loop.join();
}

TEST(HttpParser, MuitosChunksPequenosNumSoFeedNaoSaoQuadraticos)
{
    std::string message = "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n";
    const int chunks = 200000;
    for (int i = 0; i < chunks; ++i)
        message += "1\r\nx\r\n";
    message += "0\r\n\r\n";
    ct::HttpParser parser(8 * 1024 * 1024, 16 * 1024);
    ct::HttpRequest request;
    const std::clock_t begin = std::clock();
    ASSERT_EQ(parser.feed(message.data(), message.size(), request), ct::HttpParser::Done);
    const double seconds = double(std::clock() - begin) / CLOCKS_PER_SEC;
    EXPECT_EQ(request.body.size(), static_cast<std::size_t>(chunks));
    EXPECT_LT(seconds, 5.0);
}

TEST(HttpParser, ResetNoMeioDeUmaMensagemMantemOsBytesRecebidos)
{
    ct::HttpParser parser;
    ct::HttpRequest request;
    const std::string first = "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n";
    EXPECT_EQ(parser.feed(first.data(), first.size(), request), ct::HttpParser::NeedMore);
    parser.reset();
    EXPECT_EQ(parser.buffered(), first.size());
    const std::string rest = "0\r\n\r\n";
    ASSERT_EQ(parser.feed(rest.data(), rest.size(), request), ct::HttpParser::Done);
    EXPECT_EQ(request.body, "abc");
}

TEST(HttpParser, ResetDepoisDeErroDescartaOQueEstavaNoBuffer)
{
    ct::HttpParser parser;
    ct::HttpRequest request;
    const std::string bad = "GET / HTTP/1.1\r\nContent-Length: x\r\n\r\nrest";
    EXPECT_EQ(parser.feed(bad.data(), bad.size(), request), ct::HttpParser::Error);
    parser.reset();
    EXPECT_EQ(parser.buffered(), 0u);
    const std::string good = "GET /ok HTTP/1.1\r\n\r\n";
    ASSERT_EQ(parser.feed(good.data(), good.size(), request), ct::HttpParser::Done);
    EXPECT_EQ(request.path, "/ok");
}

TEST(HttpParser, RespostasToleramTransferEncodingNaoChunkedEContentLengthComChunked)
{
    {
        ct::HttpParser parser;
        ct::HttpResponse response;
        const std::string message = "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\nGZDATA";
        ASSERT_EQ(parser.feed(message.data(), message.size(), response), ct::HttpParser::NeedMore);
        ASSERT_EQ(parser.finish(response), ct::HttpParser::Done);
        EXPECT_EQ(response.body, "GZDATA");
    }
    {
        ct::HttpParser parser;
        ct::HttpResponse response;
        const std::string message = "HTTP/1.1 200 OK\r\nTransfer-Encoding: identity\r\n\r\nplain";
        ASSERT_EQ(parser.feed(message.data(), message.size(), response), ct::HttpParser::NeedMore);
        ASSERT_EQ(parser.finish(response), ct::HttpParser::Done);
        EXPECT_EQ(response.body, "plain");
    }
    {
        ct::HttpParser parser;
        ct::HttpResponse response;
        const std::string message = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n";
        ASSERT_EQ(parser.feed(message.data(), message.size(), response), ct::HttpParser::Done);
        EXPECT_EQ(response.body, "hello");
    }
}

TEST(HttpParser, PedidosContinuamEstritosComTransferEncoding)
{
    ct::HttpRequest request;
    const std::string both = "POST / HTTP/1.1\r\nContent-Length: 5\r\nTransfer-Encoding: chunked\r\n\r\n";
    ct::HttpParser a;
    EXPECT_EQ(a.feed(both.data(), both.size(), request), ct::HttpParser::Error);
    const std::string gzip = "POST / HTTP/1.1\r\nTransfer-Encoding: gzip\r\n\r\n";
    ct::HttpParser b;
    EXPECT_EQ(b.feed(gzip.data(), gzip.size(), request), ct::HttpParser::Error);
}

TEST(HttpParser, MensagensEncadeadasPartidasEmQualquerFronteira)
{
    const std::string message =
        "POST /a%20b HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n3;ext=1\r\nabc\r\n0\r\n\r\n"
        "GET /two HTTP/1.1\r\nContent-Length: 3\r\n\r\nxyz"
        "GET /three HTTP/1.1\r\n\r\n";
    for (std::size_t step = 1; step <= message.size(); ++step)
    {
        ct::HttpParser parser;
        std::vector<std::string> seen;
        ct::HttpRequest request;
        for (std::size_t i = 0; i < message.size(); i += step)
        {
            const std::size_t n = (std::min)(step, message.size() - i);
            ct::HttpParser::State state = parser.feed(message.data() + i, n, request);
            while (state == ct::HttpParser::Done)
            {
                seen.push_back(std::string(request.path.c_str()) + "|" + std::string(request.body.data(), request.body.size()));
                parser.reset();
                request = ct::HttpRequest();
                state = parser.feed(nullptr, 0, request);
            }
            ASSERT_NE(state, ct::HttpParser::Error) << "passo " << step << ": " << parser.error();
        }
        ASSERT_EQ(seen.size(), 3u) << "passo " << step;
        EXPECT_EQ(seen[0], "/a b|helloabc");
        EXPECT_EQ(seen[1], "/two|xyz");
        EXPECT_EQ(seen[2], "/three|");
    }
}

TEST(HttpParser, ResetEmQualquerPontoDeUmCorpoChunkedNaoPerdeBytes)
{
    const std::string message =
        "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nabcd\r\n2\r\nef\r\n0\r\n\r\n";
    for (std::size_t cut = 1; cut < message.size(); ++cut)
    {
        ct::HttpParser parser;
        ct::HttpRequest request;
        ct::HttpParser::State state = parser.feed(message.data(), cut, request);
        if (state == ct::HttpParser::Error)
            FAIL() << "erro antes do reset em " << cut;
        if (state == ct::HttpParser::NeedMore)
        {
            parser.reset();
            state = parser.feed(message.data() + cut, message.size() - cut, request);
        }
        ASSERT_EQ(state, ct::HttpParser::Done) << "corte " << cut;
        EXPECT_EQ(request.body, "abcdef") << "corte " << cut;
    }
}
