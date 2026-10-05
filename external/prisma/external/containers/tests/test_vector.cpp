#include <ct/vector.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

struct Tracked {
    static int live;
    int value;
    explicit Tracked(int v = 0) : value(v) { ++live; }
    Tracked(const Tracked& o) : value(o.value) { ++live; }
    Tracked(Tracked&& o) noexcept : value(o.value) { o.value = -1; ++live; }
    Tracked& operator=(const Tracked&) = default;
    Tracked& operator=(Tracked&&) = default;
    ~Tracked() { --live; }
    bool operator==(const Tracked& o) const { return value == o.value; }
};
int Tracked::live = 0;

class TrackedTest : public ::testing::Test {
protected:
    void SetUp() override { Tracked::live = 0; }
    void TearDown() override { EXPECT_EQ(Tracked::live, 0) << "leaked instances"; }
};

TEST(VectorBasic, DefaultConstructedIsEmpty) {
    ct::Vector<int> v;
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.size(), 0u);
    EXPECT_EQ(v.capacity(), 0u);
    EXPECT_EQ(v.data(), nullptr);
}

TEST(VectorBasic, CountConstructor) {
    ct::Vector<int> v(5);
    EXPECT_EQ(v.size(), 5u);
    for (int x : v)
        EXPECT_EQ(x, 0);
}

TEST(VectorBasic, CountValueConstructor) {
    ct::Vector<int> v(4, 7);
    EXPECT_EQ(v.size(), 4u);
    for (int x : v)
        EXPECT_EQ(x, 7);
}

TEST(VectorBasic, InitializerList) {
    ct::Vector<int> v{1, 2, 3, 4};
    ASSERT_EQ(v.size(), 4u);
    EXPECT_EQ(v[0], 1);
    EXPECT_EQ(v[3], 4);
}

TEST(VectorBasic, IteratorRangeConstructor) {
    std::vector<int> src{10, 20, 30};
    ct::Vector<int> v(src.begin(), src.end());
    ASSERT_EQ(v.size(), 3u);
    EXPECT_EQ(v[1], 20);
}

TEST(VectorBasic, PushBackGrows) {
    ct::Vector<int> v;
    for (int i = 0; i < 1000; ++i)
        v.push_back(i);
    ASSERT_EQ(v.size(), 1000u);
    for (int i = 0; i < 1000; ++i)
        ASSERT_EQ(v[i], i);
    EXPECT_GE(v.capacity(), 1000u);
}

TEST(VectorBasic, EmplaceBackReturnsReference) {
    ct::Vector<std::string> v;
    std::string& r = v.emplace_back(3, 'x');
    EXPECT_EQ(r, "xxx");
    EXPECT_EQ(&r, &v.back());
}

TEST(VectorBasic, PopBack) {
    ct::Vector<int> v{1, 2, 3};
    v.pop_back();
    EXPECT_EQ(v.size(), 2u);
    EXPECT_EQ(v.back(), 2);
}

TEST(VectorBasic, FrontBackData) {
    ct::Vector<int> v{5, 6, 7};
    EXPECT_EQ(v.front(), 5);
    EXPECT_EQ(v.back(), 7);
    EXPECT_EQ(v.data()[1], 6);
}

TEST(VectorBasic, AtAbortsOutOfRange) {
    ct::Vector<int> v{1};
    EXPECT_EQ(v.at(0), 1);
    EXPECT_DEATH(v.at(1), "fora dos limites");
    EXPECT_DEATH(v.at(std::size_t(-1)), "fora dos limites");
}

TEST(VectorCapacity, ReserveDoesNotChangeSize) {
    ct::Vector<int> v{1, 2};
    v.reserve(100);
    EXPECT_EQ(v.size(), 2u);
    EXPECT_GE(v.capacity(), 100u);
    EXPECT_EQ(v[0], 1);
    EXPECT_EQ(v[1], 2);
}

TEST(VectorCapacity, ReserveAvoidsReallocation) {
    ct::Vector<int> v;
    v.reserve(64);
    const int* p = v.data();
    for (int i = 0; i < 64; ++i)
        v.push_back(i);
    EXPECT_EQ(v.data(), p);
}

TEST(VectorCapacity, ShrinkToFit) {
    ct::Vector<int> v;
    v.reserve(100);
    v.push_back(1);
    v.shrink_to_fit();
    EXPECT_EQ(v.capacity(), 1u);
    EXPECT_EQ(v[0], 1);
    v.clear();
    v.shrink_to_fit();
    EXPECT_EQ(v.capacity(), 0u);
}

