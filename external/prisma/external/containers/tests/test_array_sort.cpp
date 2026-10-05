#include <ct/array.hpp>
#include <ct/sort.hpp>
#include <ct/string.hpp>
#include <ct/vector.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

TEST(Array, BasicsAndAggregateInit)
{
    ct::Array<int, 4> a = {{10, 20, 30, 40}};
    EXPECT_EQ(a.size(), 4u);
    EXPECT_FALSE(a.empty());
    EXPECT_EQ(a[2], 30);
    EXPECT_EQ(a.front(), 10);
    EXPECT_EQ(a.back(), 40);
    EXPECT_EQ(a.at(1), 20);
    EXPECT_DEATH(a.at(4), "fora dos limites");
    a[0] = 99;
    EXPECT_EQ(*a.data(), 99);
}

TEST(Array, IterationAndFill)
{
    ct::Array<int, 5> a;
    a.fill(7);
    int sum = 0;
    for (int x : a)
        sum += x;
    EXPECT_EQ(sum, 35);

    ct::Array<int, 3> b = {{1, 2, 3}};
    auto it = b.rbegin();
    EXPECT_EQ(*it, 3);
    ++it;
    EXPECT_EQ(*it, 2);
}

TEST(Array, EqualityAndSwap)
{
    ct::Array<int, 3> a = {{1, 2, 3}};
    ct::Array<int, 3> b = {{1, 2, 3}};
    ct::Array<int, 3> c = {{4, 5, 6}};
    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);
    a.swap(c);
    EXPECT_EQ(a[0], 4);
    EXPECT_EQ(c[0], 1);
}

TEST(Array, WorksWithCtSort)
{
    ct::Array<int, 6> a = {{5, 2, 6, 1, 4, 3}};
    ct::sort(a.begin(), a.end());
    for (int i = 0; i < 6; ++i)
        EXPECT_EQ(a[i], i + 1);
}

namespace
{

    static_assert(sizeof(ct::Array<int, 8>) == 8 * sizeof(int), "sem overhead");
    static_assert(sizeof(ct::Array<char, 3>) == 3, "sem overhead");
    static_assert(alignof(ct::Array<double, 3>) == alignof(double), "alinhamento do T");
    static_assert(std::is_trivial<ct::Array<int, 4>>::value, "tem de continuar trivial");
    static_assert(std::is_trivially_copyable<ct::Array<int, 4>>::value, "memcpy-able");
    static_assert(std::is_standard_layout<ct::Array<int, 4>>::value, "standard layout");

    constexpr ct::Array<int, 4> kConst = {{1, 2, 3, 4}};
    static_assert(kConst[2] == 3, "operator[] constexpr");
    static_assert(kConst.at(0) == 1, "at() constexpr");
    static_assert(kConst.front() == 1 && kConst.back() == 4, "front/back constexpr");
    static_assert(kConst.size() == 4 && !kConst.empty(), "size/empty constexpr");
    static_assert(ct::Array<int, 4>::size() == 4, "size() é estático");
    static_assert(*(kConst.begin() + 1) == 2, "begin() constexpr");
    static_assert(kConst.end() - kConst.begin() == 4, "end() constexpr");

    constexpr int constexpr_build()
    {
        ct::Array<int, 4> a = {{0, 0, 0, 0}};
        for (int i = 0; i < 4; ++i)
            a[i] = i * i;
        a.at(3) = 100;
        int sum = 0;
        for (int v : a)
            sum += v;
        return sum; 
    }
    static_assert(constexpr_build() == 105, "escrita constexpr");

    enum class Color : unsigned { Red = 1, Green = 2 };

    struct Byte1
    {
        unsigned char v;
        bool operator==(const Byte1 &o) const { return v == o.v; }
    };
    static_assert(sizeof(Byte1) == 1, "");

    struct Padded
    {
        char c;
        int i;
        bool operator==(const Padded &o) const { return c == o.c && i == o.i; }
    };

    struct Vec2
    {
        float x, y;
        bool operator==(const Vec2 &o) const { return x == o.x && y == o.y; }
    };
}

TEST(Array, AggregateInitForms)
{
    ct::Array<int, 3> a = {{1, 2, 3}}; 
    ct::Array<int, 3> b = {1, 2, 3};   
    ct::Array<int, 3> c{{1, 2, 3}};
    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a == c);

    ct::Array<int, 4> partial = {{7, 8}}; 
    EXPECT_EQ(partial[0], 7);
    EXPECT_EQ(partial[1], 8);
    EXPECT_EQ(partial[2], 0);
    EXPECT_EQ(partial[3], 0);

    ct::Array<int, 3> zeroed{};
    EXPECT_EQ(zeroed[0], 0);
    EXPECT_EQ(zeroed[2], 0);
}

