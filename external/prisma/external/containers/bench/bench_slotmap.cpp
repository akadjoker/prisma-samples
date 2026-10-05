
#include <ct/slotmap.hpp>
#include <ct/vector.hpp>

#include <cstdint>
#include <random>
#include <unordered_map>
#include <vector>

#include "bench.hpp"

volatile std::uint64_t bench::sink = 0;

namespace
{
    struct Body 
    {
        float x, y, vx, vy;
        float raio, massa;
        std::uint32_t flags, layer;
    };

    constexpr int N = 100000;

    std::vector<std::uint32_t> ordem_aleatoria(int n, unsigned seed)
    {
        std::vector<std::uint32_t> v(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i)
            v[static_cast<std::size_t>(i)] = static_cast<std::uint32_t>(i);
        std::mt19937 rng(seed);
        for (std::size_t i = v.size(); i > 1; --i)
            std::swap(v[i - 1], v[rng() % i]);
        return v;
    }
}

int main()
{
    const std::vector<std::uint32_t> ordem = ordem_aleatoria(N, 1234);

    ct::SlotMap<Body> sm;
    std::vector<ct::Handle<Body>> handles;
    std::unordered_map<std::uint64_t, Body> um;
    handles.reserve(N);
    sm.reserve(N);
    um.reserve(N);
    for (int i = 0; i < N; ++i)
    {
        Body b{float(i), 0, 1, 1, 0.5f, 1, 0, 0};
        handles.push_back(sm.insert(b));
        um.emplace(static_cast<std::uint64_t>(i), b);
    }

    std::vector<ct::Handle<Body>> handles_baralhados;
    std::vector<std::uint64_t> ids_baralhados;
    handles_baralhados.reserve(N);
    ids_baralhados.reserve(N);
    for (std::uint32_t i : ordem)
    {
        handles_baralhados.push_back(handles[i]);
        ids_baralhados.push_back(i);
    }

    bench::header("SlotMap vs unordered_map (100k corpos de 32 B)");

    bench::compare(
        "lookup aleatorio x100k",
        [&] {
            for (int r = 0; r < 10; ++r)
                for (const ct::Handle<Body> &h : handles_baralhados)
                {
                    const Body *b = sm.get(h);
                    bench::sink += static_cast<std::uint64_t>(b->x);
                }
        },
        [&] {
            for (int r = 0; r < 10; ++r)
                for (std::uint64_t id : ids_baralhados)
                {
                    auto it = um.find(id);
                    bench::sink += static_cast<std::uint64_t>(it->second.x);
                }
        });

    bench::compare(
        "integrar tudo (o loop do jogo)",
        [&] {
            for (int r = 0; r < 100; ++r)
                for (Body &b : sm.items())
                {
                    b.x += b.vx * 0.016f;
                    b.y += b.vy * 0.016f;
                }
            bench::sink += static_cast<std::uint64_t>(sm.items()[0].x);
        },
        [&] {
            for (int r = 0; r < 100; ++r)
                for (auto &kv : um)
                {
                    kv.second.x += kv.second.vx * 0.016f;
                    kv.second.y += kv.second.vy * 0.016f;
                }
            bench::sink += static_cast<std::uint64_t>(um.begin()->second.x);
        });

    bench::compare(
        "churn: apagar metade e repor",
        [&] {
            for (int r = 0; r < 10; ++r)
            {
                for (int i = 0; i < N; i += 2)
                    sm.erase(handles[static_cast<std::size_t>(i)]);
                for (int i = 0; i < N; i += 2)
                    handles[static_cast<std::size_t>(i)] =
                        sm.insert(Body{float(i), 0, 1, 1, 0.5f, 1, 0, 0});
            }
            bench::sink += sm.size();
        },
        [&] {
            for (int r = 0; r < 10; ++r)
            {
                for (int i = 0; i < N; i += 2)
                    um.erase(static_cast<std::uint64_t>(i));
                for (int i = 0; i < N; i += 2)
                    um.emplace(static_cast<std::uint64_t>(i),
                               Body{float(i), 0, 1, 1, 0.5f, 1, 0, 0});
            }
            bench::sink += um.size();
        });

    handles_baralhados.clear();
    for (std::uint32_t i : ordem)
        handles_baralhados.push_back(handles[i]);

    bench::header("custo do handle vs indexar um Vector a seco");
    ct::Vector<Body> puro;
    puro.reserve(N);
    for (int i = 0; i < N; ++i)
        puro.push_back(Body{float(i), 0, 1, 1, 0.5f, 1, 0, 0});

    bench::compare(
        "integrar: SlotMap::items() vs Vector",
        [&] {
            for (int r = 0; r < 100; ++r)
                for (Body &b : sm.items())
                    b.x += b.vx * 0.016f;
            bench::sink += static_cast<std::uint64_t>(sm.items()[0].x);
        },
        [&] {
            for (int r = 0; r < 100; ++r)
                for (Body &b : puro)
                    b.x += b.vx * 0.016f;
            bench::sink += static_cast<std::uint64_t>(puro[0].x);
        });

    bench::compare(
        "acesso: handle (com validacao) vs indice cru",
        [&] {
            for (int r = 0; r < 10; ++r)
                for (const ct::Handle<Body> &h : handles_baralhados)
                    bench::sink += static_cast<std::uint64_t>(sm[h].x);
        },
        [&] {
            for (int r = 0; r < 10; ++r)
                for (std::uint64_t i : ids_baralhados)
                    bench::sink += static_cast<std::uint64_t>(puro[i].x);
        });

    std::printf("\nsizeof: Body=%zu Handle=%zu | slots usados=%zu para %zu vivos\n",
                sizeof(Body), sizeof(ct::Handle<Body>), sm.slot_count(), sm.size());
    return 0;
}