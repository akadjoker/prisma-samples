#pragma once

// Small, header-only TCP/UDP layer.  It deliberately exposes the byte-stream
// primitives; higher level protocols live in separate headers.

#include "span.hpp"
#include "string.hpp"
#include "vector.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace ct
{
    struct NetError
    {
        const char *message;
        int code;
        explicit operator bool() const noexcept { return code != 0; }
    };

    namespace detail
    {
#if defined(_WIN32)
        using SocketHandle = SOCKET;
        static constexpr SocketHandle invalid_socket = INVALID_SOCKET;
        inline int socket_error() noexcept { return WSAGetLastError(); }
        inline void close_socket(SocketHandle s) noexcept { closesocket(s); }
        inline bool would_block(int e) noexcept { return e == WSAEWOULDBLOCK; }
        inline bool interrupted(int e) noexcept { return e == WSAEINTR; }
        inline bool already_connected(int e) noexcept { return e == WSAEISCONN; }
        inline const char *socket_error_text(int) noexcept { return "Winsock error"; }
        struct NetInit { NetInit() noexcept { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); } ~NetInit() { WSACleanup(); } };
        inline void ensure_net() noexcept { static NetInit init; (void)init; }
#else
        using SocketHandle = int;
        static constexpr SocketHandle invalid_socket = -1;
        inline int socket_error() noexcept { return errno; }
        inline void close_socket(SocketHandle s) noexcept { ::close(s); }
        inline bool would_block(int e) noexcept { return e == EAGAIN || e == EWOULDBLOCK; }
        inline bool interrupted(int e) noexcept { return e == EINTR; }
        inline bool already_connected(int e) noexcept { return e == EISCONN; }
        inline const char *socket_error_text(int e) noexcept { return std::strerror(e); }
        inline void ensure_net() noexcept {}
#endif
        inline void set_error(NetError *out, int code) noexcept
        {
            if (out) { out->code = code; out->message = socket_error_text(code); }
        }
        inline int io_chunk(std::size_t n) noexcept
        {
            const std::size_t limit = static_cast<std::size_t>((std::numeric_limits<int>::max)());
            return static_cast<int>(n < limit ? n : limit);
        }
    }

    class Address
    {
    public:
        Address() noexcept : len_(0) { std::memset(&storage_, 0, sizeof(storage_)); }

        static bool parse(StringView ip, uint16_t port, Address &out) noexcept
        {
            out = Address();
            String text(ip);
            sockaddr_in v4 = {};
            if (inet_pton(AF_INET, text.c_str(), &v4.sin_addr) == 1)
            {
                v4.sin_family = AF_INET; v4.sin_port = htons(port);
                std::memcpy(&out.storage_, &v4, sizeof(v4)); out.len_ = sizeof(v4); return true;
            }
            sockaddr_in6 v6 = {};
            if (inet_pton(AF_INET6, text.c_str(), &v6.sin6_addr) == 1)
            {
                v6.sin6_family = AF_INET6; v6.sin6_port = htons(port);
                std::memcpy(&out.storage_, &v6, sizeof(v6)); out.len_ = sizeof(v6); return true;
            }
            return false;
        }

        static bool resolve(StringView host, uint16_t port, Vector<Address> &out, NetError *err = nullptr)
        {
            detail::ensure_net(); out.clear();
            String h(host), service(6, '\0');
            std::snprintf(service.data(), service.capacity() + 1, "%u", static_cast<unsigned>(port));
            addrinfo hints = {}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
            addrinfo *result = nullptr;
            const int rc = getaddrinfo(h.c_str(), service.c_str(), &hints, &result);
            if (rc != 0) { if (err) { err->code = rc; err->message = gai_strerror(rc); } return false; }
            for (addrinfo *p = result; p; p = p->ai_next)
            {
                if (p->ai_addrlen > sizeof(sockaddr_storage)) continue;
                Address a; std::memcpy(&a.storage_, p->ai_addr, p->ai_addrlen);
                a.len_ = static_cast<socklen_t>(p->ai_addrlen); out.push_back(a);
            }
            freeaddrinfo(result); return !out.empty();
        }

        String to_string() const
        {
            char text[INET6_ADDRSTRLEN] = {}; const void *src = nullptr; bool v6 = is_v6();
            if (v6) src = &reinterpret_cast<const sockaddr_in6 *>(&storage_)->sin6_addr;
            else if (len_) src = &reinterpret_cast<const sockaddr_in *>(&storage_)->sin_addr;
            if (!src || !inet_ntop(v6 ? AF_INET6 : AF_INET, src, text, sizeof(text))) return String();
            char result[INET6_ADDRSTRLEN + 10];
            std::snprintf(result, sizeof(result), v6 ? "[%s]:%u" : "%s:%u", text, static_cast<unsigned>(port()));
            return String(result);
        }
        uint16_t port() const noexcept
        {
            if (is_v6()) return ntohs(reinterpret_cast<const sockaddr_in6 *>(&storage_)->sin6_port);
            return len_ ? ntohs(reinterpret_cast<const sockaddr_in *>(&storage_)->sin_port) : 0;
        }
        bool is_v6() const noexcept { return len_ && reinterpret_cast<const sockaddr *>(&storage_)->sa_family == AF_INET6; }
        bool valid() const noexcept { return len_ != 0; }

    private:
        friend class Socket; friend class TcpListener; friend class TcpStream; friend class UdpSocket;
        sockaddr_storage storage_; socklen_t len_;
    };

    class Socket
    {
    public:
        Socket() noexcept : fd_(detail::invalid_socket), error_{"", 0} { detail::ensure_net(); }
        ~Socket() { close(); }
        Socket(const Socket &) = delete; Socket &operator=(const Socket &) = delete;
        Socket(Socket &&o) noexcept : fd_(o.fd_), error_(o.error_) { o.fd_ = detail::invalid_socket; }
        Socket &operator=(Socket &&o) noexcept { if (this != &o) { close(); fd_ = o.fd_; error_ = o.error_; o.fd_ = detail::invalid_socket; } return *this; }
        bool valid() const noexcept { return fd_ != detail::invalid_socket; }
        void close() noexcept { if (valid()) { detail::close_socket(fd_); fd_ = detail::invalid_socket; } }
        bool set_nonblocking(bool enabled) noexcept
        {
#if defined(_WIN32)
            u_long value = enabled ? 1 : 0; return checked(ioctlsocket(fd_, FIONBIO, &value));
#else
            int flags = fcntl(fd_, F_GETFL, 0); return flags >= 0 && checked(fcntl(fd_, F_SETFL, enabled ? flags | O_NONBLOCK : flags & ~O_NONBLOCK));
#endif
        }
        bool set_reuse_addr(bool enabled = true) noexcept { int v = enabled ? 1 : 0; return checked(setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&v), sizeof(v))); }
        bool set_nodelay(bool enabled = true) noexcept { int v = enabled ? 1 : 0; return checked(setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&v), sizeof(v))); }
        bool set_timeout_ms(unsigned recv_ms, unsigned send_ms) noexcept
        {
#if defined(_WIN32)
            DWORD r = recv_ms, s = send_ms;
#else
            timeval r = { static_cast<long>(recv_ms / 1000), static_cast<long>((recv_ms % 1000) * 1000) }, s = { static_cast<long>(send_ms / 1000), static_cast<long>((send_ms % 1000) * 1000) };
#endif
            return checked(setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&r), sizeof(r))) && checked(setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&s), sizeof(s)));
        }
        NetError last_error() const noexcept { return error_; }
        bool would_block() const noexcept { return detail::would_block(error_.code); }
        Address local_address() const noexcept
        {
            Address address;
            address.len_ = sizeof(address.storage_);
            if (!valid() || getsockname(fd_, reinterpret_cast<sockaddr *>(&address.storage_), &address.len_) != 0) address.len_ = 0;
            return address;
        }

    protected:
        explicit Socket(detail::SocketHandle fd) noexcept : fd_(fd), error_{"", 0} {}
        static void no_sigpipe(detail::SocketHandle fd) noexcept
        {
#if defined(SO_NOSIGPIPE)
            int one = 1;
            setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, reinterpret_cast<const char *>(&one), sizeof(one));
