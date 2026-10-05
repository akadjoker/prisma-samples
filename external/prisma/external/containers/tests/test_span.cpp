#include <ct/array.hpp>
#include <ct/span.hpp>
#include <ct/string.hpp>
#include <ct/vector.hpp>

#include <gtest/gtest.h>

#include <string>
#include <type_traits>

using ct::Span;
using ct::StringView;

TEST(Span, ConstructionFromEverything)
{
    static_assert(sizeof(Span<int>) == 2 * sizeof(void *), "ponteiro + tamanho");

    Span<int> vazio;
    EXPECT_TRUE(vazio.empty());
    EXPECT_EQ(vazio.size(), 0u);
    EXPECT_EQ(vazio.data(), nullptr);
    EXPECT_EQ(vazio.begin(), vazio.end());

    int arr[4] = {1, 2, 3, 4};
    Span<int> a(arr);
    EXPECT_EQ(a.size(), 4u);
    EXPECT_EQ(a.data(), arr);

    Span<int> b(arr, 2);
    EXPECT_EQ(b.size(), 2u);
    Span<int> c(arr, arr + 3); 
    EXPECT_EQ(c.size(), 3u);

    ct::Vector<int> v{1, 2, 3, 4, 5};
    Span<int> d(v);
    EXPECT_EQ(d.size(), 5u);
    EXPECT_EQ(d.data(), v.data());

    ct::Array<int, 3> ar{{7, 8, 9}};
    Span<int> e(ar);
    EXPECT_EQ(e.size(), 3u);
    EXPECT_EQ(e[2], 9);

    ct::String s("abc");
    Span<const char> f(s);
    EXPECT_EQ(f.size(), 3u);
    EXPECT_EQ(f[0], 'a');
}

TEST(Span, ConstConversions)
{
    ct::Vector<int> v{1, 2, 3};
    Span<int> mut(v);
    Span<const int> a(mut); 
    EXPECT_EQ(a.size(), 3u);

    const ct::Vector<int> &cv = v;
    Span<const int> b(cv); 
    EXPECT_EQ(b.data(), v.data());

    static_assert(std::is_same<decltype(a[0]), const int &>::value, "acesso const");
    static_assert(std::is_same<decltype(mut[0]), int &>::value, "acesso mutavel");
}

TEST(Span, WritesThroughToTheContainer)
{
    ct::Vector<int> v{1, 2, 3};
    Span<int> s(v);
    s[0] = 99;
    s.back() = 77;
    for (int &x : s)
        x += 1;
    EXPECT_EQ(v[0], 100);
    EXPECT_EQ(v[1], 3);
    EXPECT_EQ(v[2], 78);

    Span<const int> cs(v);
    EXPECT_EQ(cs[0], 100);
}

TEST(Span, Subviews)
{
    int arr[6] = {0, 1, 2, 3, 4, 5};
    Span<int> s(arr);

    EXPECT_EQ(s.first(3).size(), 3u);
    EXPECT_EQ(s.first(3)[2], 2);
    EXPECT_EQ(s.last(2)[0], 4);
    EXPECT_EQ(s.subspan(2).size(), 4u);
    EXPECT_EQ(s.subspan(2)[0], 2);
    EXPECT_EQ(s.subspan(1, 3).size(), 3u);
    EXPECT_EQ(s.subspan(1, 3)[0], 1);

    EXPECT_EQ(s.subspan(4, 100).size(), 2u);

    EXPECT_TRUE(s.subspan(6).empty());
    EXPECT_TRUE(s.first(0).empty());
    EXPECT_TRUE(s.last(0).empty());

    EXPECT_DEATH(s.subspan(7), "offset fora dos limites");
    EXPECT_DEATH(s.first(7), "maior que o span");
    EXPECT_DEATH(s.last(7), "maior que o span");
}

TEST(Span, BoundsAndEmptyAreFatal)
{
    int arr[2] = {1, 2};
    Span<int> s(arr);
    EXPECT_EQ(s.at(1), 2);
    EXPECT_DEATH(s.at(2), "fora dos limites");

    Span<int> vazio;
    EXPECT_DEATH(vazio.front(), "span vazio");
    EXPECT_DEATH(vazio.back(), "span vazio");
    EXPECT_DEATH(vazio.at(0), "fora dos limites");
}

