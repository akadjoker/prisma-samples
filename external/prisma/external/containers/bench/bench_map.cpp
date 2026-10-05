
#include <ct/flatmap.hpp>
#include <ct/hashmap.hpp>
#include <ct/hashset.hpp>
#include <ct/treemap.hpp>
#include <ct/string.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "bench.hpp"

volatile std::uint64_t bench::sink = 0;

namespace
{
    constexpr int N = 1000000;
    constexpr int NF = 10000; 

    unsigned next(unsigned &s) { return s = s * 1664525u + 1013904223u; }

    std::uint64_t ct_hash_insert()
    {
        ct::HashMap<int, int> m;
        unsigned s = 1;
        for (int i = 0; i < N; ++i)
            m.put(static_cast<int>(next(s)), i);
        return m.size();
    }
    std::uint64_t std_hash_insert()
    {
        std::unordered_map<int, int> m;
        unsigned s = 1;
        for (int i = 0; i < N; ++i)
            m[static_cast<int>(next(s))] = i;
        return m.size();
    }

    std::uint64_t ct_hash_insert_reserved()
    {
        ct::HashMap<int, int> m;
        m.reserve(N);
        unsigned s = 1;
        for (int i = 0; i < N; ++i)
            m.put(static_cast<int>(next(s)), i);
        return m.size();
    }
    std::uint64_t std_hash_insert_reserved()
    {
        std::unordered_map<int, int> m;
        m.reserve(N);
        unsigned s = 1;
        for (int i = 0; i < N; ++i)
            m[static_cast<int>(next(s))] = i;
        return m.size();
    }

    template <typename M>
    M build_ct_map()
    {
        M m;
        unsigned s = 1;
        for (int i = 0; i < N; ++i)
            m.put(static_cast<int>(next(s)), i);
        return m;
    }

    std::uint64_t ct_hash_hit(ct::HashMap<int, int> &m)
    {
        std::uint64_t acc = 0;
        unsigned s = 1;
        for (int i = 0; i < N; ++i)
        {
            int *v = m.find(static_cast<int>(next(s)));
            if (v)
                acc += static_cast<std::uint64_t>(*v);
        }
        return acc;
    }
    std::uint64_t std_hash_hit(std::unordered_map<int, int> &m)
    {
        std::uint64_t acc = 0;
        unsigned s = 1;
        for (int i = 0; i < N; ++i)
        {
            auto it = m.find(static_cast<int>(next(s)));
            if (it != m.end())
                acc += static_cast<std::uint64_t>(it->second);
        }
        return acc;
    }

    std::uint64_t ct_hash_miss(ct::HashMap<int, int> &m)
    {
        std::uint64_t acc = 0;
        unsigned s = 999;
        for (int i = 0; i < N; ++i)
            acc += m.find(static_cast<int>(next(s) | 1u << 30)) != nullptr;
        return acc;
    }
    std::uint64_t std_hash_miss(std::unordered_map<int, int> &m)
    {
        std::uint64_t acc = 0;
        unsigned s = 999;
        for (int i = 0; i < N; ++i)
            acc += m.find(static_cast<int>(next(s) | 1u << 30)) != m.end();
        return acc;
    }

    template <typename M>
    std::uint64_t iterate_sum(M &m)
    {
        std::uint64_t acc = 0;
        for (auto &e : m)
            acc += static_cast<std::uint64_t>(e.value);
        return acc;
    }

    std::uint64_t ct_hash_churn()
    {
        ct::HashMap<int, int> m;
        unsigned s = 3;
        for (int i = 0; i < N; ++i)
        {
            int k = static_cast<int>(next(s) % 100000);
            if (next(s) & 1)
                m.put(k, i);
            else
                m.erase(k);
        }
        return m.size();
    }
    std::uint64_t std_hash_churn()
    {
        std::unordered_map<int, int> m;
        unsigned s = 3;
        for (int i = 0; i < N; ++i)
        {
            int k = static_cast<int>(next(s) % 100000);
            if (next(s) & 1)
                m[k] = i;
            else
                m.erase(k);
        }
        return m.size();
    }

    std::uint64_t ct_hash_strings()
    {
        ct::HashMap<ct::String, int> m;
        std::uint64_t acc = 0;
        for (int i = 0; i < 100000; ++i)
            m.put(ct::String("entity_key_") + ct::String::number(i), i);
        for (int i = 0; i < 100000; ++i)
            acc += *m.find(ct::String("entity_key_") + ct::String::number(i));
        return acc;
    }
    std::uint64_t std_hash_strings()
    {
        std::unordered_map<std::string, int> m;
        std::uint64_t acc = 0;
        for (int i = 0; i < 100000; ++i)
            m["entity_key_" + std::to_string(i)] = i;
        for (int i = 0; i < 100000; ++i)
            acc += m.find("entity_key_" + std::to_string(i))->second;
        return acc;
    }