TEST(VectorCapacity, ResizeGrowAndShrink) {
    ct::Vector<int> v;
    v.resize(10, 3);
    EXPECT_EQ(v.size(), 10u);
    for (int x : v)
        EXPECT_EQ(x, 3);
    v.resize(4, 9);
    EXPECT_EQ(v.size(), 4u);
    EXPECT_EQ(v[3], 3);
}

TEST(VectorCopyMove, CopyConstruct) {
    ct::Vector<std::string> a{"a", "bb", "ccc"};
    ct::Vector<std::string> b(a);
    EXPECT_EQ(a, b);
    b[0] = "z";
    EXPECT_EQ(a[0], "a");
}

TEST(VectorCopyMove, EmptyCopyKeepsNullIterators) {
    const ct::Vector<int> source;
    ct::Vector<int> copy(source);
    EXPECT_TRUE(copy.empty());
    EXPECT_EQ(copy.data(), nullptr);
    EXPECT_EQ(copy.begin(), copy.end());
}

TEST(VectorCopyMove, CopyAssign) {
    ct::Vector<int> a{1, 2, 3};
    ct::Vector<int> b{9};
    b = a;
    EXPECT_EQ(a, b);
    ct::Vector<int> *self = &a;
    a = *self;
    EXPECT_EQ(a.size(), 3u);
}

TEST(VectorCopyMove, MoveConstruct) {
    ct::Vector<int> a{1, 2, 3};
    const int* p = a.data();
    ct::Vector<int> b(std::move(a));
    EXPECT_EQ(b.data(), p);
    EXPECT_EQ(b.size(), 3u);
    EXPECT_TRUE(a.empty());
    EXPECT_EQ(a.data(), nullptr);
}

TEST(VectorCopyMove, MoveAssign) {
    ct::Vector<std::string> a{"x", "y"};
    ct::Vector<std::string> b{"old"};
    b = std::move(a);
    ASSERT_EQ(b.size(), 2u);
    EXPECT_EQ(b[1], "y");
    EXPECT_TRUE(a.empty());
}

TEST(VectorInsertErase, InsertMiddle) {
    ct::Vector<int> v{1, 2, 4};
    auto it = v.insert(v.begin() + 2, 3);
    EXPECT_EQ(*it, 3);
    EXPECT_EQ(v, (ct::Vector<int>{1, 2, 3, 4}));
}

TEST(VectorInsertErase, InsertBeginEnd) {
    ct::Vector<int> v{2};
    v.insert(v.begin(), 1);
    v.insert(v.end(), 3);
    EXPECT_EQ(v, (ct::Vector<int>{1, 2, 3}));
}

TEST(VectorInsertErase, InsertTriggersGrowth) {
    ct::Vector<int> v;
    for (int i = 0; i < 100; ++i)
        v.insert(v.begin(), i);  
    EXPECT_EQ(v.size(), 100u);
    EXPECT_EQ(v[0], 99);
    EXPECT_EQ(v[99], 0);
}

TEST(VectorInsertErase, EmplaceWithElementReferenceSurvivesRelocation) {
    ct::Vector<std::string> v;
    v.reserve(8);
    for (int i = 0; i < 8; ++i)
        v.push_back("value_" + std::to_string(i));

    const std::string first = v.front();
    v.emplace_back(v.front());
    EXPECT_EQ(v.back(), first);

    // O argumento tambem pode referir um elemento que sera deslocado ao abrir o gap.
    v.shrink_to_fit();
    v.emplace(v.begin() + 2, v.back());
    EXPECT_EQ(v[2], first);

    // Mesmo sem realocar, abrir o gap move e destroi a cauda original.
    v.reserve(32);
    const std::string middle = v.back();
    v.emplace(v.begin() + 1, v.back());
    EXPECT_EQ(v[1], middle);
}

TEST(VectorInsertErase, EraseSingle) {
    ct::Vector<int> v{1, 2, 3, 4};
    auto it = v.erase(v.begin() + 1);
    EXPECT_EQ(*it, 3);
    EXPECT_EQ(v, (ct::Vector<int>{1, 3, 4}));
}

TEST(VectorInsertErase, EraseRange) {
    ct::Vector<int> v{1, 2, 3, 4, 5};
    auto it = v.erase(v.begin() + 1, v.begin() + 4);
    EXPECT_EQ(*it, 5);
    EXPECT_EQ(v, (ct::Vector<int>{1, 5}));
}

