
#include <ct/sort.hpp>
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
    constexpr int N = 10000000; 

    unsigned next(unsigned &s) { return s = s * 1664525u + 1013904223u; }

    template <typename T, typename Fill>
    ct::Vector<T> make_data(int n, Fill f)
    {
        ct::Vector<T> v;
        v.reserve(n);
        unsigned s = 42;
        for (int i = 0; i < n; ++i)
            v.push_back(f(s, i));
        return v;
    }

    template <typename T>
    std::uint64_t ct_sort_copy(const ct::Vector<T> &src)
    {
        ct::Vector<T> v(src);
        ct::sort(v.begin(), v.end());
        return static_cast<std::uint64_t>(v[v.size() / 2]);
    }

    template <typename T>
    std::uint64_t ct_stable_sort_copy(const ct::Vector<T> &src)
    {
        ct::Vector<T> v(src);
        ct::stable_sort(v.begin(), v.end());
        return static_cast<std::uint64_t>(v[v.size() / 2]);
    }

    template <typename T>
    std::uint64_t std_stable_sort_copy(const ct::Vector<T> &src)
    {
        ct::Vector<T> v(src);
        std::stable_sort(v.begin(), v.end());
        return static_cast<std::uint64_t>(v[v.size() / 2]);
    }

    template <typename T>
    std::uint64_t std_sort_copy(const ct::Vector<T> &src)
    {
        ct::Vector<T> v(src);
        std::sort(v.begin(), v.end());
        return static_cast<std::uint64_t>(v[v.size() / 2]);
    }

    std::uint64_t ct_sort_small_arrays()
    {

        std::uint64_t acc = 0;
        unsigned s = 7;
        int a[64];
        for (int r = 0; r < 100000; ++r)
        {
            for (int i = 0; i < 64; ++i)
                a[i] = static_cast<int>(next(s));
            ct::sort(a, a + 64);
            acc += static_cast<std::uint64_t>(a[32]);
        }
        return acc;
    }
    std::uint64_t std_sort_small_arrays()
    {
        std::uint64_t acc = 0;
        unsigned s = 7;
        int a[64];
        for (int r = 0; r < 100000; ++r)
        {
            for (int i = 0; i < 64; ++i)
                a[i] = static_cast<int>(next(s));
            std::sort(a, a + 64);
            acc += static_cast<std::uint64_t>(a[32]);
        }
        return acc;
    }

    std::uint64_t ct_sort_strings(const ct::Vector<ct::String> &src)
    {
        ct::Vector<ct::String> v(src);
        ct::sort(v.begin(), v.end());
        return v[v.size() / 2].size();
    }
    std::uint64_t std_sort_strings(const std::vector<std::string> &src)
    {
        std::vector<std::string> v(src);
        std::sort(v.begin(), v.end());
        return v[v.size() / 2].size();
    }

} 

int main()
{
    std::printf("ct::sort (radix p/ numeros + introsort) vs std::sort\n");

    auto rnd_int = make_data<int>(N, [](unsigned &s, int) { return static_cast<int>(s = s * 1664525u + 1013904223u); });
    auto rnd_float = make_data<float>(N, [](unsigned &s, int) {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(s) / 1000.0f - 2e6f;
    });
    auto rnd_i64 = make_data<long long>(N / 2, [](unsigned &s, int) {
        s = s * 1664525u + 1013904223u;
        unsigned hi = s;
        s = s * 1664525u + 1013904223u;
        return (static_cast<long long>(hi) << 32) ^ s;
    });
    auto sorted_int = make_data<int>(N, [](unsigned &, int i) { return i; });
    auto small_range = make_data<int>(N, [](unsigned &s, int) {
        s = s * 1664525u + 1013904223u;
        return static_cast<int>(s % 256); 
    });

    bench::header("estavel (merge sort com buffer n/2) vs std::stable_sort");
    bench::compare("10M int aleatorios (stable)",
                   [&] { bench::sink += ct_stable_sort_copy(rnd_int); },
                   [&] { bench::sink += std_stable_sort_copy(rnd_int); });
    bench::compare("10M int ja ordenados (stable)",
                   [&] { bench::sink += ct_stable_sort_copy(sorted_int); },
                   [&] { bench::sink += std_stable_sort_copy(sorted_int); });
    bench::compare("10M int gama 0..255 (stable)",
                   [&] { bench::sink += ct_stable_sort_copy(small_range); },
                   [&] { bench::sink += std_stable_sort_copy(small_range); });

    bench::header("numeros (caminho radix O(n))");
    bench::compare("10M int aleatorios",
                   [&] { bench::sink += ct_sort_copy(rnd_int); },
                   [&] { bench::sink += std_sort_copy(rnd_int); });
    bench::compare("10M int ja ordenados",
                   [&] { bench::sink += ct_sort_copy(sorted_int); },
                   [&] { bench::sink += std_sort_copy(sorted_int); });
    bench::compare("10M int gama 0..255",
                   [&] { bench::sink += ct_sort_copy(small_range); },
                   [&] { bench::sink += std_sort_copy(small_range); });
    bench::compare("10M float aleatorios",
                   [&] { bench::sink += ct_sort_copy(rnd_float); },
                   [&] { bench::sink += std_sort_copy(rnd_float); });
    bench::compare("5M int64 aleatorios",
                   [&] { bench::sink += ct_sort_copy(rnd_i64); },
                   [&] { bench::sink += std_sort_copy(rnd_i64); });

    bench::header("caminho introsort (generico)");
    bench::compare("100k arrays de 64 ints",
                   [] { bench::sink += ct_sort_small_arrays(); },
                   [] { bench::sink += std_sort_small_arrays(); });
    {
        ct::Vector<ct::String> cs;
        std::vector<std::string> ss;
        unsigned s = 9;
        for (int i = 0; i < 200000; ++i)
        {
            char buf[24];
            int len = 6 + static_cast<int>(next(s) % 16);
            for (int j = 0; j < len; ++j)
                buf[j] = char('a' + (next(s) % 26));
            cs.emplace_back(buf, static_cast<std::size_t>(len));
            ss.emplace_back(buf, static_cast<std::size_t>(len));
        }
        bench::compare("200k strings",
                       [&] { bench::sink += ct_sort_strings(cs); },
                       [&] { bench::sink += std_sort_strings(ss); });
    }

    return 0;
}