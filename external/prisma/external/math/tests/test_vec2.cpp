#include <cmath>
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include "../mathc.h"

using Math::Vec2;

constexpr float kEps = 1e-5f;

::testing::AssertionResult VecNear(const Vec2& a, const glm::vec2& b, float eps = kEps) {
    if (std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps)
        return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure()
        << "Vec2(" << a.x << ", " << a.y << ") != glm::vec2(" << b.x << ", " << b.y << ")";
}

TEST(Vec2, Construct) {
    Vec2 a;
    EXPECT_FLOAT_EQ(a.x, 0.0f);
    EXPECT_FLOAT_EQ(a.y, 0.0f);

    Vec2 b(1.0f, 2.0f);
    glm::vec2 gb(1.0f, 2.0f);
    EXPECT_TRUE(VecNear(b, gb));
}

TEST(Vec2, Arithmetic) {
    Vec2 a(1.0f, 2.0f), b(3.0f, -1.0f);
    glm::vec2 ga(1.0f, 2.0f), gb(3.0f, -1.0f);

    EXPECT_TRUE(VecNear(a + b, ga + gb));
    EXPECT_TRUE(VecNear(a - b, ga - gb));
    EXPECT_TRUE(VecNear(a * b, ga * gb));
    EXPECT_TRUE(VecNear(a / b, ga / gb));
    EXPECT_TRUE(VecNear(a * 2.5f, ga * 2.5f));
    EXPECT_TRUE(VecNear(2.5f * a, 2.5f * ga));
}

TEST(Vec2, CompoundAssign) {
    Vec2 a(1.0f, 2.0f);
    glm::vec2 ga(1.0f, 2.0f);
    a += Vec2(1.0f, 1.0f); ga += glm::vec2(1.0f, 1.0f);
    a *= 2.0f; ga *= 2.0f;
    EXPECT_TRUE(VecNear(a, ga));
}

TEST(Vec2, DotCross) {
    Vec2 a(3.0f, 4.0f), b(-1.0f, 2.0f);
    glm::vec2 ga(3.0f, 4.0f), gb(-1.0f, 2.0f);
    EXPECT_NEAR(a.Dot(b), glm::dot(ga, gb), kEps);

    EXPECT_NEAR(a.Cross(b), a.x * b.y - a.y * b.x, kEps);
}

TEST(Vec2, LengthAndNormalize) {
    Vec2 a(3.0f, 4.0f);
    glm::vec2 ga(3.0f, 4.0f);
    EXPECT_NEAR(a.Length(), glm::length(ga), kEps);
    EXPECT_TRUE(VecNear(a.Normalized(), glm::normalize(ga)));
    EXPECT_TRUE(VecNear(a.NormalizedSafe(), glm::normalize(ga)));
}

TEST(Vec2, NormalizeSafeHandlesZeroVector) {
    Vec2 zero(0.0f, 0.0f);
    Vec2 result = zero.NormalizedSafe();
    EXPECT_FLOAT_EQ(result.x, 0.0f);
    EXPECT_FLOAT_EQ(result.y, 0.0f);
}

TEST(Vec2, DistanceLerp) {
    Vec2 a(0.0f, 0.0f), b(3.0f, 4.0f);
    glm::vec2 ga(0.0f, 0.0f), gb(3.0f, 4.0f);
    EXPECT_NEAR(Vec2::Distance(a, b), glm::distance(ga, gb), kEps);
    EXPECT_TRUE(VecNear(Vec2::Lerp(a, b, 0.5f), glm::mix(ga, gb, 0.5f)));
}

TEST(Vec2, MinMaxClamp) {
    Vec2 a(1.0f, 5.0f), b(3.0f, 2.0f);
    glm::vec2 ga(1.0f, 5.0f), gb(3.0f, 2.0f);
    EXPECT_TRUE(VecNear(Vec2::Min(a, b), glm::min(ga, gb)));
    EXPECT_TRUE(VecNear(Vec2::Max(a, b), glm::max(ga, gb)));
    EXPECT_TRUE(VecNear(Vec2::Clamp(Vec2(5.0f, -5.0f), Vec2::Zero, Vec2::One),
                         glm::clamp(glm::vec2(5.0f, -5.0f), glm::vec2(0.0f), glm::vec2(1.0f))));
}

TEST(Vec2, RotateMatchesGlmMat2) {
    Vec2 a(1.0f, 0.0f);
    float angle = Math::PI / 3.0f;
    glm::vec2 ga(1.0f, 0.0f);
    glm::mat2 rot(glm::cos(angle), glm::sin(angle), -glm::sin(angle), glm::cos(angle));
    EXPECT_TRUE(VecNear(a.Rotate(angle), rot * ga));
}