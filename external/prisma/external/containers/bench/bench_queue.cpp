
#include <ct/queue.hpp>

#include <cstdint>
#include <queue>
#include <string>

#include "bench.hpp"

volatile std::uint64_t bench::sink = 0;

namespace
{

    constexpr int N = 2000000;

    template <typename Q>
    std::uint64_t push_pop_all()
    {
        Q q;
        for (int i = 0; i < N; ++i)
            q.push(i);
        std::uint64_t acc = 0;
        while (!q.empty())
        {
            acc += static_cast<std::uint64_t>(q.front());
            q.pop();
        }
        return acc;
    }

    template <typename Q>
    std::uint64_t steady_fifo()
    {
        Q q;
        for (int i = 0; i < 1000; ++i)
            q.push(i);
        std::uint64_t acc = 0;
        for (int i = 0; i < N * 4; ++i)
        {
            acc += static_cast<std::uint64_t>(q.front());
            q.pop();
            q.push(i);
        }
        return acc + q.size();
    }

    template <typename Q>
    std::uint64_t bfs_expand()
    {
        Q q;
        q.push(1);
        std::uint64_t acc = 0;
        unsigned seed = 13;
        int budget = N * 2;
        while (!q.empty() && budget-- > 0)
        {
            int v = q.front();
            q.pop();
            acc += static_cast<std::uint64_t>(v);
            seed = seed * 1664525u + 1013904223u;
            unsigned kids = 1 + (seed & 1);
            for (unsigned k = 0; k < kids && q.size() < 100000; ++k)
                q.push(v + 1);
        }
        return acc + q.size();
    }

    template <typename Q>
    std::uint64_t string_events()
    {
        Q q;
        std::uint64_t acc = 0;
        for (int i = 0; i < 200000; ++i)
        {
            q.push("evento_com_payload_grande_" + std::to_string(i));
            if (q.size() > 64)
            {
                acc += q.front().size();
                q.pop();
            }
        }
        return acc + q.size();
    }

} 

int main()
{
    std::printf("ct::Queue vs std::queue — best of 7 runs, -O3 -march=native\n");

    bench::header("FIFO");
    bench::compare("push 2M + pop 2M int",
                   [] { bench::sink += push_pop_all<ct::Queue<int>>(); },
                   [] { bench::sink += push_pop_all<std::queue<int>>(); });
    bench::compare("fila estacionária 8M passagens",
                   [] { bench::sink += steady_fifo<ct::Queue<int>>(); },
                   [] { bench::sink += steady_fifo<std::queue<int>>(); });
    bench::compare("BFS 4M pops (1-2 filhos)",
                   [] { bench::sink += bfs_expand<ct::Queue<int>>(); },
                   [] { bench::sink += bfs_expand<std::queue<int>>(); });
    bench::compare("fila 200k strings (janela 64)",
                   [] { bench::sink += string_events<ct::Queue<std::string>>(); },
                   [] { bench::sink += string_events<std::queue<std::string>>(); });

    return 0;
}