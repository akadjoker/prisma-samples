#include <ct/string.hpp>
#include <ct/vector.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

using ct::String;

TEST(StringSSO, SizeIs24Bytes)
{
    EXPECT_EQ(sizeof(String), 24u);
}

TEST(StringSSO, SmallStaysInline)
{
    for (int n = 0; n <= 23; ++n)
    {
        String s(std::string(n, 'a').c_str());
        EXPECT_TRUE(s.is_small()) << "len " << n << " devia ser SSO";
        EXPECT_EQ(s.size(), std::size_t(n));
        EXPECT_EQ(s.capacity(), 23u);
        EXPECT_EQ(s.c_str()[n], '\0') << "tem de ser NUL-terminada";
    }
}

TEST(StringSSO, TwentyFourGoesToHeap)
{
    String s("abcdefghijklmnopqrstuvwx"); 
    EXPECT_FALSE(s.is_small());
    EXPECT_EQ(s.size(), 24u);
    EXPECT_STREQ(s.c_str(), "abcdefghijklmnopqrstuvwx");
}

TEST(StringSSO, BoundaryTransitionByPushBack)
{
    String s;
    for (int i = 0; i < 23; ++i)
        s.push_back(char('a' + i % 26));
    EXPECT_TRUE(s.is_small());
    EXPECT_EQ(s.size(), 23u);
    s.push_back('!'); 
    EXPECT_FALSE(s.is_small());
    EXPECT_EQ(s.size(), 24u);
    EXPECT_EQ(s.back(), '!');
    EXPECT_EQ(s[0], 'a');
    EXPECT_EQ(s.c_str()[24], '\0');
}

TEST(StringSSO, Exactly23HasImplicitNul)
{
    String s("12345678901234567890123"); 
    EXPECT_TRUE(s.is_small());
    EXPECT_EQ(s.size(), 23u);
    EXPECT_EQ(s.c_str()[23], '\0'); 
}

TEST(StringBasic, DefaultEmpty)
{
    String s;
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0u);
    EXPECT_STREQ(s.c_str(), "");
}

TEST(StringBasic, FromCString)
{
    String s("hello");
    EXPECT_EQ(s.size(), 5u);
    EXPECT_TRUE(s == "hello");
    String n(nullptr);
    EXPECT_TRUE(n.empty());
}

TEST(StringBasic, FromPtrLen)
{
    String s("hello world", 5);
    EXPECT_TRUE(s == "hello");
}

TEST(StringBasic, FillConstructor)
{
    String small(10, 'x');
    EXPECT_TRUE(small == "xxxxxxxxxx");
    EXPECT_TRUE(small.is_small());
    String big(100, 'y');
    EXPECT_EQ(big.size(), 100u);
    EXPECT_FALSE(big.is_small());
    for (char c : big)
        ASSERT_EQ(c, 'y');
}

TEST(StringBasic, StdInterop)
{
    std::string src = "interoperabilidade com std::string aqui";
    String s(src);
    EXPECT_EQ(s.size(), src.size());
    EXPECT_EQ(s.to<std::string>(), src);
}

TEST(StringBasic, CopySmallAndHeap)
{
    String small("curta");
    String cs(small);
    EXPECT_TRUE(cs == "curta");
    cs[0] = 'X';
    EXPECT_TRUE(small == "curta") << "cópia tem de ser independente";

    String heap("uma string suficientemente longa para ir ao heap");
    String ch(heap);
    EXPECT_TRUE(ch == heap);
    ch[0] = 'X';
    EXPECT_EQ(heap[0], 'u');
}

TEST(StringBasic, MoveStealsHeap)
{
    String heap("uma string suficientemente longa para ir ao heap");
    const char *p = heap.data();
    String m(std::move(heap));
    EXPECT_EQ(m.data(), p) << "move devia roubar o buffer";
    EXPECT_TRUE(heap.empty());

    String target("outra string longa que também vai parar ao heap...");
    target = std::move(m);
    EXPECT_EQ(target.data(), p);
}

TEST(StringBasic, AssignShrinkAndGrow)
{
    String s("uma string suficientemente longa para ir ao heap");
    s = "curta"; 
    EXPECT_TRUE(s == "curta");
    s = "outra string ainda mais comprida para forçar novo crescimento do buffer";
    EXPECT_TRUE(s.ends_with("buffer"));
}

TEST(StringAppend, OperatorPlusEquals)
{
    String s;
    s += "abc";
    s += String("def");
    s += 'g';
    EXPECT_TRUE(s == "abcdefg");
}