#else
            (void)fd;
#endif
        }
        bool open(int type, int family) noexcept { close(); fd_ = ::socket(family, type, 0); if (!valid()) { remember(); return false; } no_sigpipe(fd_); return true; }
        bool checked(int rc) noexcept { if (rc == 0) return true; remember(); return false; }
        void remember() noexcept { const int e = detail::socket_error(); error_ = {detail::socket_error_text(e), e}; }
        detail::SocketHandle fd_;
        NetError error_;
        friend class Poller;
    };

    class TcpStream;
    class TcpListener : public Socket
    {
    public:
        // SO_REUSEADDR is set right after open(), before the ::bind() call
        // it actually affects - a listening socket this process (or an
        // earlier run of it) just closed leaves its local (addr, port)
        // pair in TIME_WAIT for a while, and without the option set before
        // bind() a server restarted on the same port fails with
        // EADDRINUSE for as long as that lasts. This is what every
        // "restart a dev server on the same port" caller wants by
        // default - set_reuse_addr() stays public for anything unusual
        // that must NOT have it (there isn't one in this codebase today).
        bool bind(const Address &a, NetError *err = nullptr) noexcept
        {
            if (!a.valid() || !open(SOCK_STREAM, a.is_v6() ? AF_INET6 : AF_INET))
            {
                if (err) *err = error_;
                return false;
            }
            set_reuse_addr();
            if (!checked(::bind(fd_, reinterpret_cast<const sockaddr *>(&a.storage_), a.len_)))
            {
                if (err) *err = error_;
                return false;
            }
            return true;
        }
        bool listen(int backlog = 64) noexcept { return checked(::listen(fd_, backlog)); }
        bool accept(TcpStream &out, Address *peer = nullptr) noexcept;
    };

    class TcpStream : public Socket
    {
    public:
        TcpStream() noexcept : Socket() {}
        bool connect(const Address &a, unsigned timeout_ms = 0, NetError *err = nullptr) noexcept
        {
            if (!a.valid() || !open(SOCK_STREAM, a.is_v6() ? AF_INET6 : AF_INET)) { if (err) *err = error_; return false; }
            if (timeout_ms) set_timeout_ms(timeout_ms, timeout_ms);
            int rc;
            bool retried = false;
            for (;;)
            {
                rc = ::connect(fd_, reinterpret_cast<const sockaddr *>(&a.storage_), a.len_);
                if (rc == 0)
                    break;
                const int e = detail::socket_error();
                if (detail::interrupted(e))
                {
                    retried = true;
                    continue;
                }
                if (retried && detail::already_connected(e))
                    rc = 0;
                break;
            }
            if (!checked(rc)) { if (err) *err = error_; close(); return false; }
            return true;
        }
        long send(const void *data, std::size_t n) noexcept
        {
#if defined(MSG_NOSIGNAL)
            const int flags = MSG_NOSIGNAL;
#else
            const int flags = 0;
#endif
            const int chunk = detail::io_chunk(n);
            for (;;)
            {
                const auto r = ::send(fd_, static_cast<const char *>(data), chunk, flags);
                if (r >= 0) return static_cast<long>(r);
                remember();
                if (!detail::interrupted(error_.code)) return static_cast<long>(r);
            }
        }
        long recv(void *data, std::size_t n) noexcept
        {
            const int chunk = detail::io_chunk(n);
            for (;;)
            {
                const auto r = ::recv(fd_, static_cast<char *>(data), chunk, 0);
                if (r >= 0) return static_cast<long>(r);
                remember();
                if (!detail::interrupted(error_.code)) return static_cast<long>(r);
            }
        }
        bool send_all(StringView data) noexcept { std::size_t sent = 0; while (sent < data.size()) { long n = send(data.data() + sent, data.size() - sent); if (n <= 0) return false; sent += static_cast<std::size_t>(n); } return true; }
        bool recv_exact(void *data, std::size_t n) noexcept { std::size_t got = 0; while (got < n) { long r = recv(static_cast<char *>(data) + got, n - got); if (r <= 0) return false; got += static_cast<std::size_t>(r); } return true; }
        bool shutdown_write() noexcept { return checked(::shutdown(fd_,
#if defined(_WIN32)
            SD_SEND
#else
            SHUT_WR
#endif
        )); }
        Address peer() const noexcept { Address a; a.len_ = sizeof(a.storage_); if (!valid() || getpeername(fd_, reinterpret_cast<sockaddr *>(&a.storage_), &a.len_) != 0) a.len_ = 0; return a; }
    private: friend class TcpListener; explicit TcpStream(detail::SocketHandle fd) noexcept : Socket(fd) {};
    };
    inline bool TcpListener::accept(TcpStream &out, Address *peer) noexcept
    {
        Address a;
        detail::SocketHandle fd;
        for (;;)
        {
            a.len_ = sizeof(a.storage_);
            fd = ::accept(fd_, reinterpret_cast<sockaddr *>(&a.storage_), &a.len_);
            if (fd != detail::invalid_socket) break;
            remember();
            if (!detail::interrupted(error_.code)) return false;
        }
        no_sigpipe(fd);
        out = TcpStream(fd);
        if (peer) *peer = a;
        return true;
    }

    class UdpSocket : public Socket
    {
    public:
        bool bind(const Address &a, NetError *err = nullptr) noexcept { if (!a.valid() || !open(SOCK_DGRAM, a.is_v6() ? AF_INET6 : AF_INET) || !checked(::bind(fd_, reinterpret_cast<const sockaddr *>(&a.storage_), a.len_))) { if (err) *err = error_; return false; } return true; }
        long send_to(const void *data, std::size_t n, const Address &to) noexcept
        {
            const int chunk = detail::io_chunk(n);
            for (;;)
            {
                const auto r = ::sendto(fd_, static_cast<const char *>(data), chunk, 0, reinterpret_cast<const sockaddr *>(&to.storage_), to.len_);
                if (r >= 0) return static_cast<long>(r);
                remember();
                if (!detail::interrupted(error_.code)) return static_cast<long>(r);
            }
        }
        long recv_from(void *data, std::size_t n, Address *from = nullptr) noexcept
        {
            const int chunk = detail::io_chunk(n);
            for (;;)
            {
                Address a;
                a.len_ = sizeof(a.storage_);
                const auto r = ::recvfrom(fd_, static_cast<char *>(data), chunk, 0, reinterpret_cast<sockaddr *>(&a.storage_), &a.len_);
                if (r >= 0) { if (from) *from = a; return static_cast<long>(r); }
                remember();
                if (!detail::interrupted(error_.code)) return static_cast<long>(r);
            }
        }
        bool set_broadcast(bool enabled = true) noexcept { int v = enabled ? 1 : 0; return checked(setsockopt(fd_, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char *>(&v), sizeof(v))); }
    };

    class Poller
    {
    public:
        enum Event { Readable = 1, Writable = 2, Error = 4 };
        void add(const Socket &s, unsigned events) { Entry e = {&s, events, 0}; entries_.push_back(e); }
        void remove(const Socket &s) { for (std::size_t i = 0; i < entries_.size(); ++i) if (entries_[i].socket == &s) { entries_.erase(entries_.begin() + i); return; } }
        int wait(unsigned timeout_ms)
        {
            Vector<detail::SocketHandle> handles; handles.reserve(entries_.size());
#if defined(_WIN32)
            Vector<WSAPOLLFD> p; p.resize(entries_.size());
#else
            Vector<pollfd> p; p.resize(entries_.size());
#endif
            for (std::size_t i = 0; i < entries_.size(); ++i) { p[i].fd = entries_[i].socket->fd_; p[i].events = static_cast<short>((entries_[i].events & Readable ? POLLIN : 0) | (entries_[i].events & Writable ? POLLOUT : 0)); p[i].revents = 0; entries_[i].ready = 0; }
            int r;
            do
            {
#if defined(_WIN32)
                r = WSAPoll(p.data(), static_cast<ULONG>(p.size()), static_cast<int>(timeout_ms));
#else
                r = ::poll(p.data(), static_cast<nfds_t>(p.size()), static_cast<int>(timeout_ms));
#endif
            } while (r < 0 && detail::interrupted(detail::socket_error()));
            if (r < 0) return -1;
            for (std::size_t i = 0; i < entries_.size(); ++i) { if (p[i].revents & (POLLIN | POLLHUP)) entries_[i].ready |= Readable; if (p[i].revents & POLLOUT) entries_[i].ready |= Writable; if (p[i].revents & (POLLERR | POLLNVAL)) entries_[i].ready |= Error; } return r;
        }
        bool readable(std::size_t i) const noexcept { return i < entries_.size() && (entries_[i].ready & Readable); }
        bool writable(std::size_t i) const noexcept { return i < entries_.size() && (entries_[i].ready & Writable); }
        bool error(std::size_t i) const noexcept { return i < entries_.size() && (entries_[i].ready & Error); }
        std::size_t size() const noexcept { return entries_.size(); }
    private: struct Entry { const Socket *socket; unsigned events; unsigned ready; }; Vector<Entry> entries_;
    };
}
