#include <ct/slotmap.hpp>
#include <ct/string.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <random>
#include <vector>

using ct::Handle;
using ct::SlotMap;

namespace
{
    struct Contado
    {
        static int vivos;
        int valor;

        explicit Contado(int v = 0) : valor(v) { ++vivos; }
        Contado(const Contado &o) : valor(o.valor) { ++vivos; }
        Contado(Contado &&o) noexcept : valor(o.valor) { ++vivos; }
        Contado &operator=(const Contado &o)
        {
            valor = o.valor;
            return *this;
        }
        Contado &operator=(Contado &&o) noexcept
        {
            valor = o.valor;
            return *this;
        }
        ~Contado() { --vivos; }
    };
    int Contado::vivos = 0;
}

TEST(SlotMap, InsertGetErase)
{
    SlotMap<int> m;
    EXPECT_TRUE(m.empty());
    EXPECT_EQ(m.size(), 0u);

    const auto a = m.insert(10);
    const auto b = m.insert(20);
    const auto c = m.emplace(30);
    EXPECT_EQ(m.size(), 3u);
    EXPECT_FALSE(m.empty());

    EXPECT_EQ(m[a], 10);
    EXPECT_EQ(m[b], 20);
    EXPECT_EQ(m[c], 30);
    ASSERT_NE(m.get(a), nullptr);
    EXPECT_EQ(*m.get(a), 10);

    m[b] = 99;
    EXPECT_EQ(*m.get(b), 99);

    EXPECT_TRUE(m.erase(b));
    EXPECT_EQ(m.size(), 2u);
    EXPECT_EQ(m[a], 10); 
    EXPECT_EQ(m[c], 30);
}

TEST(SlotMap, StaleHandleIsDetected)
{
    SlotMap<int> m;
    const auto h = m.insert(42);
    EXPECT_TRUE(m.contains(h));

    EXPECT_TRUE(m.erase(h));
    EXPECT_FALSE(m.contains(h));
    EXPECT_EQ(m.get(h), nullptr);
    EXPECT_FALSE(m.erase(h)); 
    EXPECT_DEATH(m[h], "handle invalido");

    const auto novo = m.insert(7);
    EXPECT_EQ(novo.index, h.index);
    EXPECT_NE(novo.generation, h.generation);
    EXPECT_EQ(m.get(h), nullptr);
    EXPECT_EQ(*m.get(novo), 7);
    EXPECT_TRUE(m.contains(novo));
}

TEST(SlotMap, NullHandleIsNeverValid)
{
    SlotMap<int> m;
    Handle<int> nulo;
    EXPECT_FALSE(nulo.valid());
    EXPECT_FALSE(static_cast<bool>(nulo));
    EXPECT_FALSE(m.contains(nulo));
    EXPECT_EQ(m.get(nulo), nullptr);

    m.insert(1); 
    EXPECT_FALSE(m.contains(nulo));
    EXPECT_EQ(m.get(nulo), nullptr);

    EXPECT_FALSE(m.contains(Handle<int>(9999, 1)));
    EXPECT_FALSE(m.contains(Handle<int>(0, 12345)));
    EXPECT_FALSE(m.contains(Handle<int>(0, 2))); 
}

TEST(SlotMap, EmptyIterationUsesNullIteratorsSafely)
{
    SlotMap<int> m;
    EXPECT_EQ(m.begin(), nullptr);
    EXPECT_EQ(m.begin(), m.end());
    for (int value : m)
        FAIL() << "SlotMap vazio nao devia iterar" << value;

    m.clear();
    EXPECT_EQ(m.begin(), m.end());
}

TEST(SlotMap, SwapRemoveKeepsEverythingResolvable)
{
    SlotMap<int> m;
    std::vector<Handle<int>> hs;
    for (int i = 0; i < 10; ++i)
        hs.push_back(m.insert(i));

    m.erase(hs[0]); 
    EXPECT_EQ(m.size(), 9u);
    for (int i = 1; i < 10; ++i)
    {
        ASSERT_TRUE(m.contains(hs[i])) << i;
        EXPECT_EQ(m[hs[i]], i) << i;
    }

    m.erase(hs[9]); 
    m.erase(hs[5]); 
    EXPECT_EQ(m.size(), 7u);
    for (int i = 1; i < 9; ++i)
    {
        if (i == 5)
        {
            EXPECT_FALSE(m.contains(hs[i]));
            continue;
        }
        ASSERT_TRUE(m.contains(hs[i])) << i;
        EXPECT_EQ(m[hs[i]], i) << i;
    }

    int soma = 0;
    for (int v : m.items())
        soma += v;
    EXPECT_EQ(soma, 1 + 2 + 3 + 4 + 6 + 7 + 8);
    EXPECT_EQ(m.items().size(), m.size());
}

TEST(SlotMap, IterationAndHandleAt)
{
    SlotMap<int> m;
    std::vector<Handle<int>> hs;
    for (int i = 0; i < 5; ++i)
        hs.push_back(m.insert(i * 10));

    for (std::size_t i = 0; i < m.size(); ++i)
    {
        const Handle<int> h = m.handle_at(i);
        ASSERT_TRUE(m.contains(h));
        EXPECT_EQ(m[h], m.items()[i]);
        EXPECT_EQ(m.index_of(h), i);
    }
    EXPECT_DEATH(m.handle_at(m.size()), "fora dos limites");
    EXPECT_DEATH(m.index_of(Handle<int>()), "handle invalido");

    for (int &v : m.items())
        v += 1;
    EXPECT_EQ(m[hs[0]], 1);
    EXPECT_EQ(m[hs[4]], 41);

    int n = 0;
    for (int v : m)
    {
        (void)v;
        ++n;
    }
    EXPECT_EQ(n, 5);

    std::vector<Handle<int>> apagar;
    for (std::size_t i = 0; i < m.size(); ++i)
        if (m.items()[i] % 20 == 1)
            apagar.push_back(m.handle_at(i));
    for (Handle<int> h : apagar)
        EXPECT_TRUE(m.erase(h));
    EXPECT_EQ(m.size(), 5u - apagar.size());
}

