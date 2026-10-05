#pragma once

#include "socket.hpp"
#include "stream.hpp"

namespace ct
{
    class SocketStream : public Stream
    {
    public:
        explicit SocketStream(TcpStream &socket) noexcept : socket_(socket), closed_(false), eof_(false) {}
        std::size_t read(void *dst, std::size_t n) override
        {
            if (!is_open()) return 0;
            long result = socket_.recv(dst, n);
            if (result <= 0) { if (result < 0) set_socket_error(); else eof_ = true; return 0; }
            return static_cast<std::size_t>(result);
        }
        std::size_t write(const void *src, std::size_t n) override
        {
            if (!is_open()) return 0;
            long result = socket_.send(src, n);
            if (result < 0) { set_socket_error(); return 0; }
            return static_cast<std::size_t>(result);
        }
        bool seek(std::int64_t, Seek) override { set_error("socket streams cannot seek"); return false; }
        std::int64_t tell() const override { return -1; }
        std::int64_t size() const override { return -1; }
        void close() override { if (!closed_) { socket_.close(); closed_ = true; } }
        bool is_open() const override { return !closed_ && socket_.valid(); }
        bool eof() const override { return eof_; }
        bool can_read() const override { return is_open(); }
        bool can_write() const override { return is_open(); }
        bool can_seek() const override { return false; }
        TcpStream &socket() noexcept { return socket_; }
    private:
        void set_socket_error() { NetError error = socket_.last_error(); set_error(error.message ? error.message : "socket error"); }
        TcpStream &socket_; bool closed_, eof_;
    };
}
