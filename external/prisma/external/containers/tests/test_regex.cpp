#include <ct/regex.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <ctime>
#include <string>

using ct::Match;
using ct::Regex;
using ct::String;
using ct::StringView;

namespace
{

    struct Case
    {
        const char *kind;
        const char *pattern;
        unsigned flags;
        const char *text;
        int text_len;
        int pos;
        const char *arg;
        int ok;
        const char *spans;
        const char *list[24];
    };

    const Case kCases[] = {
#include "regex_cases.inc"
    };

    std::string span_string(const Match &m)
    {
        std::string out;
        for (std::size_t g = 0; g <= m.groups(); ++g)
        {
            if (g)
                out += ';';
            if (!m.matched(g))
                out += '-';
            else
                out += std::to_string(m.start(g)) + "," + std::to_string(m.end(g));
        }
        return out;
    }

    std::string sv(StringView s) { return std::string(s.data(), s.size()); }

    std::string describe(const Case &c)
    {
        std::string d = std::string(c.kind) + " /" + c.pattern + "/ flags=" + std::to_string(c.flags);
        if (c.text)
            d += " text=\"" + std::string(c.text) + "\"";
        if (c.arg)
            d += " arg=\"" + std::string(c.arg) + "\"";
        return d;
    }

    void check_case(const Case &c)
    {
        SCOPED_TRACE(describe(c));
        Regex::Error err;
        Regex re = Regex::compile(c.pattern, c.flags, &err);
        if (std::strcmp(c.kind, "error") == 0)
        {
            EXPECT_FALSE(re.valid()) << "python says: " << c.spans;
            EXPECT_TRUE(bool(err));
            return;
        }
        ASSERT_TRUE(re.valid()) << "compile error: " << (err.message ? err.message : "?") << " at " << err.offset;
        StringView text(c.text, static_cast<std::size_t>(c.text_len));
        Match m;
        if (std::strcmp(c.kind, "search") == 0 || std::strcmp(c.kind, "search1") == 0)
        {
            std::size_t pos = c.kind[6] == '1' ? static_cast<std::size_t>(c.pos) : 0;
            bool ok = re.search(text, &m, pos);
            EXPECT_EQ(ok, c.ok == 1);
            if (ok && c.ok)
            {
                EXPECT_EQ(span_string(m), c.spans);
            }
        }
        else if (std::strcmp(c.kind, "match") == 0 || std::strcmp(c.kind, "match1") == 0)
        {
            std::size_t pos = c.kind[5] == '1' ? static_cast<std::size_t>(c.pos) : 0;
            bool ok = re.match(text, &m, pos);
            EXPECT_EQ(ok, c.ok == 1);
            if (ok && c.ok)
            {
                EXPECT_EQ(span_string(m), c.spans);
            }
        }
        else if (std::strcmp(c.kind, "fullmatch") == 0)
        {
            bool ok = re.fullmatch(text, &m);
            EXPECT_EQ(ok, c.ok == 1);
            if (ok && c.ok)
            {
                EXPECT_EQ(span_string(m), c.spans);
            }
        }
        else if (std::strcmp(c.kind, "findall") == 0)
        {
            ct::Vector<String> got = re.findall(text);
            ASSERT_EQ(got.size(), std::size_t(c.ok));
            for (std::size_t i = 0; i < got.size(); ++i)
                EXPECT_EQ(std::string(got[i].c_str(), got[i].size()), c.list[i]) << "item " << i;
        }
        else if (std::strcmp(c.kind, "finditer") == 0)
        {
            ct::Vector<Match> got = re.finditer(text);
            ASSERT_EQ(got.size(), std::size_t(c.ok));
            std::string all;
            for (std::size_t i = 0; i < got.size(); ++i)
            {
                if (i)
                    all += ';';
                all += span_string(got[i]);
            }
            EXPECT_EQ(all, c.spans);
        }
        else if (std::strcmp(c.kind, "split") == 0 || std::strcmp(c.kind, "split1") == 0)
        {
            std::size_t maxsplit = c.kind[5] == '1' ? 1 : 0;
            ct::Vector<String> got = re.split(text, maxsplit);
            ASSERT_EQ(got.size(), std::size_t(c.ok));
            for (std::size_t i = 0; i < got.size(); ++i)
                EXPECT_EQ(std::string(got[i].c_str(), got[i].size()), c.list[i]) << "item " << i;
        }
        else if (std::strcmp(c.kind, "sub") == 0 || std::strcmp(c.kind, "sub1") == 0)
        {
            std::size_t count = c.kind[3] == '1' ? 1 : 0;
            String got = re.sub(text, c.arg, count);
            EXPECT_EQ(std::string(got.c_str(), got.size()), c.list[0]);
        }
        else
        {
            FAIL() << "unknown kind " << c.kind;
        }
    }

}

