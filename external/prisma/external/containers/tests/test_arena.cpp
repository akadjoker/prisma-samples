#include <ct/arena.hpp>
#include <ct/vector.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{

    struct ATracked
    {
        static int live;
        int value;
        explicit ATracked(int v = 0) : value(v) { ++live; }
        ATracked(const ATracked &o) : value(o.value) { ++live; }
        ATracked(ATracked &&o) noexcept : value(o.value)
        {
            o.value = -1;
            ++live;
        }
        ATracked &operator=(const ATracked &) = default;
        ATracked &operator=(ATracked &&) = default;
        ~ATracked() { --live; }
        bool operator==(const ATracked &o) const { return value == o.value; }
    };
    int ATracked::live = 0;

    bool is_aligned(const void *p, std::size_t align)
    {
        return (reinterpret_cast<std::uintptr_t>(p) & (align - 1)) == 0;
    }

} 

TEST(Arena, RespectsAlignment)
{
    ct::Arena a(1024);
    const std::size_t aligns[] = {1, 2, 4, 8, 16, 32, 64};

    for (int round = 0; round < 50; ++round)
        for (std::size_t al : aligns)
        {
            void *p = a.allocate(3 + round % 7, al);
            ASSERT_TRUE(is_aligned(p, al)) << "align " << al;
        }
}

TEST(Arena, AllocationsDoNotOverlap)
{
    ct::Arena a(256); 
    std::vector<std::pair<unsigned char *, std::size_t>> allocs;
    for (int i = 0; i < 2000; ++i)
    {
        std::size_t n = 1 + (i * 7919) % 97;
        unsigned char *p = static_cast<unsigned char *>(a.allocate(n, 1));
        std::memset(p, i & 0xFF, n); 
        allocs.emplace_back(p, n);
    }

    for (int i = 0; i < 2000; ++i)
    {
        unsigned char *p = allocs[i].first;
        for (std::size_t j = 0; j < allocs[i].second; ++j)
            ASSERT_EQ(p[j], static_cast<unsigned char>(i & 0xFF))
                << "corrupção na alocação " << i << " byte " << j;
    }
}

TEST(Arena, TryExpandOnLastAllocation)
{
    ct::Arena a(1024);
    void *p = a.allocate(64, 8);
    EXPECT_TRUE(a.try_expand(p, 64, 128)); 
    EXPECT_TRUE(a.try_expand(p, 128, 256));

    void *q = a.allocate(8, 8); 
    EXPECT_FALSE(a.try_expand(p, 256, 512)) << "p já não é a última alocação";
    EXPECT_TRUE(a.try_expand(q, 8, 16));
}

TEST(Arena, TryExpandFailsWhenBlockFull)
{
    ct::Arena a(128);
    void *p = a.allocate(64, 8);
    EXPECT_FALSE(a.try_expand(p, 64, 1 << 20)) << "não cabe no bloco";

    std::memset(p, 0xAB, 64);
    void *q = a.reallocate(p, 64, 1 << 20, 8);
    ASSERT_NE(q, nullptr);
    EXPECT_NE(q, p);
    for (int i = 0; i < 64; ++i)
        ASSERT_EQ(static_cast<unsigned char *>(q)[i], 0xAB);
}

TEST(Arena, LargeAllocationBiggerThanBlock)
{
    ct::Arena a(64);
    void *p = a.allocate(1 << 20, 16); 
    ASSERT_NE(p, nullptr);
    EXPECT_TRUE(a.owns(p));
    std::memset(p, 0xCD, 1 << 20); 
}

TEST(Arena, OwnsRejectsForeignPointers)
{
    ct::Arena a(1024);
    void *p = a.allocate(16, 8);
    EXPECT_TRUE(a.owns(p));
    int stack_var = 0;
    EXPECT_FALSE(a.owns(&stack_var));
    int *heap = new int(1);
    EXPECT_FALSE(a.owns(heap));
    delete heap;
}