TEST(Array, ContiguousAndSizeOne)
{
    ct::Array<int, 4> a = {{1, 2, 3, 4}};
    EXPECT_EQ(a.data(), &a[0]);
    EXPECT_EQ(&a[1] - &a[0], 1);
    EXPECT_EQ(a.end() - a.begin(), 4);
    EXPECT_EQ(reinterpret_cast<const void *>(&a), reinterpret_cast<const void *>(a.data()));

    ct::Array<int, 1> one = {{42}};
    EXPECT_EQ(&one.front(), &one.back());
    EXPECT_EQ(one.begin() + 1, one.end());
    EXPECT_EQ(one.at(0), 42);
}

TEST(Array, AtBoundsEdges)
{
    ct::Array<int, 3> a = {{1, 2, 3}};
    const ct::Array<int, 3> &ca = a;

    EXPECT_EQ(a.at(2), 3);  
    EXPECT_EQ(ca.at(2), 3);
    EXPECT_DEATH(a.at(3), "fora dos limites");                            
    EXPECT_DEATH(ca.at(3), "fora dos limites");                           
    EXPECT_DEATH(a.at(static_cast<std::size_t>(-1)), "fora dos limites"); 
    EXPECT_DEATH(a.at(1u << 30), "fora dos limites");

    a.at(0) = 99; 
    EXPECT_EQ(a[0], 99);
}

TEST(Array, FillSelfReference)
{
    ct::Array<int, 8> a;
    for (int i = 0; i < 8; ++i)
        a[i] = i + 1;
    a.fill(a[0]); 
    for (int i = 0; i < 8; ++i)
        EXPECT_EQ(a[i], 1);
}

TEST(Array, FillOneByteTypesGoesThroughMemset)
{
    ct::Array<unsigned char, 7> u;
    u.fill(0xAB);
    for (int i = 0; i < 7; ++i)
        EXPECT_EQ(u[i], 0xAB);

    ct::Array<char, 4> c;
    c.fill('x');
    EXPECT_EQ(std::memcmp(c.data(), "xxxx", 4), 0);

    ct::Array<bool, 5> b;
    b.fill(true);
    for (int i = 0; i < 5; ++i)
        EXPECT_TRUE(b[i]);
    b.fill(false);
    for (int i = 0; i < 5; ++i)
        EXPECT_FALSE(b[i]);

    ct::Array<signed char, 3> sc;
    sc.fill(-3);
    for (int i = 0; i < 3; ++i)
        EXPECT_EQ(sc[i], -3);

    ct::Array<Byte1, 6> s;
    s.fill(Byte1{0x5A});
    for (int i = 0; i < 6; ++i)
        EXPECT_EQ(s[i].v, 0x5A);
}

TEST(Array, FillLargeTrivialAndNonTrivial)
{
    ct::Array<int, 1024> big;
    big.fill(-1);
    long long sum = 0;
    for (int v : big)
        sum += v;
    EXPECT_EQ(sum, -1024);

    ct::Array<double, 3> d;
    d.fill(1.5);
    EXPECT_DOUBLE_EQ(d[0] + d[1] + d[2], 4.5);

    ct::Array<std::string, 3> s; 
    s.fill("uma string grande o suficiente para nao caber em SSO");
    for (int i = 0; i < 3; ++i)
        EXPECT_EQ(s[i], "uma string grande o suficiente para nao caber em SSO");

    ct::Array<Vec2, 4> v;
    v.fill(Vec2{1.0f, 2.0f});
    for (int i = 0; i < 4; ++i)
        EXPECT_TRUE((v[i] == Vec2{1.0f, 2.0f}));
}

TEST(Array, EqualityFloatEdgeCasesNotBytewise)
{

    ct::Array<double, 2> a = {{0.0, 1.0}};
    ct::Array<double, 2> b = {{-0.0, 1.0}};
    ASSERT_NE(std::memcmp(a.data(), b.data(), sizeof(a)), 0);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);

    const double nan = std::numeric_limits<double>::quiet_NaN();
    ct::Array<double, 1> n1 = {{nan}};
    ct::Array<double, 1> n2 = n1;
    ASSERT_EQ(std::memcmp(n1.data(), n2.data(), sizeof(n1)), 0);
    EXPECT_FALSE(n1 == n2);
    EXPECT_FALSE(n1 == n1);
    EXPECT_TRUE(n1 != n2);

    ct::Array<float, 2> f1 = {{0.0f, -0.0f}};
    ct::Array<float, 2> f2 = {{-0.0f, 0.0f}};
    EXPECT_TRUE(f1 == f2);
}

