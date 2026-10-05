#include <ct/deque.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <deque>
#include <functional>
#include <memory>
#include <string>

struct DqTracked {
    static int live;
    int value;
    explicit DqTracked(int v = 0) : value(v) { ++live; }
    DqTracked(const DqTracked& o) : value(o.value) { ++live; }
    DqTracked(DqTracked&& o) noexcept : value(o.value) { o.value = -1; ++live; }
    DqTracked& operator=(const DqTracked&) = default;
    DqTracked& operator=(DqTracked&&) = default;
    ~DqTracked() { --live; }
    bool operator==(const DqTracked& o) const { return value == o.value; }
};
int DqTracked::live = 0;

class DequeTrackedTest : public ::testing::Test {
protected:
    void SetUp() override { DqTracked::live = 0; }
    void TearDown() override { EXPECT_EQ(DqTracked::live, 0) << "leaked instances"; }
};

TEST(DequeBasic, DefaultConstructedIsEmpty) {
    ct::Deque<int> d;
    EXPECT_TRUE(d.empty());
    EXPECT_EQ(d.size(), 0u);
    EXPECT_EQ(d.capacity(), 0u);
}

TEST(DequeBasic, CountConstructor) {
    ct::Deque<int> d(5);
    EXPECT_EQ(d.size(), 5u);
    for (int x : d)
        EXPECT_EQ(x, 0);
}

TEST(DequeBasic, CountValueConstructor) {
    ct::Deque<int> d(4, 7);
    EXPECT_EQ(d.size(), 4u);
    for (int x : d)
        EXPECT_EQ(x, 7);
}

TEST(DequeBasic, InitializerList) {
    ct::Deque<int> d{1, 2, 3, 4};
    ASSERT_EQ(d.size(), 4u);
    EXPECT_EQ(d[0], 1);
    EXPECT_EQ(d[3], 4);
}

TEST(DequeBasic, PushBackGrows) {
    ct::Deque<int> d;
    for (int i = 0; i < 1000; ++i)
        d.push_back(i);
    ASSERT_EQ(d.size(), 1000u);
    for (int i = 0; i < 1000; ++i)
        ASSERT_EQ(d[i], i);
    EXPECT_GE(d.capacity(), 1000u);
}

TEST(DequeBasic, PushFrontOrder) {
    ct::Deque<int> d;
    for (int i = 0; i < 1000; ++i)
        d.push_front(i);
    ASSERT_EQ(d.size(), 1000u);
    for (int i = 0; i < 1000; ++i)
        ASSERT_EQ(d[i], 999 - i);
    EXPECT_EQ(d.front(), 999);
    EXPECT_EQ(d.back(), 0);
}

TEST(DequeBasic, MixedEndsOrder) {
    ct::Deque<int> d;
    for (int i = 1; i <= 100; ++i) {
        d.push_back(i);
        d.push_front(-i);
    }
    ASSERT_EQ(d.size(), 200u);
    EXPECT_EQ(d.front(), -100);
    EXPECT_EQ(d.back(), 100);
    for (int i = 0; i < 100; ++i)
        ASSERT_EQ(d[i], -(100 - i));
    for (int i = 0; i < 100; ++i)
        ASSERT_EQ(d[100 + i], i + 1);
}

TEST(DequeBasic, PopBoth) {
    ct::Deque<int> d{1, 2, 3, 4, 5};
    d.pop_front();
    d.pop_back();
    ASSERT_EQ(d.size(), 3u);
    EXPECT_EQ(d.front(), 2);
    EXPECT_EQ(d.back(), 4);
}

TEST(DequeBasic, EmplaceReturnsReference) {
    ct::Deque<std::string> d;
    std::string& r1 = d.emplace_back(3, 'x');
    EXPECT_EQ(r1, "xxx");
    std::string& r2 = d.emplace_front(2, 'y');
    EXPECT_EQ(r2, "yy");
    EXPECT_EQ(d.front(), "yy");
    EXPECT_EQ(d.back(), "xxx");
}

TEST(DequeBasic, AtOutOfRangeDies) {
    ct::Deque<int> d{1};
    EXPECT_DEATH(d.at(1), "fora dos limites");
    EXPECT_DEATH(d.at(std::size_t(-1)), "fora dos limites");
}

TEST(DequeWrap, FifoSteadyStateWraps) {
    ct::Deque<int> d;
    for (int i = 0; i < 8; ++i)
        d.push_back(i);

    for (int i = 8; i < 10000; ++i) {
        ASSERT_EQ(d.front(), i - 8);
        d.pop_front();
        d.push_back(i);
    }
    ASSERT_EQ(d.size(), 8u);
    for (int i = 0; i < 8; ++i)
        ASSERT_EQ(d[i], 9992 + i);
}

