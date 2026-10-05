#include <cmath>
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include "../mathc.h"

using Math::Vec4;

constexpr float kEps = 1e-4f;

::testing::AssertionResult VecNear(const Vec4& a, const glm::vec4& b, float eps = kEps) {
    if (std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps &&
        std::fabs(a.z - b.z) < eps && std::fabs(a.w - b.w) < eps)
        return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure()
        << "Vec4(" << a.x << ", " << a.y << ", " << a.z << ", " << a.w << ") != glm::vec4("
        << b.x << ", " << b.y << ", " << b.z << ", " << b.w << ")";
}

TEST(Vec4, ConstructAndAlignment) {
    Vec4 a;
    EXPECT_FLOAT_EQ(a.x, 0.0f);
    EXPECT_EQ(alignof(Vec4), 16u);

    Vec4 b(1.0f, 2.0f, 3.0f, 4.0f);
    glm::vec4 gb(1.0f, 2.0f, 3.0f, 4.0f);
    EXPECT_TRUE(VecNear(b, gb));
}

TEST(Vec4, Arithmetic) {
    Vec4 a(1.0f, 2.0f, -3.0f, 0.5f), b(3.0f, -1.0f, 0.5f, 2.0f);
    glm::vec4 ga(1.0f, 2.0f, -3.0f, 0.5f), gb(3.0f, -1.0f, 0.5f, 2.0f);

    EXPECT_TRUE(VecNear(a + b, ga + gb));
    EXPECT_TRUE(VecNear(a - b, ga - gb));
    EXPECT_TRUE(VecNear(a * b, ga * gb));
    EXPECT_TRUE(VecNear(a / b, ga / gb));
    EXPECT_TRUE(VecNear(a * 2.5f, ga * 2.5f));
    EXPECT_TRUE(VecNear(2.5f * a, 2.5f * ga));
}

TEST(Vec4, CompoundAssign) {
    Vec4 a(1.0f, 2.0f, 3.0f, 4.0f);
    glm::vec4 ga(1.0f, 2.0f, 3.0f, 4.0f);
    a += Vec4(1.0f, 1.0f, 1.0f, 1.0f); ga += glm::vec4(1.0f);
    a *= 2.0f; ga *= 2.0f;
    EXPECT_TRUE(VecNear(a, ga));
}

TEST(Vec4, DotLengthNormalize) {
    Vec4 a(1.0f, 2.0f, 2.0f, 4.0f), b(0.5f, -1.0f, 2.0f, 1.0f);
    glm::vec4 ga(1.0f, 2.0f, 2.0f, 4.0f), gb(0.5f, -1.0f, 2.0f, 1.0f);
    EXPECT_NEAR(a.Dot(b), glm::dot(ga, gb), kEps);
    EXPECT_NEAR(a.Length(), glm::length(ga), kEps);
    EXPECT_TRUE(VecNear(a.Normalized(), glm::normalize(ga)));
    EXPECT_TRUE(VecNear(a.NormalizedSafe(), glm::normalize(ga)));
}

TEST(Vec4, NormalizeSafeHandlesZeroVector) {
    Vec4 result = Vec4::Zero.NormalizedSafe();
    EXPECT_FLOAT_EQ(result.x, 0.0f);
    EXPECT_FLOAT_EQ(result.y, 0.0f);
    EXPECT_FLOAT_EQ(result.z, 0.0f);
    EXPECT_FLOAT_EQ(result.w, 0.0f);
}

TEST(Vec4, DistanceLerp) {
    Vec4 a(0.0f, 0.0f, 0.0f, 0.0f), b(2.0f, 3.0f, 6.0f, 0.0f);
    glm::vec4 ga(0.0f, 0.0f, 0.0f, 0.0f), gb(2.0f, 3.0f, 6.0f, 0.0f);
    EXPECT_NEAR(Vec4::Distance(a, b), glm::distance(ga, gb), kEps);
    EXPECT_TRUE(VecNear(Vec4::Lerp(a, b, 0.5f), glm::mix(ga, gb, 0.5f)));
}

TEST(Vec4, MinMaxClamp) {
    Vec4 a(1.0f, 5.0f, -2.0f, 3.0f), b(3.0f, 2.0f, 4.0f, -1.0f);
    glm::vec4 ga(1.0f, 5.0f, -2.0f, 3.0f), gb(3.0f, 2.0f, 4.0f, -1.0f);
    EXPECT_TRUE(VecNear(Vec4::Min(a, b), glm::min(ga, gb)));
    EXPECT_TRUE(VecNear(Vec4::Max(a, b), glm::max(ga, gb)));
}

TEST(Vec4, SwizzleDown) {
    Vec4 a(1.0f, 2.0f, 3.0f, 4.0f);
    EXPECT_FLOAT_EQ(a.xy().x, 1.0f);
    EXPECT_FLOAT_EQ(a.xy().y, 2.0f);
    EXPECT_FLOAT_EQ(a.xyz().z, 3.0f);
}