TEST(Regex, MatchesPython)
{
    for (const Case &c : kCases)
        check_case(c);
}

TEST(Regex, InvalidRegexIsFalsy)
{
    Regex::Error err;
    Regex re = Regex::compile("(", 0, &err);
    EXPECT_FALSE(re);
    EXPECT_TRUE(bool(err));
    EXPECT_STREQ(err.message, "missing ), unterminated subpattern");
    EXPECT_EQ(err.offset, 0u);
    EXPECT_FALSE(Regex().valid());
}

TEST(Regex, ErrorOffsets)
{
    Regex::Error err;
    Regex::compile("ab*+*", 0, &err);
    EXPECT_STREQ(err.message, "multiple repeat");
    EXPECT_EQ(err.offset, 4u);
    Regex::compile("a\\q", 0, &err);
    EXPECT_STREQ(err.message, "bad escape");
    EXPECT_EQ(err.offset, 1u);
}

TEST(Regex, NamedGroups)
{
    Regex re = Regex::compile("(?P<year>\\d{4})-(?P<month>\\d{2})");
    ASSERT_TRUE(re);
    EXPECT_EQ(re.group_count(), 2u);
    EXPECT_EQ(re.group_index("year"), 1);
    EXPECT_EQ(re.group_index("month"), 2);
    EXPECT_EQ(re.group_index("day"), -1);
    EXPECT_EQ(sv(re.group_name(1)), "year");
    EXPECT_EQ(sv(re.group_name(0)), "");
    Match m;
    ASSERT_TRUE(re.search("on 2026-09", &m));
    EXPECT_EQ(sv(m.group("year")), "2026");
    EXPECT_EQ(sv(m["month"]), "09");
    EXPECT_EQ(m.start("month"), 8u);
    EXPECT_EQ(m.end("month"), 10u);
    EXPECT_TRUE(m.matched("year"));
}

TEST(Regex, MatchObject)
{
    Regex re = Regex::compile("(a)|(b)");
    Match m;
    ASSERT_TRUE(re.search("xb", &m));
    EXPECT_EQ(m.groups(), 2u);
    EXPECT_FALSE(m.matched(1));
    EXPECT_TRUE(m.matched(2));
    EXPECT_EQ(m.start(1), Match::npos);
    EXPECT_TRUE(m.group(1).empty());
    EXPECT_EQ(sv(m.group(2)), "b");
    EXPECT_EQ(sv(m[0]), "b");
    EXPECT_EQ(m.regex(), &re);
    EXPECT_EQ(sv(m.text()), "xb");
    EXPECT_FALSE(Match().valid());
}

TEST(Regex, SubWithCallback)
{
    Regex re = Regex::compile("\\d+");
    String out = re.sub("a1b22c333", [](const Match &m) {
        String r;
        r.append_number(static_cast<int>(m.group().size()));
        return r;
    });
    EXPECT_EQ(std::string(out.c_str()), "a1b2c3");
    out = re.sub("a1b22c333", [](const Match &) { return String("#"); }, 2);
    EXPECT_EQ(std::string(out.c_str()), "a#b#c333");
}

TEST(Regex, Escape)
{
    String e = Regex::escape("a.b*c?(d) [e]{f}|g^h$i\\j-k#l m\tn&o~p");
    EXPECT_EQ(std::string(e.c_str()), "a\\.b\\*c\\?\\(d\\)\\ \\[e\\]\\{f\\}\\|g\\^h\\$i\\\\j\\-k\\#l\\ m\\\tn\\&o\\~p");
    String plain = Regex::escape("abc_123 é");
    EXPECT_EQ(std::string(plain.c_str()), "abc_123\\ é");
    Regex re = Regex::compile(Regex::escape("1+1=2?"));
    EXPECT_TRUE(re.fullmatch("1+1=2?"));
    EXPECT_FALSE(re.fullmatch("11=2"));
}