TEST(Arena, ResetReusesMemory)
{
    ct::Arena a(256);
    for (int i = 0; i < 100; ++i)
        a.allocate(64, 8); 
    EXPECT_GT(a.bytes_used(), 0u);

    a.reset();
    EXPECT_EQ(a.bytes_used(), 0u);

    for (int cycle = 0; cycle < 3; ++cycle)
    {
        for (int i = 0; i < 100; ++i)
            a.allocate(64, 8);
        a.reset();
    }
    std::size_t stable = a.bytes_reserved();
    for (int i = 0; i < 100; ++i)
        a.allocate(64, 8);
    EXPECT_EQ(a.bytes_reserved(), stable)
        << "depois de estabilizar, a mesma carga não deve reservar mais";
}

TEST(Arena, UsedAndReservedAccounting)
{
    ct::Arena a(1024);
    EXPECT_EQ(a.bytes_used(), 0u);
    EXPECT_GE(a.bytes_reserved(), 1024u);
    a.allocate(100, 8);
    EXPECT_EQ(a.bytes_used(), 100u);
    void *p = a.allocate(50, 8);
    EXPECT_EQ(a.bytes_used(), 150u);
    a.try_expand(p, 50, 80);
    EXPECT_EQ(a.bytes_used(), 180u);
    EXPECT_LE(a.bytes_used(), a.bytes_reserved());
}

TEST(Arena, CreateConstructsObject)
{
    ct::Arena a;
    struct Node
    {
        int v;
        Node *next;
        Node(int v_, Node *n) : v(v_), next(n) {}
    };
    Node *head = nullptr;
    for (int i = 0; i < 1000; ++i)
        head = a.create<Node>(i, head);
    int expect = 999, count = 0;
    for (Node *n = head; n; n = n->next, --expect, ++count)
        ASSERT_EQ(n->v, expect);
    EXPECT_EQ(count, 1000);
}

TEST(Arena, AllocateArrayAligned)
{
    ct::Arena a;
    a.allocate(1, 1); 
    double *d = a.allocate_array<double>(10);
    EXPECT_TRUE(is_aligned(d, alignof(double)));
    for (int i = 0; i < 10; ++i)
        d[i] = i * 1.5;
    EXPECT_EQ(d[9], 13.5);
}

TEST(Arena, StressRandomSizesAndAligns)
{
    ct::Arena a(512);
    std::vector<std::pair<unsigned char *, std::size_t>> allocs;
    unsigned seed = 42;
    for (int i = 0; i < 10000; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        std::size_t n = 1 + (seed % 300);
        std::size_t al = std::size_t(1) << (seed >> 16) % 7; 
        unsigned char *p = static_cast<unsigned char *>(a.allocate(n, al));
        ASSERT_TRUE(is_aligned(p, al));
        std::memset(p, i & 0xFF, n);
        allocs.emplace_back(p, n);
    }
    for (int i = 0; i < 10000; ++i)
        for (std::size_t j = 0; j < allocs[i].second; ++j)
            ASSERT_EQ(allocs[i].first[j], static_cast<unsigned char>(i & 0xFF));
    EXPECT_LE(a.bytes_used(), a.bytes_reserved());
}

using AVec = ct::Vector<int, ct::ArenaAlloc>;

TEST(ArenaVector, PushBackGrowth)
{
    ct::Arena arena;
    AVec v{ct::ArenaAlloc(arena)};
    for (int i = 0; i < 100000; ++i)
        v.push_back(i);
    ASSERT_EQ(v.size(), 100000u);
    for (int i = 0; i < 100000; ++i)
        ASSERT_EQ(v[i], i);
    EXPECT_TRUE(arena.owns(v.data()));
}

TEST(ArenaVector, GrowthIsInPlaceWhenAlone)
{

    ct::Arena arena(1 << 20);
    AVec v{ct::ArenaAlloc(arena)};
    v.push_back(0);
    const int *p0 = v.data();
    for (int i = 1; i < 10000; ++i) 
        v.push_back(i);
    EXPECT_EQ(v.data(), p0) << "devia ter crescido in-place na arena";
}