TEST(StringAppend, GrowthAcrossManyAppends)
{
    String s;
    std::string ref;
    for (int i = 0; i < 1000; ++i)
    {
        s += "chunk";
        ref += "chunk";
    }
    EXPECT_EQ(s.size(), ref.size());
    EXPECT_EQ(s.to<std::string>(), ref);
}

TEST(StringAppend, SelfAppendIsSafe)
{
    String s("abcdefghij"); 
    s += s;
    EXPECT_TRUE(s == "abcdefghijabcdefghij");
    s += s; 
    EXPECT_EQ(s.size(), 40u);
    EXPECT_TRUE(s.starts_with("abcdefghijabcdefghij"));
    String h("string longa no heap para o self append ser perigoso!");
    ASSERT_EQ(h.size(), 53u);
    h += h;
    EXPECT_EQ(h.size(), 106u);
    EXPECT_TRUE(h.ends_with("perigoso!"));
}

TEST(StringEdit, AppendInsertAndErase)
{
    String s("abcd");
    s.append(2, '!');
    EXPECT_TRUE(s == "abcd!!");
    s.insert(2, 3, '-');
    EXPECT_TRUE(s == "ab---cd!!");
    s.erase(2, 3);
    EXPECT_TRUE(s == "abcd!!");
    s.erase(s.begin() + 4, s.end());
    EXPECT_TRUE(s == "abcd");
}

TEST(StringEdit, InsertAliasesItsOwnStorage)
{
    String complete("abcd");
    complete.insert(1, complete);
    EXPECT_TRUE(complete == "aabcdbcd");

    String tail("abcd");
    tail.insert(1, tail.data() + 1, 3);
    EXPECT_TRUE(tail == "abcdbcd");

    String overlap("abcd");
    overlap.insert(2, overlap.data() + 1, 3);
    EXPECT_TRUE(overlap == "abbcdcd");
}

TEST(StringAppend, OperatorPlus)
{
    String a("foo");
    String b("bar");
    EXPECT_TRUE(a + b == "foobar");
    EXPECT_TRUE(a + "baz" == "foobaz");
    EXPECT_TRUE("pre" + b == "prebar");
    EXPECT_TRUE(a + '!' == "foo!");
}

TEST(StringNumber, Integers)
{
    EXPECT_TRUE(String::number(0) == "0");
    EXPECT_TRUE(String::number(42) == "42");
    EXPECT_TRUE(String::number(-1234567) == "-1234567");
    EXPECT_TRUE(String::number(9223372036854775807ll) == "9223372036854775807");
    EXPECT_TRUE(String::number(18446744073709551615ull) == "18446744073709551615");
}

TEST(StringNumber, Doubles)
{
    EXPECT_TRUE(String::number(1.5) == "1.5");
    EXPECT_TRUE(String::number(0.25) == "0.25");
    String pi = String::number(3.14159265, 3);
    EXPECT_TRUE(pi == "3.14");
}

TEST(StringNumber, AppendChain)
{
    String s("pos=");
    s.append_number(10);
    s += ',';
    s.append_number(-5);
    EXPECT_TRUE(s == "pos=10,-5");
}

TEST(StringFind, CharAndSubstring)
{
    String s("the quick brown fox jumps over the lazy dog");
    EXPECT_EQ(s.find('q'), 4u);
    EXPECT_TRUE(s.find('z') != String::npos);
    EXPECT_TRUE(s.find('!') == String::npos);
    EXPECT_EQ(s.find("brown"), 10u);
    EXPECT_EQ(s.find("the"), 0u);
    EXPECT_EQ(s.find("the", 1), 31u);
    EXPECT_TRUE(s.find("cat") == String::npos);
    EXPECT_EQ(s.rfind('o'), 41u); 
    EXPECT_EQ(s.find_first_of("xyz"), 18u); 
}

TEST(StringFind, AgainstStdReference)
{

    std::string ref = "abracadabra alakazam abracadabra";
    String s(ref);
    const char *needles[] = {"abra", "cad", "zam", "a", "abracadabra", "xyz", ""};
    for (const char *n : needles)
        for (std::size_t pos = 0; pos <= ref.size(); pos += 3)
        {
            std::size_t got = s.find(n, pos);
            std::size_t want = ref.find(n, pos);
            EXPECT_TRUE(got == want) << "needle '" << n << "' pos " << pos;
        }
}

