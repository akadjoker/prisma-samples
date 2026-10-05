
#include <ct/arena.hpp>
#include <ct/array.hpp>
#include <ct/flatmap.hpp>
#include <ct/hashmap.hpp>
#include <ct/pool.hpp>
#include <ct/sort.hpp>
#include <ct/string.hpp>
#include <ct/treemap.hpp>
#include <ct/vector.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    unsigned g_seed = 1;
    unsigned rng() { return g_seed = g_seed * 1664525u + 1013904223u; }

    int failures = 0;

    template <typename T>
    T &&detail_move(T &x) { return static_cast<T &&>(x); }

#define CHECK(cond, msg)                                                     \
    do                                                                       \
    {                                                                        \
        if (!(cond))                                                         \
        {                                                                    \
            std::printf("FALHA: %s (linha %d)\n", msg, __LINE__);            \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

    void torture_vector(int iters)
    {
        ct::Vector<int> v;
        std::vector<int> ref;
        for (int i = 0; i < iters; ++i)
        {
            unsigned op = rng() % 12;
            int val = static_cast<int>(rng());
            switch (op)
            {
            case 0:
            case 1:
            case 2:
                v.push_back(val);
                ref.push_back(val);
                break;
            case 3:
                if (!ref.empty())
                {
                    v.pop_back();
                    ref.pop_back();
                }
                break;
            case 4:
                if (!ref.empty())
                {
                    std::size_t pos = rng() % ref.size();
                    v.insert(v.begin() + pos, val);
                    ref.insert(ref.begin() + pos, val);
                }
                else
                {
                    v.push_back(val);
                    ref.push_back(val);
                }
                break;
            case 5:
                if (!ref.empty())
                {
                    std::size_t pos = rng() % ref.size();
                    std::size_t len = rng() % (ref.size() - pos + 1);
                    v.erase(v.begin() + pos, v.begin() + pos + len);
                    ref.erase(ref.begin() + pos, ref.begin() + pos + len);
                }
                break;
            case 6:
            {
                std::size_t n = rng() % 300;
                v.resize(n, val);
                ref.resize(n, val);
                break;
            }
            case 7:
                v.reserve(rng() % 500);
                break;
            case 8:
                v.shrink_to_fit();
                break;
            case 9:
            {
                ct::Vector<int> copy(v);
                CHECK(copy.size() == ref.size(), "vector copy size");
                v = copy; 
                break;
            }
            case 10:
            {
                ct::Vector<int> tmp(detail_move(v));
                v = detail_move(tmp);
                break;
            }
            case 11:
                if (!ref.empty())
                {
                    std::size_t pos = rng() % ref.size();
                    CHECK(v[pos] == ref[pos], "vector conteudo");
                }
                break;
            }
            CHECK(v.size() == ref.size(), "vector size");
        }
        CHECK(std::equal(v.begin(), v.end(), ref.begin()), "vector final");
    }

    void torture_vector_strings(int iters)
    {
        ct::Vector<ct::String> v;
        std::vector<std::string> ref;
        for (int i = 0; i < iters; ++i)
        {
            unsigned op = rng() % 8;
            unsigned len = rng() % 40; 
            std::string s;
            for (unsigned j = 0; j < len; ++j)
                s.push_back(char('a' + rng() % 26));
            switch (op)
            {
            case 0:
            case 1:
            case 2:
            case 3:
                v.emplace_back(s.data(), s.size());
                ref.push_back(s);
                break;
            case 4:
                if (!ref.empty())
                {
                    v.pop_back();
                    ref.pop_back();
                }
                break;
            case 5:
                if (!ref.empty())
                {
                    std::size_t pos = rng() % ref.size();
                    v.insert(v.begin() + pos, ct::String(s.data(), s.size()));
                    ref.insert(ref.begin() + pos, s);
                }
                break;
            case 6:
                if (!ref.empty())
                {
                    std::size_t pos = rng() % ref.size();
                    v.erase(v.begin() + pos);
                    ref.erase(ref.begin() + pos);
                }
                break;
            case 7:
                if (!ref.empty())
                {
                    std::size_t pos = rng() % ref.size();
                    CHECK(v[pos].size() == ref[pos].size(), "vecstr len");
                    CHECK(std::memcmp(v[pos].data(), ref[pos].data(), ref[pos].size()) == 0,
                          "vecstr conteudo");
                    CHECK(std::strlen(v[pos].c_str()) <= ref[pos].size(), "vecstr NUL");
                }
                break;
            }
            CHECK(v.size() == ref.size(), "vecstr size");
        }
    }

    void torture_string(int iters)
    {
        ct::String s;
        std::string ref;
        for (int i = 0; i < iters; ++i)
        {
            unsigned op = rng() % 10;
            char c = char('a' + rng() % 26);
            switch (op)
            {
            case 0:
            case 1:
            case 2:
                s.push_back(c);
                ref.push_back(c);
                break;
            case 3:
                if (!ref.empty())
                {
                    s.pop_back();
                    ref.pop_back();
                }
                break;
            case 4:
            {
                char buf[16];
                unsigned n = rng() % 16;
                for (unsigned j = 0; j < n; ++j)
                    buf[j] = char('A' + rng() % 26);
                s.append(buf, n);
                ref.append(buf, n);
                break;
            }
            case 5: 
                if (ref.size() < 5000)
                {
                    s += s;
                    ref += ref;
                }
                break;
            case 6:
            {
                std::size_t n = rng() % 100;
                s.resize(n, c);
                ref.resize(n, c);
                break;
            }
            case 7:
                if (!ref.empty())
                {
                    std::size_t pos = rng() % ref.size();
                    std::size_t len = rng() % 30;
                    ct::String sub = s.substr(pos, len);
                    std::string rsub = ref.substr(pos, len);
                    CHECK(sub.size() == rsub.size(), "substr len");
                    CHECK(std::memcmp(sub.data(), rsub.data(), rsub.size()) == 0, "substr");
                    break;
                }
                break;
            case 8:
                CHECK(s.find(c) == ref.find(c), "find char");
                break;
            case 9:
            {
                ct::String copy(s);
                ct::String moved(detail_move(copy));
                s = detail_move(moved);
                break;
            }
            }
            CHECK(s.size() == ref.size(), "string size");
            if (i % 64 == 0)
            {
                CHECK(std::memcmp(s.data(), ref.data(), ref.size()) == 0, "string conteudo");
                CHECK(s.c_str()[s.size()] == '\0', "string NUL final");
            }
        }
    }

    void torture_maps(int iters)
    {
        ct::HashMap<int, int> hm;
        ct::TreeMap<int, int> tm;
        ct::FlatMap<int, int> fm;
        std::map<int, int> ref;
        for (int i = 0; i < iters; ++i)
        {
            unsigned op = rng() % 4;
            int k = static_cast<int>(rng() % 3000);
            int val = static_cast<int>(rng());
            switch (op)
            {
            case 0:
            case 1:
                hm.put(k, val);
                tm.put(k, val);
                fm.put(k, val);
                ref[k] = val;
                break;
            case 2:
            {
                bool e = ref.erase(k) > 0;
                CHECK(hm.erase(k) == e, "hashmap erase");
                CHECK(tm.erase(k) == e, "treemap erase");
                CHECK(fm.erase(k) == e, "flatmap erase");
                break;
            }
            case 3:
            {
                auto it = ref.find(k);
                int *h = hm.find(k);
                int *t = tm.find(k);
                int *f = fm.find(k);
                if (it == ref.end())
                {
                    CHECK(!h && !t && !f, "maps find miss");
                }
                else
                {
                    CHECK(h && *h == it->second, "hashmap find");
                    CHECK(t && *t == it->second, "treemap find");
                    CHECK(f && *f == it->second, "flatmap find");
                }
                break;
            }
            }
            CHECK(hm.size() == ref.size(), "hashmap size");
            CHECK(tm.size() == ref.size(), "treemap size");
            CHECK(fm.size() == ref.size(), "flatmap size");
            if (i % 2048 == 0)
                CHECK(tm.validate(), "treemap invariantes RB");
        }

        auto it = ref.begin();
        for (auto &e : tm)
        {
            CHECK(e.key == it->first && e.value == it->second, "treemap ordem final");
            ++it;
        }
        it = ref.begin();
        for (auto &e : fm)
        {
            CHECK(e.key == it->first && e.value == it->second, "flatmap ordem final");
            ++it;
        }
    }

    void torture_pool_arena(int iters)
    {
        struct Obj
        {
            std::uint64_t stamp;
            char pad[24];
        };
        ct::Pool<Obj> pool;
        std::vector<Obj *> alive;
        std::uint64_t next_stamp = 1;
        for (int i = 0; i < iters; ++i)
        {
            if (alive.empty() || (rng() & 3) != 0)
            {
                Obj *o = pool.allocate();
                o->stamp = next_stamp++;
                alive.push_back(o);
            }
            else
            {
                std::size_t idx = rng() % alive.size();
                CHECK(alive[idx]->stamp != 0, "pool stamp vivo");
                alive[idx]->stamp = 0;
                pool.deallocate(alive[idx]);
                alive[idx] = alive.back();
                alive.pop_back();
            }
            CHECK(pool.live() == alive.size(), "pool live");
        }
        for (Obj *o : alive)
            CHECK(o->stamp != 0, "pool stamps finais");

        ct::Arena arena(1024); 
        for (int frame = 0; frame < iters / 100 + 1; ++frame)
        {
            ct::Vector<int, ct::ArenaAlloc> a{ct::ArenaAlloc(arena)};
            ct::Vector<int, ct::ArenaAlloc> b{ct::ArenaAlloc(arena)};
            int n = static_cast<int>(rng() % 500);
            for (int j = 0; j < n; ++j)
            {
                a.push_back(j);
                b.push_back(-j);
            }
            for (int j = 0; j < n; ++j)
            {
                CHECK(a[j] == j, "arena vec a");
                CHECK(b[j] == -j, "arena vec b");
            }

            a.clear();
            b.clear();
            arena.reset();
        }
    }

    void torture_sort(int iters)
    {
        for (int r = 0; r < iters / 500 + 1; ++r)
        {
            std::size_t n = rng() % 600; 
            ct::Vector<int> v;
            std::vector<int> ref;
            unsigned mode = rng() % 4;
            for (std::size_t i = 0; i < n; ++i)
            {
                int x = mode == 0 ? static_cast<int>(rng())
                        : mode == 1 ? static_cast<int>(i)
                        : mode == 2 ? static_cast<int>(n - i)
                                    : static_cast<int>(rng() % 5);
                v.push_back(x);
                ref.push_back(x);
            }
            ct::sort(v.begin(), v.end());
            std::sort(ref.begin(), ref.end());
            for (std::size_t i = 0; i < n; ++i)
                CHECK(v[i] == ref[i], "sort resultado");

            ct::Vector<float> fv;
            std::vector<float> fref;
            for (std::size_t i = 0; i < n; ++i)
            {
                float f = (static_cast<float>(rng()) - 2147483648.0f) / 1000.0f;
                fv.push_back(f);
                fref.push_back(f);
            }
            ct::sort(fv.begin(), fv.end());
            std::sort(fref.begin(), fref.end());
            for (std::size_t i = 0; i < n; ++i)
                CHECK(fv[i] == fref[i], "sort floats");
        }
    }

} 

int main(int argc, char **argv)
{
    unsigned seed = argc > 1 ? static_cast<unsigned>(std::atoi(argv[1])) : 1;
    int iters = argc > 2 ? std::atoi(argv[2]) : 100000;
    g_seed = seed ? seed : 1;

    std::printf("tortura: seed=%u iters=%d\n", seed, iters);
    torture_vector(iters);
    torture_vector_strings(iters / 4);
    torture_string(iters / 4);
    torture_maps(iters);
    torture_pool_arena(iters);
    torture_sort(iters);

    if (failures)
    {
        std::printf("RESULTADO: %d FALHAS\n", failures);
        return 1;
    }
    std::printf("RESULTADO: limpo\n");
    return 0;
}