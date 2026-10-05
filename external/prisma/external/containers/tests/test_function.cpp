#include <ct/function.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <utility>

using ct::Function;

namespace
{
    int add(int a, int b) { return a + b; }

    struct Counter
    {
        int n = 0;
        int operator()(int x) { n += x; return n; }
    };

    struct BigCapture
    {
        char pad[64];
        int tag = 7;
        int operator()() const { return tag; }
    };

    struct CopyCounter
    {
        static int copies;
        static int moves;
        int value;
        explicit CopyCounter(int v) : value(v) {}
        CopyCounter(const CopyCounter &o) : value(o.value) { ++copies; }
        CopyCounter(CopyCounter &&o) noexcept : value(o.value) { ++moves; }
        int operator()() const { return value; }
    };
    int CopyCounter::copies = 0;
    int CopyCounter::moves = 0;
}

TEST(Function, DefaultIsEmpty)
{
    Function<int(int)> f;
    EXPECT_FALSE(f);
    EXPECT_TRUE(f == nullptr);
    EXPECT_TRUE(nullptr == f);
}

TEST(Function, NullptrConstruction)
{
    Function<int(int)> f = nullptr;
    EXPECT_FALSE(f);
}

TEST(Function, FreeFunction)
{
    Function<int(int, int)> f = add;
    EXPECT_TRUE(f);
    EXPECT_EQ(f(2, 3), 5);
}

TEST(Function, LambdaWithCapture)
{
    int base = 100;
    Function<int(int)> f = [base](int x) { return base + x; };
    EXPECT_EQ(f(5), 105);
}

TEST(Function, FunctorCopiedIntoSbo)
{
    Counter c;
    Function<int(int)> f = c; 
    EXPECT_EQ(f(1), 1);
    EXPECT_EQ(f(2), 3);
    EXPECT_EQ(c.n, 0); 
}

TEST(Function, LargeCallableGoesToHeap)
{
    static_assert(sizeof(BigCapture) > Function<int()>::kSboSize, "teste pressupoe isto");
    Function<int()> f = BigCapture{};
    EXPECT_EQ(f(), 7);
}

TEST(Function, CallingEmptyIsFatal)
{
    Function<void()> f;
    EXPECT_DEATH(f(), "");
}

TEST(Function, CopyIsIndependent)
{
    int base = 1;
    Function<int(int)> a = [base](int x) { return base + x; };
    Function<int(int)> b = a;
    b = [](int x) { return x * 2; };
    EXPECT_EQ(a(5), 6);
    EXPECT_EQ(b(5), 10);
}

TEST(Function, CopyConstructsTarget)
{
    CopyCounter::copies = 0;
    Function<int()> a = CopyCounter(3); 
    CopyCounter::copies = 0;
    Function<int()> b = a; 
    EXPECT_EQ(CopyCounter::copies, 1);
    EXPECT_EQ(b(), 3);
}

TEST(Function, MoveLeavesSourceEmpty)
{
    Function<int(int)> a = [](int x) { return x + 1; };
    Function<int(int)> b(ct::detail::move(a));
    EXPECT_EQ(b(5), 6);
    EXPECT_FALSE(a);
}

TEST(Function, MoveAssignLeavesSourceEmpty)
{
    Function<int(int)> a = [](int x) { return x + 1; };
    Function<int(int)> b;
    b = ct::detail::move(a);
    EXPECT_EQ(b(5), 6);
    EXPECT_FALSE(a);
}

TEST(Function, MoveOfHeapTargetStealsPointerNoCopy)
{
    CopyCounter::copies = 0;
    CopyCounter::moves = 0;
    Function<int()> a = CopyCounter(9);
    CopyCounter::copies = 0;
    CopyCounter::moves = 0;
    Function<int()> b(ct::detail::move(a));
    EXPECT_EQ(b(), 9);
    EXPECT_FALSE(a);

}

TEST(Function, ReassignAfterMoveWorks)
{
    Function<int(int)> a = [](int x) { return x + 1; };
    Function<int(int)> b(ct::detail::move(a));
    a = [](int x) { return x * 3; };
    EXPECT_EQ(a(5), 15);
}

TEST(Function, Swap)
{
    Function<int(int)> a = [](int x) { return x + 1; };
    Function<int(int)> b = [](int x) { return x * 10; };
    a.swap(b);
    EXPECT_EQ(a(5), 50);
    EXPECT_EQ(b(5), 6);
}

TEST(Function, ResetClearsTarget)
{
    Function<int(int)> f = [](int x) { return x; };
    EXPECT_TRUE(f);
    f.reset();
    EXPECT_FALSE(f);
    f = nullptr;
    EXPECT_FALSE(f);
}

TEST(Function, VoidReturn)
{
    int seen = 0;
    Function<void(int)> f = [&seen](int x) { seen = x; };
    f(42);
    EXPECT_EQ(seen, 42);
}
namespace
{
    struct alignas(16) Vec4
    {
        float v[4];
    };

    struct SomaVec4
    {
        Vec4 a;
        float operator()() const { return a.v[0] + a.v[1] + a.v[2] + a.v[3]; }
    };

    struct ComPrefixo
    {
        std::uint64_t antes;
        Function<float()> f;
    };
}

TEST(Function, SboRespeitaAlinhamentoDoAlvo)
{
    static_assert(sizeof(SomaVec4) <= Function<float()>::kSboSize, "teste pressupoe SBO");
    static_assert(alignof(Function<float()>) >= alignof(SomaVec4), "SBO tem de alinhar o alvo");
    ComPrefixo h{1, SomaVec4{{{1.f, 2.f, 3.f, 4.f}}}};
    EXPECT_EQ(h.f(), 10.f);
    Function<float()> copia = h.f;
    EXPECT_EQ(copia(), 10.f);
    Function<float()> movida = std::move(copia);
    EXPECT_EQ(movida(), 10.f);
}
