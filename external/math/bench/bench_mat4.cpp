#include <chrono>
#include <cstdio>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "../mathc.h"

using Math::Mat4;
using Math::Vec3;
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
    std::printf("%-32s %10.3f ms  %12.1f ops/sec\n", label, ms, iterations / ms * 1000.0);
    return ms;
}

int main() {
    const int N = 5'000'000;

    std::printf("mathc::Mat4 vs glm::mat4  (%d iterations per test)\n\n", N);

    Time("mathc translate*rotate*scale", N, [](int n) {
        Vec4 acc(0.0f, 0.0f, 0.0f, 0.0f);
        for (int i = 0; i < n; i++) {
            Mat4 t = Mat4::Translation(Vec3(i * 0.001f, 0.0f, 0.0f));
            Mat4 r = Mat4::RotationY(i * 0.0001f);
            Mat4 s = Mat4::Scale(Vec3(1.001f, 1.001f, 1.001f));
            Mat4 combined = t * r * s;
            acc += combined * Vec4(1.0f, 1.0f, 1.0f, 1.0f);
        }
        DoNotOptimize(acc);
    });

    Time("glm    translate*rotate*scale", N, [](int n) {
        glm::vec4 acc(0.0f, 0.0f, 0.0f, 0.0f);
        for (int i = 0; i < n; i++) {
            glm::mat4 t = glm::translate(glm::mat4(1.0f), glm::vec3(i * 0.001f, 0.0f, 0.0f));
            glm::mat4 r = glm::rotate(glm::mat4(1.0f), i * 0.0001f, glm::vec3(0, 1, 0));
            glm::mat4 s = glm::scale(glm::mat4(1.0f), glm::vec3(1.001f, 1.001f, 1.001f));
            glm::mat4 combined = t * r * s;
            acc += combined * glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        }
        DoNotOptimize(acc);
    });

    return 0;
}