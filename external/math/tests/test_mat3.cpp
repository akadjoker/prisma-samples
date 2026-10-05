#include <cmath>
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "../mathc.h"

using Math::Mat3;
using Math::Transform2D;
using Math::Vec2;
using Math::Vec3;

constexpr float kEps = 1e-4f;

::testing::AssertionResult MatNear(const Mat3& a, const glm::mat3& b, float eps = kEps) {
    for (int c = 0; c < 3; c++)
        for (int r = 0; r < 3; r++)
            if (std::fabs(a[c][r] - b[c][r]) >= eps)
                return ::testing::AssertionFailure() << "mismatch at col " << c << " row " << r
                    << ": " << a[c][r] << " != " << b[c][r];
    return ::testing::AssertionSuccess();
}

TEST(Mat3, IdentityMatchesGlm) {
    Mat3 m;
    glm::mat3 g(1.0f);
    EXPECT_TRUE(MatNear(m, g));
}

TEST(Mat3, ColumnMajorLayoutMatchesGlm) {
    Mat3 m(1, 2, 3, 4, 5, 6, 7, 8, 9);
    glm::mat3 g(1, 2, 3, 4, 5, 6, 7, 8, 9);
    EXPECT_TRUE(MatNear(m, g));
}

TEST(Mat3, Arithmetic) {
    Mat3 a(1, 2, 3, 4, 5, 6, 7, 8, 9), b(9, 8, 7, 6, 5, 4, 3, 2, 1);
    glm::mat3 ga(1, 2, 3, 4, 5, 6, 7, 8, 9), gb(9, 8, 7, 6, 5, 4, 3, 2, 1);

    EXPECT_TRUE(MatNear(a + b, ga + gb));
    EXPECT_TRUE(MatNear(a - b, ga - gb));
    EXPECT_TRUE(MatNear(a * 2.0f, ga * 2.0f));
    EXPECT_TRUE(MatNear(a * b, ga * gb));
}

TEST(Mat3, MatrixVectorMultiply) {
    Mat3 m(1, 2, 3, 4, 5, 6, 7, 8, 9);
    glm::mat3 g(1, 2, 3, 4, 5, 6, 7, 8, 9);
    Vec3 v(1.0f, 2.0f, 3.0f);
    glm::vec3 gv(1.0f, 2.0f, 3.0f);
    glm::vec3 expected = g * gv;
    Vec3 result = m * v;
    EXPECT_NEAR(result.x, expected.x, kEps);
    EXPECT_NEAR(result.y, expected.y, kEps);
    EXPECT_NEAR(result.z, expected.z, kEps);
}

TEST(Mat3, TransposeDeterminantInverse) {
    Mat3 m(2, 0, 1, 1, 3, 0, 0, 1, 4);
    glm::mat3 g(2, 0, 1, 1, 3, 0, 0, 1, 4);

    EXPECT_TRUE(MatNear(m.Transposed(), glm::transpose(g)));
    EXPECT_NEAR(m.Determinant(), glm::determinant(g), kEps);
    EXPECT_TRUE(MatNear(m.Inverse(), glm::inverse(g)));
}

TEST(Mat3, RotationXMatchesGlm) {
    float angle = Math::PI / 5.0f;
    Mat3 m = Mat3::RotationX(angle);
    glm::mat3 g = glm::mat3(glm::rotate(glm::mat4(1.0f), angle, glm::vec3(1, 0, 0)));
    EXPECT_TRUE(MatNear(m, g));
}

TEST(Mat3, RotationYMatchesGlm) {
    float angle = Math::PI / 5.0f;
    Mat3 m = Mat3::RotationY(angle);
    glm::mat3 g = glm::mat3(glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0, 1, 0)));
    EXPECT_TRUE(MatNear(m, g));
}

TEST(Mat3, RotationZMatchesGlm) {
    float angle = Math::PI / 5.0f;
    Mat3 m = Mat3::RotationZ(angle);
    glm::mat3 g = glm::mat3(glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0, 0, 1)));
    EXPECT_TRUE(MatNear(m, g));
}

TEST(Mat3, ScaleMatchesGlm) {
    Mat3 m = Mat3::Scale(Vec3(2.0f, 3.0f, 4.0f));
    glm::mat3 g = glm::mat3(glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 3.0f, 4.0f)));
    EXPECT_TRUE(MatNear(m, g));
}

TEST(Mat3, FromTransform2DMatchesHomogeneousGlm) {
    Vec2 translation(3.0f, -2.0f);
    float angle = Math::PI / 6.0f;
    Vec2 scale(2.0f, 0.5f);
    Transform2D t = Transform2D::FromTRS(translation, angle, scale, 0.0f, 0.0f);
    Mat3 m(t);

    float c = std::cos(angle), s = std::sin(angle);
    glm::mat3 g(
        c * scale.x, s * scale.x, 0.0f,
        -s * scale.y, c * scale.y, 0.0f,
        translation.x, translation.y, 1.0f
    );
    EXPECT_TRUE(MatNear(m, g));

    Vec2 p(4.0f, 1.0f);
    glm::vec3 gp = g * glm::vec3(p.x, p.y, 1.0f);
    Vec2 result = m.TransformPoint2D(p);
    EXPECT_NEAR(result.x, gp.x, kEps);
    EXPECT_NEAR(result.y, gp.y, kEps);

    Vec2 direct = t.TransformPoint(p);
    EXPECT_NEAR(result.x, direct.x, kEps);
    EXPECT_NEAR(result.y, direct.y, kEps);
}

TEST(Mat3, TransformVector2DIgnoresTranslation) {
    Transform2D t = Transform2D::Translation(Vec2(50.0f, 50.0f));
    Mat3 m(t);
    Vec2 v(1.0f, 0.0f);
    Vec2 result = m.TransformVector2D(v);
    EXPECT_NEAR(result.x, 1.0f, kEps);
    EXPECT_NEAR(result.y, 0.0f, kEps);
}