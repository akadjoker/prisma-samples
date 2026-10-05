
#include <ct/string.hpp>
#include <ct/vector.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "bench.hpp"

volatile std::uint64_t bench::sink = 0;

namespace
{

    template <typename Str>
    std::uint64_t create_len(int len, int count)
    {
        const char *src = "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOP";
        std::uint64_t acc = 0;
        for (int i = 0; i < count; ++i)
        {
            Str s(src, static_cast<std::size_t>(len));
            acc += s.size() + static_cast<unsigned char>(s[0]);
            bench::escape(&s);
        }
        return acc;
    }

    template <typename Vec, typename Str>
    std::uint64_t vector_of_18(int count)
    {
        Vec v;
        for (int i = 0; i < count; ++i)
            v.push_back(Str("entidade_num_18ch_", 18));
        Vec copy(v); 
        return copy.size() + static_cast<unsigned char>(copy[count / 2][0]);
    }

    template <typename Str>
    std::uint64_t build_lines(int count)
    {
        std::uint64_t acc = 0;
        for (int i = 0; i < count; ++i)
        {
            Str line;
            line += "entity";
            line += '=';
            line += "player";
            line += ';';
            line += "hp";
            line += '=';
            line += "100";
            line += ';';
            acc += line.size();
            bench::escape(&line);
        }
        return acc;
    }

    template <typename Str>
    std::uint64_t push_chars(int count)
    {
        Str s;
        for (int i = 0; i < count; ++i)
            s.push_back(char('a' + (i & 15)));
        return s.size() + static_cast<unsigned char>(s[count / 2]);
    }

    template <typename Str>
    std::uint64_t find_in_text(const Str &text, int reps)
    {
        std::uint64_t acc = 0;
        for (int i = 0; i < reps; ++i)
        {
            acc += text.find("needle", i % 64);
            acc += text.find('Q', i % 64);
        }
        return acc;
    }

    template <typename Vec, typename Str>
    std::uint64_t sort_shorts(int count)
    {
        Vec v;
        unsigned seed = 9;
        char buf[20];
        for (int i = 0; i < count; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            int len = 10 + (seed % 9); 
            for (int j = 0; j < len; ++j)
                buf[j] = char('a' + ((seed >> (j & 7)) & 15));
            v.push_back(Str(buf, static_cast<std::size_t>(len)));
        }
        std::sort(v.begin(), v.end());
        return v.size() + static_cast<unsigned char>(v[count / 2][0]);
    }

    std::uint64_t ct_numbers(int count)
    {
        std::uint64_t acc = 0;
        for (int i = 0; i < count; ++i)
        {
            ct::String s = ct::String::number(i * 37 - 500000);
            acc += s.size();
            bench::escape(&s);
        }
        return acc;
    }

    std::uint64_t std_numbers(int count)
    {
        std::uint64_t acc = 0;
        for (int i = 0; i < count; ++i)
        {
            std::string s = std::to_string(i * 37 - 500000);
            acc += s.size();
            bench::escape(&s);
        }
        return acc;
    }

    std::uint64_t ct_split(int reps)
    {
        ct::String line("pos_x,pos_y,vel_x,vel_y,health,armor,ammo,name_of_entity");
        std::uint64_t acc = 0;
        for (int i = 0; i < reps; ++i)
        {
            auto parts = line.split(',');
            acc += parts.size() + parts[3].size();
        }
        return acc;
    }

    std::uint64_t std_split(int reps)
    {
        std::string line("pos_x,pos_y,vel_x,vel_y,health,armor,ammo,name_of_entity");
        std::uint64_t acc = 0;
        for (int i = 0; i < reps; ++i)
        {
            std::vector<std::string> parts;
            std::size_t start = 0;
            for (std::size_t p = 0; p <= line.size(); ++p)
                if (p == line.size() || line[p] == ',')
                {
                    if (p > start)
                        parts.emplace_back(line, start, p - start);
                    start = p + 1;
                }
            acc += parts.size() + parts[3].size();
        }
        return acc;
    }

} 

int main()
{
    std::printf("ct::String (24 B, SSO 23) vs std::string (32 B, SSO 15)\n");

    bench::header("criação em massa (1M strings)");
    bench::compare("len 8 (SSO nos dois)",
                   [] { bench::sink += create_len<ct::String>(8, 1000000); },
                   [] { bench::sink += create_len<std::string>(8, 1000000); });
    bench::compare("len 18 (SSO nosso, heap do std)",
                   [] { bench::sink += create_len<ct::String>(18, 1000000); },
                   [] { bench::sink += create_len<std::string>(18, 1000000); });
    bench::compare("len 23 (limite do nosso SSO)",
                   [] { bench::sink += create_len<ct::String>(23, 1000000); },
                   [] { bench::sink += create_len<std::string>(23, 1000000); });
    bench::compare("len 48 (heap nos dois)",
                   [] { bench::sink += create_len<ct::String>(48, 1000000); },
                   [] { bench::sink += create_len<std::string>(48, 1000000); });

    bench::header("operações");
    bench::compare("vector 200k strings 18ch + deep copy",
                   [] { bench::sink += vector_of_18<ct::Vector<ct::String>, ct::String>(200000); },
                   [] { bench::sink += vector_of_18<std::vector<std::string>, std::string>(200000); });
    bench::compare("montar 200k linhas key=value",
                   [] { bench::sink += build_lines<ct::String>(200000); },
                   [] { bench::sink += build_lines<std::string>(200000); });
    bench::compare("push_back 4M chars",
                   [] { bench::sink += push_chars<ct::String>(4000000); },
                   [] { bench::sink += push_chars<std::string>(4000000); });
    {
        std::string base;
        for (int i = 0; i < 10000; ++i)
            base += "texto de enchimento sem o alvo aqui dentro pois nao ";
        base += "needle";
        ct::String ctext(base.data(), base.size());
        std::string stext(base);
        bench::compare("find substring em texto de 500 KB x200",
                       [&] { bench::sink += find_in_text(ctext, 200); },
                       [&] { bench::sink += find_in_text(stext, 200); });
    }
    bench::compare("sort 200k strings 10-18ch",
                   [] { bench::sink += sort_shorts<ct::Vector<ct::String>, ct::String>(200000); },
                   [] { bench::sink += sort_shorts<std::vector<std::string>, std::string>(200000); });
    bench::compare("1M int->string (number vs to_string)",
                   [] { bench::sink += ct_numbers(1000000); },
                   [] { bench::sink += std_numbers(1000000); });
    bench::compare("200k split CSV 8 campos",
                   [] { bench::sink += ct_split(200000); },
                   [] { bench::sink += std_split(200000); });

    return 0;
}