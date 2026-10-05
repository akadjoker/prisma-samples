#include <ct/stack.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <stack>
#include <string>

TEST(StackBasic, PushTopPop) {
    ct::Stack<int> s;
    EXPECT_TRUE(s.empty());
    s.push(1);
    s.push(2);
    s.push(3);
    EXPECT_EQ(s.size(), 3u);
    EXPECT_EQ(s.top(), 3);
    s.pop();
    EXPECT_EQ(s.top(), 2);
    s.pop();
    s.pop();
    EXPECT_TRUE(s.empty());
}

TEST(StackBasic, EmplaceReturnsReference) {
    ct::Stack<std::string> s;
    std::string& r = s.emplace(3, 'z');
    EXPECT_EQ(r, "zzz");
    EXPECT_EQ(s.top(), "zzz");
}

TEST(StackBasic, MoveOnly) {
    ct::Stack<std::unique_ptr<int>> s;
    for (int i = 0; i < 1000; ++i)
        s.push(std::unique_ptr<int>(new int(i)));
    EXPECT_EQ(*s.top(), 999);
    std::unique_ptr<int> taken = std::move(s.top());
    s.pop();
    EXPECT_EQ(*taken, 999);
    EXPECT_EQ(*s.top(), 998);
}

TEST(StackBasic, ReserveAndClear) {
    ct::Stack<int> s;
    s.reserve(100);
    EXPECT_GE(s.capacity(), 100u);
    std::size_t cap = s.capacity();
    for (int i = 0; i < 100; ++i)
        s.push(i);
    EXPECT_EQ(s.capacity(), cap);
    s.clear();
    EXPECT_TRUE(s.empty());
}

TEST(StackBasic, FromContainerAndEquality) {
    ct::Stack<int> a(ct::Vector<int>{1, 2, 3});
    ct::Stack<int> b(ct::Vector<int>{1, 2, 3});
    ct::Stack<int> c(ct::Vector<int>{1, 2, 4});
    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);
    EXPECT_EQ(a.top(), 3);
    EXPECT_EQ(a.container().size(), 3u);
}

TEST(StackBasic, SwapAndCopy) {
    ct::Stack<int> a, b;
    a.push(1);
    a.push(2);
    b.push(9);
    swap(a, b);
    EXPECT_EQ(a.size(), 1u);
    EXPECT_EQ(a.top(), 9);
    EXPECT_EQ(b.top(), 2);
    ct::Stack<int> copy(b);
    EXPECT_TRUE(copy == b);
    ct::Stack<int> moved(std::move(b));
    EXPECT_EQ(moved.top(), 2);
    EXPECT_TRUE(b.empty());
}

TEST(StackFuzz, MatchesStd) {
    ct::Stack<int> cs;
    std::stack<int> ss;
    unsigned seed = 42;
    for (int step = 0; step < 100000; ++step) {
        seed = seed * 1664525u + 1013904223u;
        if (ss.empty() || (seed >> 28) < 9) {
            cs.push(static_cast<int>(seed % 1000));
            ss.push(static_cast<int>(seed % 1000));
        } else {
            ASSERT_EQ(cs.top(), ss.top());
            cs.pop();
            ss.pop();
        }
        ASSERT_EQ(cs.size(), ss.size());
    }
    while (!ss.empty()) {
        ASSERT_EQ(cs.top(), ss.top());
        cs.pop();
        ss.pop();
    }
    EXPECT_TRUE(cs.empty());
}