TEST(VectorInsertErase, EraseEmptyRangeIsNoop) {
    ct::Vector<int> v{1, 2};
    auto it = v.erase(v.begin() + 1, v.begin() + 1);
    EXPECT_EQ(*it, 2);
    EXPECT_EQ(v.size(), 2u);
}

TEST_F(TrackedTest, PushBackAndDestroy) {
    {
        ct::Vector<Tracked> v;
        for (int i = 0; i < 100; ++i)
            v.emplace_back(i);
        EXPECT_EQ(Tracked::live, 100);
        EXPECT_EQ(v[50].value, 50);
    }
    EXPECT_EQ(Tracked::live, 0);
}

TEST_F(TrackedTest, ClearDestroysAll) {
    ct::Vector<Tracked> v;
    for (int i = 0; i < 10; ++i)
        v.emplace_back(i);
    v.clear();
    EXPECT_EQ(Tracked::live, 0);
    EXPECT_TRUE(v.empty());
}

TEST_F(TrackedTest, ResizeShrinkDestroys) {
    ct::Vector<Tracked> v;
    for (int i = 0; i < 10; ++i)
        v.emplace_back(i);
    v.resize(3);
    EXPECT_EQ(Tracked::live, 3);
    EXPECT_EQ(v[2].value, 2);
}

TEST_F(TrackedTest, EraseDestroys) {
    ct::Vector<Tracked> v;
    for (int i = 0; i < 5; ++i)
        v.emplace_back(i);
    v.erase(v.begin() + 1, v.begin() + 3);
    EXPECT_EQ(Tracked::live, 3);
    EXPECT_EQ(v[0].value, 0);
    EXPECT_EQ(v[1].value, 3);
    EXPECT_EQ(v[2].value, 4);
}

TEST_F(TrackedTest, GrowthRelocatesCorrectly) {
    ct::Vector<Tracked> v;
    for (int i = 0; i < 1000; ++i)
        v.emplace_back(i);
    EXPECT_EQ(Tracked::live, 1000);
    for (int i = 0; i < 1000; ++i)
        ASSERT_EQ(v[i].value, i);
}

TEST_F(TrackedTest, CopyAndMoveSemantics) {
    ct::Vector<Tracked> a;
    for (int i = 0; i < 8; ++i)
        a.emplace_back(i);
    ct::Vector<Tracked> b(a);
    EXPECT_EQ(Tracked::live, 16);
    ct::Vector<Tracked> c(std::move(a));
    EXPECT_EQ(Tracked::live, 16);
    EXPECT_EQ(c[7].value, 7);
}

TEST(VectorString, ManyStrings) {
    ct::Vector<std::string> v;
    for (int i = 0; i < 500; ++i)
        v.push_back("string_" + std::to_string(i));
    ASSERT_EQ(v.size(), 500u);
    EXPECT_EQ(v[123], "string_123");
    v.erase(v.begin());
    EXPECT_EQ(v[0], "string_1");
}

TEST(VectorVsStd, SameResultsRandomOps) {
    ct::Vector<int> a;
    std::vector<int> b;
    unsigned seed = 12345;
    for (int i = 0; i < 5000; ++i) {
        seed = seed * 1664525u + 1013904223u;
        int op = seed % 4;
        int val = static_cast<int>(seed >> 8);
        if (op == 0 || b.size() < 2) {
            a.push_back(val);
            b.push_back(val);
        } else if (op == 1) {
            a.pop_back();
            b.pop_back();
        } else if (op == 2) {
            size_t pos = val % b.size();
            a.insert(a.begin() + pos, val);
            b.insert(b.begin() + pos, val);
        } else {
            size_t pos = val % b.size();
            a.erase(a.begin() + pos);
            b.erase(b.begin() + pos);
        }
        ASSERT_EQ(a.size(), b.size());
    }
    ASSERT_TRUE(std::equal(a.begin(), a.end(), b.begin()));
}

TEST(VectorAlgo, SortAndReverseIterators) {
    ct::Vector<int> v{5, 3, 1, 4, 2};
    std::sort(v.begin(), v.end());
    EXPECT_EQ(v, (ct::Vector<int>{1, 2, 3, 4, 5}));
    ct::Vector<int> r(v.rbegin(), v.rend());
    EXPECT_EQ(r, (ct::Vector<int>{5, 4, 3, 2, 1}));
}

TEST(VectorAlgo, Swap) {
    ct::Vector<int> a{1, 2};
    ct::Vector<int> b{3};
    swap(a, b);
    EXPECT_EQ(a, (ct::Vector<int>{3}));
    EXPECT_EQ(b, (ct::Vector<int>{1, 2}));
}