TEST(StringFind, StartsEndsContains)
{
    String s("filename.tar.gz");
    EXPECT_TRUE(s.starts_with("file"));
    EXPECT_FALSE(s.starts_with("dir"));
    EXPECT_TRUE(s.ends_with(".gz"));
    EXPECT_TRUE(s.ends_with(String(".tar.gz")));
    EXPECT_FALSE(s.ends_with(".zip"));
    EXPECT_TRUE(s.contains(".tar."));
    EXPECT_TRUE(s.contains('.'));
    EXPECT_FALSE(s.contains('!'));
    EXPECT_TRUE(s.starts_with("filename.tar.gz")); 
    EXPECT_FALSE(String("gz").ends_with(".tar.gz")); 
}

TEST(StringFind, NotOfAndLastOf)
{
    EXPECT_EQ(String("  texto").find_first_not_of(" "), 2u);
    EXPECT_TRUE(String("   ").find_first_not_of(" ") == String::npos);
    EXPECT_EQ(String("file.json").find_last_of("."), 4u);
    EXPECT_EQ(String("a.b.c").find_last_of(".", 3), 1u);
    EXPECT_TRUE(String("abc").find_last_of(".") == String::npos);
}

// ---------- fatias / utils ----------

TEST(StringSlice, Substr)
{
    String s("hello world");
    EXPECT_TRUE(s.substr(0, 5) == "hello");
    EXPECT_TRUE(s.substr(6) == "world");
    EXPECT_TRUE(s.substr(6, 100) == "world");
    EXPECT_TRUE(s.substr(11) == "");
    EXPECT_DEATH(s.substr(12), "fora dos limites");
}

TEST(StringSlice, Trimmed)
{
    EXPECT_TRUE(String("  \t hello \n ").trimmed() == "hello");
    EXPECT_TRUE(String("nada").trimmed() == "nada");
    EXPECT_TRUE(String("   ").trimmed() == "");
    EXPECT_TRUE(String().trimmed() == "");
}

TEST(StringSlice, Split)
{
    auto parts = String("a,bb,ccc").split(',');
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_TRUE(parts[0] == "a");
    EXPECT_TRUE(parts[1] == "bb");
    EXPECT_TRUE(parts[2] == "ccc");

    auto sparse = String(",a,,b,").split(',');
    ASSERT_EQ(sparse.size(), 2u); 
    EXPECT_TRUE(sparse[0] == "a");

    auto kept = String(",a,,b,").split(',', true);
    ASSERT_EQ(kept.size(), 5u); 
    EXPECT_TRUE(kept[0] == "");
    EXPECT_TRUE(kept[4] == "");
}

TEST(StringCompare, Operators)
{
    EXPECT_TRUE(String("abc") == String("abc"));
    EXPECT_TRUE(String("abc") != String("abd"));
    EXPECT_TRUE(String("abc") < String("abd"));
    EXPECT_TRUE(String("abc") < String("abcd"));
    EXPECT_TRUE(String("b") > String("abc"));
    EXPECT_TRUE(String("") < String("a"));
    EXPECT_TRUE(String("") == String(""));
    EXPECT_TRUE(String("abc") == "abc");
    EXPECT_TRUE("abc" == String("abc"));
    EXPECT_TRUE(String("ab\0c", 4) != String("ab\0d", 4)) << "binário com NUL interno";
}

TEST(StringCompare, SlicesAndConversions)
{
    String s("prefix-value");
    EXPECT_EQ(s.compare(0, 6, "prefix"), 0);
    EXPECT_LT(s.compare(7, 5, String("zebra")), 0);
    EXPECT_EQ(String("42").to_int(), 42);
    EXPECT_FLOAT_EQ(String("3.5").to_float(), 3.5f);
}

TEST(StringCompare, SortMatchesStd)
{
    std::vector<std::string> ref = {"pear", "apple", "fig", "apple", "banana", "", "z"};
    ct::Vector<String> v;
    for (const auto &r : ref)
        v.emplace_back(r);
    std::sort(v.begin(), v.end());
    std::sort(ref.begin(), ref.end());
    for (std::size_t i = 0; i < ref.size(); ++i)
        EXPECT_TRUE(v[i] == ref[i].c_str());
}

TEST(StringMisc, ResizeReserve)
{
    String s("abc");
    s.resize(6, 'x');
    EXPECT_TRUE(s == "abcxxx");
    s.resize(2);
    EXPECT_TRUE(s == "ab");
    s.reserve(500);
    EXPECT_GE(s.capacity(), 500u);
    EXPECT_TRUE(s == "ab");
}

TEST(StringMisc, HashDistinguishes)
{
    EXPECT_EQ(String("hello").hash(), String("hello").hash());
    EXPECT_NE(String("hello").hash(), String("hellp").hash());
    EXPECT_NE(String("").hash(), String("\0", 1).hash());
}

