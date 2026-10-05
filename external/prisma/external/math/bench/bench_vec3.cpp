#include <chrono>
#include <cstdio>
#include <glm/glm.hpp>
#include "../mathc.h"

using Math::Vec3;
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

    std::printf("mathc::Vec3 vs glm::vec3  (%d iterations per test)\n\n", N);

    Time("mathc add/cross/dot/norm", N, [](int n) {
        Vec3 acc(0.0f, 0.0f, 0.0f);
        for (int i = 0; i < n; i++) {
            Vec3 a(i * 0.001f, i * 0.002f, i * 0.0005f);
            Vec3 b(i * 0.003f, i * 0.004f, i * 0.0007f);
            Vec3 sum = a + b;
            float d = a.Dot(b);
            Vec3 cr = a.Cross(b);
            Vec3 norm = a.Normalized();
            acc += sum * d + cr + norm;
        }
        DoNotOptimize(acc);
    });

    Time("glm   add/cross/dot/norm", N, [](int n) {
        glm::vec3 acc(0.0f, 0.0f, 0.0f);
        for (int i = 0; i < n; i++) {
            glm::vec3 a(i * 0.001f, i * 0.002f, i * 0.0005f);
            glm::vec3 b(i * 0.003f, i * 0.004f, i * 0.0007f);
            glm::vec3 sum = a + b;
            float d = glm::dot(a, b);
            glm::vec3 cr = glm::cross(a, b);
            glm::vec3 norm = glm::normalize(a);
            acc += sum * d + cr + norm;
        }
        DoNotOptimize(acc);
    });

    return 0;
}