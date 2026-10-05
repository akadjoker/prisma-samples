#pragma once

#include "stream.hpp"

namespace ct
{
    class BinaryReader
    {
    public:
        explicit BinaryReader(Stream &stream) noexcept : stream_(stream), ok_(true) {}
        std::uint8_t u8() { return scalar<std::uint8_t>(); }
        std::uint16_t u16() { return scalar<std::uint16_t>(); }
        std::uint32_t u32() { return scalar<std::uint32_t>(); }
        std::uint64_t u64() { return scalar<std::uint64_t>(); }
        std::int8_t i8() { return scalar<std::int8_t>(); }
        std::int16_t i16() { return scalar<std::int16_t>(); }
        std::int32_t i32() { return scalar<std::int32_t>(); }
        std::int64_t i64() { return scalar<std::int64_t>(); }
        float f32() { std::uint32_t bits = u32(); float value = 0; std::memcpy(&value, &bits, sizeof(value)); return value; }
        double f64() { std::uint64_t bits = u64(); double value = 0; std::memcpy(&value, &bits, sizeof(value)); return value; }
        bool boolean() { return u8() != 0; }
        std::uint32_t varint()
        {
            std::uint32_t value = 0;
            for (unsigned shift = 0; shift < 35; shift += 7) { std::uint8_t byte = u8(); if (!ok_ || (shift == 28 && (byte & 0xf0))) { ok_ = false; return 0; } value |= std::uint32_t(byte & 0x7f) << shift; if (!(byte & 0x80)) return value; }
            ok_ = false; return 0;
        }
        bool string(String &out) { std::uint32_t n = u32(); if (!ok_) return false; return sized(out, n); }
        bool bytes(void *dst, std::size_t n) { if (!ok_ || !stream_.read_exact(dst, n)) { ok_ = false; return false; } return true; }
        bool bytes(Vector<std::uint8_t> &out, std::size_t n) { return sized(out, n); }
        template <typename T> bool pod(T &out) { static_assert(std::is_trivially_copyable<T>::value, "pod requires a trivially copyable type"); return bytes(&out, sizeof(T)); }
        bool ok() const noexcept { return ok_; }
        Stream &stream() noexcept { return stream_; }
    private:
        static constexpr std::size_t kSizedFirstChunk = 4096;

        template <typename C>
        bool sized(C &out, std::size_t n)
        {
            out.clear();
            if (!ok_)
                return false;
            while (n)
            {
                const std::size_t have = out.size();
                const std::size_t step = have > kSizedFirstChunk ? have : kSizedFirstChunk;
                const std::size_t part = n < step ? n : step;
                out.resize(have + part);
                if (!bytes(out.data() + have, part))
                {
                    out.resize(have);
                    return false;
                }
                n -= part;
            }
            return true;
        }

        template <typename T> T scalar()
        {
            T value = 0; std::uint8_t bytes_[sizeof(T)]; if (!bytes(bytes_, sizeof(bytes_))) return 0;
            typename std::make_unsigned<T>::type raw = 0;
            for (std::size_t i = 0; i < sizeof(T); ++i) raw |= typename std::make_unsigned<T>::type(bytes_[i]) << (i * 8);
            std::memcpy(&value, &raw, sizeof(value)); return value;
        }
        Stream &stream_; bool ok_;
    };

    class BinaryWriter
    {
    public:
        explicit BinaryWriter(Stream &stream) noexcept : stream_(stream), ok_(true) {}
        void u8(std::uint8_t v) { scalar(v); }
        void u16(std::uint16_t v) { scalar(v); }
        void u32(std::uint32_t v) { scalar(v); }
        void u64(std::uint64_t v) { scalar(v); }
        void i8(std::int8_t v) { scalar(v); }
        void i16(std::int16_t v) { scalar(v); }
        void i32(std::int32_t v) { scalar(v); }
        void i64(std::int64_t v) { scalar(v); }
        void f32(float v) { std::uint32_t bits; std::memcpy(&bits, &v, sizeof(bits)); u32(bits); }
        void f64(double v) { std::uint64_t bits; std::memcpy(&bits, &v, sizeof(bits)); u64(bits); }
        void boolean(bool v) { u8(v ? 1 : 0); }
        void varint(std::uint32_t v) { do { std::uint8_t byte = static_cast<std::uint8_t>(v & 0x7f); v >>= 7; u8(static_cast<std::uint8_t>(byte | (v ? 0x80 : 0))); } while (v); }
        void string(StringView value) { u32(static_cast<std::uint32_t>(value.size())); bytes(value.data(), value.size()); }
        bool bytes(const void *src, std::size_t n) { if (!ok_ || !stream_.write_all(src, n)) { ok_ = false; return false; } return true; }
        template <typename T> bool pod(const T &value) { static_assert(std::is_trivially_copyable<T>::value, "pod requires a trivially copyable type"); return bytes(&value, sizeof(T)); }
        bool ok() const noexcept { return ok_; }
        Stream &stream() noexcept { return stream_; }
    private:
        template <typename T> void scalar(T value)
        {
            using U = typename std::make_unsigned<T>::type; U raw = 0; std::memcpy(&raw, &value, sizeof(raw)); std::uint8_t bytes_[sizeof(T)];
            for (std::size_t i = 0; i < sizeof(T); ++i) bytes_[i] = static_cast<std::uint8_t>(raw >> (i * 8));
            bytes(bytes_, sizeof(bytes_));
        }
        Stream &stream_; bool ok_;
    };
}