TEST(SlotMap, ClearInvalidatesButKeepsSlots)
{
    SlotMap<int> m;
    std::vector<Handle<int>> hs;
    for (int i = 0; i < 6; ++i)
        hs.push_back(m.insert(i));
    const std::size_t slots = m.slot_count();

    m.clear();
    EXPECT_EQ(m.size(), 0u);
    EXPECT_TRUE(m.empty());
    EXPECT_EQ(m.slot_count(), slots); 
    for (Handle<int> h : hs)
    {
        EXPECT_FALSE(m.contains(h));
        EXPECT_EQ(m.get(h), nullptr);
    }

    const auto novo = m.insert(1);
    EXPECT_TRUE(m.contains(novo));
    EXPECT_EQ(m.slot_count(), slots); 
    for (Handle<int> h : hs)
        EXPECT_FALSE(m.contains(h));

    m.clear();
    m.clear(); 
    EXPECT_EQ(m.size(), 0u);
}

TEST(SlotMap, NonTrivialTypesAreDestroyed)
{
    Contado::vivos = 0;
    {
        SlotMap<Contado> m;
        std::vector<Handle<Contado>> hs;
        for (int i = 0; i < 20; ++i)
            hs.push_back(m.emplace(i));
        EXPECT_EQ(Contado::vivos, 20);

        for (int i = 0; i < 20; i += 2)
            EXPECT_TRUE(m.erase(hs[i]));
        EXPECT_EQ(Contado::vivos, 10);
        EXPECT_EQ(m.size(), 10u);
        for (int i = 1; i < 20; i += 2)
            EXPECT_EQ(m[hs[i]].valor, i);

        m.clear();
        EXPECT_EQ(Contado::vivos, 0);

        for (int i = 0; i < 5; ++i)
            m.emplace(i);
        EXPECT_EQ(Contado::vivos, 5);
    }
    EXPECT_EQ(Contado::vivos, 0);

    SlotMap<ct::String> s;
    const auto h = s.insert(ct::String("uma string suficientemente grande para ir ao heap"));
    EXPECT_EQ(s[h], ct::String("uma string suficientemente grande para ir ao heap"));
    s.erase(h);
    EXPECT_EQ(s.size(), 0u);
}

TEST(SlotMap, HandleBitsRoundTrip)
{
    SlotMap<int> m;
    const auto h = m.insert(5);
    const std::uint64_t bits = h.bits();
    const Handle<int> volta = Handle<int>::from_bits(bits);
    EXPECT_EQ(volta, h);
    EXPECT_TRUE(m.contains(volta));
    EXPECT_EQ(sizeof(Handle<int>), 8u);
    EXPECT_NE(Handle<int>(1, 1), Handle<int>(1, 3));
    EXPECT_NE(Handle<int>(1, 1), Handle<int>(2, 1));
}

TEST(SlotMap, ReserveAndGrowth)
{
    SlotMap<int> m;
    m.reserve(1000);
    EXPECT_GE(m.capacity(), 1000u);
    std::vector<Handle<int>> hs;
    for (int i = 0; i < 5000; ++i)
        hs.push_back(m.insert(i));
    EXPECT_EQ(m.size(), 5000u);
    for (int i = 0; i < 5000; ++i)
        ASSERT_EQ(m[hs[i]], i) << i; 
}

TEST(SlotMap, RandomOpsAgainstModel)
{
    SlotMap<int> m;
    std::map<std::uint64_t, int> modelo; 
    std::vector<Handle<int>> vivos;
    std::mt19937 rng(20260822);
    int proximo = 0;

    for (int passo = 0; passo < 200000; ++passo)
    {
        const int op = rng() % 100;
        if (op < 45 || vivos.empty()) 
        {
            const int v = proximo++;
            const Handle<int> h = m.insert(v);
            ASSERT_FALSE(modelo.count(h.bits())) << "handle repetido!";
            modelo[h.bits()] = v;
            vivos.push_back(h);
        }
        else if (op < 85) 
        {
            const std::size_t i = rng() % vivos.size();
            const Handle<int> h = vivos[i];
            ASSERT_TRUE(m.erase(h));
            modelo.erase(h.bits());
            vivos[i] = vivos.back();
            vivos.pop_back();
            ASSERT_FALSE(m.contains(h));
            ASSERT_EQ(m.get(h), nullptr);
        }
        else if (op < 97) 
        {
            const Handle<int> h = vivos[rng() % vivos.size()];
            const int *p = m.get(h);
            ASSERT_NE(p, nullptr);
            ASSERT_EQ(*p, modelo[h.bits()]);
        }
        else 
        {
            ASSERT_EQ(m.size(), modelo.size());
            ASSERT_EQ(m.items().size(), modelo.size());
            for (std::size_t i = 0; i < m.size(); ++i)
            {
                const Handle<int> h = m.handle_at(i);
                ASSERT_TRUE(m.contains(h));
                ASSERT_EQ(m.items()[i], modelo[h.bits()]);
            }
        }
        ASSERT_EQ(m.size(), modelo.size());
    }

    for (const Handle<int> h : vivos)
    {
        const int *p = m.get(h);
        ASSERT_NE(p, nullptr);
        ASSERT_EQ(*p, modelo[h.bits()]);
    }
    std::printf("[          ] %zu vivos, %d inseridos no total, %zu slots\n", vivos.size(),
                proximo, m.slot_count());
}