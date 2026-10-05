#include <cmath>
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include "../mathc.h"

using Math::Mat2;
using Math::Transform2D;
using Math::Vec2;

constexpr float kEps = 1e-4f;

static void ExpectVecNear(const Vec2& a, const glm::vec2& b, float eps = kEps) {
    EXPECT_NEAR(a.x, b.x, eps);
    EXPECT_NEAR(a.y, b.y, eps);
}

static void ExpectVecNear(const Vec2& a, const Vec2& b, float eps = kEps) {
    EXPECT_NEAR(a.x, b.x, eps);
    EXPECT_NEAR(a.y, b.y, eps);
}

static glm::mat3 GlmTRS(glm::vec2 translation, float rotationRad, glm::vec2 scale) {
    float c = std::cos(rotationRad), s = std::sin(rotationRad);
    return glm::mat3(
        c * scale.x, s * scale.x, 0.0f,
        -s * scale.y, c * scale.y, 0.0f,
        translation.x, translation.y, 1.0f
    );
}

TEST(Transform2D, IdentityIsNoOp) {
    Transform2D t;
    Vec2 p(3.0f, -2.0f);
    ExpectVecNear(t.TransformPoint(p), glm::vec2(3.0f, -2.0f));
}

TEST(Transform2D, TranslationOnly) {
    Transform2D t = Transform2D::Translation(Vec2(5.0f, -1.0f));
    Vec2 p(1.0f, 2.0f);
    glm::mat3 g = GlmTRS(glm::vec2(5.0f, -1.0f), 0.0f, glm::vec2(1.0f));
    glm::vec3 expected = g * glm::vec3(1.0f, 2.0f, 1.0f);
    ExpectVecNear(t.TransformPoint(p), glm::vec2(expected));
}

TEST(Transform2D, RigidMatchesGlmTranslateRotate) {
    Vec2 translation(2.0f, 3.0f);
    float angle = Math::PI / 4.0f;
    Transform2D t = Transform2D::Rigid(translation, angle);

    glm::mat3 g = GlmTRS(glm::vec2(2.0f, 3.0f), angle, glm::vec2(1.0f));
    Vec2 p(1.0f, 0.0f);
    glm::vec3 expected = g * glm::vec3(1.0f, 0.0f, 1.0f);
    ExpectVecNear(t.TransformPoint(p), glm::vec2(expected));
}

TEST(Transform2D, FromTRSNoSkewMatchesGlm) {
    Vec2 translation(1.0f, -2.0f);
    float angle = Math::PI / 6.0f;
    Vec2 scale(2.0f, 0.5f);
    Transform2D t = Transform2D::FromTRS(translation, angle, scale, 0.0f, 0.0f);

    glm::mat3 g = GlmTRS(glm::vec2(1.0f, -2.0f), angle, glm::vec2(2.0f, 0.5f));
    Vec2 p(3.0f, 4.0f);
    glm::vec3 expected = g * glm::vec3(3.0f, 4.0f, 1.0f);
    ExpectVecNear(t.TransformPoint(p), glm::vec2(expected));
}

TEST(Transform2D, TransformVectorIgnoresTranslation) {
    Transform2D t = Transform2D::Translation(Vec2(100.0f, 100.0f));
    Vec2 v(1.0f, 0.0f);
    ExpectVecNear(t.TransformVector(v), glm::vec2(1.0f, 0.0f));
}

TEST(Transform2D, SkewXMovesYAxisOnlyBeforeRotation) {
    float skewX = Math::PI / 6.0f;
    Transform2D t = Transform2D::FromTRS(Vec2::Zero, 0.0f, Vec2::One, skewX, 0.0f);

    Vec2 ex = t.TransformPoint(Vec2(1.0f, 0.0f));
    Vec2 ey = t.TransformPoint(Vec2(0.0f, 1.0f));
    ExpectVecNear(ex, glm::vec2(1.0f, 0.0f));
    ExpectVecNear(ey, glm::vec2(std::tan(skewX), 1.0f));
}

TEST(Transform2D, SkewYMovesXAxisOnlyBeforeRotation) {
    float skewY = Math::PI / 6.0f;
    Transform2D t = Transform2D::FromTRS(Vec2::Zero, 0.0f, Vec2::One, 0.0f, skewY);
    Vec2 ex = t.TransformPoint(Vec2(1.0f, 0.0f));
    Vec2 ey = t.TransformPoint(Vec2(0.0f, 1.0f));
    ExpectVecNear(ex, glm::vec2(1.0f, std::tan(skewY)));
    ExpectVecNear(ey, glm::vec2(0.0f, 1.0f));
}

