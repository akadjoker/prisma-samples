#include <ct/queue.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <queue>
#include <string>

TEST(QueueBasic, PushFrontBackPop) {
    ct::Queue<int> q;
    EXPECT_TRUE(q.empty());
    q.push(1);
    q.push(2);
    q.push(3);
    EXPECT_EQ(q.size(), 3u);
    EXPECT_EQ(q.front(), 1);
    EXPECT_EQ(q.back(), 3);
    q.pop();
    EXPECT_EQ(q.front(), 2);
    q.pop();
    q.pop();
    EXPECT_TRUE(q.empty());
}

TEST(QueueBasic, EmplaceReturnsReference) {
    ct::Queue<std::string> q;
    std::string& r = q.emplace(2, 'k');
    EXPECT_EQ(r, "kk");
    EXPECT_EQ(q.front(), "kk");
    EXPECT_EQ(q.back(), "kk");
}

TEST(QueueBasic, MoveOnly) {
    ct::Queue<std::unique_ptr<int>> q;
    for (int i = 0; i < 1000; ++i)
        q.push(std::unique_ptr<int>(new int(i)));
    EXPECT_EQ(*q.front(), 0);
    EXPECT_EQ(*q.back(), 999);
    std::unique_ptr<int> taken = std::move(q.front());
    q.pop();
    EXPECT_EQ(*taken, 0);
    EXPECT_EQ(*q.front(), 1);
}

TEST(QueueBasic, ReserveClearSwapEquality) {
    ct::Queue<int> q;
    q.reserve(100);
    EXPECT_GE(q.capacity(), 100u);
    for (int i = 0; i < 100; ++i)
        q.push(i);
    q.clear();
    EXPECT_TRUE(q.empty());
    ct::Queue<int> a(ct::Deque<int>{1, 2}), b(ct::Deque<int>{1, 2}), c(ct::Deque<int>{2, 1});
    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);
    swap(a, c);
    EXPECT_EQ(a.front(), 2);
    EXPECT_EQ(c.front(), 1);
}

TEST(QueueFuzz, MatchesStd) {
    ct::Queue<int> cq;
    std::queue<int> sq;
    unsigned seed = 99;
    for (int step = 0; step < 100000; ++step) {
        seed = seed * 1664525u + 1013904223u;
        if (sq.empty() || (seed >> 28) < 9) {
            cq.push(static_cast<int>(seed % 1000));
            sq.push(static_cast<int>(seed % 1000));
        } else {
            ASSERT_EQ(cq.front(), sq.front());
            ASSERT_EQ(cq.back(), sq.back());
            cq.pop();
            sq.pop();
        }
        ASSERT_EQ(cq.size(), sq.size());
    }
    while (!sq.empty()) {
        ASSERT_EQ(cq.front(), sq.front());
        cq.pop();
        sq.pop();
    }
    EXPECT_TRUE(cq.empty());
}