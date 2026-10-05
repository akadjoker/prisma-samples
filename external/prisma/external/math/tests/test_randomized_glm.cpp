#include <cmath>
#include <limits>
#include <random>

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "../mathc.h"

using Math::Box;
using Math::Frustum;
using Math::Mat2;
using Math::Mat3;
using Math::Mat4;
using Math::Quaternion;
using Math::Vec2;
using Math::Vec3;
using Math::Vec4;

namespace {

constexpr float kEps = 2e-4f;

float RandomFloat(std::mt19937 &rng, float low, float high) {
    return std::uniform_real_distribution<float>(low, high)(rng);
}

Vec3 RandomUnitVec3(std::mt19937 &rng) {
    Vec3 value;
    do {
        value = Vec3(RandomFloat(rng, -1.0f, 1.0f), RandomFloat(rng, -1.0f, 1.0f), RandomFloat(rng, -1.0f, 1.0f));
    } while (value.LengthSquared() < 1e-4f);
    return value.Normalized();
}

bool Near(float a, float b, float epsilon = kEps) {
    return std::fabs(a - b) <= epsilon;
}

bool Near(const Vec2 &a, const glm::vec2 &b, float epsilon = kEps) {
    return Near(a.x, b.x, epsilon) && Near(a.y, b.y, epsilon);
}

bool Near(const Vec3 &a, const glm::vec3 &b, float epsilon = kEps) {
    return Near(a.x, b.x, epsilon) && Near(a.y, b.y, epsilon) && Near(a.z, b.z, epsilon);
}

bool Near(const Vec4 &a, const glm::vec4 &b, float epsilon = kEps) {
    return Near(a.x, b.x, epsilon) && Near(a.y, b.y, epsilon) && Near(a.z, b.z, epsilon) && Near(a.w, b.w, epsilon);
}

template <int N, typename MathMatrix, typename GlmMatrix>
bool MatrixNear(const MathMatrix &a, const GlmMatrix &b, float epsilon = kEps) {
    for (int col = 0; col < N; ++col)
        for (int row = 0; row < N; ++row)
            if (!Near(a[col][row], b[col][row], epsilon))
                return false;
    return true;
}

Mat4 RandomAffine(std::mt19937 &rng) {
    Vec3 translation(RandomFloat(rng, -20.0f, 20.0f), RandomFloat(rng, -20.0f, 20.0f), RandomFloat(rng, -20.0f, 20.0f));
    Vec3 scale(RandomFloat(rng, 0.2f, 4.0f), RandomFloat(rng, 0.2f, 4.0f), RandomFloat(rng, 0.2f, 4.0f));
    return Mat4::Translation(translation) * Mat4::RotationX(RandomFloat(rng, -Math::PI, Math::PI)) *
           Mat4::RotationY(RandomFloat(rng, -Math::PI, Math::PI)) * Mat4::RotationZ(RandomFloat(rng, -Math::PI, Math::PI)) *
           Mat4::Scale(scale);
}

glm::mat4 ToGlm(const Mat4 &m) {
    glm::mat4 result(1.0f);
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            result[col][row] = m[col][row];
    return result;
}

} // namespace

