#include <ct/socket.hpp>

#include <gtest/gtest.h>

#include <thread>

using ct::Address;
using ct::NetError;
using ct::Poller;
using ct::TcpListener;
using ct::TcpStream;
using ct::UdpSocket;

TEST(Socket, AddressParseAndResolve)
{
    Address a;
    ASSERT_TRUE(Address::parse("127.0.0.1", 4321, a));
    EXPECT_EQ(a.port(), 4321);
    EXPECT_FALSE(a.is_v6());
    EXPECT_EQ(a.to_string(), "127.0.0.1:4321");
    ASSERT_TRUE(Address::parse("::1", 80, a));
    EXPECT_TRUE(a.is_v6());
    EXPECT_EQ(a.to_string(), "[::1]:80");
    ct::Vector<Address> resolved;
    NetError error = {"", 0};
    EXPECT_TRUE(Address::resolve("localhost", 1234, resolved, &error));
    EXPECT_FALSE(error);
    EXPECT_FALSE(resolved.empty());
}

TEST(Socket, TcpLoopbackAndPoller)
{
    Address address;
    ASSERT_TRUE(Address::parse("127.0.0.1", 0, address));
    TcpListener listener;
    if (!listener.bind(address)) GTEST_SKIP() << "TCP port unavailable";
    ASSERT_TRUE(listener.listen());
    address = listener.local_address();
    std::thread server([&] {
        TcpStream incoming;
        EXPECT_TRUE(listener.accept(incoming));
        char buffer[8] = {};
        ASSERT_EQ(incoming.recv(buffer, sizeof(buffer)), 4);
        EXPECT_TRUE(incoming.send_all(ct::StringView(buffer, 4)));
    });
    TcpStream client;
    ASSERT_TRUE(client.connect(address, 1000));
    Poller p;
    p.add(client, Poller::Writable);
    ASSERT_EQ(p.wait(1000), 1);
    EXPECT_TRUE(p.writable(0));
    ASSERT_TRUE(client.send_all("pong"));
    char response[8] = {};
    ASSERT_EQ(client.recv(response, sizeof(response)), 4);
    EXPECT_EQ(std::memcmp(response, "pong", 4), 0);
    server.join();
}

TEST(Socket, UdpLoopback)
{
    Address address;
    ASSERT_TRUE(Address::parse("127.0.0.1", 0, address));
    UdpSocket receiver;
    if (!receiver.bind(address)) GTEST_SKIP() << "UDP port unavailable";
    address = receiver.local_address();
    UdpSocket sender;
    Address source;
    ASSERT_TRUE(Address::parse("127.0.0.1", 0, source));
    ASSERT_TRUE(sender.bind(source));
    const char payload[] = "ping";
    ASSERT_EQ(sender.send_to(payload, 4, address), 4);
    Poller p;
    p.add(receiver, Poller::Readable);
    ASSERT_EQ(p.wait(1000), 1);
    ASSERT_TRUE(p.readable(0));
    char buffer[8] = {};
    Address from;
    ASSERT_EQ(receiver.recv_from(buffer, sizeof(buffer), &from), 4);
    EXPECT_EQ(std::memcmp(buffer, payload, 4), 0);
    EXPECT_TRUE(from.valid());
}