    std::uint64_t ct_set_insert_contains()
    {
        ct::HashSet<int> s;
        unsigned r = 1;
        for (int i = 0; i < N; ++i)
            s.insert(static_cast<int>(next(r) % (N / 2)));
        std::uint64_t acc = s.size();
        r = 1;
        for (int i = 0; i < N; ++i)
            acc += s.contains(static_cast<int>(next(r)));
        return acc;
    }
    std::uint64_t std_set_insert_contains()
    {
        std::unordered_set<int> s;
        unsigned r = 1;
        for (int i = 0; i < N; ++i)
            s.insert(static_cast<int>(next(r) % (N / 2)));
        std::uint64_t acc = s.size();
        r = 1;
        for (int i = 0; i < N; ++i)
            acc += s.count(static_cast<int>(next(r)));
        return acc;
    }

    std::uint64_t ct_flat_insert()
    {
        ct::FlatMap<int, int> m;
        unsigned s = 5;
        for (int i = 0; i < NF; ++i)
            m.put(static_cast<int>(next(s)), i);
        return m.size();
    }
    std::uint64_t std_map_insert()
    {
        std::map<int, int> m;
        unsigned s = 5;
        for (int i = 0; i < NF; ++i)
            m[static_cast<int>(next(s))] = i;
        return m.size();
    }

    std::uint64_t ct_flat_lookup(ct::FlatMap<int, int> &m)
    {
        std::uint64_t acc = 0;
        unsigned s = 5;
        for (int r = 0; r < 100; ++r)
            for (int i = 0; i < NF; ++i)
            {
                int *v = m.find(static_cast<int>(next(s)));
                if (v)
                    acc += static_cast<std::uint64_t>(*v);
            }
        return acc;
    }
    std::uint64_t std_map_lookup(std::map<int, int> &m)
    {
        std::uint64_t acc = 0;
        unsigned s = 5;
        for (int r = 0; r < 100; ++r)
            for (int i = 0; i < NF; ++i)
            {
                auto it = m.find(static_cast<int>(next(s)));
                if (it != m.end())
                    acc += static_cast<std::uint64_t>(it->second);
            }
        return acc;
    }

    std::uint64_t ct_flat_iter(ct::FlatMap<int, int> &m)
    {
        std::uint64_t acc = 0;
        for (int r = 0; r < 1000; ++r)
            for (auto &e : m)
                acc += static_cast<std::uint64_t>(e.value);
        return acc;
    }
    std::uint64_t std_map_iter(std::map<int, int> &m)
    {
        std::uint64_t acc = 0;
        for (int r = 0; r < 1000; ++r)
            for (auto &e : m)
                acc += static_cast<std::uint64_t>(e.second);
        return acc;
    }

    std::uint64_t ct_tree_insert()
    {
        ct::TreeMap<int, int> m;
        unsigned s = 5;
        for (int i = 0; i < NF; ++i)
            m.put(static_cast<int>(next(s)), i);
        return m.size();
    }

    std::uint64_t ct_tree_lookup(ct::TreeMap<int, int> &m)
    {
        std::uint64_t acc = 0;
        unsigned s = 5;
        for (int r = 0; r < 100; ++r)
            for (int i = 0; i < NF; ++i)
            {
                int *v = m.find(static_cast<int>(next(s)));
                if (v)
                    acc += static_cast<std::uint64_t>(*v);
            }
        return acc;
    }

    std::uint64_t ct_tree_iter(ct::TreeMap<int, int> &m)
    {
        std::uint64_t acc = 0;
        for (int r = 0; r < 1000; ++r)
            for (auto &e : m)
                acc += static_cast<std::uint64_t>(e.value);
        return acc;
    }

    std::uint64_t ct_tree_churn()
    {
        ct::TreeMap<int, int> m;
        unsigned s = 3;
        for (int i = 0; i < 200000; ++i)
        {
            int k = static_cast<int>(next(s) % 20000);
            if (next(s) & 1)
                m.put(k, i);
            else
                m.erase(k);
        }
        return m.size();
    }
    std::uint64_t std_map_churn()
    {
        std::map<int, int> m;
        unsigned s = 3;
        for (int i = 0; i < 200000; ++i)
        {
            int k = static_cast<int>(next(s) % 20000);
            if (next(s) & 1)
                m[k] = i;
            else
                m.erase(k);
        }
        return m.size();
    }

    std::uint64_t ct_flat_churn()
    {
        ct::FlatMap<int, int> m;
        unsigned s = 3;
        for (int i = 0; i < 200000; ++i)
        {
            int k = static_cast<int>(next(s) % 20000);
            if (next(s) & 1)
                m.put(k, i);
            else
                m.erase(k);
        }
        return m.size();
    }

} 