TEST(Span, EmptySubviewsDoNotDoNullPointerArithmetic)
{
    Span<int> vazio;
    EXPECT_EQ(vazio.end(), nullptr);
    EXPECT_EQ(vazio.last(0).data(), nullptr);
    EXPECT_EQ(vazio.subspan(0).data(), nullptr);

    // Tambem e uma forma valida de representar um intervalo vazio.
    Span<int> de_ponteiros(nullptr, nullptr);
    EXPECT_TRUE(de_ponteiros.empty());
    EXPECT_EQ(de_ponteiros.begin(), de_ponteiros.end());
}

TEST(Span, IterationAndBytes)
{
    ct::Vector<int> v{1, 2, 3, 4};
    Span<int> s(v);
    int soma = 0;
    for (int x : s)
        soma += x;
    EXPECT_EQ(soma, 10);

    int rev = 0;
    for (auto it = s.rbegin(); it != s.rend(); ++it)
        rev = rev * 10 + *it;
    EXPECT_EQ(rev, 4321);

    EXPECT_EQ(s.size_bytes(), 4 * sizeof(int));
    auto bytes = ct::as_bytes(s);
    EXPECT_EQ(bytes.size(), 4 * sizeof(int));
    EXPECT_EQ(bytes.data(), reinterpret_cast<const unsigned char *>(v.data()));
}

TEST(StringView, Construction)
{
    static_assert(sizeof(StringView) == 2 * sizeof(void *), "ponteiro + tamanho");

    StringView vazia;
    EXPECT_TRUE(vazia.empty());
    EXPECT_EQ(vazia.size(), 0u);
    EXPECT_NE(vazia.data(), nullptr); 

    StringView nula(static_cast<const char *>(nullptr));
    EXPECT_TRUE(nula.empty());
    EXPECT_NE(nula.data(), nullptr);

    StringView a("ola");
    EXPECT_EQ(a.size(), 3u);
    EXPECT_EQ(a, StringView("ola"));

    StringView b("a\0b", 3); 
    EXPECT_EQ(b.size(), 3u);
    EXPECT_EQ(b[1], '\0');

    ct::String s("do ct::String");
    StringView c(s);
    EXPECT_EQ(c.size(), s.size());
    EXPECT_EQ(c.data(), s.data()); 

    std::string ss("do std::string");
    StringView d(ss);
    EXPECT_EQ(d.size(), ss.size());

    ct::String volta(c); 
    EXPECT_EQ(volta, s);
}

TEST(StringView, AccessAndBounds)
{
    StringView s("abcde");
    EXPECT_EQ(s.front(), 'a');
    EXPECT_EQ(s.back(), 'e');
    EXPECT_EQ(s.at(4), 'e');
    EXPECT_DEATH(s.at(5), "fora dos limites");

    StringView vazia;
    EXPECT_DEATH(vazia.front(), "vista vazia");
    EXPECT_DEATH(vazia.back(), "vista vazia");
}

TEST(StringView, SubstrAndTrim)
{
    StringView s("0123456789");
    EXPECT_EQ(s.substr(3), StringView("3456789"));
    EXPECT_EQ(s.substr(3, 2), StringView("34"));
    EXPECT_EQ(s.substr(8, 100), StringView("89")); 
    EXPECT_TRUE(s.substr(10).empty());             
    EXPECT_DEATH(s.substr(11), "pos fora dos limites");

    StringView t = s;
    t.remove_prefix(3);
    t.remove_suffix(4);
    EXPECT_EQ(t, StringView("345"));
    t.remove_prefix(100); 
    EXPECT_TRUE(t.empty());

    EXPECT_EQ(StringView("  \t ola \n ").trimmed(), StringView("ola"));
    EXPECT_EQ(StringView("ola").trimmed(), StringView("ola"));
    EXPECT_TRUE(StringView("  \t\n\r ").trimmed().empty());
    EXPECT_TRUE(StringView().trimmed().empty());
    EXPECT_EQ(StringView(" a ").trimmed().size(), 1u);
}

