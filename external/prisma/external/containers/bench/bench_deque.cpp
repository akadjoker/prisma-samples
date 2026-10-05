
#include <ct/deque.hpp>

#include <cstdint>
#include <deque>
#include <memory>
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

    template <typename Dq>
    std::uint64_t push_back_ints()
    {
        Dq d;
        for (int i = 0; i < N; ++i)
            d.push_back(i);
        return static_cast<std::uint64_t>(d.back()) + d.size();
    }

    template <typename Dq>
    std::uint64_t push_front_ints()
    {
        Dq d;
        for (int i = 0; i < N; ++i)
            d.push_front(i);
        return static_cast<std::uint64_t>(d.front()) + d.size();
    }

    template <typename Dq>
    std::uint64_t push_pod32()
    {
        Dq d;
        for (int i = 0; i < N / 4; ++i)
            d.push_back(Pod32{double(i), 0, 0, 0});
        return static_cast<std::uint64_t>(d.size() + std::uint64_t(d.back().a));
    }

    template <typename Dq>
    std::uint64_t fifo_churn_small()
    {
        Dq d;
        for (int i = 0; i < 1000; ++i)
            d.push_back(i);
        std::uint64_t acc = 0;
        for (int i = 0; i < N * 4; ++i)
        {
            acc += static_cast<std::uint64_t>(d.front());
            d.pop_front();
            d.push_back(i);
        }
        return acc + d.size();
    }

    template <typename Dq>
    std::uint64_t sliding_window()
    {
        Dq d;
        std::uint64_t acc = 0;
        for (int i = 0; i < N; ++i)
        {
            d.push_back(i);
            if (d.size() > 256)
                d.pop_front();
            if ((i & 1023) == 0)
            {
                d.push_front(-i);
                acc += static_cast<std::uint64_t>(d.front() + d.back());
                d.pop_front();
            }
        }
        return acc + d.size();
    }

    template <typename Dq>
    std::uint64_t random_access_sum(const Dq &d, const std::vector<int> &idx)
    {
        std::uint64_t s = 0;
        for (int i : idx)
            s += static_cast<std::uint64_t>(d[static_cast<std::size_t>(i)]);
        return s;
    }

    template <typename Dq>
    std::uint64_t iterate_sum(const Dq &d, int reps)
    {
        std::uint64_t s = 0;
        for (int r = 0; r < reps; ++r)
            for (int x : d)
                s += static_cast<std::uint64_t>(x);
        return s;
    }

    CT_NOINLINE std::uint64_t span_sum(const ct::Deque<int> &d, int reps)
    {
        std::uint64_t s = 0;
        for (int r = 0; r < reps; ++r)
        {
            ct::Deque<int>::ConstSpan a = d.first_span(), b = d.second_span();
            for (std::size_t i = 0; i < a.len; ++i)
                s += static_cast<std::uint64_t>(a.ptr[i]);
            for (std::size_t i = 0; i < b.len; ++i)
                s += static_cast<std::uint64_t>(b.ptr[i]);
        }
        return s;
    }

    template <typename Dq>
    std::uint64_t clear_refill_cycles()
    {
        Dq d;
        std::uint64_t acc = 0;
        for (int cycle = 0; cycle < 100; ++cycle)
        {
            d.clear();
            for (int i = 0; i < 100000; ++i)
                d.push_back(i + cycle);
            acc += static_cast<std::uint64_t>(d.back());
        }
        return acc;
    }

    template <typename Dq>
    std::uint64_t string_queue()
    {
        Dq d;
        std::uint64_t acc = 0;
        for (int i = 0; i < 200000; ++i)
        {
            d.push_back("evento_com_payload_grande_" + std::to_string(i));
            if (d.size() > 64)
            {
                acc += d.front().size();
                d.pop_front();
            }
        }
        return acc + d.size();
    }

    template <typename Dq>
    std::uint64_t move_only_queue()
    {
        Dq d;
        std::uint64_t acc = 0;
        for (int i = 0; i < 500000; ++i)
        {
            d.push_back(std::unique_ptr<int>(new int(i)));
            if (d.size() > 128)
            {
                acc += static_cast<std::uint64_t>(*d.front()) & 1;
                d.pop_front();
            }
        }
        return acc + d.size();
    }

} 

int main()
{
    std::printf("ct::Deque vs std::deque — best of 7 runs, -O3 -march=native\n");

    bench::header("pontas");
    bench::compare("push_back 2M int",
                   [] { bench::sink += push_back_ints<ct::Deque<int>>(); },
                   [] { bench::sink += push_back_ints<std::deque<int>>(); });
    bench::compare("push_front 2M int",
                   [] { bench::sink += push_front_ints<ct::Deque<int>>(); },
                   [] { bench::sink += push_front_ints<std::deque<int>>(); });
    bench::compare("push_back 500k x POD 32 B",
                   [] { bench::sink += push_pod32<ct::Deque<Pod32>>(); },
                   [] { bench::sink += push_pod32<std::deque<Pod32>>(); });
    bench::compare("fila FIFO 8M passagens (janela 1k)",
                   [] { bench::sink += fifo_churn_small<ct::Deque<int>>(); },
                   [] { bench::sink += fifo_churn_small<std::deque<int>>(); });
    bench::compare("sliding window 2M (duas pontas)",
                   [] { bench::sink += sliding_window<ct::Deque<int>>(); },
                   [] { bench::sink += sliding_window<std::deque<int>>(); });
    bench::compare("100 ciclos clear+refill 100k",
                   [] { bench::sink += clear_refill_cycles<ct::Deque<int>>(); },
                   [] { bench::sink += clear_refill_cycles<std::deque<int>>(); });

    bench::header("acesso e iteração");
    {
        ct::Deque<int> cd;
        std::deque<int> sd;
        std::vector<int> idx;
        unsigned seed = 17;
        for (int i = 0; i < N; ++i)
        {
            cd.push_back(i);
            sd.push_back(i);
            seed = seed * 1664525u + 1013904223u;
            idx.push_back(static_cast<int>(seed % N));
        }

        for (int i = 0; i < 1000; ++i)
        {
            cd.pop_front();
            cd.push_back(i);
            sd.pop_front();
            sd.push_back(i);
        }
        bench::compare("2M acessos aleatórios d[i]",
                       [&] { bench::sink += random_access_sum(cd, idx); },
                       [&] { bench::sink += random_access_sum(sd, idx); });
        bench::compare("iterar 2M x10 (range-for)",
                       [&] { bench::sink += iterate_sum(cd, 10); },
                       [&] { bench::sink += iterate_sum(sd, 10); });
        bench::compare("iterar 2M x10 (spans ct vs range-for std)",
                       [&] { bench::sink += span_sum(cd, 10); },
                       [&] { bench::sink += iterate_sum(sd, 10); });
    }

    bench::header("tipos complexos");
    bench::compare("fila 200k strings (janela 64)",
                   [] { bench::sink += string_queue<ct::Deque<std::string>>(); },
                   [] { bench::sink += string_queue<std::deque<std::string>>(); });
    bench::compare("fila 500k unique_ptr (janela 128)",
                   [] { bench::sink += move_only_queue<ct::Deque<std::unique_ptr<int>>>(); },
                   [] { bench::sink += move_only_queue<std::deque<std::unique_ptr<int>>>(); });

    return 0;
}