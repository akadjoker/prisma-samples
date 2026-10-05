
#include <ct/vector.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include "bench.hpp"

volatile std::uint64_t bench::sink = 0;

namespace
{

    constexpr int N = 2000000;

    struct Pod32 
    {
        double a, b, c, d;
    };

    struct Pod256 
    {
        double m[32];
    };

    struct Mixed 
    {
        std::string name;
        float x, y, z;
        int id;
        Mixed() : x(0), y(0), z(0), id(0) {}
        Mixed(std::string n, int i) : name(std::move(n)), x(1), y(2), z(3), id(i) {}
    };

    template <typename Vec>
    std::uint64_t push_bytes() 
    {
        Vec v;
        for (int i = 0; i < N * 4; ++i)
            v.push_back(static_cast<char>(i));
        return v.size() + static_cast<std::uint64_t>(static_cast<unsigned char>(v.back()));
    }

    template <typename Vec>
    std::uint64_t push_int_cold()
    {
        Vec v;
        for (int i = 0; i < N; ++i)
            v.push_back(i);
        return static_cast<std::uint64_t>(v.back()) + v.size();
    }

    template <typename Vec>
    std::uint64_t push_int_reserved()
    {
        Vec v;
        v.reserve(N);
        for (int i = 0; i < N; ++i)
            v.push_back(i);
        return static_cast<std::uint64_t>(v.back()) + v.size();
    }

    template <typename Vec>
    std::uint64_t push_pod32()
    {
        Vec v;
        for (int i = 0; i < N / 4; ++i)
            v.push_back(Pod32{double(i), 0, 0, 0});
        return static_cast<std::uint64_t>(v.size());
    }

    template <typename Vec>
    std::uint64_t push_pod256()
    {
        Vec v;
        Pod256 p{};
        for (int i = 0; i < N / 16; ++i)
        {
            p.m[0] = i;
            v.push_back(p);
        }
        return static_cast<std::uint64_t>(v.size() + std::uint64_t(v.back().m[0]));
    }

    template <typename Vec>
    std::uint64_t insert_front() 
    {
        Vec v;
        for (int i = 0; i < 20000; ++i)
            v.insert(v.begin(), i);
        return static_cast<std::uint64_t>(v[0]) + v.size();
    }

    template <typename Vec>
    std::uint64_t insert_random_middle()
    {
        Vec v;
        v.push_back(0);
        unsigned seed = 5;
        for (int i = 1; i < 20000; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            v.insert(v.begin() + seed % v.size(), i);
        }
        return static_cast<std::uint64_t>(v[100]) + v.size();
    }

    template <typename Vec>
    std::uint64_t erase_random_middle()
    {
        Vec v;
        for (int i = 0; i < 40000; ++i)
            v.push_back(i);
        unsigned seed = 11;
        while (v.size() > 20000)
        {
            seed = seed * 1664525u + 1013904223u;
            v.erase(v.begin() + seed % v.size());
        }
        return static_cast<std::uint64_t>(v[0]) + v.size();
    }

    template <typename Vec>
    std::uint64_t random_access_sum(const Vec &v, const std::vector<int> &idx)
    {
        std::uint64_t s = 0;
        for (int i : idx)
            s += static_cast<std::uint64_t>(v[static_cast<std::size_t>(i)]);
        return s;
    }

    template <typename Vec>
    std::uint64_t clear_refill_cycles() 
    {
        Vec v;
        std::uint64_t acc = 0;
        for (int cycle = 0; cycle < 100; ++cycle)
        {
            v.clear();
            for (int i = 0; i < 100000; ++i)
                v.push_back(i + cycle);
            acc += static_cast<std::uint64_t>(v.back());
        }
        return acc;
    }

    template <typename Vec>
    std::uint64_t resize_oscillate() 
    {
        Vec v;
        std::uint64_t acc = 0;
        for (int i = 0; i < 1000; ++i)
        {
            v.resize(10000, i);
            acc += static_cast<std::uint64_t>(v.back());
            v.resize(10);
        }
        return acc + v.size();
    }

    template <typename Vec>
    std::uint64_t push_short_strings() 
    {
        Vec v;
        for (int i = 0; i < 200000; ++i)
            v.push_back(std::string("id") + char('0' + i % 10));
        return v.size() + v.back().size();
    }

    template <typename Vec>
    std::uint64_t push_long_strings() 
    {
        Vec v;
        for (int i = 0; i < 200000; ++i)
            v.push_back("uma_string_comprida_que_nao_cabe_no_sso_de_certeza_" +
                        std::to_string(i));
        return v.size() + v.back().size();
    }

    template <typename OuterVec, typename InnerVec>
    std::uint64_t nested_vectors() 
    {
        OuterVec outer;
        for (int i = 0; i < 1000; ++i)
        {
            InnerVec inner;
            for (int j = 0; j < 1000; ++j)
                inner.push_back(i + j);
            outer.push_back(std::move(inner));
        }
        OuterVec copy(outer); 
        return copy.size() + static_cast<std::uint64_t>(copy[999][999]);
    }

    template <typename Vec>
    std::uint64_t move_only_ptrs() 
    {
        Vec v;
        for (int i = 0; i < 500000; ++i)
            v.push_back(std::unique_ptr<int>(new int(i)));
        std::uint64_t s = 0;
        for (const auto &p : v)
            s += static_cast<std::uint64_t>(*p) & 1;
        return s;
    }