TEST(Regex, PathologicalNestedQuantifiersFinish)
{
    Regex re = Regex::compile("^(a|a)*$");
    EXPECT_TRUE(re.fullmatch("aaaaaaaaaaaaaaaaaaaa"));
    Regex re2 = Regex::compile("(a*)*b");
    std::string text(18, 'a');
    EXPECT_FALSE(re2.search(StringView(text.data(), text.size())));
    Regex re3 = Regex::compile("(x+x+)+y");
    std::string xs(2000, 'x');
    EXPECT_FALSE(re3.search(StringView(xs.data(), xs.size())));
    Regex re4 = Regex::compile("^(a+)+$");
    std::string as(2000, 'a');
    as += 'b';
    EXPECT_FALSE(re4.search(StringView(as.data(), as.size())));
    Regex re5 = Regex::compile("(a|aa)+$");
    EXPECT_FALSE(re5.search(StringView(as.data(), as.size())));
    Regex re6 = Regex::compile("(?:a{2,5}){2,5}b");
    EXPECT_FALSE(re6.search(StringView(xs.data(), xs.size())));
    EXPECT_TRUE(re6.search(StringView(as.data(), as.size())));
    Regex re7 = Regex::compile("(.*a){12}b");
    EXPECT_FALSE(re7.search(StringView(xs.data(), xs.size())));
    EXPECT_TRUE(re7.search(StringView(as.data(), as.size())));
}

TEST(Regex, LongInputsAndDeepBacktracking)
{
    std::string text(100000, 'a');
    text += 'b';
    Regex re = Regex::compile("a*b");
    Match m;
    ASSERT_TRUE(re.search(StringView(text.data(), text.size()), &m));
    EXPECT_EQ(m.start(), 0u);
    EXPECT_EQ(m.end(), text.size());
    Regex re2 = Regex::compile("(a)*b");
    ASSERT_TRUE(re2.search(StringView(text.data(), text.size()), &m));
    EXPECT_EQ(m.start(1), text.size() - 2);
    Regex re3 = Regex::compile("a*?b");
    ASSERT_TRUE(re3.fullmatch(StringView(text.data(), text.size()), &m));
    std::string only_a(100000, 'a');
    Regex re4 = Regex::compile("a*b");
    EXPECT_FALSE(re4.search(StringView(only_a.data(), only_a.size())));
    Regex re5 = Regex::compile("(?:a|b)*c");
    EXPECT_FALSE(re5.search(StringView(only_a.data(), only_a.size())));
    EXPECT_TRUE(re5.finditer(StringView(only_a.data(), only_a.size())).empty());
}

TEST(Regex, CopyAndMoveKeepWorking)
{
    Regex a = Regex::compile("(\\w+)-(\\w+)");
    Regex b = a;
    Regex c(ct::detail::move(a));
    Match m;
    EXPECT_TRUE(b.search("x foo-bar", &m));
    EXPECT_EQ(sv(m.group(2)), "bar");
    EXPECT_TRUE(c.search("foo-bar", &m));
    EXPECT_EQ(std::string(c.pattern().c_str()), "(\\w+)-(\\w+)");
    ct::Vector<Regex> many;
    for (int i = 0; i < 50; ++i)
        many.push_back(Regex::compile("x+"));
    EXPECT_TRUE(many[49].search("axxb"));
}

TEST(Regex, SearchWithPosSeesLookbehindContext)
{
    Regex re = Regex::compile("(?<=a)b");
    Match m;
    EXPECT_TRUE(re.search("ab", &m, 1));
    EXPECT_EQ(m.start(), 1u);
    EXPECT_FALSE(re.match("ab", nullptr, 0));
    EXPECT_TRUE(re.match("ab", nullptr, 1));
    Regex bol = Regex::compile("^b");
    EXPECT_FALSE(bol.search("ab", nullptr, 1));
    EXPECT_FALSE(bol.match("ab", nullptr, 1));
    Regex any = Regex::compile("b");
    EXPECT_TRUE(any.search("ab", nullptr, 1));
    EXPECT_FALSE(any.search("ab", nullptr, 3));
}

TEST(Regex, EmbeddedNulBytes)
{
    const char text[] = "a\0b\0c";
    StringView sv(text, 5);
    Regex re = Regex::compile("\\x00");
    ct::Vector<Match> ms = re.finditer(sv);
    ASSERT_EQ(ms.size(), 2u);
    EXPECT_EQ(ms[0].start(), 1u);
    EXPECT_EQ(ms[1].start(), 3u);
    Regex any = Regex::compile("^.+$");
    EXPECT_TRUE(any.fullmatch(sv));
}

TEST(Regex, PosAlemDoFimEEncostadoAoFim)
{
    Regex empty = Regex::compile("");
    Match m;
    ASSERT_TRUE(empty.search("abc", &m, 10));
    EXPECT_EQ(m.start(0), 3u);
    EXPECT_EQ(m.end(0), 3u);
    ASSERT_TRUE(empty.match("abc", &m, 10));
    EXPECT_EQ(m.start(0), 3u);
    Regex a = Regex::compile("a");
    EXPECT_FALSE(a.search("abc", &m, 10));
    EXPECT_FALSE(a.fullmatch("abc", &m, 10));
}

