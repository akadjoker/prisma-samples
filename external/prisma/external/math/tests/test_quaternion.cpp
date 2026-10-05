#include <cmath>
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "../mathc.h"

using Math::Mat3;
using Math::Quaternion;
using Math::Vec3;

constexpr float kEps = 1e-4f;

static void ExpectVec3Near(const Vec3& a, const glm::vec3& b, float eps = kEps) {
    EXPECT_NEAR(a.x, b.x, eps);
    EXPECT_NEAR(a.y, b.y, eps);
    EXPECT_NEAR(a.z, b.z, eps);
}

static ::testing::AssertionResult QuatNear(const Quaternion& a, const glm::quat& b, float eps = kEps) {
    if (std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps &&
        std::fabs(a.z - b.z) < eps && std::fabs(a.w - b.w) < eps)
        return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure()
        << "Quaternion(" << a.x << "," << a.y << "," << a.z << "," << a.w << ") != glm::quat("
        << b.x << "," << b.y << "," << b.z << "," << b.w << ")";
}

TEST(Quaternion, IdentityMatchesGlm) {
    Quaternion q;
    glm::quat g(1.0f, 0.0f, 0.0f, 0.0f); 
    EXPECT_TRUE(QuatNear(q, g));
}

TEST(Quaternion, FromAxisAngleMatchesGlmAngleAxis) {
    Vec3 axis(0.0f, 1.0f, 0.0f);
    float angle = Math::PI / 3.0f;
    Quaternion q = Quaternion::FromAxisAngle(axis, angle);
    glm::quat g = glm::angleAxis(angle, glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_TRUE(QuatNear(q, g));
}

TEST(Quaternion, RotateVectorMatchesGlm) {
    Quaternion q = Quaternion::FromAxisAngle(Vec3(0, 0, 1), Math::PI / 2.0f);
    glm::quat g = glm::angleAxis(Math::PI / 2.0f, glm::vec3(0, 0, 1));

    Vec3 v(1.0f, 0.0f, 0.0f);
    Vec3 rotated = q * v;
    glm::vec3 grotated = g * glm::vec3(1.0f, 0.0f, 0.0f);
    ExpectVec3Near(rotated, grotated);
}

TEST(Quaternion, MultiplyComposesRotationsLikeGlm) {
    Quaternion qx = Quaternion::FromAxisAngle(Vec3(1, 0, 0), Math::PI / 4.0f);
    Quaternion qy = Quaternion::FromAxisAngle(Vec3(0, 1, 0), Math::PI / 6.0f);
    Quaternion combined = qx * qy;

    glm::quat gx = glm::angleAxis(Math::PI / 4.0f, glm::vec3(1, 0, 0));
    glm::quat gy = glm::angleAxis(Math::PI / 6.0f, glm::vec3(0, 1, 0));
    glm::quat gcombined = gx * gy;

    EXPECT_TRUE(QuatNear(combined, gcombined));

    Vec3 v(0.3f, 0.7f, -0.2f);
    Vec3 rotated = combined * v;
    glm::vec3 grotated = gcombined * glm::vec3(0.3f, 0.7f, -0.2f);
    ExpectVec3Near(rotated, grotated);
}

TEST(Quaternion, NormalizeConjugateInverse) {
    Quaternion q(1.0f, 2.0f, 3.0f, 4.0f);
    glm::quat g(4.0f, 1.0f, 2.0f, 3.0f); 

    EXPECT_TRUE(QuatNear(q.Normalized(), glm::normalize(g)));
    EXPECT_TRUE(QuatNear(q.Conjugate(), glm::conjugate(g)));
    EXPECT_TRUE(QuatNear(q.Inverse(), glm::inverse(g)));
    EXPECT_NEAR(q.Dot(q), glm::dot(g, g), kEps);
}

TEST(Quaternion, ToMat3MatchesGlmMat3Cast) {
    Quaternion q = Quaternion::FromAxisAngle(Vec3(0.3f, 1.0f, -0.4f).Normalized(), Math::PI / 3.5f);
    glm::quat g(q.w, q.x, q.y, q.z);

    Mat3 m = q.ToMat3();
    glm::mat3 gm = glm::mat3_cast(g);
    for (int c = 0; c < 3; c++)
        for (int r = 0; r < 3; r++)
            EXPECT_NEAR(m[c][r], gm[c][r], kEps);
}

TEST(Quaternion, FromEulerAnglesMatchesGlm) {
    float pitch = 0.4f, yaw = 0.9f, roll = -0.3f;
    Quaternion q = Quaternion::FromEulerAngles(pitch, yaw, roll);
    glm::quat g(glm::vec3(pitch, yaw, roll));
    EXPECT_TRUE(QuatNear(q, g));
}

TEST(Quaternion, ToEulerAnglesRoundTripsThroughFromEulerAngles) {
    const float pitch = 0.4f, yaw = 0.9f, roll = -0.3f;
    const Quaternion q = Quaternion::FromEulerAngles(pitch, yaw, roll);
    const Vec3 euler = q.ToEulerAngles();
    EXPECT_NEAR(euler.x, pitch, 1e-4f);
    EXPECT_NEAR(euler.y, yaw, 1e-4f);
    EXPECT_NEAR(euler.z, roll, 1e-4f);
}

TEST(Quaternion, ToEulerAnglesMatchesGlmEulerAngles) {
    const Quaternion q = Quaternion::FromAxisAngle(Vec3(0.2f, -0.6f, 0.8f).Normalized(), 1.1f);
    const Vec3 euler = q.ToEulerAngles();
    const glm::vec3 g = glm::eulerAngles(glm::quat(q.w, q.x, q.y, q.z));
    EXPECT_NEAR(euler.x, g.x, 1e-4f);
    EXPECT_NEAR(euler.y, g.y, 1e-4f);
    EXPECT_NEAR(euler.z, g.z, 1e-4f);
}

TEST(Quaternion, ToEulerAnglesAtGimbalLockStillRoundTrips) {
    const Quaternion q = Quaternion::FromEulerAngles(0.0f, Math::PI * 0.5f, 0.7f);
    const Vec3 euler = q.ToEulerAngles();
    const Quaternion again = Quaternion::FromEulerAngles(euler.x, euler.y, euler.z);
    EXPECT_TRUE(QuatNear(again, glm::quat(q.w, q.x, q.y, q.z)));
}

TEST(Quaternion, FromMat3RoundTripsThroughToMat3) {
    Quaternion original = Quaternion::FromAxisAngle(Vec3(0.2f, -0.6f, 0.8f).Normalized(), 1.2f);
    Mat3 m = original.ToMat3();
    Quaternion recovered = Quaternion::FromMat3(m);

    Vec3 v(0.5f, -0.3f, 0.9f);
    Vec3 a = original * v;
    Vec3 b = recovered * v;
    ExpectVec3Near(a, glm::vec3(b.x, b.y, b.z), 1e-3f);
}

TEST(Quaternion, FromMat3MatchesGlmQuatCast) {
    glm::quat gsrc = glm::angleAxis(0.9f, glm::normalize(glm::vec3(1.0f, 0.5f, -0.3f)));
    glm::mat3 gm = glm::mat3_cast(gsrc);

    Mat3 m(gm[0][0], gm[0][1], gm[0][2], gm[1][0], gm[1][1], gm[1][2], gm[2][0], gm[2][1], gm[2][2]);
    Quaternion q = Quaternion::FromMat3(m);
    glm::quat gdst = glm::quat_cast(gm);
    EXPECT_TRUE(QuatNear(q, gdst));
}

TEST(Quaternion, FromToRotatesFromOntoTo) {
    Vec3 from(1.0f, 0.0f, 0.0f);
    Vec3 to = Vec3(0.3f, 1.0f, 0.2f).Normalized();
    Quaternion q = Quaternion::FromTo(from, to);
    Vec3 result = (q * from).Normalized();
    ExpectVec3Near(result, glm::vec3(to.x, to.y, to.z), 1e-3f);
}

TEST(Quaternion, FromToHandlesOppositeVectors) {
    Vec3 from(1.0f, 0.0f, 0.0f);
    Vec3 to(-1.0f, 0.0f, 0.0f);
    Quaternion q = Quaternion::FromTo(from, to);
    Vec3 result = (q * from).Normalized();
    ExpectVec3Near(result, glm::vec3(-1.0f, 0.0f, 0.0f), 1e-3f);
}

TEST(Quaternion, SlerpMatchesGlmSlerp) {
    Quaternion a = Quaternion::FromAxisAngle(Vec3(0, 1, 0), 0.0f);
    Quaternion b = Quaternion::FromAxisAngle(Vec3(0, 1, 0), Math::PI / 2.0f);
    glm::quat ga = glm::angleAxis(0.0f, glm::vec3(0, 1, 0));
    glm::quat gb = glm::angleAxis(Math::PI / 2.0f, glm::vec3(0, 1, 0));

    for (float t = 0.0f; t <= 1.0f; t += 0.25f) {
        Quaternion s = Quaternion::Slerp(a, b, t);
        glm::quat gs = glm::slerp(ga, gb, t);
        EXPECT_TRUE(QuatNear(s, gs)) << "at t=" << t;
    }
}

TEST(Quaternion, SlerpEndpointsMatchInputs) {
    Quaternion a = Quaternion::FromAxisAngle(Vec3(1, 0, 0), 0.3f);
    Quaternion b = Quaternion::FromAxisAngle(Vec3(0, 0, 1), 1.1f);
    EXPECT_TRUE(QuatNear(Quaternion::Slerp(a, b, 0.0f), glm::quat(a.w, a.x, a.y, a.z)));
    EXPECT_TRUE(QuatNear(Quaternion::Slerp(a, b, 1.0f), glm::quat(b.w, b.x, b.y, b.z)));
}

TEST(Quaternion, LerpStaysNormalized) {
    Quaternion a = Quaternion::FromAxisAngle(Vec3(1, 0, 0), 0.1f);
    Quaternion b = Quaternion::FromAxisAngle(Vec3(0, 1, 0), 0.2f);
    Quaternion l = Quaternion::Lerp(a, b, 0.5f);
    EXPECT_NEAR(l.Length(), 1.0f, kEps);
}

TEST(Quaternion, LookRotationIsInternallyConsistentWithToMat3) {
    Vec3 forward(0.0f, 0.0f, 1.0f), up(0.0f, 1.0f, 0.0f);
    Quaternion q = Quaternion::LookRotation(forward, up);

    Vec3 rotatedForward = q * Vec3(0.0f, 0.0f, 1.0f);
    ExpectVec3Near(rotatedForward, glm::vec3(0.0f, 0.0f, 1.0f), 1e-3f);
}