TEST(Array, EqualityBytewiseTypes)
{
    ct::Array<int, 4> a = {{1, 2, 3, 4}};
    ct::Array<int, 4> b = {{1, 2, 3, 4}};
    ct::Array<int, 4> c = {{1, 2, 3, 5}}; 
    ct::Array<int, 4> d = {{9, 2, 3, 4}}; 
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
    EXPECT_FALSE(a == d);
    EXPECT_TRUE(a != c);

    int x = 0, y = 0;
    ct::Array<int *, 2> p1 = {{&x, &y}};
    ct::Array<int *, 2> p2 = {{&x, &y}};
    ct::Array<int *, 2> p3 = {{&y, &x}};
    EXPECT_TRUE(p1 == p2);
    EXPECT_TRUE(p1 != p3);

    ct::Array<Color, 2> e1 = {{Color::Red, Color::Green}};
    ct::Array<Color, 2> e2 = {{Color::Red, Color::Green}};
    ct::Array<Color, 2> e3 = {{Color::Green, Color::Green}};
    EXPECT_TRUE(e1 == e2);
    EXPECT_TRUE(e1 != e3);

    ct::Array<std::uint64_t, 3> u1 = {{1, 2, 3}};
    ct::Array<std::uint64_t, 3> u2 = {{1, 2, 3}};
    EXPECT_TRUE(u1 == u2);
}

TEST(Array, EqualityIgnoresStructPadding)
{

    ct::Array<Padded, 2> a;
    ct::Array<Padded, 2> b;
    std::memset(static_cast<void *>(&a), 0x00, sizeof(a));
    std::memset(static_cast<void *>(&b), 0xFF, sizeof(b));
    for (int i = 0; i < 2; ++i)
    {
        a[i].c = 'x';
        a[i].i = 42;
        b[i].c = 'x';
        b[i].i = 42;
    }
    ASSERT_NE(std::memcmp(&a, &b, sizeof(a)), 0);
    EXPECT_TRUE(a == b);

    ct::Array<std::string, 2> s1 = {{std::string("aa"), std::string("bb")}};
    ct::Array<std::string, 2> s2 = {{std::string("aa"), std::string("bb")}};
    EXPECT_TRUE(s1 == s2);
    s2[1] = "cc";
    EXPECT_TRUE(s1 != s2);
}

TEST(Array, OrderingLexicographic)
{
    ct::Array<int, 3> a = {{1, 2, 3}};
    ct::Array<int, 3> b = {{1, 2, 4}};
    ct::Array<int, 3> c = {{2, 0, 0}};
    ct::Array<int, 3> a2 = a;

    EXPECT_TRUE(a < b);
    EXPECT_FALSE(b < a);
    EXPECT_TRUE(b > a);
    EXPECT_TRUE(a < c); 
    EXPECT_FALSE(a < a2);
    EXPECT_FALSE(a > a2);
    EXPECT_TRUE(a <= a2);
    EXPECT_TRUE(a >= a2);
    EXPECT_TRUE(a <= b);
    EXPECT_TRUE(b >= a);

    ct::Array<std::string, 2> s1 = {{std::string("ab"), std::string("c")}};
    ct::Array<std::string, 2> s2 = {{std::string("ab"), std::string("d")}};
    EXPECT_TRUE(s1 < s2);
}

TEST(Array, OrderingSignedBytesNotMemcmp)
{

    ct::Array<signed char, 1> n = {{-1}};
    ct::Array<signed char, 1> p = {{1}};
    EXPECT_TRUE(n < p);
    EXPECT_FALSE(p < n);

    ct::Array<signed char, 3> s1 = {{0, -128, 0}};
    ct::Array<signed char, 3> s2 = {{0, 127, 0}};
    EXPECT_TRUE(s1 < s2);

    ct::Array<unsigned char, 1> u200 = {{200}};
    ct::Array<unsigned char, 1> u5 = {{5}};
    EXPECT_TRUE(u5 < u200);
    EXPECT_FALSE(u200 < u5);

    ct::Array<unsigned char, 3> pre1 = {{1, 5, 9}};
    ct::Array<unsigned char, 3> pre2 = {{1, 200, 0}}; 
    EXPECT_TRUE(pre1 < pre2);

    ct::Array<unsigned char, 2> eq1 = {{7, 7}};
    ct::Array<unsigned char, 2> eq2 = {{7, 7}};
    EXPECT_FALSE(eq1 < eq2);
    EXPECT_TRUE(eq1 <= eq2);

    ct::Array<int, 2> i1 = {{-5, 0}};
    ct::Array<int, 2> i2 = {{5, 0}};
    EXPECT_TRUE(i1 < i2);
}