TEST(RandomizedGlm, VectorOperations) {
    std::mt19937 rng(0xDEC0DEu);
    for (int i = 0; i < 5'000; ++i) {
        Vec2 a2(RandomFloat(rng, -4.0f, 4.0f), RandomFloat(rng, -4.0f, 4.0f));
        Vec2 b2(RandomFloat(rng, 0.2f, 4.0f), RandomFloat(rng, 0.2f, 4.0f));
        glm::vec2 ga2(a2.x, a2.y), gb2(b2.x, b2.y);
        ASSERT_TRUE(Near(a2 + b2, ga2 + gb2) && Near(a2 - b2, ga2 - gb2) && Near(a2 * b2, ga2 * gb2) && Near(a2 / b2, ga2 / gb2)) << "Vec2 iteration " << i;
        ASSERT_NEAR(a2.Dot(b2), glm::dot(ga2, gb2), kEps) << "Vec2 iteration " << i;
        ASSERT_NEAR(a2.Cross(b2), ga2.x * gb2.y - ga2.y * gb2.x, kEps) << "Vec2 iteration " << i;
        if (a2.LengthSquared() > 1e-4f)
            ASSERT_TRUE(Near(a2.Normalized(), glm::normalize(ga2))) << "Vec2 iteration " << i;

        Vec3 a3(RandomFloat(rng, -4.0f, 4.0f), RandomFloat(rng, -4.0f, 4.0f), RandomFloat(rng, -4.0f, 4.0f));
        Vec3 b3(RandomFloat(rng, 0.2f, 4.0f), RandomFloat(rng, 0.2f, 4.0f), RandomFloat(rng, 0.2f, 4.0f));
        glm::vec3 ga3(a3.x, a3.y, a3.z), gb3(b3.x, b3.y, b3.z);
        ASSERT_TRUE(Near(a3 + b3, ga3 + gb3) && Near(a3 - b3, ga3 - gb3) && Near(a3 * b3, ga3 * gb3) && Near(a3 / b3, ga3 / gb3)) << "Vec3 iteration " << i;
        ASSERT_TRUE(Near(a3.Cross(b3), glm::cross(ga3, gb3))) << "Vec3 iteration " << i;
        ASSERT_NEAR(a3.Dot(b3), glm::dot(ga3, gb3), kEps) << "Vec3 iteration " << i;
        if (a3.LengthSquared() > 1e-4f)
            ASSERT_TRUE(Near(a3.Normalized(), glm::normalize(ga3))) << "Vec3 iteration " << i;

        Vec4 a4(RandomFloat(rng, -4.0f, 4.0f), RandomFloat(rng, -4.0f, 4.0f), RandomFloat(rng, -4.0f, 4.0f), RandomFloat(rng, -4.0f, 4.0f));
        Vec4 b4(RandomFloat(rng, 0.2f, 4.0f), RandomFloat(rng, 0.2f, 4.0f), RandomFloat(rng, 0.2f, 4.0f), RandomFloat(rng, 0.2f, 4.0f));
        glm::vec4 ga4(a4.x, a4.y, a4.z, a4.w), gb4(b4.x, b4.y, b4.z, b4.w);
        ASSERT_TRUE(Near(a4 + b4, ga4 + gb4) && Near(a4 - b4, ga4 - gb4) && Near(a4 * b4, ga4 * gb4) && Near(a4 / b4, ga4 / gb4)) << "Vec4 iteration " << i;
        ASSERT_NEAR(a4.Dot(b4), glm::dot(ga4, gb4), kEps) << "Vec4 iteration " << i;
        if (a4.LengthSquared() > 1e-4f)
            ASSERT_TRUE(Near(a4.Normalized(), glm::normalize(ga4))) << "Vec4 iteration " << i;
    }
}

