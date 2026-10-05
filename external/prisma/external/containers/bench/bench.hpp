
#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>

namespace bench {

extern volatile std::uint64_t sink;

inline void escape(const void *p)
{
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "g"(p) : "memory");
#else
    sink += reinterpret_cast<std::uintptr_t>(p) & 1;
#endif
}

template <typename F>
double time_best_ms(F&& f, int reps = 7) {
    using clock = std::chrono::steady_clock;
    double best = 1e300;
    for (int r = 0; r < reps; ++r) {
        auto t0 = clock::now();
        f();
        auto t1 = clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (ms < best)
            best = ms;
    }
    return best;
}

template <typename FCt, typename FStd>
void compare(const char* name, FCt&& fct, FStd&& fstd, int reps = 7) {
    double ct_ms = time_best_ms(fct, reps);
    double std_ms = time_best_ms(fstd, reps);
    std::printf("%-38s %10.3f ms %10.3f ms   x%.2f %s\n", name, ct_ms, std_ms,
                std_ms / ct_ms, ct_ms <= std_ms ? "(ct wins)" : "(std wins)");
}

inline void header(const char* title) {
    std::printf("\n== %s ==\n%-38s %13s %13s   %s\n", title, "benchmark", "ct", "std",
                "speedup");
}

}  