TEST(Array, SwapIncludingSelfAndAdl)
{
    ct::Array<int, 3> a = {{1, 2, 3}};
    ct::Array<int, 3> b = {{4, 5, 6}};
    a.swap(b);
    EXPECT_EQ(a[0], 4);
    EXPECT_EQ(b[2], 3);

    swap(a, b); 
    EXPECT_EQ(a[0], 1);
    EXPECT_EQ(b[0], 4);

    a.swap(a); 
    EXPECT_TRUE((a == ct::Array<int, 3>{{1, 2, 3}}));

    ct::Array<std::string, 2> s1 = {{std::string("um"), std::string("dois")}};
    ct::Array<std::string, 2> s2 = {{std::string("tres"), std::string("quatro")}};
    s1.swap(s2);
    EXPECT_EQ(s1[0], "tres");
    EXPECT_EQ(s2[1], "dois");
    s1.swap(s1);
    EXPECT_EQ(s1[0], "tres");
    EXPECT_EQ(s1[1], "quatro");
}

TEST(Array, ReverseIterators)
{
    ct::Array<int, 4> a = {{1, 2, 3, 4}};

    int seen[4] = {0, 0, 0, 0};
    int n = 0;
    for (auto it = a.rbegin(); it != a.rend(); ++it)
        seen[n++] = *it;
    EXPECT_EQ(n, 4);
    EXPECT_EQ(seen[0], 4);
    EXPECT_EQ(seen[3], 1);

    *a.rbegin() = 99; 
    EXPECT_EQ(a[3], 99);

    const ct::Array<int, 4> &ca = a;
    n = 0;
    for (auto it = ca.rbegin(); it != ca.rend(); ++it)
        ++n;
    EXPECT_EQ(n, 4);
    EXPECT_EQ(*ca.crbegin(), 99);

    n = 0;
    for (auto it = ca.crbegin(); it != ca.crend(); ++it)
        ++n;
    EXPECT_EQ(n, 4);

    auto it = a.rbegin();
    ++it;
    --it; 
    EXPECT_EQ(*it, 99);
    EXPECT_EQ(it.base(), a.end());

    ct::Array<int, 1> one = {{5}};
    EXPECT_EQ(*one.rbegin(), 5);
    EXPECT_TRUE(++one.rbegin() == one.rend());
}

TEST(Array, ConstIterationAndCbegin)
{
    const ct::Array<int, 3> a = {{1, 2, 3}};
    int sum = 0;
    for (int v : a)
        sum += v;
    EXPECT_EQ(sum, 6);
    EXPECT_EQ(a.cend() - a.cbegin(), 3);
    EXPECT_EQ(*a.cbegin(), 1);
    static_assert(std::is_same<decltype(a.begin()), const int *>::value, "const_iterator");
}

TEST(Array, NestedArrays)
{
    ct::Array<ct::Array<int, 2>, 3> grid = {{{{3, 1}}, {{1, 2}}, {{2, 9}}}};
    EXPECT_EQ(grid[0][0], 3);
    EXPECT_EQ(grid[2][1], 9);
    EXPECT_EQ(sizeof(grid), 6 * sizeof(int));

    ct::sort(grid.begin(), grid.end()); 
    EXPECT_TRUE((grid[0] == ct::Array<int, 2>{{1, 2}}));
    EXPECT_TRUE((grid[1] == ct::Array<int, 2>{{2, 9}}));
    EXPECT_TRUE((grid[2] == ct::Array<int, 2>{{3, 1}}));

    grid[1].fill(0);
    EXPECT_EQ(grid[1][0], 0);
    EXPECT_EQ(grid[1][1], 0);
}

