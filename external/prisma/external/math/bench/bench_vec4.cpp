#include <chrono>
#include <cstdio>
#include <glm/glm.hpp>
#include "../mathc.h"

using Math::Vec4;
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

    std::printf("mathc::Vec4 vs glm::vec4  (%d iterations per test)\n\n", N);

    Time("mathc add/mul/dot/norm", N, [](int n) {
        Vec4 acc(0.0f, 0.0f, 0.0f, 0.0f);
        for (int i = 0; i < n; i++) {
            Vec4 a(i * 0.001f, i * 0.002f, i * 0.0005f, i * 0.0003f);
            Vec4 b(i * 0.003f, i * 0.004f, i * 0.0007f, i * 0.0002f);
            Vec4 sum = a + b;
            float d = a.Dot(b);
            Vec4 norm = a.Normalized();
            acc += sum * d + norm;
        }
        DoNotOptimize(acc);
    });

    Time("glm   add/mul/dot/norm", N, [](int n) {
        glm::vec4 acc(0.0f, 0.0f, 0.0f, 0.0f);
        for (int i = 0; i < n; i++) {
            glm::vec4 a(i * 0.001f, i * 0.002f, i * 0.0005f, i * 0.0003f);
            glm::vec4 b(i * 0.003f, i * 0.004f, i * 0.0007f, i * 0.0002f);
            glm::vec4 sum = a + b;
            float d = glm::dot(a, b);
            glm::vec4 norm = glm::normalize(a);
            acc += sum * d + norm;
        }
        DoNotOptimize(acc);
    });

    return 0;
}