TEST(DequeWrap, GrowWhileWrappedKeepsOrder) {
    ct::Deque<int> d;
    for (int i = 0; i < 8; ++i)
        d.push_back(i);
    for (int i = 0; i < 5; ++i)
        d.pop_front();
    for (int i = 8; i < 13; ++i)
        d.push_back(i); 
    std::size_t cap_before = d.capacity();
    for (int i = 13; i < 100; ++i)
        d.push_back(i); 
    EXPECT_GT(d.capacity(), cap_before);
    ASSERT_EQ(d.size(), 95u);
    for (int i = 0; i < 95; ++i)
        ASSERT_EQ(d[i], i + 5);
}

TEST(DequeWrap, GrowWhileWrappedNonTrivial) {
    ct::Deque<std::string> d;
    for (int i = 0; i < 8; ++i)
        d.push_back("s" + std::to_string(i));
    for (int i = 0; i < 6; ++i)
        d.pop_front();
    for (int i = 8; i < 40; ++i)
        d.push_back("s" + std::to_string(i));
    ASSERT_EQ(d.size(), 34u);
    for (int i = 0; i < 34; ++i)
        ASSERT_EQ(d[i], "s" + std::to_string(i + 6));
}

TEST(DequeWrap, EmplaceWithElementReferenceSurvivesGrowth) {
    ct::Deque<std::string> d;
    d.reserve(8);
    for (int i = 0; i < 8; ++i)
        d.push_back("value_" + std::to_string(i));

    const std::string front = d.front();
    d.emplace_back(d.front());
    EXPECT_EQ(d.back(), front);

    d.shrink_to_fit();
    d.emplace_front(d.back());
    EXPECT_EQ(d.front(), front);
}

TEST(DequeFuzz, RandomOpsMatchStd) {
    ct::Deque<int> cd;
    std::deque<int> sd;
    unsigned seed = 12345;
    for (int step = 0; step < 200000; ++step) {
        seed = seed * 1664525u + 1013904223u;
        unsigned op = seed >> 28;
        int v = static_cast<int>(seed % 1000);
        if (sd.empty() || op < 6) {
            if (op & 1) {
                cd.push_back(v);
                sd.push_back(v);
            } else {
                cd.push_front(v);
                sd.push_front(v);
            }
        } else if (op < 13) {
            if (op & 1) {
                cd.pop_back();
                sd.pop_back();
            } else {
                cd.pop_front();
                sd.pop_front();
            }
        } else if (op == 13 && !sd.empty()) {
            std::size_t i = seed % sd.size();
            ASSERT_EQ(cd[i], sd[i]);
        } else if (op == 14) {
            ASSERT_EQ(cd.size(), sd.size());
        } else {
            if (!sd.empty()) {
                ASSERT_EQ(cd.front(), sd.front());
                ASSERT_EQ(cd.back(), sd.back());
            }
        }
    }
    ASSERT_EQ(cd.size(), sd.size());
    for (std::size_t i = 0; i < sd.size(); ++i)
        ASSERT_EQ(cd[i], sd[i]);
}

TEST_F(DequeTrackedTest, CopyConstruct) {
    ct::Deque<DqTracked> d;
    for (int i = 0; i < 50; ++i)
        d.push_back(DqTracked(i));
    for (int i = 0; i < 20; ++i)
        d.pop_front(); 
    for (int i = 50; i < 70; ++i)
        d.push_back(DqTracked(i)); 
    ct::Deque<DqTracked> c(d);
    ASSERT_EQ(c.size(), d.size());
    for (std::size_t i = 0; i < d.size(); ++i)
        ASSERT_EQ(c[i], d[i]);
}

TEST_F(DequeTrackedTest, MoveConstructStealsStorage) {
    ct::Deque<DqTracked> d;
    for (int i = 0; i < 30; ++i)
        d.push_back(DqTracked(i));
    ct::Deque<DqTracked> m(std::move(d));
    EXPECT_TRUE(d.empty());
    ASSERT_EQ(m.size(), 30u);
    EXPECT_EQ(m[7].value, 7);
}

TEST_F(DequeTrackedTest, CopyAndMoveAssign) {
    ct::Deque<DqTracked> a, b;
    for (int i = 0; i < 20; ++i)
        a.push_back(DqTracked(i));
    b.push_back(DqTracked(99));
    b = a;
    ASSERT_EQ(b.size(), 20u);
    EXPECT_EQ(b[5].value, 5);
    ct::Deque<DqTracked> c;
    c.push_back(DqTracked(1));
    c = std::move(a);
    EXPECT_TRUE(a.empty());
    ASSERT_EQ(c.size(), 20u);
    EXPECT_EQ(c[19].value, 19);
}

TEST_F(DequeTrackedTest, ClearAndResizeDestroy) {
    ct::Deque<DqTracked> d;
    for (int i = 0; i < 40; ++i)
        d.push_front(DqTracked(i));
    d.resize(10);
    EXPECT_EQ(d.size(), 10u);
    EXPECT_EQ(DqTracked::live, 10);
    d.resize(25, DqTracked(7));
    EXPECT_EQ(d.size(), 25u);
    EXPECT_EQ(d[24].value, 7);
    d.clear();
    EXPECT_TRUE(d.empty());
    EXPECT_EQ(DqTracked::live, 0);
}