TEST(Array, CopyAndAssignAreBitwise)
{
    ct::Array<int, 5> a = {{1, 2, 3, 4, 5}};
    ct::Array<int, 5> b = a; 
    EXPECT_TRUE(a == b);
    b[0] = 9;
    EXPECT_EQ(a[0], 1); 
    a = b;              
    EXPECT_TRUE(a == b);

    ct::Array<std::string, 2> s = {{std::string("a"), std::string("b")}};
    ct::Array<std::string, 2> t = s;
    EXPECT_EQ(t[1], "b");
    t[1] = "z";
    EXPECT_EQ(s[1], "b");
}

namespace
{
    unsigned rng_state = 12345;
    unsigned rng()
    {
        return rng_state = rng_state * 1664525u + 1013904223u;
    }

    template <typename T, typename Gen>
    void check_sort_matches_std(std::size_t n, Gen gen)
    {
        std::vector<T> ref;
        ct::Vector<T> v;
        for (std::size_t i = 0; i < n; ++i)
        {
            T x = gen(i);
            ref.push_back(x);
            v.push_back(x);
        }
        std::sort(ref.begin(), ref.end());
        ct::sort(v.begin(), v.end());
        for (std::size_t i = 0; i < n; ++i)
            ASSERT_EQ(v[i], ref[i]) << "n=" << n << " i=" << i;
    }
} 

TEST(Sort, IntAllSizesAndPatterns)
{

    for (std::size_t n : {0u, 1u, 2u, 3u, 23u, 24u, 25u, 127u, 128u, 129u, 1000u, 100000u})
    {
        check_sort_matches_std<int>(n, [](std::size_t) { return static_cast<int>(rng()); });
        check_sort_matches_std<int>(n, [](std::size_t i) { return static_cast<int>(i); });      
        check_sort_matches_std<int>(n, [n](std::size_t i) { return static_cast<int>(n - i); }); 
        check_sort_matches_std<int>(n, [](std::size_t) { return static_cast<int>(rng() % 4); }); 
        check_sort_matches_std<int>(n, [](std::size_t) { return 7; });                           
    }
}

TEST(Sort, NegativeIntsAndLimits)
{
    check_sort_matches_std<int>(50000, [](std::size_t) { return static_cast<int>(rng()); });
    ct::Vector<int> v{-2147483647 - 1, 2147483647, 0, -1, 1, -2147483647, 42};
    ct::sort(v.begin(), v.end());
    EXPECT_EQ(v[0], -2147483647 - 1);
    EXPECT_EQ(v.back(), 2147483647);
    EXPECT_EQ(v[3], 0);
}

TEST(Sort, OtherIntegerTypes)
{
    check_sort_matches_std<unsigned>(10000, [](std::size_t) { return rng(); });
    check_sort_matches_std<long long>(10000, [](std::size_t) {
        return (static_cast<long long>(rng()) << 32) ^ rng();
    });
    check_sort_matches_std<unsigned long long>(10000, [](std::size_t) {
        return (static_cast<unsigned long long>(rng()) << 32) | rng();
    });
    check_sort_matches_std<short>(10000, [](std::size_t) { return static_cast<short>(rng()); });
}

TEST(Sort, Floats)
{
    check_sort_matches_std<float>(50000, [](std::size_t) {
        return (static_cast<float>(rng()) / 1e6f - 2000.0f);
    });

    ct::Vector<float> v;
    for (int i = 0; i < 300; ++i)
        v.push_back(static_cast<float>(300 - i) * (i % 2 ? -1.0f : 1.0f));
    v.push_back(0.0f);
    v.push_back(-0.0f);
    ct::sort(v.begin(), v.end());
    for (std::size_t i = 1; i < v.size(); ++i)
        ASSERT_LE(v[i - 1], v[i]);
}

TEST(Sort, Doubles)
{
    check_sort_matches_std<double>(50000, [](std::size_t) {
        return static_cast<double>(rng()) / 1e3 - 2e6;
    });
}

TEST(Sort, CustomComparator)
{
    ct::Vector<int> v;
    for (int i = 0; i < 5000; ++i)
        v.push_back(static_cast<int>(rng()));
    ct::sort(v.begin(), v.end(), [](int a, int b) { return a > b; }); 
    for (std::size_t i = 1; i < v.size(); ++i)
        ASSERT_GE(v[i - 1], v[i]);
}

TEST(Sort, Strings)
{
    ct::Vector<ct::String> v;
    std::vector<std::string> ref;
    for (int i = 0; i < 3000; ++i)
    {
        unsigned r = rng();
        std::string s;
        for (unsigned j = 0; j < 3 + r % 20; ++j)
            s.push_back(char('a' + (r >> (j % 8)) % 26));
        ref.push_back(s);
        v.emplace_back(s.data(), s.size());
    }
    std::sort(ref.begin(), ref.end());
    ct::sort(v.begin(), v.end());
    for (std::size_t i = 0; i < ref.size(); ++i)
        ASSERT_TRUE(v[i] == ref[i].c_str()) << i;
}