int main()
{
    std::printf("ct::HashMap vs std::unordered_map (1M chaves int aleatorias)\n");
    bench::header("HashMap");
    bench::compare("insert 1M",
                   [] { bench::sink += ct_hash_insert(); },
                   [] { bench::sink += std_hash_insert(); });
    bench::compare("insert 1M (reserved)",
                   [] { bench::sink += ct_hash_insert_reserved(); },
                   [] { bench::sink += std_hash_insert_reserved(); });
    {
        auto cm = build_ct_map<ct::HashMap<int, int>>();
        std::unordered_map<int, int> sm;
        unsigned s = 1;
        for (int i = 0; i < N; ++i)
            sm[static_cast<int>(next(s))] = i;
        bench::compare("lookup 1M (hit)",
                       [&] { bench::sink += ct_hash_hit(cm); },
                       [&] { bench::sink += std_hash_hit(sm); });
        bench::compare("lookup 1M (miss)",
                       [&] { bench::sink += ct_hash_miss(cm); },
                       [&] { bench::sink += std_hash_miss(sm); });
        bench::compare("iterar tudo",
                       [&] { bench::sink += iterate_sum(cm); },
                       [&] { std::uint64_t a = 0; for (auto &e : sm) a += (std::uint64_t)e.second; bench::sink += a; });
    }
    bench::compare("1M churn put/erase",
                   [] { bench::sink += ct_hash_churn(); },
                   [] { bench::sink += std_hash_churn(); });
    bench::compare("HashSet: 1M insert + 1M contains",
                   [] { bench::sink += ct_set_insert_contains(); },
                   [] { bench::sink += std_set_insert_contains(); });
    bench::compare("100k chaves String put+find",
                   [] { bench::sink += ct_hash_strings(); },
                   [] { bench::sink += std_hash_strings(); });

    std::printf("\nct::FlatMap vs std::map (10k entradas — tamanho tipico de jogo)\n");
    bench::header("FlatMap (ordenado)");
    bench::compare("insert 10k aleatorio",
                   [] { bench::sink += ct_flat_insert(); },
                   [] { bench::sink += std_map_insert(); });
    {
        ct::FlatMap<int, int> fm;
        std::map<int, int> sm;
        unsigned s = 5;
        for (int i = 0; i < NF; ++i)
        {
            int k = static_cast<int>(next(s));
            fm.put(k, i);
            sm[k] = i;
        }
        bench::compare("lookup 1M",
                       [&] { bench::sink += ct_flat_lookup(fm); },
                       [&] { bench::sink += std_map_lookup(sm); });
        bench::compare("iterar ordenado x1000",
                       [&] { bench::sink += ct_flat_iter(fm); },
                       [&] { bench::sink += std_map_iter(sm); });
    }

    std::printf("\nct::TreeMap (red-black + Pool) vs std::map (10k entradas)\n");
    bench::header("TreeMap (ordenado)");
    bench::compare("insert 10k aleatorio",
                   [] { bench::sink += ct_tree_insert(); },
                   [] { bench::sink += std_map_insert(); });
    {
        ct::TreeMap<int, int> tm;
        std::map<int, int> sm;
        unsigned s = 5;
        for (int i = 0; i < NF; ++i)
        {
            int k = static_cast<int>(next(s));
            tm.put(k, i);
            sm[k] = i;
        }
        bench::compare("lookup 1M",
                       [&] { bench::sink += ct_tree_lookup(tm); },
                       [&] { bench::sink += std_map_lookup(sm); });
        bench::compare("iterar ordenado x1000",
                       [&] { bench::sink += ct_tree_iter(tm); },
                       [&] { bench::sink += std_map_iter(sm); });
    }
    bench::compare("200k churn put/erase",
                   [] { bench::sink += ct_tree_churn(); },
                   [] { bench::sink += std_map_churn(); });

    std::printf("\nFlatMap vs TreeMap (os dois ct, ordenados — coluna ct=Flat, std=Tree)\n");
    bench::header("Flat vs Tree");
    bench::compare("insert 10k aleatorio",
                   [] { bench::sink += ct_flat_insert(); },
                   [] { bench::sink += ct_tree_insert(); });
    {
        ct::FlatMap<int, int> fm;
        ct::TreeMap<int, int> tm;
        unsigned s = 5;
        for (int i = 0; i < NF; ++i)
        {
            int k = static_cast<int>(next(s));
            fm.put(k, i);
            tm.put(k, i);
        }
        bench::compare("lookup 1M",
                       [&] { bench::sink += ct_flat_lookup(fm); },
                       [&] { bench::sink += ct_tree_lookup(tm); });
        bench::compare("iterar ordenado x1000",
                       [&] { bench::sink += ct_flat_iter(fm); },
                       [&] { bench::sink += ct_tree_iter(tm); });
    }
    bench::compare("200k churn put/erase",
                   [] { bench::sink += ct_flat_churn(); },
                   [] { bench::sink += ct_tree_churn(); });

    return 0;
}