    template <typename Vec>
    std::uint64_t mixed_struct_churn() 
    {
        Vec v;
        for (int i = 0; i < 50000; ++i)
            v.push_back(Mixed("entity_" + std::to_string(i), i));
        v.erase(v.begin() + 10000, v.begin() + 40000); 
        for (int i = 0; i < 30000; ++i)
            v.push_back(Mixed("respawn_" + std::to_string(i), i));
        return v.size() + static_cast<std::uint64_t>(v.back().id);
    }

    template <typename Vec>
    std::uint64_t sort_structs()
    {
        Vec v;
        unsigned seed = 3;
        for (int i = 0; i < 200000; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            v.push_back(Pod32{double(seed % 100000), double(i), 0, 0});
        }
        std::sort(v.begin(), v.end(),
                  [](const Pod32 &a, const Pod32 &b) { return a.a < b.a; });
        return static_cast<std::uint64_t>(v.back().a);
    }

} 

int main()
{
    std::printf("ct::Vector vs std::vector — best of 7 runs, -O3 -march=native\n");

    bench::header("dados crus");
    bench::compare("push 8M x 1 byte",
                   [] { bench::sink += push_bytes<ct::Vector<char>>(); },
                   [] { bench::sink += push_bytes<std::vector<char>>(); });
    bench::compare("push 2M int (sem reserve)",
                   [] { bench::sink += push_int_cold<ct::Vector<int>>(); },
                   [] { bench::sink += push_int_cold<std::vector<int>>(); });
    bench::compare("push 2M int (reserved)",
                   [] { bench::sink += push_int_reserved<ct::Vector<int>>(); },
                   [] { bench::sink += push_int_reserved<std::vector<int>>(); });
    bench::compare("push 500k x POD 32 B",
                   [] { bench::sink += push_pod32<ct::Vector<Pod32>>(); },
                   [] { bench::sink += push_pod32<std::vector<Pod32>>(); });
    bench::compare("push 125k x POD 256 B",
                   [] { bench::sink += push_pod256<ct::Vector<Pod256>>(); },
                   [] { bench::sink += push_pod256<std::vector<Pod256>>(); });
    bench::compare("insert(begin) x20k [pior caso]",
                   [] { bench::sink += insert_front<ct::Vector<int>>(); },
                   [] { bench::sink += insert_front<std::vector<int>>(); });
    bench::compare("insert aleatório no meio x20k",
                   [] { bench::sink += insert_random_middle<ct::Vector<int>>(); },
                   [] { bench::sink += insert_random_middle<std::vector<int>>(); });
    bench::compare("erase aleatório no meio x20k",
                   [] { bench::sink += erase_random_middle<ct::Vector<int>>(); },
                   [] { bench::sink += erase_random_middle<std::vector<int>>(); });
    {
        ct::Vector<int> cv;
        std::vector<int> sv, idx;
        unsigned seed = 17;
        for (int i = 0; i < N; ++i)
        {
            cv.push_back(i);
            sv.push_back(i);
            seed = seed * 1664525u + 1013904223u;
            idx.push_back(static_cast<int>(seed % N));
        }
        bench::compare("2M acessos aleatórios",
                       [&] { bench::sink += random_access_sum(cv, idx); },
                       [&] { bench::sink += random_access_sum(sv, idx); });
    }
    bench::compare("100 ciclos clear+refill 100k",
                   [] { bench::sink += clear_refill_cycles<ct::Vector<int>>(); },
                   [] { bench::sink += clear_refill_cycles<std::vector<int>>(); });
    bench::compare("1000 ciclos resize 10k<->10",
                   [] { bench::sink += resize_oscillate<ct::Vector<int>>(); },
                   [] { bench::sink += resize_oscillate<std::vector<int>>(); });

    bench::header("tipos complexos");
    bench::compare("200k strings curtas (SSO)",
                   [] { bench::sink += push_short_strings<ct::Vector<std::string>>(); },
                   [] { bench::sink += push_short_strings<std::vector<std::string>>(); });
    bench::compare("200k strings longas (heap)",
                   [] { bench::sink += push_long_strings<ct::Vector<std::string>>(); },
                   [] { bench::sink += push_long_strings<std::vector<std::string>>(); });
    bench::compare("1000x1000 aninhado + deep copy",
                   [] {
                       bench::sink += nested_vectors<ct::Vector<ct::Vector<int>>,
                                                     ct::Vector<int>>();
                   },
                   [] {
                       bench::sink += nested_vectors<std::vector<std::vector<int>>,
                                                     std::vector<int>>();
                   });
    bench::compare("500k unique_ptr (move-only)",
                   [] { bench::sink += move_only_ptrs<ct::Vector<std::unique_ptr<int>>>(); },
                   [] { bench::sink += move_only_ptrs<std::vector<std::unique_ptr<int>>>(); });
    bench::compare("churn struct{string,floats}",
                   [] { bench::sink += mixed_struct_churn<ct::Vector<Mixed>>(); },
                   [] { bench::sink += mixed_struct_churn<std::vector<Mixed>>(); });
    bench::compare("sort 200k POD 32 B",
                   [] { bench::sink += sort_structs<ct::Vector<Pod32>>(); },
                   [] { bench::sink += sort_structs<std::vector<Pod32>>(); });

    return 0;
}