TEST(Transform2D, ComposeMatchesGlmMultiplication) {
    Transform2D a = Transform2D::Rigid(Vec2(1.0f, 0.0f), Math::PI / 3.0f);
    Transform2D b = Transform2D::Rigid(Vec2(0.0f, 2.0f), Math::PI / 5.0f);
    Transform2D composed = a * b;

    glm::mat3 ga = GlmTRS(glm::vec2(1.0f, 0.0f), Math::PI / 3.0f, glm::vec2(1.0f));
    glm::mat3 gb = GlmTRS(glm::vec2(0.0f, 2.0f), Math::PI / 5.0f, glm::vec2(1.0f));
    glm::mat3 gComposed = ga * gb;

    Vec2 p(1.0f, 1.0f);
    glm::vec3 expected = gComposed * glm::vec3(1.0f, 1.0f, 1.0f);
    ExpectVecNear(composed.TransformPoint(p), glm::vec2(expected));

    ExpectVecNear(composed.TransformPoint(p), a.TransformPoint(b.TransformPoint(p)));
}

TEST(Transform2D, RigidInverseUndoesRigid) {
    Transform2D t = Transform2D::Rigid(Vec2(4.0f, -3.0f), Math::PI / 3.5f);
    Transform2D inv = t.RigidInverse();
    Vec2 p(2.0f, 5.0f);
    Vec2 roundTrip = inv.TransformPoint(t.TransformPoint(p));
    ExpectVecNear(roundTrip, glm::vec2(2.0f, 5.0f));
}

TEST(Transform2D, RotationAroundPivotKeepsPivotFixed) {
    Vec2 pivot(5.0f, 5.0f);
    float angle = Math::PI / 2.0f;
    Transform2D t = Transform2D::RotationAround(pivot, angle);

    ExpectVecNear(t.TransformPoint(pivot), glm::vec2(5.0f, 5.0f));

    Vec2 p(6.0f, 5.0f);
    ExpectVecNear(t.TransformPoint(p), glm::vec2(5.0f, 6.0f));
}

TEST(Transform2D, RotationAroundPivotMatchesTranslateRotateTranslateCompose) {
    Vec2 pivot(3.0f, -2.0f);
    float angle = Math::PI / 7.0f;
    Transform2D direct = Transform2D::RotationAround(pivot, angle);
    Transform2D composed = Transform2D::Translation(pivot) * Transform2D::Rotation(angle) *
                            Transform2D::Translation(-pivot);

    Vec2 p(10.0f, 4.0f);
    ExpectVecNear(direct.TransformPoint(p), glm::vec2(composed.TransformPoint(p).x, composed.TransformPoint(p).y));
}

TEST(Transform2D, ScaleAroundPivotKeepsPivotFixed) {
    Vec2 pivot(2.0f, 2.0f);
    Transform2D t = Transform2D::ScaleAround(pivot, Vec2(2.0f, 2.0f));

    ExpectVecNear(t.TransformPoint(pivot), glm::vec2(2.0f, 2.0f));
    ExpectVecNear(t.TransformPoint(Vec2(3.0f, 2.0f)), glm::vec2(4.0f, 2.0f));
}

TEST(Transform2D, FromTRSWithPivotRotatesAroundPivotThenTranslates) {
    Vec2 pivot(1.0f, 0.0f);
    Vec2 translation(10.0f, 0.0f);
    float angle = Math::PI / 2.0f;
    Transform2D t = Transform2D::FromTRS(translation, angle, Vec2::One, 0.0f, 0.0f, pivot);

    ExpectVecNear(t.TransformPoint(pivot), glm::vec2(10.0f, 0.0f));
}

TEST(Transform2D, InverseUndoesFullTRS) {
    Transform2D t = Transform2D::FromTRS(Vec2(3.0f, -1.0f), Math::PI / 4.0f, Vec2(2.0f, 0.5f), 0.2f, -0.1f);
    Transform2D inv = t.Inverse();
    Vec2 p(1.5f, -2.5f);
    Vec2 roundTrip = inv.TransformPoint(t.TransformPoint(p));
    ExpectVecNear(roundTrip, glm::vec2(1.5f, -2.5f));
}