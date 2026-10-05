#include <chrono>
#include <cstdio>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "../mathc.h"

using Math::Quaternion;
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
    std::printf("%-32s %10.3f ms  %12.1f ops/sec\n", label, ms, iterations / ms * 1000.0);
    return ms;
}

int main() {
    const int N = 20'000'000;

    std::printf("mathc::Quaternion vs glm::quat  (%d iterations per test)\n\n", N);

    Time("mathc multiply+rotate", N, [](int n) {
        Vec3 acc(0.0f, 0.0f, 0.0f);
        for (int i = 0; i < n; i++) {
            Quaternion a = Quaternion::FromAxisAngle(Vec3(0, 1, 0), i * 0.0001f);
            Quaternion b = Quaternion::FromAxisAngle(Vec3(1, 0, 0), i * 0.0002f);
            Quaternion c = a * b;
            acc += c * Vec3(1.0f, 0.0f, 0.0f);
            DoNotOptimize(acc);
        }
    });

    Time("glm    multiply+rotate", N, [](int n) {
        glm::vec3 acc(0.0f, 0.0f, 0.0f);
        for (int i = 0; i < n; i++) {
            glm::quat a = glm::angleAxis(i * 0.0001f, glm::vec3(0, 1, 0));
            glm::quat b = glm::angleAxis(i * 0.0002f, glm::vec3(1, 0, 0));
            glm::quat c = a * b;
            acc += c * glm::vec3(1.0f, 0.0f, 0.0f);
            DoNotOptimize(acc);
        }
    });

    const int NS = 5'000'000;
    Quaternion qa = Quaternion::FromAxisAngle(Vec3(0, 1, 0), 0.0f);
    Quaternion qb = Quaternion::FromAxisAngle(Vec3(0, 1, 0), Math::PI * 0.9f);
    glm::quat gqa = glm::angleAxis(0.0f, glm::vec3(0, 1, 0));
    glm::quat gqb = glm::angleAxis(Math::PI * 0.9f, glm::vec3(0, 1, 0));

    Time("mathc slerp", NS, [&](int n) {
        Quaternion acc;
        for (int i = 0; i < n; i++) {
            float t = (i % 1000) * 0.001f;
            acc = Quaternion::Slerp(qa, qb, t);
            DoNotOptimize(acc); 

        }
    });

    Time("glm    slerp", NS, [&](int n) {
        glm::quat acc;
        for (int i = 0; i < n; i++) {
            float t = (i % 1000) * 0.001f;
            acc = glm::slerp(gqa, gqb, t);
            DoNotOptimize(acc);
        }
    });

    return 0;
}