TEST(ArenaVector, TwoVectorsInterleaved)
{

    ct::Arena arena(1024);
    AVec a{ct::ArenaAlloc(arena)};
    AVec b{ct::ArenaAlloc(arena)};
    for (int i = 0; i < 5000; ++i)
    {
        a.push_back(i);
        b.push_back(-i);
    }
    for (int i = 0; i < 5000; ++i)
    {
        ASSERT_EQ(a[i], i);
        ASSERT_EQ(b[i], -i);
    }
}

TEST(ArenaVector, NonTrivialTypeNoLeaks)
{
    ATracked::live = 0;
    ct::Arena arena;
    {
        ct::Vector<ATracked, ct::ArenaAlloc> v{ct::ArenaAlloc(arena)};
        for (int i = 0; i < 1000; ++i)
            v.emplace_back(i);
        EXPECT_EQ(ATracked::live, 1000);
        for (int i = 0; i < 1000; ++i)
            ASSERT_EQ(v[i].value, i);
        v.resize(10);
        EXPECT_EQ(ATracked::live, 10);
    }

    EXPECT_EQ(ATracked::live, 0);
}

TEST(ArenaVector, Strings)
{
    ct::Arena arena;
    ct::Vector<std::string, ct::ArenaAlloc> v{ct::ArenaAlloc(arena)};
    for (int i = 0; i < 2000; ++i)
        v.push_back("value_" + std::to_string(i));
    ASSERT_EQ(v.size(), 2000u);
    EXPECT_EQ(v[1234], "value_1234");
    v.erase(v.begin(), v.begin() + 1000);
    EXPECT_EQ(v[0], "value_1000");
}

TEST(ArenaVector, CopyAndMove)
{
    ct::Arena arena;
    AVec a{ct::ArenaAlloc(arena)};
    for (int i = 0; i < 100; ++i)
        a.push_back(i);

    AVec b(a); 
    EXPECT_TRUE(arena.owns(b.data()));
    EXPECT_EQ(a, b);
    b[0] = 999;
    EXPECT_EQ(a[0], 0);

    AVec c(std::move(a));
    EXPECT_EQ(c.size(), 100u);
    EXPECT_TRUE(a.empty());
    EXPECT_TRUE(arena.owns(c.data()));
}

TEST(ArenaVector, FuzzVsStd)
{
    ct::Arena arena;
    ct::Vector<int, ct::ArenaAlloc> a{ct::ArenaAlloc(arena)};
    std::vector<int> b;
    unsigned seed = 777;
    for (int i = 0; i < 5000; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        int op = seed % 4;
        int val = static_cast<int>(seed >> 8);
        if (op == 0 || b.size() < 2)
        {
            a.push_back(val);
            b.push_back(val);
        }
        else if (op == 1)
        {
            a.pop_back();
            b.pop_back();
        }
        else if (op == 2)
        {
            size_t pos = val % b.size();
            a.insert(a.begin() + pos, val);
            b.insert(b.begin() + pos, val);
        }
        else
        {
            size_t pos = val % b.size();
            a.erase(a.begin() + pos);
            b.erase(b.begin() + pos);
        }
        ASSERT_EQ(a.size(), b.size());
    }
    ASSERT_TRUE(std::equal(a.begin(), a.end(), b.begin()));
}

TEST(ArenaVector, MixedWithHeapVector)
{

    ct::Arena arena;
    ct::Vector<int, ct::ArenaAlloc> a{ct::ArenaAlloc(arena)};
    ct::Vector<int> h;
    for (int i = 0; i < 50; ++i)
    {
        a.push_back(i);
        h.push_back(i);
    }
    EXPECT_TRUE(a == h);
    h.push_back(99);
    EXPECT_TRUE(a != h);
}