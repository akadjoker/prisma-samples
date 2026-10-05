
#include <ct/regex.hpp>

#include <cstdint>
#include <cstdio>
#include <regex>
#include <string>

#include "bench.hpp"

volatile std::uint64_t bench::sink = 0;

namespace
{

    std::string make_log(int lines)
    {
        std::string out;
        const char *hosts[] = {"alpha", "beta", "gamma", "delta"};
        for (int i = 0; i < lines; ++i)
        {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                          "2026-09-12 10:%02d:%02d host=%s ip=192.168.%d.%d user=user%d@site%d.com latency=%d.%03d ms status=%d\n",
                          (i / 60) % 60, i % 60, hosts[i & 3], (i * 7) % 256, (i * 13) % 256, i % 100, i % 7,
                          (i * 37) % 900, (i * 91) % 1000, (i % 11 == 0) ? 500 : 200);
            out += buf;
        }
        return out;
    }

    std::uint64_t ct_search(const ct::Regex &re, const std::string &text)
    {
        ct::Match m;
        return re.search(ct::StringView(text.data(), text.size()), &m) ? m.start() + m.end() : 0;
    }

    std::uint64_t std_search(const std::regex &re, const std::string &text)
    {
        std::cmatch m;
        return std::regex_search(text.data(), text.data() + text.size(), re) ? 1 : 0;
    }

    std::uint64_t ct_findall(const ct::Regex &re, const std::string &text)
    {
        std::uint64_t acc = 0;
        ct::Vector<ct::Match> ms = re.finditer(ct::StringView(text.data(), text.size()));
        for (std::size_t i = 0; i < ms.size(); ++i)
            acc += ms[i].end() - ms[i].start();
        return acc + ms.size();
    }

    std::uint64_t std_findall(const std::regex &re, const std::string &text)
    {
        std::uint64_t acc = 0;
        for (std::cregex_iterator it(text.data(), text.data() + text.size(), re), end; it != end; ++it)
            acc += static_cast<std::uint64_t>((*it)[0].length()) + 1;
        return acc;
    }

    std::uint64_t ct_sub(const ct::Regex &re, const std::string &text, const char *repl)
    {
        ct::String out = re.sub(ct::StringView(text.data(), text.size()), repl);
        return out.size();
    }

    std::uint64_t std_sub(const std::regex &re, const std::string &text, const char *repl)
    {
        std::string out = std::regex_replace(text, re, repl);
        return out.size();
    }

    std::uint64_t ct_fullmatch_many(const ct::Regex &re, const std::string *items, int count)
    {
        std::uint64_t acc = 0;
        for (int i = 0; i < count; ++i)
            acc += re.fullmatch(ct::StringView(items[i].data(), items[i].size())) ? 1 : 0;
        return acc;
    }

    std::uint64_t std_fullmatch_many(const std::regex &re, const std::string *items, int count)
    {
        std::uint64_t acc = 0;
        for (int i = 0; i < count; ++i)
            acc += std::regex_match(items[i], re) ? 1 : 0;
        return acc;
    }

}

