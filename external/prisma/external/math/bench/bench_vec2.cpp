#include <chrono>
#include <cstdio>
#include <glm/glm.hpp>
#include "../mathc.h"

using Math::Vec2;
using Clock = std::chrono::high_resolution_clock;

template <typename T>
static inline void DoNotOptimize(T const& value) {
    asm volatile("" : : "r,m"(value) : "memory");
}

template <typename Fn>
static double Time(const char* label, int iterations, Fn&& fn) {
    auto start = Clock::now();
    fn(iterations);
    auto end = Clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    std::printf("%-28s %10.3f ms  %12.1f ops/sec\n", label, ms, iterations / ms * 1000.0);
    return ms;
}

int main() {
    const int N = 20'000'000;

    std::printf("mathc::Vec2 vs glm::vec2  (%d iterations per test)\n\n", N);

    Time("mathc add/mul/dot/norm", N, [](int n) {
        Vec2 acc(0.0f, 0.0f);
        for (int i = 0; i < n; i++) {
            Vec2 a(i * 0.001f, i * 0.002f);
            Vec2 b(i * 0.003f, i * 0.004f);
            Vec2 sum = a + b;
            float d = a.Dot(b);
            Vec2 norm = a.Normalized();
            acc += sum * d + norm;
        }
        DoNotOptimize(acc);
    });

    Time("glm   add/mul/dot/norm", N, [](int n) {
        glm::vec2 acc(0.0f, 0.0f);
        for (int i = 0; i < n; i++) {
            glm::vec2 a(i * 0.001f, i * 0.002f);
            glm::vec2 b(i * 0.003f, i * 0.004f);
            glm::vec2 sum = a + b;
            float d = glm::dot(a, b);
            glm::vec2 norm = glm::normalize(a);
            acc += sum * d + norm;
        }
        DoNotOptimize(acc);
    });

    return 0;
}