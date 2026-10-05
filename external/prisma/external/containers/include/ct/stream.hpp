#pragma once

#include <cstdio>
#include <cerrno>

#include "span.hpp"
#include "string.hpp"
#include "vector.hpp"

namespace ct
{
    enum class Seek { Set, Cur, End };

    class Stream
    {
    public:
        virtual ~Stream() {}
        virtual std::size_t read(void *dst, std::size_t n) = 0;
        virtual std::size_t write(const void *src, std::size_t n) = 0;
        virtual bool seek(std::int64_t offset, Seek origin) = 0;
        virtual std::int64_t tell() const = 0;
        virtual std::int64_t size() const = 0;
        virtual bool flush() { return true; }
        virtual void close() = 0;
        virtual bool is_open() const = 0;
        virtual bool eof() const = 0;
        virtual bool can_read() const = 0;
        virtual bool can_write() const = 0;
        virtual bool can_seek() const = 0;
        const char *error() const noexcept { return error_.empty() ? nullptr : error_.c_str(); }

        bool read_exact(void *dst, std::size_t n)
        {
            char *p = static_cast<char *>(dst);
            while (n) { std::size_t r = read(p, n); if (!r) return false; p += r; n -= r; }
            return true;
        }
        bool write_all(const void *src, std::size_t n)
        {
            const char *p = static_cast<const char *>(src);
            while (n) { std::size_t w = write(p, n); if (!w) return false; p += w; n -= w; }
            return true;
        }
        bool skip(std::int64_t n) { return seek(n, Seek::Cur); }
        bool read_all(Vector<std::uint8_t> &out)
        {
            out.clear(); std::uint8_t buf[4096];
            while (!eof()) { std::size_t n = read(buf, sizeof(buf)); if (!n) break; out.reserve(out.size() + n); for (std::size_t i = 0; i < n; ++i) out.push_back(buf[i]); }
            return !error();
        }
        bool read_all(String &out)
        {
            out.clear(); char buf[4096];
            while (!eof()) { std::size_t n = read(buf, sizeof(buf)); if (!n) break; out.append(buf, n); }
            return !error();
        }

    protected:
        void set_error(const char *message) { error_ = message ? message : "I/O error"; }
        void clear_error() { error_.clear(); }
        String error_;
    };

    class FileStream : public Stream
    {
    public:
        enum Mode { Read, Write, Append, ReadWrite };
        FileStream() noexcept : file_(nullptr), readable_(false), writable_(false) {}
        FileStream(StringView path, Mode mode) : FileStream() { open(path, mode); }
        ~FileStream() { close(); }
        FileStream(const FileStream &) = delete;
        FileStream &operator=(const FileStream &) = delete;

        bool open(StringView path, Mode mode)
        {
            close(); clear_error(); String p(path);
            const char *m = mode == Read ? "rb" : mode == Write ? "wb" : mode == Append ? "ab" : "r+b";
            file_ = std::fopen(p.c_str(), m);
            if (!file_ && mode == ReadWrite && errno == ENOENT) file_ = std::fopen(p.c_str(), "w+b");
            if (!file_) { set_errno(); return false; }
            readable_ = mode == Read || mode == ReadWrite;
            writable_ = mode != Read;
            return true;
        }
        std::size_t read(void *dst, std::size_t n) override
        {
            if (!file_ || !readable_) { set_error("stream is not readable"); return 0; }
            std::size_t r = std::fread(dst, 1, n, file_); if (r != n && std::ferror(file_)) set_errno(); return r;
        }
        std::size_t write(const void *src, std::size_t n) override
        {
            if (!file_ || !writable_) { set_error("stream is not writable"); return 0; }
            std::size_t w = std::fwrite(src, 1, n, file_); if (w != n) set_errno(); return w;
        }
        bool seek(std::int64_t offset, Seek origin) override
        {
            if (!file_) return false;
            int whence = origin == Seek::Set ? SEEK_SET : origin == Seek::Cur ? SEEK_CUR : SEEK_END;
#if defined(_WIN32)
            if (_fseeki64(file_, offset, whence) != 0) { set_errno(); return false; }
#else
            if (fseeko(file_, static_cast<off_t>(offset), whence) != 0) { set_errno(); return false; }
#endif
            clearerr(file_); return true;
        }
        std::int64_t tell() const override
        {
            if (!file_) return -1;
#if defined(_WIN32)
            return _ftelli64(file_);
#else
            return static_cast<std::int64_t>(ftello(file_));
#endif
        }
        std::int64_t size() const override
        {
            if (!file_) return -1;
            std::int64_t here = tell();
            if (here < 0) return -1;
            const bool at_eof = std::feof(file_) != 0;
            FileStream *self = const_cast<FileStream *>(this); if (!self->seek(0, Seek::End)) return -1;
            std::int64_t result = tell(); self->seek(here, Seek::Set);
            if (at_eof) { int c = std::fgetc(file_); if (c != EOF) std::ungetc(c, file_); }
            return result;
        }
        bool flush() override { if (!file_) return false; if (std::fflush(file_) != 0) { set_errno(); return false; } return true; }
        void close() override { if (file_) { std::fclose(file_); file_ = nullptr; } readable_ = writable_ = false; }
        bool is_open() const override { return file_ != nullptr; }
        bool eof() const override { return !file_ || std::feof(file_) != 0; }
        bool can_read() const override { return readable_; }
        bool can_write() const override { return writable_; }
        bool can_seek() const override { return true; }
    private:
        void set_errno() { set_error(std::strerror(errno)); }
        std::FILE *file_; bool readable_, writable_;
    };

