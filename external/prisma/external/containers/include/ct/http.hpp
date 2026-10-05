#pragma once

#include "span.hpp"
#include "string.hpp"
#include "vector.hpp"

namespace ct
{
    namespace detail
    {
        inline char http_lower(char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; }
        inline bool http_iequal(StringView a, StringView b) noexcept
        {
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0; i < a.size(); ++i) if (http_lower(a[i]) != http_lower(b[i])) return false;
            return true;
        }
        inline StringView http_trim(StringView s) noexcept
        {
            std::size_t a = 0, b = s.size();
            while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
            while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
            return StringView(s.data() + a, b - a);
        }
        inline bool http_is_tchar(char c) noexcept
        {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return true;
            switch (c)
            {
            case '!': case '#': case '$': case '%': case '&': case '\'': case '*': case '+': case '-': case '.':
            case '^': case '_': case '`': case '|': case '~':
                return true;
            default:
                return false;
            }
        }
        inline bool http_valid_token(StringView s) noexcept
        {
            if (s.empty()) return false;
            for (std::size_t i = 0; i < s.size(); ++i) if (!http_is_tchar(s[i])) return false;
            return true;
        }
        inline bool http_valid_header_value(StringView s) noexcept
        {
            for (std::size_t i = 0; i < s.size(); ++i) if (s[i] == '\r' || s[i] == '\n' || s[i] == '\0') return false;
            return true;
        }
        inline bool http_valid_target(StringView s) noexcept
        {
            if (s.empty()) return false;
            for (std::size_t i = 0; i < s.size(); ++i)
            {
                const unsigned char c = static_cast<unsigned char>(s[i]);
                if (c <= 0x20 || c == 0x7f) return false;
            }
            return true;
        }
        inline int http_hex_digit(char c) noexcept
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }
        inline bool http_percent_decode_path(StringView in, String &out)
        {
            out.clear();
            out.reserve(in.size());
            for (std::size_t i = 0; i < in.size(); ++i)
            {
                if (in[i] != '%') { out.push_back(in[i]); continue; }
                if (i + 2 >= in.size()) return false;
                const int hi = http_hex_digit(in[i + 1]), lo = http_hex_digit(in[i + 2]);
                if (hi < 0 || lo < 0) return false;
                const char decoded = static_cast<char>(hi * 16 + lo);
                if (static_cast<unsigned char>(decoded) < 0x20 || decoded == 0x7f) return false;
                if (decoded == '/') { out.append("%2F", 3); i += 2; continue; }
                if (decoded == '%') { out.append("%25", 3); i += 2; continue; }
                out.push_back(decoded);
                i += 2;
            }
            return true;
        }
        inline bool http_last_token_is(StringView value, StringView token) noexcept
        {
            std::size_t b = value.size();
            while (b > 0 && (value[b - 1] == ' ' || value[b - 1] == '\t')) --b;
            std::size_t a = b;
            while (a > 0 && value[a - 1] != ',') --a;
            return http_iequal(http_trim(StringView(value.data() + a, b - a)), token);
        }
        inline bool http_has_token(StringView value, StringView token) noexcept
        {
            for (std::size_t a = 0; a <= value.size();)
            {
                std::size_t b = a; while (b < value.size() && value[b] != ',') ++b;
                if (http_iequal(http_trim(StringView(value.data() + a, b - a)), token)) return true;
                if (b == value.size()) break;
                a = b + 1;
            }
            return false;
        }
    }

    struct HttpHeader { String name; String value; };
    struct HttpParam { String name; String value; };

    struct HttpRequest
    {
        String method, path, query, version;
        Vector<HttpHeader> headers;
        Vector<HttpParam> params;
        String body;
        StringView header(StringView name) const noexcept
        {
            for (std::size_t i = 0; i < headers.size(); ++i) if (detail::http_iequal(headers[i].name, name)) return headers[i].value;
            return StringView();
        }
        StringView param(StringView name) const noexcept
        {
            for (std::size_t i = 0; i < params.size(); ++i) if (StringView(params[i].name) == name) return params[i].value;
            return StringView();
        }
    };

    struct HttpResponse
    {
        int status = 200;
        String version = "HTTP/1.1", reason;
        Vector<HttpHeader> headers;
        String body;
        StringView header(StringView name) const noexcept
        {
            for (std::size_t i = 0; i < headers.size(); ++i) if (detail::http_iequal(headers[i].name, name)) return headers[i].value;
            return StringView();
        }
        void set(StringView name, StringView value)
        {
            for (std::size_t i = 0; i < headers.size(); ++i) if (detail::http_iequal(headers[i].name, name)) { headers[i].value = String(value); return; }
            headers.push_back(HttpHeader{String(name), String(value)});
        }
        void text(StringView value) { set("Content-Type", "text/plain; charset=utf-8"); body = String(value); }
        void json(StringView value) { set("Content-Type", "application/json"); body = String(value); }
    };

    class HttpParser
    {
    public:
        enum State { NeedMore, Done, Error };
        static constexpr std::size_t kMaxHeaders = 16 * 1024;
        static constexpr std::size_t kMaxBody = 8 * 1024 * 1024;
        static constexpr std::size_t kMaxChunkLine = 1024;
        explicit HttpParser(std::size_t max_body = kMaxBody, std::size_t max_headers = kMaxHeaders)
            : max_body_(max_body), max_headers_(max_headers), state_(NeedMore), consumed_(0), error_(nullptr),
              head_response_(false), head_done_(false), blank_scan_(0), chunk_at_(0) {}
        void expect_head_response(bool enabled) noexcept { head_response_ = enabled; }
        State feed(const char *p, std::size_t n, HttpRequest &out) { if (!append(p, n)) return state_; return request(out); }
        State feed(const char *p, std::size_t n, HttpResponse &out) { if (!append(p, n)) return state_; return response(out, false); }
        State finish(HttpResponse &out) { return state_ == NeedMore ? response(out, true) : state_; }
        void reset()
        {
            if (state_ == Done && consumed_ <= data_.size()) data_.erase(0, consumed_);
            else if (state_ == Error) data_.clear();
            else if (head_done_ && head_.chunked && !decoded_.empty()) restore_decoded_chunks();
            state_ = NeedMore; consumed_ = 0; error_ = nullptr;
            head_done_ = false; blank_scan_ = 0; chunk_at_ = 0; decoded_.clear();
            request_ = HttpRequest(); response_ = HttpResponse();
        }
        State state() const noexcept { return state_; }
        const char *error() const noexcept { return error_; }
        std::size_t buffered() const noexcept { return data_.size(); }
        std::size_t consumed() const noexcept { return consumed_; }
    private:
        struct Head { std::size_t body, length; bool chunked, has_length; };
        bool fail(const char *s) { state_ = Error; error_ = s; return false; }
        void compact_chunks()
        {
            if (chunk_at_ <= head_.body) return;
            data_.erase(head_.body, chunk_at_ - head_.body);
            chunk_at_ = head_.body;
        }
        void restore_decoded_chunks()
        {
            char digits[2 * sizeof(std::size_t) + 3];
            std::size_t n = 0;
            for (std::size_t v = decoded_.size(); v; v >>= 4) digits[n++] = "0123456789abcdef"[v & 15];
            String chunk;
            while (n) chunk.push_back(digits[--n]);
            chunk.append("\r\n");
            chunk.append(decoded_);
            chunk.append("\r\n");
            data_.insert(head_.body, chunk);
        }
        bool append(const char *p, std::size_t n)
        {
            if (state_ == Error) return false;
            if (state_ == Done) return fail("HTTP message already complete; call reset()");
            if (n && !p) return fail("null HTTP input");
            if (n > max_headers_ + max_body_ || data_.size() > max_headers_ + max_body_ - n) return fail("HTTP message exceeds limit");
            data_.append(p, n); return true;
        }
        std::size_t crlf(std::size_t at) const noexcept
        {
            for (std::size_t i = at; i + 1 < data_.size(); ++i) if (data_[i] == '\r' && data_[i + 1] == '\n') return i;
            return String::npos;
        }
        bool head_complete(const char *too_long)
        {
            for (std::size_t i = blank_scan_; i + 3 < data_.size(); ++i)
            {
                if (data_[i] == '\r' && data_[i + 1] == '\n' && data_[i + 2] == '\r' && data_[i + 3] == '\n')
                {
                    if (i + 4 > max_headers_) return fail("HTTP headers exceed limit");
                    return true;
                }
            }
            blank_scan_ = data_.size() > 3 ? data_.size() - 3 : 0;
            if (data_.size() > max_headers_) fail(too_long);
            return false;
        }
        bool headers(std::size_t at, Vector<HttpHeader> &out, Head &head, bool strict)
        {
            out.clear(); head.length = 0; head.chunked = false; head.has_length = false;
            bool has_encoding = false;
            for (;;)
            {
                std::size_t end = crlf(at);
                if (end == String::npos) return fail("malformed HTTP headers");
                if (end == at)
                {
                    if (strict && head.has_length && head.chunked) return fail("Content-Length with Transfer-Encoding");
                    if (has_encoding) head.has_length = false;
                    head.body = end + 2;
                    return true;
                }
                std::size_t colon = at; while (colon < end && data_[colon] != ':') ++colon;
                if (colon == at || colon == end) return fail("malformed HTTP header");
                StringView name(data_.data() + at, colon - at), value = detail::http_trim(StringView(data_.data() + colon + 1, end - colon - 1));
                if (!detail::http_valid_token(name) || !detail::http_valid_header_value(value)) return fail("malformed HTTP header");
                out.push_back(HttpHeader{String(name), String(value)});
                if (detail::http_iequal(name, "Content-Length"))
                {
                    if (head.has_length || value.empty()) return fail("invalid Content-Length");
                    head.has_length = true;
                    for (std::size_t i = 0; i < value.size(); ++i)
                    {
                        if (value[i] < '0' || value[i] > '9') return fail("invalid Content-Length");
                        std::size_t d = static_cast<std::size_t>(value[i] - '0');
                        if (d > max_body_ || head.length > (max_body_ - d) / 10) return fail("HTTP body exceeds limit");
                        head.length = head.length * 10 + d;
                    }
                }
                if (detail::http_iequal(name, "Transfer-Encoding"))
                {
                    if (strict)
                    {
                        if (head.chunked || !detail::http_last_token_is(value, "chunked")) return fail("unsupported Transfer-Encoding");
                        head.chunked = true;
                    }
                    else
                    {
                        has_encoding = true;
                        head.chunked = detail::http_last_token_is(value, "chunked");
                    }
                }
                at = end + 2;
            }
        }
        State body(String &out)
        {
            if (!head_.chunked)
            {
                if (data_.size() < head_.body + head_.length) return NeedMore;
                out.assign(data_.data() + head_.body, head_.length); consumed_ = head_.body + head_.length; state_ = Done; return Done;
            }
            for (;;)
            {
                std::size_t end = crlf(chunk_at_);
                if (end == String::npos) { if (data_.size() - chunk_at_ > kMaxChunkLine) return fail("invalid chunk size"), Error; compact_chunks(); return NeedMore; }
                if (end - chunk_at_ > kMaxChunkLine) return fail("invalid chunk size"), Error;
                std::size_t size = 0, digits = 0;
                for (std::size_t i = chunk_at_; i < end && data_[i] != ';'; ++i)
                {
                    char c = data_[i]; unsigned d;
                    if (c >= '0' && c <= '9') d = c - '0'; else if (c >= 'a' && c <= 'f') d = c - 'a' + 10; else if (c >= 'A' && c <= 'F') d = c - 'A' + 10; else return fail("invalid chunk size"), Error;
                    if (d > max_body_ || size > (max_body_ - d) / 16) return fail("HTTP body exceeds limit"), Error;
                    size = size * 16 + d;
                    ++digits;
                }
                if (!digits) return fail("invalid chunk size"), Error;
                std::size_t at = end + 2;
                if (!size)
                {
                    if (data_.size() < at + 2) { compact_chunks(); return NeedMore; }
                    if (data_[at] != '\r' || data_[at + 1] != '\n') return fail("chunk trailers are not supported"), Error;
                    out = detail::move(decoded_); decoded_.clear(); consumed_ = at + 2; state_ = Done; return Done;
                }
                if (decoded_.size() > max_body_ - size) return fail("HTTP body exceeds limit"), Error;
                if (data_.size() < at + size + 2) { compact_chunks(); return NeedMore; }
                if (data_[at + size] != '\r' || data_[at + size + 1] != '\n') return fail("malformed HTTP chunk"), Error;
                decoded_.append(data_.data() + at, size);
                chunk_at_ = at + size + 2;
            }
        }
        State request(HttpRequest &out)
        {
            if (!head_done_)
            {
                if (!head_complete("request line too long")) return state_;
                std::size_t end = crlf(0);
                std::size_t a = 0; while (a < end && data_[a] != ' ') ++a; std::size_t b = a + 1; while (b < end && data_[b] != ' ') ++b;
                if (!a || a >= end || b >= end || b == a + 1) return fail("malformed request line"), Error;
                HttpRequest value; value.method.assign(data_.data(), a); StringView target(data_.data() + a + 1, b - a - 1); std::size_t q = target.find('?');
                if (!detail::http_valid_token(value.method) || !detail::http_valid_target(target)) return fail("malformed request line"), Error;
                StringView raw_path = q == StringView::npos ? target : target.substr(0, q);
                if (q != StringView::npos) value.query.assign(target.data() + q + 1, target.size() - q - 1);
                if (!detail::http_percent_decode_path(raw_path, value.path)) return fail("invalid percent-encoding in request path"), Error;
                value.version.assign(data_.data() + b + 1, end - b - 1); if (value.version != "HTTP/1.0" && value.version != "HTTP/1.1") return fail("unsupported HTTP version"), Error;
                if (!headers(end + 2, value.headers, head_, true)) return state_;
                request_ = detail::move(value); head_done_ = true; chunk_at_ = head_.body; decoded_.clear();
            }
            State result = body(request_.body); if (result == Done) out = detail::move(request_); return result;
        }
        State response(HttpResponse &out, bool eof)
        {
            if (!head_done_)
            {
                if (!head_complete("status line too long")) return state_;
                std::size_t end = crlf(0);
                std::size_t a = 0; while (a < end && data_[a] != ' ') ++a;
                if (a + 4 > end) return fail("malformed status line"), Error;
                HttpResponse value; value.version.assign(data_.data(), a); if (value.version != "HTTP/1.0" && value.version != "HTTP/1.1") return fail("unsupported HTTP version"), Error;
                for (std::size_t i = a + 1; i < a + 4; ++i) if (data_[i] < '0' || data_[i] > '9') return fail("invalid HTTP status"), Error;
                value.status = (data_[a + 1] - '0') * 100 + (data_[a + 2] - '0') * 10 + data_[a + 3] - '0';
                if (a + 4 < end) { if (data_[a + 4] != ' ') return fail("malformed status line"), Error; value.reason.assign(data_.data() + a + 5, end - a - 5); }
                if (!headers(end + 2, value.headers, head_, false)) return state_;
                response_ = detail::move(value); head_done_ = true; chunk_at_ = head_.body; decoded_.clear();
                const bool no_body = head_response_ || (response_.status >= 100 && response_.status < 200) || response_.status == 204 || response_.status == 304;
                if (no_body) { consumed_ = head_.body; state_ = Done; out = detail::move(response_); return Done; }
            }
            if (!head_.has_length && !head_.chunked)
            {
                if (!eof) return NeedMore;
                const std::size_t length = data_.size() - head_.body;
                if (length > max_body_) return fail("HTTP body exceeds limit"), Error;
                response_.body.assign(data_.data() + head_.body, length); consumed_ = data_.size(); state_ = Done; out = detail::move(response_); return Done;
            }
            State result = body(response_.body); if (result == Done) out = detail::move(response_); return result;
        }
        String data_; std::size_t max_body_, max_headers_; State state_; std::size_t consumed_; const char *error_; bool head_response_;
        bool head_done_; std::size_t blank_scan_; Head head_; std::size_t chunk_at_; String decoded_; HttpRequest request_; HttpResponse response_;
    };
}