TEST(Sort, StructByField)
{
    struct P
    {
        float depth;
        int id;
    };
    ct::Vector<P> v;
    for (int i = 0; i < 10000; ++i)
        v.push_back(P{static_cast<float>(rng() % 100000) / 100.0f, i});
    ct::sort(v.begin(), v.end(), [](const P &a, const P &b) { return a.depth < b.depth; });
    for (std::size_t i = 1; i < v.size(); ++i)
        ASSERT_LE(v[i - 1].depth, v[i].depth);
}

TEST(Sort, KillerPatternForQuicksort)
{

    ct::Vector<int> v;
    for (int i = 0; i < 50000; ++i)
        v.push_back(i % 100);
    for (int i = 0; i < 50000; ++i)
        v.push_back((50000 - i) % 100);
    std::vector<int> ref(v.begin(), v.end());
    std::sort(ref.begin(), ref.end());
    ct::sort(v.begin(), v.end(), [](int a, int b) { return a < b; }); 
    for (std::size_t i = 0; i < ref.size(); ++i)
        ASSERT_EQ(v[i], ref[i]);
}

TEST(Sort, PlainCArray)
{
    int a[300];
    for (int i = 0; i < 300; ++i)
        a[i] = static_cast<int>(rng());
    ct::sort(a, a + 300);
    for (int i = 1; i < 300; ++i)
        ASSERT_LE(a[i - 1], a[i]);
}

namespace
{
    struct Rec
    {
        int key;
        int seq;
    };
    bool rec_less(const Rec &a, const Rec &b) { return a.key < b.key; }
}

TEST(StableSort, KeepsOrderOfEqualKeysAllSizes)
{
    for (std::size_t n : {0u, 1u, 2u, 23u, 24u, 25u, 47u, 48u, 49u, 97u, 1000u, 4097u, 100000u})
    {
        std::vector<Rec> ref;
        ct::Vector<Rec> v;
        for (std::size_t i = 0; i < n; ++i)
        {
            Rec r{static_cast<int>(rng() % 16), static_cast<int>(i)};
            ref.push_back(r);
            v.push_back(r);
        }
        std::stable_sort(ref.begin(), ref.end(), rec_less);
        ct::stable_sort(v.begin(), v.end(), rec_less);
        for (std::size_t i = 0; i < n; ++i)
        {
            ASSERT_EQ(v[i].key, ref[i].key) << "n=" << n << " i=" << i;
            ASSERT_EQ(v[i].seq, ref[i].seq) << "n=" << n << " i=" << i;
        }
    }
}

TEST(StableSort, MatchesStdOnPatterns)
{
    for (std::size_t n : {3u, 24u, 25u, 128u, 129u, 1000u, 65536u})
    {
        std::vector<int> ref;
        ct::Vector<int> v;
        for (std::size_t i = 0; i < n; ++i)
        {
            int x = (n & 1) ? static_cast<int>(rng()) : static_cast<int>(n - i);
            ref.push_back(x);
            v.push_back(x);
        }
        std::stable_sort(ref.begin(), ref.end());
        ct::stable_sort(v.begin(), v.end());
        for (std::size_t i = 0; i < n; ++i)
            ASSERT_EQ(v[i], ref[i]) << "n=" << n << " i=" << i;
    }
}

TEST(StableSort, NonTrivialElements)
{
    ct::Vector<ct::String> v;
    std::vector<std::string> ref;
    for (int i = 0; i < 3000; ++i)
    {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%03u-%d-a-rather-long-string-to-leave-the-sso", rng() % 50, i);
        v.push_back(ct::String(buf));
        ref.push_back(buf);
    }
    auto by_prefix = [](const ct::String &a, const ct::String &b) { return std::memcmp(a.c_str(), b.c_str(), 3) < 0; };
    auto by_prefix_std = [](const std::string &a, const std::string &b) { return std::memcmp(a.c_str(), b.c_str(), 3) < 0; };
    ct::stable_sort(v.begin(), v.end(), by_prefix);
    std::stable_sort(ref.begin(), ref.end(), by_prefix_std);
    for (std::size_t i = 0; i < ref.size(); ++i)
        ASSERT_STREQ(v[i].c_str(), ref[i].c_str()) << "i=" << i;
}
