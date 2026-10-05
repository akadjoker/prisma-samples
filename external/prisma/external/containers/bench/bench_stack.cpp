
#include <ct/stack.hpp>

#include <cstdint>
#include <stack>
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

    template <typename St>
    std::uint64_t push_pop_all()
    {
        St s;
        for (int i = 0; i < N; ++i)
            s.push(i);
        std::uint64_t acc = 0;
        while (!s.empty())
        {
            acc += static_cast<std::uint64_t>(s.top());
            s.pop();
        }
        return acc;
    }

    template <typename St>
    std::uint64_t dfs_churn()
    {
        St s;
        std::uint64_t acc = 0;
        unsigned seed = 7;
        s.push(0);
        for (int i = 0; i < N * 4; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            if (s.empty() || (seed & 7) < 5)
                s.push(static_cast<int>(seed % 1000));
            else
            {
                acc += static_cast<std::uint64_t>(s.top());
                s.pop();
            }
        }
        return acc + s.size();
    }

    template <typename St>
    std::uint64_t push_pod32()
    {
        St s;
        for (int i = 0; i < N / 4; ++i)
            s.push(Pod32{double(i), 0, 0, 0});
        std::uint64_t acc = static_cast<std::uint64_t>(s.top().a);
        while (!s.empty())
            s.pop();
        return acc;
    }

    template <typename St>
    std::uint64_t shallow_cycles()
    {
        St s;
        std::uint64_t acc = 0;
        for (int frame = 0; frame < 200000; ++frame)
        {
            for (int d = 0; d < 40; ++d)
                s.push(frame + d);
            acc += static_cast<std::uint64_t>(s.top());
            while (!s.empty())
                s.pop();
        }
        return acc;
    }

} 

int main()
{
    std::printf("ct::Stack vs std::stack — best of 7 runs, -O3 -march=native\n");

    bench::header("vs std::stack default (deque)");
    bench::compare("push 2M + pop 2M int",
                   [] { bench::sink += push_pop_all<ct::Stack<int>>(); },
                   [] { bench::sink += push_pop_all<std::stack<int>>(); });
    bench::compare("DFS churn 8M ops",
                   [] { bench::sink += dfs_churn<ct::Stack<int>>(); },
                   [] { bench::sink += dfs_churn<std::stack<int>>(); });
    bench::compare("push+pop 500k x POD 32 B",
                   [] { bench::sink += push_pod32<ct::Stack<Pod32>>(); },
                   [] { bench::sink += push_pod32<std::stack<Pod32>>(); });
    bench::compare("200k ciclos pilha rasa (40)",
                   [] { bench::sink += shallow_cycles<ct::Stack<int>>(); },
                   [] { bench::sink += shallow_cycles<std::stack<int>>(); });

    bench::header("vs std::stack<T, std::vector<T>>");
    bench::compare("push 2M + pop 2M int",
                   [] { bench::sink += push_pop_all<ct::Stack<int>>(); },
                   [] { bench::sink += push_pop_all<std::stack<int, std::vector<int>>>(); });
    bench::compare("DFS churn 8M ops",
                   [] { bench::sink += dfs_churn<ct::Stack<int>>(); },
                   [] { bench::sink += dfs_churn<std::stack<int, std::vector<int>>>(); });
    bench::compare("push+pop 500k x POD 32 B",
                   [] { bench::sink += push_pod32<ct::Stack<Pod32>>(); },
                   [] {
                       bench::sink +=
                           push_pod32<std::stack<Pod32, std::vector<Pod32>>>();
                   });
    bench::compare("200k ciclos pilha rasa (40)",
                   [] { bench::sink += shallow_cycles<ct::Stack<int>>(); },
                   [] {
                       bench::sink +=
                           shallow_cycles<std::stack<int, std::vector<int>>>();
                   });

    return 0;
}