TEST(Regex, ContadorDeRepeticaoDemasiadoGrandeEErro)
{
    Regex::Error err;
    Regex re = Regex::compile("a{99999999999999999999}", 0, &err);
    EXPECT_FALSE(static_cast<bool>(re));
    ASSERT_NE(err.message, nullptr);
    EXPECT_STREQ(err.message, "the repetition number is too large");
    Regex::Error err2;
    Regex re2 = Regex::compile("a{1,4294967296}", 0, &err2);
    EXPECT_FALSE(static_cast<bool>(re2));
    Regex ok = Regex::compile("a{2147483647}");
    EXPECT_TRUE(static_cast<bool>(ok));
}

TEST(Regex, LookBehindComLarguraEnormeSaturaEmVezDeFazerOverflow)
{
    Regex::Error err;
    Regex re = Regex::compile("(?<=(?:ab){2000000000})x", 0, &err);
    ASSERT_TRUE(static_cast<bool>(re));
    EXPECT_FALSE(re.search("abababx"));
    Regex nested = Regex::compile("(?<=(?:(?:ab){70000}){70000})x", 0, &err);
    ASSERT_TRUE(static_cast<bool>(nested));
    EXPECT_FALSE(nested.search("abababx"));
}

TEST(Regex, RepeticoesContadasAninhadasCompilamEmTempoLinear)
{
    String pattern("a{2}");
    for (int level = 0; level < 26; ++level)
        pattern = String("(?:") + pattern + "){2}";
    const clock_t start = std::clock();
    Regex re = Regex::compile(pattern);
    const double seconds = double(std::clock() - start) / CLOCKS_PER_SEC;
    ASSERT_TRUE(static_cast<bool>(re));
    EXPECT_LT(seconds, 5.0) << "compilar " << pattern.size() << " bytes demorou " << seconds << " s";
    EXPECT_FALSE(re.fullmatch("aaaa"));
    EXPECT_TRUE(re.search("", nullptr) == false);
    Regex small = Regex::compile("(?:(?:a{2}){2}){2}");
    EXPECT_TRUE(small.fullmatch("aaaaaaaa"));
    EXPECT_FALSE(small.fullmatch("aaaaaaa"));
}

TEST(Regex, MatchesVaziosNaoPartemUtf8)
{
    Regex empty = Regex::compile("");
    String out = empty.sub("\xC3\xA9", "-");
    EXPECT_EQ(out.size(), 4u);
    EXPECT_EQ(std::memcmp(out.data(), "-\xC3\xA9-", 4), 0);
    ct::Vector<String> parts = empty.split("a\xC3\xA9");
    EXPECT_EQ(parts.size(), 4u);
    Regex cont = Regex::compile("[\\x80-\\xbf]");
    EXPECT_FALSE(cont.search("\xC3\xA9"));
    Regex nb = Regex::compile("\\B");
    EXPECT_FALSE(nb.search(""));
    EXPECT_TRUE(nb.search("ab"));
}

TEST(Regex, MaisDeNoventaENoveGruposEUmLimiteGrandeComErro)
{
    String many("(a)");
    for (int i = 1; i < 300; ++i)
        many.append("(a)");
    Regex re = Regex::compile(many);
    ASSERT_TRUE(static_cast<bool>(re));
    EXPECT_EQ(re.group_count(), 300u);
    std::string text(300, 'a');
    Match m;
    ASSERT_TRUE(re.fullmatch(StringView(text.data(), text.size()), &m));
    EXPECT_EQ(m.start(300), 299u);
    EXPECT_EQ(m.end(300), 300u);
    String too_many;
    for (int i = 0; i < 40000; ++i)
        too_many.append("()");
    Regex::Error err;
    Regex bad = Regex::compile(too_many, 0, &err);
    EXPECT_FALSE(static_cast<bool>(bad));
    ASSERT_NE(err.message, nullptr);
    EXPECT_STREQ(err.message, "too many groups");
}

