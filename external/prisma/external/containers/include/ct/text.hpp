#pragma once

#include <cstdarg>
#include <cctype>

#include "stream.hpp"

namespace ct
{
    class TextReader
    {
    public:
        explicit TextReader(Stream &stream) noexcept : stream_(stream), begin_(0), end_(0), initialized_(false), pending_begin_(0), pending_end_(0) {}
        bool read_line(String &out)
        {
            out.clear(); int c;
            while ((c = get()) != -1) { if (c == '\n') return true; if (c == '\r') { if (peek() == '\n') get(); return true; } out.append(1, static_cast<char>(c)); }
            return !out.empty();
        }
        bool read_all(String &out)
        {
            out.clear(); init();
            while (pending_begin_ < pending_end_) out.append(1, static_cast<char>(pending_[pending_begin_++]));
            if (begin_ < end_) { out.append(buffer_ + begin_, end_ - begin_); begin_ = end_ = 0; }
            for (;;) { std::size_t n = stream_.read(buffer_, sizeof(buffer_)); if (!n) break; out.append(buffer_, n); }
            return !stream_.error();
        }
        bool read_word(String &out)
        {
            out.clear(); int c; while ((c = get()) != -1 && std::isspace(static_cast<unsigned char>(c))) {}
            if (c == -1) return false;
            do { out.append(1, static_cast<char>(c)); c = get(); } while (c != -1 && !std::isspace(static_cast<unsigned char>(c)));
            return true;
        }
        bool read_int(long long &out) { String word; if (!read_word(word)) return false; char *end = nullptr; out = std::strtoll(word.c_str(), &end, 10); return end && *end == '\0'; }
        bool read_double(double &out) { String word; if (!read_word(word)) return false; char *end = nullptr; out = std::strtod(word.c_str(), &end); return end && *end == '\0'; }
        int peek() { init(); if (pending_begin_ < pending_end_) return pending_[pending_begin_]; if (begin_ == end_ && !fill()) return -1; return static_cast<unsigned char>(buffer_[begin_]); }
        int get() { int c = peek(); if (c == -1) return -1; if (pending_begin_ < pending_end_) ++pending_begin_; else ++begin_; return c; }
        bool eof() const { return initialized_ && pending_begin_ == pending_end_ && begin_ == end_ && stream_.eof(); }
    private:
        bool fill() { begin_ = 0; end_ = stream_.read(buffer_, sizeof(buffer_)); return end_ != 0; }
        int raw_get() { if (begin_ == end_ && !fill()) return -1; return static_cast<unsigned char>(buffer_[begin_++]); }
        void init()
        {
            if (initialized_) return;
            initialized_ = true;
            int bytes[3]; unsigned n = 0;
            for (; n < 3; ++n) { bytes[n] = raw_get(); if (bytes[n] == -1) break; }
            if (n == 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf) return;
            for (unsigned i = 0; i < n; ++i) pending_[pending_end_++] = bytes[i];
        }
        Stream &stream_; char buffer_[4096]; std::size_t begin_, end_; bool initialized_; int pending_[3]; unsigned pending_begin_, pending_end_;
    };

    class TextWriter
    {
    public:
        explicit TextWriter(Stream &stream) noexcept : stream_(stream), size_(0), ok_(true) {}
        ~TextWriter() { flush(); }
        TextWriter(const TextWriter &) = delete;
        TextWriter &operator=(const TextWriter &) = delete;
        TextWriter &write(StringView text) { append(text.data(), text.size()); return *this; }
        TextWriter &write(char c) { append(&c, 1); return *this; }
        TextWriter &line(StringView text = "") { write(text); return write('\n'); }
        template <typename I, typename = typename detail::enable_if<std::is_integral<I>::value || std::is_enum<I>::value>::type>
        TextWriter &number(I value) { String text; text.append_number(value); return write(text); }
        TextWriter &number(double value, int precision = 6) { String text; text.append_number(value, precision); return write(text); }
        TextWriter &fmt(const char *format, ...)
        {
            va_list args; va_start(args, format); va_list copy; va_copy(copy, args); int n = std::vsnprintf(nullptr, 0, format, copy); va_end(copy);
            if (n < 0) { ok_ = false; va_end(args); return *this; } String text(static_cast<std::size_t>(n), '\0'); std::vsnprintf(text.data(), text.size() + 1, format, args); va_end(args); return write(text);
        }
        bool flush()
        {
            if (size_ && !stream_.write_all(buffer_, size_)) ok_ = false;
            size_ = 0; return ok_ && stream_.flush();
        }
        bool ok() const noexcept { return ok_; }
    private:
        void append(const char *data, std::size_t n)
        {
            while (n) { std::size_t space = sizeof(buffer_) - size_; if (!space) { if (!flush()) return; space = sizeof(buffer_); } std::size_t part = n < space ? n : space; std::memcpy(buffer_ + size_, data, part); size_ += part; data += part; n -= part; }
        }
        Stream &stream_; char buffer_[4096]; std::size_t size_; bool ok_;
    };
}
