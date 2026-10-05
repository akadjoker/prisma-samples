#include <cmath>
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "../mathc.h"

using Math::Mat3;
using Math::Mat4;
using Math::Vec3;
using Math::Vec4;

constexpr float kEps = 1e-3f;

::testing::AssertionResult MatNear(const Mat4& a, const glm::mat4& b, float eps = kEps) {
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            if (std::fabs(a[c][r] - b[c][r]) >= eps)
                return ::testing::AssertionFailure() << "mismatch at col " << c << " row " << r
                    << ": " << a[c][r] << " != " << b[c][r];
    return ::testing::AssertionSuccess();
}

static void ExpectVec3Near(const Vec3& a, const glm::vec3& b, float eps = kEps) {
    EXPECT_NEAR(a.x, b.x, eps);
    EXPECT_NEAR(a.y, b.y, eps);
    EXPECT_NEAR(a.z, b.z, eps);
}

TEST(Mat4, IdentityMatchesGlm) {
    Mat4 m;
    glm::mat4 g(1.0f);
    EXPECT_TRUE(MatNear(m, g));
}

TEST(Mat4, ColumnMajorLayoutMatchesGlm) {
    Mat4 m(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
    glm::mat4 g(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
    EXPECT_TRUE(MatNear(m, g));
}

TEST(Mat4, Arithmetic) {
    Mat4 a = Mat4::Translation(Vec3(1, 2, 3));
    Mat4 b = Mat4::Scale(Vec3(2, 2, 2));
    glm::mat4 ga = glm::translate(glm::mat4(1.0f), glm::vec3(1, 2, 3));
    glm::mat4 gb = glm::scale(glm::mat4(1.0f), glm::vec3(2, 2, 2));

    EXPECT_TRUE(MatNear(a + b, ga + gb));
    EXPECT_TRUE(MatNear(a * 2.0f, ga * 2.0f));
    EXPECT_TRUE(MatNear(a * b, ga * gb));
}

TEST(Mat4, MatrixVectorMultiply) {
    Mat4 m = Mat4::Translation(Vec3(1, 2, 3)) * Mat4::RotationZ(Math::PI / 4.0f);
    glm::mat4 g = glm::translate(glm::mat4(1.0f), glm::vec3(1, 2, 3)) *
                  glm::rotate(glm::mat4(1.0f), Math::PI / 4.0f, glm::vec3(0, 0, 1));
    Vec4 v(1.0f, 0.5f, -1.0f, 1.0f);
    glm::vec4 gv(1.0f, 0.5f, -1.0f, 1.0f);
    Vec4 result = m * v;
    glm::vec4 expected = g * gv;
    EXPECT_NEAR(result.x, expected.x, kEps);
    EXPECT_NEAR(result.y, expected.y, kEps);
    EXPECT_NEAR(result.z, expected.z, kEps);
    EXPECT_NEAR(result.w, expected.w, kEps);
}

TEST(Mat4, RotationsMatchGlm) {
    float angle = Math::PI / 5.0f;
    EXPECT_TRUE(MatNear(Mat4::RotationX(angle), glm::rotate(glm::mat4(1.0f), angle, glm::vec3(1, 0, 0))));
    EXPECT_TRUE(MatNear(Mat4::RotationY(angle), glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0, 1, 0))));
    EXPECT_TRUE(MatNear(Mat4::RotationZ(angle), glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0, 0, 1))));
}

TEST(Mat4, TransposeAndDeterminant) {
    Mat4 m = Mat4::Translation(Vec3(1, 2, 3)) * Mat4::RotationY(Math::PI / 6.0f) * Mat4::Scale(Vec3(2, 3, 4));
    glm::mat4 g = glm::translate(glm::mat4(1.0f), glm::vec3(1, 2, 3)) *
                  glm::rotate(glm::mat4(1.0f), Math::PI / 6.0f, glm::vec3(0, 1, 0)) *
                  glm::scale(glm::mat4(1.0f), glm::vec3(2, 3, 4));

    EXPECT_TRUE(MatNear(m.Transposed(), glm::transpose(g)));
    EXPECT_NEAR(m.Determinant(), glm::determinant(g), kEps);
}