    class MemoryStream : public Stream
    {
    public:
        MemoryStream() noexcept : view_(nullptr), view_size_(0), pos_(0), writable_(true), open_(true) {}
        MemoryStream(const void *data, std::size_t n) noexcept : view_(static_cast<const std::uint8_t *>(data)), view_size_(n), pos_(0), writable_(false), open_(true) {}
        explicit MemoryStream(Vector<std::uint8_t> own) noexcept : own_(detail::move(own)), view_(nullptr), view_size_(0), pos_(0), writable_(true), open_(true) {}
        std::size_t read(void *dst, std::size_t n) override
        {
            if (!open_) return 0;
            std::size_t total = data_size();
            if (pos_ >= total) return 0;
            n = n < total - pos_ ? n : total - pos_;
            std::memcpy(dst, data_ptr() + pos_, n); pos_ += n; return n;
        }
        std::size_t write(const void *src, std::size_t n) override
        {
            if (!open_ || !writable_) { set_error("stream is not writable"); return 0; }
            if (pos_ + n < pos_) { set_error("stream too large"); return 0; }
            if (pos_ + n > own_.size()) own_.resize(pos_ + n);
            std::memcpy(own_.data() + pos_, src, n); pos_ += n; return n;
        }
        bool seek(std::int64_t offset, Seek origin) override
        {
            std::int64_t base = origin == Seek::Set ? 0 : origin == Seek::Cur ? static_cast<std::int64_t>(pos_) : static_cast<std::int64_t>(data_size());
            if (offset > 0 && base > std::numeric_limits<std::int64_t>::max() - offset) return false;
            std::int64_t next = base + offset; if (next < 0 || static_cast<std::uint64_t>(next) > data_size()) return false; pos_ = static_cast<std::size_t>(next); return true;
        }
        std::int64_t tell() const override { return open_ ? static_cast<std::int64_t>(pos_) : -1; }
        std::int64_t size() const override { return static_cast<std::int64_t>(data_size()); }
        void close() override { open_ = false; }
        bool is_open() const override { return open_; }
        bool eof() const override { return pos_ >= data_size(); }
        bool can_read() const override { return open_; }
        bool can_write() const override { return open_ && writable_; }
        bool can_seek() const override { return open_; }
        Span<const std::uint8_t> data() const noexcept { return Span<const std::uint8_t>(data_ptr(), data_size()); }
        Vector<std::uint8_t> take() noexcept { pos_ = 0; return detail::move(own_); }
    private:
        const std::uint8_t *data_ptr() const noexcept { return view_ ? view_ : own_.data(); }
        std::size_t data_size() const noexcept { return view_ ? view_size_ : own_.size(); }
        Vector<std::uint8_t> own_; const std::uint8_t *view_; std::size_t view_size_, pos_; bool writable_, open_;
    };