TEST(Regex, AlternanciaDeUmSoCaracterEquivaleAUmaClasse)
{
    Regex re = Regex::compile("(?:a|b)*c");
    std::string text(2000000, 'a');
    for (std::size_t i = 0; i < text.size(); i += 3)
        text[i] = 'b';
    text += 'c';
    Match m;
    ASSERT_TRUE(re.fullmatch(StringView(text.data(), text.size()), &m));
    EXPECT_EQ(m.end(), text.size());
    EXPECT_FALSE(re.search(StringView(text.data(), text.size() - 1)));
    Regex icase = Regex::compile("(?i:a|B)+x");
    EXPECT_TRUE(icase.fullmatch("aAbBx"));
    EXPECT_FALSE(icase.fullmatch("aAcBx"));
    Regex mixed = Regex::compile("(?:a|(?i:b))+");
    EXPECT_TRUE(mixed.fullmatch("aBbA") == false);
    EXPECT_TRUE(mixed.fullmatch("aBba"));
    Regex neg = Regex::compile("(?:[^a]|b)+");
    ASSERT_TRUE(neg.fullmatch("bcdb"));
    EXPECT_FALSE(neg.fullmatch("bca"));
    Regex wide = Regex::compile("(?:\xC3\xA9|a)+");
    EXPECT_TRUE(wide.fullmatch("\xC3\xA9" "a\xC3\xA9"));
    EXPECT_FALSE(wide.fullmatch("\xE9"));
    Regex captured = Regex::compile("(a|b)+");
    ASSERT_TRUE(captured.fullmatch("abba", &m));
    EXPECT_EQ(m.start(1), 3u);
    EXPECT_EQ(m.end(1), 4u);
}

TEST(Regex, SearchComecaEmBytesDeContinuacaoSoltos)
{
    Match m;
    Regex high = Regex::compile("[\\x80-\\xff]");
    ASSERT_TRUE(high.search("\x80", &m));
    EXPECT_EQ(m.start(), 0u);
    EXPECT_EQ(m.end(), 1u);
    EXPECT_EQ(high.finditer("\x80").size(), 1u);
    Regex nonword = Regex::compile("\\W");
    ASSERT_TRUE(nonword.search("\x80", &m));
    EXPECT_EQ(m.start(), 0u);
    Regex not_a = Regex::compile("[^a]");
    ASSERT_TRUE(not_a.search("a\x80", &m));
    EXPECT_EQ(m.start(), 1u);
    EXPECT_EQ(m.end(), 2u);
    EXPECT_EQ(Regex::compile("x*").sub("\x80\x80", "-"), String("-\x80-\x80-"));
    EXPECT_EQ(Regex::compile("[\\x80-\\xff]").split("\xe2\x82").size(), 3u);
    Regex any = Regex::compile(".");
    EXPECT_EQ(any.finditer("\xc3\xa9").size(), 1u);
    EXPECT_EQ(Regex::compile("x*").sub("\xc3\xa9", "-"), String("-\xc3\xa9-"));
}

TEST(Regex, FinditerComPosAlemDoFimEncostaAoFim)
{
    Regex end = Regex::compile("$");
    Match m;
    ASSERT_TRUE(end.search("abc", &m, 10));
    EXPECT_EQ(m.start(), 3u);
    ct::Vector<Match> all = end.finditer("abc", 10);
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].start(), 3u);
    EXPECT_EQ(end.findall("abc", 10).size(), 1u);
}

TEST(Regex, LargurasDeLookBehindSomadasNaoFazemOverflow)
{
    Regex a = Regex::compile("(?<=a{2147483647}a)b");
    EXPECT_FALSE(a.search("aab"));
    Regex b = Regex::compile("(?<=(?:aa){1073741824})b");
    EXPECT_FALSE(b.search("aab"));
    Regex c = Regex::compile("(?<!a{2147483647}a)b");
    EXPECT_TRUE(c.search("aab"));
}

TEST(Regex, MuitosGruposNaoPenalizamPosicoesOndeNadaFoiCapturado)
{
    String pattern = ".z";
    for (int i = 0; i < 20000; ++i)
        pattern += "()";
    Regex re = Regex::compile(pattern);
    ASSERT_TRUE(static_cast<bool>(re));
    std::string text(200000, 'a');
    const std::clock_t begin = std::clock();
    EXPECT_FALSE(re.search(StringView(text.data(), text.size())));
    const double seconds = double(std::clock() - begin) / CLOCKS_PER_SEC;
    EXPECT_LT(seconds, 5.0);
    Match m;
    ASSERT_TRUE(re.search("aaz", &m));
    EXPECT_EQ(m.start(), 1u);
    EXPECT_EQ(m.end(), 3u);
    EXPECT_EQ(m.start(20000), 3u);
    ASSERT_TRUE(re.search("bz", &m));
    EXPECT_EQ(m.start(), 0u);
}