TEST(Mat4, InverseMatchesGlmForGeneralMatrix) {

    Mat4 m(1, 2, 3, 4, 0, 1, 4, 2, 5, 6, 0, 3, 1, 0, 2, 1);
    glm::mat4 g(1, 2, 3, 4, 0, 1, 4, 2, 5, 6, 0, 3, 1, 0, 2, 1);
    EXPECT_TRUE(MatNear(m.Inverse(), glm::inverse(g)));
}

TEST(Mat4, InverseMatchesGlmForAffineMatrix) {
    Mat4 m = Mat4::Translation(Vec3(3, -1, 2)) * Mat4::RotationX(Math::PI / 3.0f) * Mat4::Scale(Vec3(2, 1, 0.5f));
    glm::mat4 g = glm::translate(glm::mat4(1.0f), glm::vec3(3, -1, 2)) *
                  glm::rotate(glm::mat4(1.0f), Math::PI / 3.0f, glm::vec3(1, 0, 0)) *
                  glm::scale(glm::mat4(1.0f), glm::vec3(2, 1, 0.5f));
    EXPECT_TRUE(MatNear(m.Inverse(), glm::inverse(g)));
}

TEST(Mat4, InverseAffineMatchesGeneralInverseForAffineMatrix) {
    Mat4 m = Mat4::Translation(Vec3(3, -1, 2)) * Mat4::RotationZ(Math::PI / 5.0f) * Mat4::Scale(Vec3(1.5f, 2.0f, 0.7f));
    Mat4 fast = m.InverseAffine();
    Mat4 general = m.Inverse();
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            EXPECT_NEAR(fast[c][r], general[c][r], kEps);
}

TEST(Mat4, TransformPointAndVector) {
    Mat4 m = Mat4::Translation(Vec3(5, 0, 0)) * Mat4::RotationZ(Math::PI / 2.0f);
    glm::mat4 g = glm::translate(glm::mat4(1.0f), glm::vec3(5, 0, 0)) *
                  glm::rotate(glm::mat4(1.0f), Math::PI / 2.0f, glm::vec3(0, 0, 1));

    Vec3 p(1.0f, 0.0f, 0.0f);
    glm::vec4 gp = g * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    ExpectVec3Near(m.TransformPoint(p), glm::vec3(gp));

    Vec3 v(1.0f, 0.0f, 0.0f);
    glm::vec4 gv = g * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    ExpectVec3Near(m.TransformVector(v), glm::vec3(gv));
}