    class SubStream : public Stream
    {
    public:
        SubStream(Stream &base, std::int64_t offset, std::int64_t length) : base_(base), offset_(offset < 0 ? 0 : offset), length_(0), pos_(0), hit_end_(false)
        {
            const std::int64_t limit = std::numeric_limits<std::int64_t>::max();
            if (length < 0) length = 0;
            if (length > limit - offset_) length = limit - offset_;
            length_ = length;
        }
        std::size_t read(void *dst, std::size_t n) override
        {
            if (!is_open()) { set_error("substream is closed"); return 0; }
            hit_end_ = false;
            if (pos_ >= length_) return 0;
            if (!base_.seek(offset_ + pos_, Seek::Set))
            {
                if (past_base_end(offset_ + pos_)) { hit_end_ = true; return 0; }
                set_error(base_.error() ? base_.error() : "substream seek failed");
                return 0;
            }
            std::int64_t left = length_ - pos_; n = n < static_cast<std::size_t>(left) ? n : static_cast<std::size_t>(left);
            std::size_t r = base_.read(dst, n); if (r != n) { if (base_.error()) set_error(base_.error()); else hit_end_ = true; } pos_ += static_cast<std::int64_t>(r); return r;
        }
        std::size_t write(const void *, std::size_t) override { set_error("substream is read-only"); return 0; }
        bool seek(std::int64_t offset, Seek origin) override
        {
            std::int64_t base = origin == Seek::Set ? 0 : origin == Seek::Cur ? pos_ : length_;
            if (offset > 0 && base > std::numeric_limits<std::int64_t>::max() - offset) return false;
            if (offset < 0 && base < std::numeric_limits<std::int64_t>::min() - offset) return false;
            std::int64_t next = base + offset; if (next < 0 || next > length_) return false; pos_ = next; hit_end_ = false; return true;
        }
        std::int64_t tell() const override { return pos_; }
        std::int64_t size() const override { return available_length(); }
        void close() override { closed_ = true; }
        bool is_open() const override { return !closed_ && base_.is_open(); }
        bool eof() const override { return (!base_.can_seek() && hit_end_) || pos_ >= available_length(); }
        bool can_read() const override { return is_open() && base_.can_read(); }
        bool can_write() const override { return false; }
        bool can_seek() const override { return is_open() && base_.can_seek(); }
    private:
        bool past_base_end(std::int64_t at) const
        {
            if (!base_.can_seek()) return false;
            const std::int64_t total = base_.size();
            return total >= 0 && at > total;
        }
        std::int64_t available_length() const
        {
            if (!base_.can_seek()) return length_;
            const std::int64_t total = base_.size();
            if (total < 0) return length_;
            const std::int64_t available = offset_ < total ? total - offset_ : 0;
            return length_ < available ? length_ : available;
        }
        Stream &base_; std::int64_t offset_, length_, pos_; bool hit_end_; bool closed_ = false;
    };

    struct StreamCallbacks
    {
        static int read(void *user, char *data, int size) { return size > 0 ? static_cast<int>(static_cast<Stream *>(user)->read(data, static_cast<std::size_t>(size))) : 0; }
        static void skip(void *user, int n) { static_cast<Stream *>(user)->skip(n); }
        static int eof(void *user) { return static_cast<Stream *>(user)->eof() ? 1 : 0; }
    };

    struct File
    {
        static bool read_all(StringView path, Vector<std::uint8_t> &out) { FileStream f(path, FileStream::Read); return f.is_open() && f.read_all(out); }
        static bool read_all(StringView path, String &out) { FileStream f(path, FileStream::Read); return f.is_open() && f.read_all(out); }
        static bool write_all(StringView path, const void *data, std::size_t n) { FileStream f(path, FileStream::Write); return f.is_open() && f.write_all(data, n) && f.flush(); }
        static bool write_all(StringView path, StringView text) { return write_all(path, text.data(), text.size()); }
        static bool exists(StringView path) { FileStream f(path, FileStream::Read); return f.is_open(); }
        static std::int64_t size(StringView path) { FileStream f(path, FileStream::Read); return f.size(); }
        static bool remove(StringView path) { String p(path); return std::remove(p.c_str()) == 0; }
    };
}
