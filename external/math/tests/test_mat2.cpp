#include <cmath>
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include "../mathc.h"

using Math::Mat2;
using Math::Vec2;

constexpr float kEps = 1e-4f;

::testing::AssertionResult MatNear(const Mat2& a, const glm::mat2& b, float eps = kEps) {
    for (int c = 0; c < 2; c++)
        for (int r = 0; r < 2; r++)
            if (std::fabs(a[c][r] - b[c][r]) >= eps)
                return ::testing::AssertionFailure() << "mismatch at col " << c << " row " << r
                    << ": " << a[c][r] << " != " << b[c][r];
    return ::testing::AssertionSuccess();
}

TEST(Mat2, IdentityMatchesGlm) {
    Mat2 m;
    glm::mat2 g(1.0f);
    EXPECT_TRUE(MatNear(m, g));
}

TEST(Mat2, ColumnMajorLayoutMatchesGlm) {
    Mat2 m(1.0f, 2.0f, 3.0f, 4.0f);
    glm::mat2 g(1.0f, 2.0f, 3.0f, 4.0f);
    EXPECT_TRUE(MatNear(m, g));
}

TEST(Mat2, Arithmetic) {
    Mat2 a(1.0f, 2.0f, 3.0f, 4.0f), b(5.0f, 6.0f, 7.0f, 8.0f);
    glm::mat2 ga(1.0f, 2.0f, 3.0f, 4.0f), gb(5.0f, 6.0f, 7.0f, 8.0f);

    EXPECT_TRUE(MatNear(a + b, ga + gb));
    EXPECT_TRUE(MatNear(a - b, ga - gb));
    EXPECT_TRUE(MatNear(a * 2.0f, ga * 2.0f));
    EXPECT_TRUE(MatNear(a * b, ga * gb));
}

TEST(Mat2, MatrixVectorMultiply) {
    Mat2 m(1.0f, 2.0f, 3.0f, 4.0f);
    glm::mat2 g(1.0f, 2.0f, 3.0f, 4.0f);
    Vec2 v(5.0f, 6.0f);
    glm::vec2 gv(5.0f, 6.0f);
    glm::vec2 expected = g * gv;
    Vec2 result = m * v;
    EXPECT_NEAR(result.x, expected.x, kEps);
    EXPECT_NEAR(result.y, expected.y, kEps);
}

TEST(Mat2, TransposeDeterminantInverse) {
    Mat2 m(2.0f, 1.0f, 0.0f, 3.0f);
    glm::mat2 g(2.0f, 1.0f, 0.0f, 3.0f);

    EXPECT_TRUE(MatNear(m.Transposed(), glm::transpose(g)));
    EXPECT_NEAR(m.Determinant(), glm::determinant(g), kEps);
    EXPECT_TRUE(MatNear(m.Inverse(), glm::inverse(g)));
}

TEST(Mat2, RotationMatchesGlmRotationConstruction) {
    float angle = Math::PI / 5.0f;
    Mat2 m = Mat2::Rotation(angle);
    float c = std::cos(angle), s = std::sin(angle);
    glm::mat2 g(c, s, -s, c);
    EXPECT_TRUE(MatNear(m, g));
}

TEST(Mat2, ScaleMatchesGlmScaleConstruction) {
    Mat2 m = Mat2::Scale(2.0f, 3.0f);
    glm::mat2 g(2.0f, 0.0f, 0.0f, 3.0f);
    EXPECT_TRUE(MatNear(m, g));
}

TEST(Mat2, SkewXShearsYAxisOnly) {
    float angle = Math::PI / 6.0f;
    Mat2 m = Mat2::SkewX(angle);

    Vec2 ex = m * Vec2(1.0f, 0.0f);
    Vec2 ey = m * Vec2(0.0f, 1.0f);
    EXPECT_NEAR(ex.x, 1.0f, kEps);
    EXPECT_NEAR(ex.y, 0.0f, kEps);
    EXPECT_NEAR(ey.x, std::tan(angle), kEps);
    EXPECT_NEAR(ey.y, 1.0f, kEps);
}

TEST(Mat2, SkewYShearsXAxisOnly) {
    float angle = Math::PI / 6.0f;
    Mat2 m = Mat2::SkewY(angle);
    Vec2 ex = m * Vec2(1.0f, 0.0f);
    Vec2 ey = m * Vec2(0.0f, 1.0f);
    EXPECT_NEAR(ex.x, 1.0f, kEps);
    EXPECT_NEAR(ex.y, std::tan(angle), kEps);
    EXPECT_NEAR(ey.x, 0.0f, kEps);
    EXPECT_NEAR(ey.y, 1.0f, kEps);
}

TEST(Mat2, FromScaleRotationSkewRoundTrip) {
    Vec2 scale(2.0f, 3.0f);
    float rotation = Math::PI / 7.0f;
    float skew = Math::PI / 9.0f;

    Mat2 m = Mat2::FromScaleRotationSkew(scale, rotation, skew);

    EXPECT_NEAR(m.GetScale().x, scale.x, kEps);
    EXPECT_NEAR(m.GetScale().y, scale.y, kEps);
    EXPECT_NEAR(m.GetRotation(), rotation, kEps);
    EXPECT_NEAR(m.GetSkew(), skew, kEps);
}

TEST(Mat2, FromScaleRotationSkewWithZeroSkewIsPureRotationScale) {
    Vec2 scale(1.5f, 1.5f);
    float rotation = Math::PI / 3.0f;

    Mat2 m = Mat2::FromScaleRotationSkew(scale, rotation, 0.0f);
    Mat2 expected = Mat2::Rotation(rotation) * Mat2::Scale(scale);

    EXPECT_TRUE(MatNear(m, glm::mat2(expected[0].x, expected[0].y, expected[1].x, expected[1].y)));
    EXPECT_NEAR(m.GetSkew(), 0.0f, kEps);
}