TEST(StringMisc, Swap)
{
    String a("pequena");
    String b("uma string bem grande que vive no heap de certeza");
    swap(a, b);
    EXPECT_TRUE(a.ends_with("certeza"));
    EXPECT_TRUE(b == "pequena");
}

TEST(StringFuzz, RandomOpsMatchStd)
{
    String s;
    std::string ref;
    unsigned seed = 321;
    for (int i = 0; i < 20000; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        int op = seed % 5;
        char c = char('a' + (seed >> 8) % 26);
        if (op == 0 || op == 1) 
        {
            s.push_back(c);
            ref.push_back(c);
        }
        else if (op == 2 && !ref.empty())
        {
            s.pop_back();
            ref.pop_back();
        }
        else if (op == 3)
        {
            s.append("xy", 2);
            ref.append("xy", 2);
        }
        else if (!ref.empty())
        {
            std::size_t pos = (seed >> 8) % ref.size();
            ASSERT_EQ(s[pos], ref[pos]);
        }
        ASSERT_EQ(s.size(), ref.size());
    }
    EXPECT_EQ(s.to<std::string>(), ref);
    EXPECT_EQ(std::strlen(s.c_str()), ref.size()) << "NUL na posição certa";
}

TEST(StringNumber, TodosOsInteirosSemAmbiguidade)
{
    EXPECT_TRUE(String::number(std::size_t(5)) == "5");
    EXPECT_TRUE(String::number(-3L) == "-3");
    EXPECT_TRUE(String::number(7UL) == "7");
    EXPECT_TRUE(String::number(short(-2)) == "-2");
    EXPECT_TRUE(String::number(static_cast<unsigned char>(200)) == "200");
    EXPECT_TRUE(String::number(1.5f) == "1.5");
    String s;
    s.append_number(std::size_t(42)).append_number(-1L).append_number(3u).append_number(9LL);
    EXPECT_TRUE(s == "42-139");
}

TEST(StringFind, ConjuntosNaoCasamNulEmbebido)
{
    String s("ab\0cd", 5);
    EXPECT_EQ(s.find_first_of("xyz"), String::npos);
    EXPECT_EQ(s.find_first_of("d"), 4u);
    EXPECT_EQ(s.find_first_not_of("ab"), 2u);
    EXPECT_EQ(s.find_last_of("a"), 0u);
    EXPECT_EQ(s.find_last_of("xyz"), String::npos);
    std::string ref("ab\0cd", 5);
    EXPECT_EQ(s.find_first_of("xyz"), ref.find_first_of("xyz"));
    EXPECT_EQ(s.find_first_not_of("ab"), ref.find_first_not_of("ab"));
}

TEST(StringMisc, TrimmedRemoveTodosOsBrancosAscii)
{
    String s("\f\v \t x \r\n\f");
    EXPECT_TRUE(s.trimmed() == "x");
}

TEST(StringMisc, IteradoresInversosFuncionamComStd)
{
    ct::Vector<int> v{3, 1, 2};
    std::sort(v.rbegin(), v.rend());
    EXPECT_EQ(v[0], 3);
    EXPECT_EQ(v[1], 2);
    EXPECT_EQ(v[2], 1);
    EXPECT_EQ(std::distance(v.rbegin(), v.rend()), 3);
    EXPECT_EQ(*(v.rbegin() + 1), 2);
    EXPECT_EQ(v.rbegin()[2], 3);
}

namespace
{
    enum class Nivel : short
    {
        Baixo = -3,
        Alto = 7
    };
    enum Antigo
    {
        Um = 1,
        Dois
    };
}

TEST(String, NumberAceitaEnums)
{
    EXPECT_EQ(ct::String::number(Nivel::Baixo), "-3");
    EXPECT_EQ(ct::String::number(Nivel::Alto), "7");
    EXPECT_EQ(ct::String::number(Dois), "2");
    ct::String s;
    s.append_number(Um);
    s.append_number(Nivel::Alto);
    EXPECT_EQ(s, "17");
}

TEST(String, NposTemTipoSizeTEPodeSerLidoPorValorEReferencia)
{
    ct::String s("hello");
    auto pos = ct::String::npos;
    static_assert(std::is_same<decltype(pos), std::size_t>::value, "npos e size_t");
    pos = s.find("l");
    EXPECT_EQ(pos, 2u);
    const std::size_t &ref = ct::String::npos;
    EXPECT_EQ(ref, static_cast<std::size_t>(-1));
    EXPECT_EQ((std::min)(ct::String::npos, std::size_t(5)), 5u);
    EXPECT_EQ(s.find("z"), ct::String::npos);
}