TEST(StringView, SplitOnce)
{
    StringView h, t;
    EXPECT_TRUE(StringView("chave=valor").split_once('=', h, t));
    EXPECT_EQ(h, StringView("chave"));
    EXPECT_EQ(t, StringView("valor"));

    EXPECT_TRUE(StringView("a=b=c").split_once('=', h, t)); 
    EXPECT_EQ(h, StringView("a"));
    EXPECT_EQ(t, StringView("b=c"));

    EXPECT_FALSE(StringView("sem separador").split_once('=', h, t));
    EXPECT_EQ(h, StringView("sem separador"));
    EXPECT_TRUE(t.empty());

    EXPECT_TRUE(StringView("=valor").split_once('=', h, t));
    EXPECT_TRUE(h.empty());
    EXPECT_EQ(t, StringView("valor"));

    EXPECT_TRUE(StringView("chave=").split_once('=', h, t));
    EXPECT_EQ(h, StringView("chave"));
    EXPECT_TRUE(t.empty());

    StringView ini("largura=1280\naltura=720\nvsync=1\n");
    int linhas = 0, soma = 0;
    StringView resto = ini;
    while (!resto.empty())
    {
        StringView linha;
        resto.split_once('\n', linha, resto);
        if (linha.trimmed().empty())
            continue;
        StringView k, v;
        ASSERT_TRUE(linha.split_once('=', k, v));
        ++linhas;
        soma += static_cast<int>(v.size());
        EXPECT_FALSE(k.empty());
    }
    EXPECT_EQ(linhas, 3);
    EXPECT_EQ(soma, 4 + 3 + 1);
}

TEST(StringView, Search)
{
    StringView s("abracadabra");
    EXPECT_EQ(s.find('a'), 0u);
    EXPECT_EQ(s.find('a', 1), 3u);
    EXPECT_EQ(s.find('z'), StringView::npos);
    EXPECT_EQ(s.find('a', 100), StringView::npos);
    EXPECT_EQ(s.rfind('a'), 10u);
    EXPECT_EQ(s.rfind('z'), StringView::npos);

    EXPECT_EQ(s.find(StringView("cad")), 4u);
    EXPECT_EQ(s.find(StringView("abra")), 0u);
    EXPECT_EQ(s.find(StringView("abra"), 1), 7u);
    EXPECT_EQ(s.find(StringView("xyz")), StringView::npos);
    EXPECT_EQ(s.find(StringView("")), 0u);          
    EXPECT_EQ(s.find(StringView(""), 5), 5u);
    EXPECT_EQ(s.find(StringView("abracadabras")), StringView::npos); 

    EXPECT_TRUE(s.contains('d'));
    EXPECT_TRUE(s.contains(StringView("cada")));
    EXPECT_TRUE(s.starts_with("abra"));
    EXPECT_TRUE(s.ends_with("abra"));
    EXPECT_FALSE(s.starts_with("abrb"));
    EXPECT_TRUE(s.starts_with(""));
    EXPECT_FALSE(s.starts_with("abracadabra!"));
    EXPECT_TRUE(StringView("").starts_with(""));
}

TEST(StringView, Comparisons)
{
    EXPECT_TRUE(StringView("abc") == StringView("abc"));
    EXPECT_TRUE(StringView("abc") != StringView("abd"));
    EXPECT_TRUE(StringView("abc") < StringView("abd"));
    EXPECT_TRUE(StringView("abc") < StringView("abcd")); 
    EXPECT_TRUE(StringView("abcd") > StringView("abc"));
    EXPECT_TRUE(StringView("abc") <= StringView("abc"));
    EXPECT_TRUE(StringView("abc") >= StringView("abc"));
    EXPECT_TRUE(StringView("") < StringView("a"));
    EXPECT_TRUE(StringView("") == StringView());
    EXPECT_EQ(StringView("abc").compare(StringView("abc")), 0);
    EXPECT_LT(StringView("a").compare(StringView("b")), 0);
    EXPECT_GT(StringView("b").compare(StringView("a")), 0);

    EXPECT_TRUE(StringView("a\0b", 3) != StringView("a\0c", 3));
    EXPECT_TRUE(StringView("a\0b", 3) == StringView("a\0b", 3));

    EXPECT_TRUE(StringView("abc") == "abc");
    EXPECT_TRUE("abc" == StringView("abc"));
    EXPECT_TRUE(StringView("abc") != "abd");
    ct::String s("abc");
    EXPECT_TRUE(StringView("abc") == s);
    EXPECT_TRUE(StringView("abd") != s);
    EXPECT_TRUE(StringView(s) == StringView("abc"));
}

TEST(StringView, ConstexprBasics)
{
    constexpr StringView vazia;
    static_assert(vazia.size() == 0, "");
    static_assert(vazia.empty(), "");
    constexpr StringView s("abc", 3);
    static_assert(s.size() == 3, "");
    static_assert(!s.empty(), "");
    static_assert(StringView::npos == static_cast<std::size_t>(-1), "");
}