TEST(DequeMoveOnly, UniquePtr) {
    ct::Deque<std::unique_ptr<int>> d;
    for (int i = 0; i < 1000; ++i) {
        if (i & 1)
            d.push_back(std::unique_ptr<int>(new int(i)));
        else
            d.push_front(std::unique_ptr<int>(new int(i)));
    }
    ASSERT_EQ(d.size(), 1000u);
    EXPECT_EQ(*d.front(), 998);
    EXPECT_EQ(*d.back(), 999);
    while (!d.empty())
        d.pop_front();
}

TEST(DequeIter, ForwardAndReverse) {
    ct::Deque<int> d;
    for (int i = 0; i < 10; ++i)
        d.push_back(i);
    d.pop_front();
    d.pop_front();
    d.push_back(10);
    d.push_back(11); 
    int expect = 2;
    for (int x : d)
        ASSERT_EQ(x, expect++);
    EXPECT_EQ(expect, 12);
    auto it = d.begin();
    it += 3;
    EXPECT_EQ(*it, 5);
    EXPECT_EQ(it - d.begin(), 3);
    EXPECT_EQ(d.end() - d.begin(), static_cast<std::ptrdiff_t>(d.size()));
    int rexpect = 11;
    for (auto r = d.rbegin(); r != d.rend(); ++r)
        ASSERT_EQ(*r, rexpect--);
}

TEST(DequeIter, ConstIteration) {
    const ct::Deque<int> d{1, 2, 3};
    int sum = 0;
    for (auto it = d.cbegin(); it != d.cend(); ++it)
        sum += *it;
    EXPECT_EQ(sum, 6);
    ct::Deque<int> m{5};
    ct::Deque<int>::const_iterator ci = m.begin(); 
    EXPECT_EQ(*ci, 5);
}

TEST(DequeMisc, SpansCoverElementsInOrder) {
    ct::Deque<int> d;
    for (int i = 0; i < 20; ++i)
        d.push_back(i);
    for (int i = 0; i < 12; ++i)
        d.pop_front();
    for (int i = 20; i < 34; ++i)
        d.push_back(i); 
    auto a = d.first_span();
    auto b = d.second_span();
    ASSERT_EQ(a.len + b.len, d.size());
    EXPECT_GT(b.len, 0u); 
    std::size_t k = 0;
    for (std::size_t i = 0; i < a.len; ++i, ++k)
        ASSERT_EQ(a.ptr[i], d[k]);
    for (std::size_t i = 0; i < b.len; ++i, ++k)
        ASSERT_EQ(b.ptr[i], d[k]);
    ct::Deque<int> empty;
    EXPECT_EQ(empty.first_span().len, 0u);
    EXPECT_EQ(empty.second_span().len, 0u);
}

TEST(DequeMisc, ReserveAndShrink) {
    ct::Deque<int> d;
    d.reserve(100);
    EXPECT_GE(d.capacity(), 100u);
    std::size_t cap = d.capacity();
    for (int i = 0; i < 100; ++i)
        d.push_back(i);
    EXPECT_EQ(d.capacity(), cap); 
    d.resize(5);
    d.shrink_to_fit();
    EXPECT_LT(d.capacity(), cap);
    for (int i = 0; i < 5; ++i)
        ASSERT_EQ(d[i], i);
    d.clear();
    d.shrink_to_fit();
    EXPECT_EQ(d.capacity(), 0u);
}

TEST(DequeMisc, EqualityAndSwap) {
    ct::Deque<int> a{1, 2, 3}, b{1, 2, 3}, c{1, 2, 4};
    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);
    a.pop_front();
    b.pop_front();
    b.push_front(1);
    b.pop_front(); 
    EXPECT_TRUE(a == b);
    ct::Deque<int> x{9}, y{7, 8};
    swap(x, y);
    ASSERT_EQ(x.size(), 2u);
    EXPECT_EQ(x[0], 7);
    ASSERT_EQ(y.size(), 1u);
    EXPECT_EQ(y[0], 9);
}

TEST(DequeMisc, IteradoresFuncionamComAlgoritmosStd) {
    ct::Deque<int> d;
    for (int i = 0; i < 5; ++i) d.push_front(i);
    for (int i = 5; i < 40; ++i) d.push_back(i);
    for (int i = 0; i < 10; ++i) d.pop_front();
    std::sort(d.begin(), d.end(), std::greater<int>());
    EXPECT_TRUE(std::is_sorted(d.begin(), d.end(), std::greater<int>()));
    std::reverse(d.begin(), d.end());
    EXPECT_TRUE(std::is_sorted(d.begin(), d.end()));
    EXPECT_EQ(std::distance(d.begin(), d.end()), static_cast<std::ptrdiff_t>(d.size()));
    std::sort(d.rbegin(), d.rend());
    EXPECT_TRUE(std::is_sorted(d.begin(), d.end(), std::greater<int>()));
    EXPECT_EQ(*std::min_element(d.begin(), d.end()), d.back());
}
