#pragma once

#include "http.hpp"
#include "socket.hpp"

namespace ct
{
    class HttpClient
    {
    public:
        static bool get(StringView url, HttpResponse &out, NetError *error = nullptr, unsigned timeout_ms = 5000)
        {
            HttpRequest request;
            request.method = "GET";
            return from_url(url, request, out, error, timeout_ms);
        }

        static bool post(StringView url, StringView body, StringView content_type,
                         HttpResponse &out, NetError *error = nullptr, unsigned timeout_ms = 5000)
        {
            HttpRequest request;
            request.method = "POST";
            request.body = String(body);
            request.headers.push_back(HttpHeader{String("Content-Type"), String(content_type)});
            return from_url(url, request, out, error, timeout_ms);
        }

        static bool request(const HttpRequest &request, const Address &address,
                            HttpResponse &out, NetError *error = nullptr, unsigned timeout_ms = 5000)
        {
            StringView method = request.method.empty() ? StringView("GET") : StringView(request.method);
            StringView path = request.path.empty() ? StringView("/") : StringView(request.path);
            if (!detail::http_valid_token(method) || !detail::http_valid_target(path) ||
                (!request.query.empty() && !detail::http_valid_target(request.query)))
            {
                set_error(error, "invalid characters in HTTP request line", 0);
                return false;
            }
            for (std::size_t i = 0; i < request.headers.size(); ++i)
            {
                if (!detail::http_valid_token(request.headers[i].name) || !detail::http_valid_header_value(request.headers[i].value))
                {
                    set_error(error, "invalid characters in HTTP header", 0);
                    return false;
                }
            }
            TcpStream stream;
            if (!stream.connect(address, timeout_ms, error)) return false;
            String wire;
            wire.append(method.data(), method.size()).append(" ").append(path.data(), path.size());
            if (!request.query.empty()) wire.append("?").append(request.query);
            wire.append(" HTTP/1.1\r\n");
            bool has_length = false, has_connection = false;
            for (std::size_t i = 0; i < request.headers.size(); ++i)
            {
                const HttpHeader &header = request.headers[i];
                if (detail::http_iequal(header.name, "Content-Length")) has_length = true;
                if (detail::http_iequal(header.name, "Connection")) has_connection = true;
                wire.append(header.name).append(": ").append(header.value).append("\r\n");
            }
            if (!has_length && !request.body.empty()) wire.append("Content-Length: ").append_number(static_cast<unsigned long long>(request.body.size())).append("\r\n");
            if (!has_connection) wire.append("Connection: close\r\n");
            wire.append("\r\n").append(request.body);
            if (!stream.send_all(wire)) { if (error) *error = stream.last_error(); return false; }
            HttpParser parser;
            parser.expect_head_response(detail::http_iequal(method, "HEAD"));
            char buffer[8192];
            for (;;)
            {
                long count = stream.recv(buffer, sizeof(buffer));
                if (count < 0) { if (error) *error = stream.last_error(); return false; }
                if (count == 0)
                {
                    HttpParser::State state = skip_informational(parser, parser.finish(out), out, true);
                    if (state == HttpParser::Done) return true;
                    set_error(error, state == HttpParser::Error ? parser.error() : "connection closed before complete HTTP response", 0);
                    return false;
                }
                HttpParser::State state = skip_informational(parser, parser.feed(buffer, static_cast<std::size_t>(count), out), out, false);
                if (state == HttpParser::Done) return true;
                if (state == HttpParser::Error) { set_error(error, parser.error(), 0); return false; }
            }
        }

    private:
        static HttpParser::State skip_informational(HttpParser &parser, HttpParser::State state, HttpResponse &out, bool eof)
        {
            while (state == HttpParser::Done && out.status >= 100 && out.status < 200 && out.status != 101)
            {
                parser.reset();
                state = parser.feed(nullptr, 0, out);
                if (state == HttpParser::NeedMore && eof) state = parser.finish(out);
            }
            return state;
        }

        static void set_error(NetError *error, const char *message, int code)
        {
            if (error) { error->message = message; error->code = code ? code : -1; }
        }

        static bool from_url(StringView url, HttpRequest &request, HttpResponse &out,
                             NetError *error, unsigned timeout_ms)
        {
            const StringView scheme("http://");
            if (!url.starts_with(scheme)) { set_error(error, "only http:// URLs are supported", 0); return false; }
            StringView rest = url.substr(scheme.size());
            std::size_t slash = rest.find('/');
            StringView authority = slash == StringView::npos ? rest : rest.substr(0, slash);
            StringView target = slash == StringView::npos ? StringView("/") : rest.substr(slash);
            if (authority.empty()) { set_error(error, "HTTP URL has no host", 0); return false; }
            StringView host = authority;
            unsigned port = 80;
            if (authority[0] == '[')
            {
                std::size_t close = authority.find(']');
                if (close == StringView::npos) { set_error(error, "invalid IPv6 HTTP URL", 0); return false; }
                host = authority.substr(1, close - 1);
                if (close + 1 < authority.size())
                {
                    if (authority[close + 1] != ':' || !parse_port(authority.substr(close + 2), port)) { set_error(error, "invalid HTTP port", 0); return false; }
                }
            }
            else
            {
                std::size_t colon = authority.rfind(':');
                if (colon != StringView::npos)
                {
                    host = authority.substr(0, colon);
                    if (!parse_port(authority.substr(colon + 1), port)) { set_error(error, "invalid HTTP port", 0); return false; }
                }
            }
            std::size_t query = target.find('?');
            if (query == StringView::npos) request.path = String(target);
            else { request.path = String(target.substr(0, query)); request.query = String(target.substr(query + 1)); }
            request.headers.push_back(HttpHeader{String("Host"), String(authority)});
            Vector<Address> addresses;
            if (!Address::resolve(host, static_cast<std::uint16_t>(port), addresses, error)) return false;
            NetError last = {"connection failed", -1};
            for (std::size_t i = 0; i < addresses.size(); ++i)
            {
                if (HttpClient::request(request, addresses[i], out, &last, timeout_ms)) return true;
            }
            if (error) *error = last;
            return false;
        }

        static bool parse_port(StringView text, unsigned &port) noexcept
        {
            if (text.empty()) return false;
            unsigned value = 0;
            for (std::size_t i = 0; i < text.size(); ++i)
            {
                if (text[i] < '0' || text[i] > '9') return false;
                value = value * 10 + static_cast<unsigned>(text[i] - '0');
                if (value > 65535) return false;
            }
            if (!value) return false;
            port = value;
            return true;
        }
    };
}