TEST(RandomizedGlm, Mat2AndMat3Operations) {
    std::mt19937 rng(0xC0FFEEu);
    for (int i = 0; i < 2'000; ++i) {
        float a = RandomFloat(rng, -4.0f, 4.0f), b = RandomFloat(rng, -4.0f, 4.0f);
        float c = RandomFloat(rng, -4.0f, 4.0f), d = RandomFloat(rng, -4.0f, 4.0f);
        Mat2 math2(a, b, c, d);
        glm::mat2 glm2(a, b, c, d);
        Vec2 point(RandomFloat(rng, -5.0f, 5.0f), RandomFloat(rng, -5.0f, 5.0f));
        glm::vec2 glmPoint(point.x, point.y);

        ASSERT_NEAR(math2.Determinant(), glm::determinant(glm2), kEps) << "iteration " << i;
        Vec2 mathResult = math2 * point;
        glm::vec2 glmResult = glm2 * glmPoint;
        ASSERT_TRUE(Near(mathResult.x, glmResult.x) && Near(mathResult.y, glmResult.y)) << "iteration " << i;
        if (std::fabs(glm::determinant(glm2)) > 0.2f)
            ASSERT_TRUE(MatrixNear<2>(math2.Inverse(), glm::inverse(glm2), 1e-3f)) << "iteration " << i;

        Vec3 col0(RandomFloat(rng, -3.0f, 3.0f), RandomFloat(rng, -3.0f, 3.0f), RandomFloat(rng, -3.0f, 3.0f));
        Vec3 col1(RandomFloat(rng, -3.0f, 3.0f), RandomFloat(rng, -3.0f, 3.0f), RandomFloat(rng, -3.0f, 3.0f));
        Vec3 col2(RandomFloat(rng, -3.0f, 3.0f), RandomFloat(rng, -3.0f, 3.0f), RandomFloat(rng, -3.0f, 3.0f));
        Mat3 math3(col0, col1, col2);
        glm::mat3 glm3(glm::vec3(col0.x, col0.y, col0.z), glm::vec3(col1.x, col1.y, col1.z), glm::vec3(col2.x, col2.y, col2.z));

        ASSERT_NEAR(math3.Determinant(), glm::determinant(glm3), 2e-3f) << "iteration " << i;
        if (std::fabs(glm::determinant(glm3)) > 0.2f)
            ASSERT_TRUE(MatrixNear<3>(math3.Inverse(), glm::inverse(glm3), 2e-3f)) << "iteration " << i;
    }
}

