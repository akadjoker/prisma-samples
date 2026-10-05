#include <ct/hashmap.hpp>
#include <ct/slotmap32.hpp>

#include <gtest/gtest.h>

#include <cstdint>

using ct::Handle32;
using ct::SlotMap32;

namespace
{
    struct Contado32
    {
        static int vivos;
        int valor;

        explicit Contado32(int v = 0) : valor(v) { ++vivos; }
        Contado32(const Contado32 &o) : valor(o.valor) { ++vivos; }
        Contado32(Contado32 &&o) noexcept : valor(o.valor) { ++vivos; }
        Contado32 &operator=(const Contado32 &o)
        {
            valor = o.valor;
            return *this;
        }
        Contado32 &operator=(Contado32 &&o) noexcept
        {
            valor = o.valor;
            return *this;
        }
        ~Contado32() { --vivos; }
    };
    int Contado32::vivos = 0;
}

static_assert(sizeof(Handle32<int>) == 4, "Handle32 tem de ocupar 4 bytes");

TEST(SlotMap32, HandleLayout)
{
    const Handle32<int> vazio;
    EXPECT_FALSE(vazio.valid());
    EXPECT_FALSE(static_cast<bool>(vazio));
    EXPECT_EQ(vazio.bits(), 0u);

    const Handle32<int> h(123456u, 77u);
    EXPECT_EQ(h.index(), 123456u);
    EXPECT_EQ(h.generation(), 77u);
    EXPECT_TRUE(h.valid());
    EXPECT_EQ(Handle32<int>::from_bits(h.bits()), h);
}

TEST(SlotMap32, InsertGetErase)
{
    SlotMap32<int> m;
    EXPECT_TRUE(m.empty());

    const auto a = m.insert(10);
    const auto b = m.insert(20);
    const auto c = m.insert(30);
    EXPECT_EQ(m.size(), 3u);
    EXPECT_EQ(m[a], 10);
    EXPECT_EQ(m[b], 20);
    EXPECT_EQ(m[c], 30);

    EXPECT_TRUE(m.erase(b));
    EXPECT_FALSE(m.erase(b));
    EXPECT_EQ(m.size(), 2u);
    EXPECT_EQ(m.get(b), nullptr);
    EXPECT_EQ(m[a], 10);
    EXPECT_EQ(m[c], 30);
}

TEST(SlotMap32, StaleHandleIsRejectedAfterReuse)
{
    SlotMap32<int> m;
    const auto velho = m.insert(1);
    EXPECT_TRUE(m.erase(velho));

    const auto novo = m.insert(2);
    EXPECT_EQ(novo.index(), velho.index());
    EXPECT_NE(novo, velho);
    EXPECT_FALSE(m.contains(velho));
    EXPECT_TRUE(m.contains(novo));
    EXPECT_EQ(m[novo], 2);
}

TEST(SlotMap32, GenerationWrapKeepsLiveHandleValid)
{
    SlotMap32<int> m;
    auto atual = m.insert(0);
    for (int i = 1; i <= 5000; ++i)
    {
        const auto anterior = atual;
        EXPECT_TRUE(m.erase(atual));
        atual = m.insert(i);
        ASSERT_TRUE(atual.valid());
        ASSERT_TRUE(m.contains(atual));
        ASSERT_FALSE(m.contains(anterior));
        ASSERT_EQ(m[atual], i);
    }
    EXPECT_EQ(m.size(), 1u);
    EXPECT_EQ(m.slot_count(), 1u);
}

TEST(SlotMap32, ClearInvalidatesEverything)
{
    SlotMap32<Contado32> m;
    const auto a = m.emplace(1);
    const auto b = m.emplace(2);
    EXPECT_EQ(Contado32::vivos, 2);

    m.clear();
    EXPECT_EQ(Contado32::vivos, 0);
    EXPECT_TRUE(m.empty());
    EXPECT_FALSE(m.contains(a));
    EXPECT_FALSE(m.contains(b));

    const auto c = m.emplace(3);
    EXPECT_TRUE(m.contains(c));
    EXPECT_EQ(m[c].valor, 3);
}

TEST(SlotMap32, DenseIterationAndHandleAt)
{
    SlotMap32<int> m;
    const auto a = m.insert(1);
    const auto b = m.insert(2);
    const auto c = m.insert(3);
    EXPECT_TRUE(m.erase(a));

    int soma = 0;
    for (int v : m)
        soma += v;
    EXPECT_EQ(soma, 5);

    for (std::size_t i = 0; i < m.size(); ++i)
    {
        const auto h = m.handle_at(i);
        EXPECT_TRUE(m.contains(h));
        EXPECT_EQ(m.index_of(h), i);
    }
    EXPECT_TRUE(m.contains(b));
    EXPECT_TRUE(m.contains(c));
}

TEST(SlotMap32, HandleIsHashMapKey)
{
    SlotMap32<int> m;
    const auto a = m.insert(1);
    const auto b = m.insert(2);

    ct::HashMap<Handle32<int>, int> nomes;
    nomes.put(a, 100);
    nomes.put(b, 200);
    ASSERT_NE(nomes.find(a), nullptr);
    EXPECT_EQ(*nomes.find(a), 100);
    EXPECT_EQ(*nomes.find(b), 200);
}