int main()
{
    std::string log = make_log(20000);
    std::printf("log text: %zu bytes\n", log.size());

    struct Pat
    {
        const char *name;
        const char *ct;
        const char *std;
    };
    const Pat pats[] = {
        {"literal 'status=500'", "status=500", "status=500"},
        {"literal 'needle' (absent)", "needle", "needle"},
        {"ip [0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+", "[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+", "[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+"},
        {"email [\\w.]+@[\\w.]+\\.com", "[\\w.]+@[\\w.]+\\.com", "[\\w.]+@[\\w.]+\\.com"},
        {"float latency=(\\d+)\\.(\\d+)", "latency=(\\d+)\\.(\\d+)", "latency=(\\d+)\\.(\\d+)"},
        {"alternation of 4 hosts", "host=(alpha|beta|gamma|delta)", "host=(alpha|beta|gamma|delta)"},
        {"line start \\n2026-\\d\\d-\\d\\d", "\\n2026-\\d\\d-\\d\\d", "\\n2026-\\d\\d-\\d\\d"},
        {"lazy user=(.+?)@", "user=(.+?)@", "user=(.+?)@"},
        {"class run [a-z]+=\\d+", "[a-z]+=\\d+", "[a-z]+=\\d+"},
        {"icase HOST=ALPHA", "HOST=ALPHA", "HOST=ALPHA"},
    };

    bench::header("search (first match in 1.5 MB)");
    for (const Pat &p : pats)
    {
        unsigned ctf = 0;
        auto stdf = std::regex::ECMAScript | std::regex::optimize;
        if (p.name[0] == 'i')
        {
            ctf |= ct::Regex::IgnoreCase;
            stdf |= std::regex::icase;
        }
        ct::Regex cre = ct::Regex::compile(p.ct, ctf);
        std::regex sre(p.std, stdf);
        bench::compare(p.name,
                       [&] { bench::sink += ct_search(cre, log); },
                       [&] { bench::sink += std_search(sre, log); });
    }

    bench::header("finditer / findall over 1.5 MB");
    for (const Pat &p : pats)
    {
        unsigned ctf = 0;
        auto stdf = std::regex::ECMAScript | std::regex::optimize;
        if (p.name[0] == 'i')
        {
            ctf |= ct::Regex::IgnoreCase;
            stdf |= std::regex::icase;
        }
        ct::Regex cre = ct::Regex::compile(p.ct, ctf);
        std::regex sre(p.std, stdf);
        bench::compare(p.name,
                       [&] { bench::sink += ct_findall(cre, log); },
                       [&] { bench::sink += std_findall(sre, log); }, 5);
    }

    bench::header("sub over 1.5 MB");
    {
        ct::Regex cre = ct::Regex::compile("\\s+");
        std::regex sre("\\s+", std::regex::optimize);
        bench::compare("\\s+ -> ' '",
                       [&] { bench::sink += ct_sub(cre, log, " "); },
                       [&] { bench::sink += std_sub(sre, log, " "); }, 5);
        ct::Regex cre2 = ct::Regex::compile("(\\w+)@(\\w+)");
        std::regex sre2("(\\w+)@(\\w+)", std::regex::optimize);
        bench::compare("(\\w+)@(\\w+) -> \\2:\\1",
                       [&] { bench::sink += ct_sub(cre2, log, "\\2:\\1"); },
                       [&] { bench::sink += std_sub(sre2, log, "$2:$1"); }, 5);
    }

    bench::header("fullmatch on 100k short strings");
    {
        std::string items[1000];
        for (int i = 0; i < 1000; ++i)
        {
            char buf[64];
            if (i & 1)
                std::snprintf(buf, sizeof(buf), "user%d@site%d.com", i, i % 9);
            else
                std::snprintf(buf, sizeof(buf), "not an email %d", i);
            items[i] = buf;
        }
        ct::Regex cre = ct::Regex::compile("[a-z0-9]+@[a-z0-9]+\\.(com|org)");
        std::regex sre("[a-z0-9]+@[a-z0-9]+\\.(com|org)", std::regex::optimize);
        bench::compare("email fullmatch x100",
                       [&] { for (int r = 0; r < 100; ++r) bench::sink += ct_fullmatch_many(cre, items, 1000); },
                       [&] { for (int r = 0; r < 100; ++r) bench::sink += std_fullmatch_many(sre, items, 1000); });
    }

    bench::header("pathological (x+x+)+y on 30 x, no match");
    {
        std::string xs(30, 'x');
        ct::Regex cre = ct::Regex::compile("(x+x+)+y");
        std::regex sre("(x+x+)+y", std::regex::optimize);
        bench::compare("(x+x+)+y x1000 [std: 18 x]",
                       [&] { for (int r = 0; r < 1000; ++r) bench::sink += ct_search(cre, xs); },
                       [&] { std::string s18(18, 'x'); for (int r = 0; r < 1000; ++r) bench::sink += std_search(sre, s18); }, 3);
    }

    bench::header("compile");
    {
        bench::compare("compile email regex x2000",
                       [&] { for (int r = 0; r < 2000; ++r) { ct::Regex re = ct::Regex::compile("[\\w.]+@[\\w.]+\\.(com|org|net)"); bench::escape(&re); } },
                       [&] { for (int r = 0; r < 2000; ++r) { std::regex re("[\\w.]+@[\\w.]+\\.(com|org|net)"); bench::escape(&re); } });
    }
    return 0;
}
