#include <ct/variant.hpp>
#include <ct/string.hpp>

#include <gtest/gtest.h>

using ct::String;
using ct::Variant;

namespace
{
    using V = Variant<int, double, String>;

    struct Stringify
    {
        String operator()(int x) const { return String::number(x); }
        String operator()(double) const { return String("double"); }
        String operator()(const String &s) const { return s; }
    };

    struct CopyCounter
    {
        static int copies;
        int value;
        explicit CopyCounter(int v) : value(v) {}
        CopyCounter(const CopyCounter &o) : value(o.value) { ++copies; }
        CopyCounter(CopyCounter &&) noexcept = default;
    };
    int CopyCounter::copies = 0;
}

TEST(Variant, DefaultConstructsFirstAlternative)
{
    V v;
    EXPECT_EQ(v.index(), 0u);
    EXPECT_TRUE(v.is<int>());
    EXPECT_EQ(v.get<int>(), 0);
}

TEST(Variant, ConstructFromAlternative)
{
    V a = 42;
    EXPECT_TRUE(a.is<int>());
    EXPECT_EQ(a.get<int>(), 42);

    V b = 3.5;
    EXPECT_TRUE(b.is<double>());
    EXPECT_EQ(b.get<double>(), 3.5);

    V c = String("ola");
    EXPECT_TRUE(c.is<String>());
    EXPECT_EQ(c.get<String>(), "ola");
}

TEST(Variant, AssignSwitchesActiveType)
{
    V v = 1;
    EXPECT_TRUE(v.is<int>());
    v = 2.0;
    EXPECT_TRUE(v.is<double>());
    v = String("s");
    EXPECT_TRUE(v.is<String>());
    v = 5; 
    EXPECT_TRUE(v.is<int>());
    EXPECT_EQ(v.get<int>(), 5);
}

TEST(Variant, GetIf)
{
    V v = 42;
    EXPECT_NE(v.get_if<int>(), nullptr);
    EXPECT_EQ(v.get_if<double>(), nullptr);
    EXPECT_EQ(v.get_if<String>(), nullptr);
    *v.get_if<int>() = 7;
    EXPECT_EQ(v.get<int>(), 7);
}

TEST(Variant, WrongGetIsFatal)
{
    V v = 42;
    EXPECT_DEATH(v.get<String>(), "");
}

TEST(Variant, CopyIsDeep)
{
    V a = String("uma string bem comprida para forcar heap na String!!");
    V b = a;
    b.get<String>().push_back('!');
    EXPECT_NE(a.get<String>(), b.get<String>());
}

TEST(Variant, CopyAssignReplacesActiveType)
{
    V a = 42;
    V b = String("x");
    b = a;
    EXPECT_TRUE(b.is<int>());
    EXPECT_EQ(b.get<int>(), 42);
}

TEST(Variant, MoveLeavesSourceInSameAlternativeButMovedFrom)
{
    V a = String("string longa o suficiente para ir para o heap, de certeza!!");
    V b = ct::detail::move(a);
    EXPECT_TRUE(b.is<String>());
    EXPECT_EQ(b.get<String>(), "string longa o suficiente para ir para o heap, de certeza!!");
    EXPECT_TRUE(a.is<String>()); 
}

TEST(Variant, Swap)
{
    V a = 1;
    V b = String("s");
    a.swap(b);
    EXPECT_TRUE(a.is<String>());
    EXPECT_TRUE(b.is<int>());
    EXPECT_EQ(b.get<int>(), 1);
}

TEST(Variant, VisitDispatchesToActiveType)
{
    Stringify f;
    V v = 42;
    EXPECT_EQ(v.visit(f), "42");
    v = 1.5;
    EXPECT_EQ(v.visit(f), "double");
    v = String("hi");
    EXPECT_EQ(v.visit(f), "hi");
}

TEST(Variant, VisitOnConstVariant)
{
    Stringify f;
    const V v = String("hi");
    EXPECT_EQ(v.visit(f), "hi");
}

TEST(Variant, CopyConstructsTargetOnce)
{
    CopyCounter::copies = 0;
    Variant<int, CopyCounter> a = CopyCounter(3);
    CopyCounter::copies = 0;
    Variant<int, CopyCounter> b = a; 
    EXPECT_EQ(CopyCounter::copies, 1);
    EXPECT_EQ(b.get<CopyCounter>().value, 3);
}

TEST(Variant, TwoAlternativesTypeAtWorks)
{
    Variant<int, int> nope = 1; 

    EXPECT_EQ(nope.index(), 0u);
    EXPECT_EQ(nope.get<int>(), 1);
}

TEST(Variant, SingleAlternative)
{
    Variant<int> v = 5;
    EXPECT_EQ(v.get<int>(), 5);
    v = 6;
    EXPECT_EQ(v.get<int>(), 6);
}