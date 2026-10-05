#include <cmath>
#include <gtest/gtest.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include "../mathc.h"

using Math::Vec3;

constexpr float kEps = 1e-4f;

::testing::AssertionResult VecNear(const Vec3& a, const glm::vec3& b, float eps = kEps) {
    if (std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps && std::fabs(a.z - b.z) < eps)
        return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure()
        << "Vec3(" << a.x << ", " << a.y << ", " << a.z << ") != glm::vec3("
        << b.x << ", " << b.y << ", " << b.z << ")";
}

TEST(Vec3, Construct) {
    Vec3 a;
    EXPECT_FLOAT_EQ(a.x, 0.0f);
    EXPECT_FLOAT_EQ(a.y, 0.0f);
    EXPECT_FLOAT_EQ(a.z, 0.0f);

    Vec3 b(1.0f, 2.0f, 3.0f);
    glm::vec3 gb(1.0f, 2.0f, 3.0f);
    EXPECT_TRUE(VecNear(b, gb));
}

TEST(Vec3, Arithmetic) {
    Vec3 a(1.0f, 2.0f, -3.0f), b(3.0f, -1.0f, 0.5f);
    glm::vec3 ga(1.0f, 2.0f, -3.0f), gb(3.0f, -1.0f, 0.5f);

    EXPECT_TRUE(VecNear(a + b, ga + gb));
    EXPECT_TRUE(VecNear(a - b, ga - gb));
    EXPECT_TRUE(VecNear(a * b, ga * gb));
    EXPECT_TRUE(VecNear(a * 2.5f, ga * 2.5f));
    EXPECT_TRUE(VecNear(2.5f * a, 2.5f * ga));
}

TEST(Vec3, CompoundAssign) {
    Vec3 a(1.0f, 2.0f, 3.0f);
    glm::vec3 ga(1.0f, 2.0f, 3.0f);
    a += Vec3(1.0f, 1.0f, 1.0f); ga += glm::vec3(1.0f, 1.0f, 1.0f);
    a *= 2.0f; ga *= 2.0f;
    EXPECT_TRUE(VecNear(a, ga));
}

TEST(Vec3, DotCross) {
    Vec3 a(1.0f, 0.0f, 0.0f), b(0.0f, 1.0f, 0.0f);
    glm::vec3 ga(1.0f, 0.0f, 0.0f), gb(0.0f, 1.0f, 0.0f);
    EXPECT_NEAR(a.Dot(b), glm::dot(ga, gb), kEps);
    EXPECT_TRUE(VecNear(a.Cross(b), glm::cross(ga, gb)));
}

TEST(Vec3, LengthAndNormalize) {
    Vec3 a(1.0f, 2.0f, 2.0f);
    glm::vec3 ga(1.0f, 2.0f, 2.0f);
    EXPECT_NEAR(a.Length(), glm::length(ga), kEps);
    EXPECT_TRUE(VecNear(a.Normalized(), glm::normalize(ga)));
    EXPECT_TRUE(VecNear(a.NormalizedSafe(), glm::normalize(ga)));
}

TEST(Vec3, NormalizeSafeHandlesZeroVector) {
    Vec3 result = Vec3::Zero.NormalizedSafe();
    EXPECT_FLOAT_EQ(result.x, 0.0f);
    EXPECT_FLOAT_EQ(result.y, 0.0f);
    EXPECT_FLOAT_EQ(result.z, 0.0f);
}

TEST(Vec3, DistanceLerp) {
    Vec3 a(0.0f, 0.0f, 0.0f), b(2.0f, 3.0f, 6.0f);
    glm::vec3 ga(0.0f, 0.0f, 0.0f), gb(2.0f, 3.0f, 6.0f);
    EXPECT_NEAR(Vec3::Distance(a, b), glm::distance(ga, gb), kEps);
    EXPECT_TRUE(VecNear(Vec3::Lerp(a, b, 0.5f), glm::mix(ga, gb, 0.5f)));
}

TEST(Vec3, MinMaxClamp) {
    Vec3 a(1.0f, 5.0f, -2.0f), b(3.0f, 2.0f, 4.0f);
    glm::vec3 ga(1.0f, 5.0f, -2.0f), gb(3.0f, 2.0f, 4.0f);
    EXPECT_TRUE(VecNear(Vec3::Min(a, b), glm::min(ga, gb)));
    EXPECT_TRUE(VecNear(Vec3::Max(a, b), glm::max(ga, gb)));
}

TEST(Vec3, RotateZMatchesGlmRotateAroundZ) {
    Vec3 a(1.0f, 0.0f, 0.0f);
    float angle = Math::PI / 4.0f;
    glm::vec3 ga(1.0f, 0.0f, 0.0f);
    glm::vec3 rotated = glm::rotateZ(ga, angle);
    EXPECT_TRUE(VecNear(a.RotateZ(angle), rotated));
}

TEST(Vec3, AngleBetween) {
    Vec3 a(1.0f, 0.0f, 0.0f), b(0.0f, 1.0f, 0.0f);
    EXPECT_NEAR(Vec3::AngleBetween(a, b), Math::PI / 2.0f, kEps);
}