TEST(RandomizedGlm, Mat4TransformsProjectionsAndInverses) {
    std::mt19937 rng(0xA11CEu);
    for (int i = 0; i < 1'000; ++i) {
        Mat4 math = RandomAffine(rng);
        glm::mat4 glmMatrix = ToGlm(math);
        ASSERT_TRUE(MatrixNear<4>(math.Inverse(), glm::inverse(glmMatrix), 3e-3f)) << "iteration " << i;
        ASSERT_TRUE(MatrixNear<4>(math.InverseAffine(), glm::inverse(glmMatrix), 3e-3f)) << "iteration " << i;

        Vec3 point(RandomFloat(rng, -10.0f, 10.0f), RandomFloat(rng, -10.0f, 10.0f), RandomFloat(rng, -10.0f, 10.0f));
        ASSERT_TRUE(Near(math.TransformPoint(point), glm::vec3(glmMatrix * glm::vec4(point.x, point.y, point.z, 1.0f)), 2e-3f)) << "iteration " << i;
        ASSERT_TRUE(Near(math.TransformVector(point), glm::vec3(glmMatrix * glm::vec4(point.x, point.y, point.z, 0.0f)), 2e-3f)) << "iteration " << i;

        float fovy = RandomFloat(rng, 0.25f, 2.5f);
        float aspect = RandomFloat(rng, 0.5f, 3.0f);
        float nearZ = RandomFloat(rng, 0.01f, 2.0f);
        float farZ = nearZ + RandomFloat(rng, 1.0f, 1'000.0f);
        ASSERT_TRUE(MatrixNear<4>(Mat4::Perspective(fovy, aspect, nearZ, farZ), glm::perspective(fovy, aspect, nearZ, farZ), 2e-4f)) << "iteration " << i;
    }
}

TEST(RandomizedGlm, QuaternionsMatchGlmRotationsAndInterpolation) {
    std::mt19937 rng(0x51E7u);
    for (int i = 0; i < 2'000; ++i) {
        Vec3 axis = RandomUnitVec3(rng);
        float angle = RandomFloat(rng, -Math::PI, Math::PI);
        Quaternion math = Quaternion::FromAxisAngle(axis, angle);
        glm::quat glmQuat = glm::angleAxis(angle, glm::vec3(axis.x, axis.y, axis.z));
        Vec3 point(RandomFloat(rng, -4.0f, 4.0f), RandomFloat(rng, -4.0f, 4.0f), RandomFloat(rng, -4.0f, 4.0f));

        ASSERT_TRUE(Near(math * point, glmQuat * glm::vec3(point.x, point.y, point.z))) << "iteration " << i;
        ASSERT_TRUE(MatrixNear<3>(math.ToMat3(), glm::mat3_cast(glmQuat), 2e-4f)) << "iteration " << i;

        Quaternion recovered = Quaternion::FromMat3(math.ToMat3());
        ASSERT_TRUE(Near(recovered * point, glmQuat * glm::vec3(point.x, point.y, point.z), 2e-3f)) << "iteration " << i;

        Vec3 otherAxis = RandomUnitVec3(rng);
        float otherAngle = RandomFloat(rng, -Math::PI, Math::PI);
        Quaternion other = Quaternion::FromAxisAngle(otherAxis, otherAngle);
        glm::quat glmOther = glm::angleAxis(otherAngle, glm::vec3(otherAxis.x, otherAxis.y, otherAxis.z));
        float t = RandomFloat(rng, 0.0f, 1.0f);
        ASSERT_TRUE(Near(Quaternion::Slerp(math, other, t) * point,
                         glm::slerp(glmQuat, glmOther, t) * glm::vec3(point.x, point.y, point.z), 2e-3f)) << "iteration " << i;
    }
}

TEST(RandomizedGlm, BoxesAndFrustaAgreeWithClipSpace) {
    std::mt19937 rng(0xB0123u);
    Mat4 view = Mat4::LookAt(Vec3(4.0f, 3.0f, 6.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    Mat4 projection = Mat4::Perspective(1.1f, 16.0f / 9.0f, 0.1f, 100.0f);
    Mat4 viewProjection = projection * view;
    glm::mat4 glmViewProjection = ToGlm(viewProjection);
    Frustum frustum = Frustum::FromViewProjection(viewProjection);

    for (int i = 0; i < 1'000; ++i) {
        Box box(Vec3(RandomFloat(rng, -5.0f, 0.0f), RandomFloat(rng, -5.0f, 0.0f), RandomFloat(rng, -5.0f, 0.0f)),
                Vec3(RandomFloat(rng, 0.1f, 5.0f), RandomFloat(rng, 0.1f, 5.0f), RandomFloat(rng, 0.1f, 5.0f)));
        Mat4 affine = RandomAffine(rng);
        Box transformed = box.Transformed(affine);

        glm::vec3 expectedMin(std::numeric_limits<float>::max());
        glm::vec3 expectedMax(std::numeric_limits<float>::lowest());
        Vec3 corners[8];
        box.GetCorners(corners);
        glm::mat4 glmAffine = ToGlm(affine);
        for (const Vec3 &corner : corners) {
            glm::vec3 transformedCorner = glm::vec3(glmAffine * glm::vec4(corner.x, corner.y, corner.z, 1.0f));
            expectedMin = glm::min(expectedMin, transformedCorner);
            expectedMax = glm::max(expectedMax, transformedCorner);
        }
        ASSERT_TRUE(Near(transformed.min, expectedMin, 3e-3f) && Near(transformed.max, expectedMax, 3e-3f)) << "box iteration " << i;

        Vec3 point(RandomFloat(rng, -200.0f, 200.0f), RandomFloat(rng, -200.0f, 200.0f), RandomFloat(rng, -200.0f, 200.0f));
        glm::vec4 clip = glmViewProjection * glm::vec4(point.x, point.y, point.z, 1.0f);
        bool insideClip = clip.x >= -clip.w && clip.x <= clip.w && clip.y >= -clip.w && clip.y <= clip.w &&
                          clip.z >= -clip.w && clip.z <= clip.w;
        EXPECT_EQ(frustum.ContainsPoint(point), insideClip) << "frustum iteration " << i;
    }
}