TEST(Mat4, LookAtMatchesGlm) {
    Vec3 eye(3.0f, 4.0f, 5.0f), center(0.0f, 0.0f, 0.0f), up(0.0f, 1.0f, 0.0f);
    Mat4 m = Mat4::LookAt(eye, center, up);
    glm::mat4 g = glm::lookAt(glm::vec3(3, 4, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    EXPECT_TRUE(MatNear(m, g));
}

TEST(Mat4, PerspectiveMatchesGlm) {
    Mat4 m = Mat4::Perspective(Math::PI / 4.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    glm::mat4 g = glm::perspective(Math::PI / 4.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    EXPECT_TRUE(MatNear(m, g));
}

TEST(Mat4, OrthoMatchesGlm) {
    Mat4 m = Mat4::Ortho(-10.0f, 10.0f, -5.0f, 5.0f, 0.1f, 100.0f);
    glm::mat4 g = glm::ortho(-10.0f, 10.0f, -5.0f, 5.0f, 0.1f, 100.0f);
    EXPECT_TRUE(MatNear(m, g));
}

TEST(Mat4, PerspectiveProjectsPointsMatchingGlm) {
    Mat4 proj = Mat4::Perspective(Math::PI / 3.0f, 4.0f / 3.0f, 0.1f, 100.0f);
    Mat4 view = Mat4::LookAt(Vec3(0, 2, 5), Vec3(0, 0, 0), Vec3(0, 1, 0));
    glm::mat4 gproj = glm::perspective(Math::PI / 3.0f, 4.0f / 3.0f, 0.1f, 100.0f);
    glm::mat4 gview = glm::lookAt(glm::vec3(0, 2, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));

    Vec3 worldPoint(1.0f, 0.5f, 0.0f);
    Vec3 clip = proj.TransformPointPerspective(view.TransformPoint(worldPoint));
    glm::vec4 gclip4 = gproj * gview * glm::vec4(1.0f, 0.5f, 0.0f, 1.0f);
    glm::vec3 gclip = glm::vec3(gclip4) / gclip4.w;

    ExpectVec3Near(clip, gclip);
}

TEST(Mat4, ChainedMultiplyMatchesGlmSkeletonHierarchy) {

    Mat4 localHip = Mat4::Translation(Vec3(0, 1, 0)) * Mat4::RotationY(0.3f);
    Mat4 localSpine = Mat4::Translation(Vec3(0, 0.5f, 0)) * Mat4::RotationX(0.15f);
    Mat4 localShoulder = Mat4::Translation(Vec3(0.3f, 0.4f, 0)) * Mat4::RotationZ(-0.4f);
    Mat4 localArm = Mat4::Translation(Vec3(0.5f, 0, 0)) * Mat4::RotationX(0.9f);

    Mat4 worldHip = localHip;
    Mat4 worldSpine = worldHip * localSpine;
    Mat4 worldShoulder = worldSpine * localShoulder;
    Mat4 worldArm = worldShoulder * localArm;

    glm::mat4 glocalHip = glm::translate(glm::mat4(1.0f), glm::vec3(0, 1, 0)) *
                           glm::rotate(glm::mat4(1.0f), 0.3f, glm::vec3(0, 1, 0));
    glm::mat4 glocalSpine = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0.5f, 0)) *
                             glm::rotate(glm::mat4(1.0f), 0.15f, glm::vec3(1, 0, 0));
    glm::mat4 glocalShoulder = glm::translate(glm::mat4(1.0f), glm::vec3(0.3f, 0.4f, 0)) *
                                glm::rotate(glm::mat4(1.0f), -0.4f, glm::vec3(0, 0, 1));
    glm::mat4 glocalArm = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 0, 0)) *
                           glm::rotate(glm::mat4(1.0f), 0.9f, glm::vec3(1, 0, 0));

    glm::mat4 gworldHip = glocalHip;
    glm::mat4 gworldSpine = gworldHip * glocalSpine;
    glm::mat4 gworldShoulder = gworldSpine * glocalShoulder;
    glm::mat4 gworldArm = gworldShoulder * glocalArm;

    EXPECT_TRUE(MatNear(worldArm, gworldArm, 1e-5f));

    Vec3 handLocal(0.2f, 0.0f, 0.0f);
    Vec3 handWorld = worldArm.TransformPoint(handLocal);
    glm::vec4 ghandWorld = gworldArm * glm::vec4(0.2f, 0.0f, 0.0f, 1.0f);
    ExpectVec3Near(handWorld, glm::vec3(ghandWorld), 1e-5f);
}

TEST(Mat4, UpperLeftAndTranslationExtraction) {
    Mat4 m = Mat4::Translation(Vec3(7, 8, 9)) * Mat4::RotationY(Math::PI / 4.0f);
    ExpectVec3Near(m.GetTranslation(), glm::vec3(7, 8, 9));

    glm::mat3 gUpper = glm::mat3(glm::rotate(glm::mat4(1.0f), Math::PI / 4.0f, glm::vec3(0, 1, 0)));
    Mat3 upper = m.UpperLeft3x3();
    for (int c = 0; c < 3; c++)
        for (int r = 0; r < 3; r++)
            EXPECT_NEAR(upper[c][r], gUpper[c][r], kEps);
}