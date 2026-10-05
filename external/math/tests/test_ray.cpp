#include <gtest/gtest.h>
#include "../mathc.h"

using Math::Ray;
using Math::Vec3;

constexpr float kEps = 1e-5f;

TEST(Ray, PointAt) {
    Ray r(Vec3(1.0f, 2.0f, 3.0f), Vec3(0.0f, 0.0f, 1.0f));
    Vec3 p = r.PointAt(5.0f);
    EXPECT_NEAR(p.x, 1.0f, kEps);
    EXPECT_NEAR(p.y, 2.0f, kEps);
    EXPECT_NEAR(p.z, 8.0f, kEps);
}

TEST(Ray, FromPointsNormalizesDirection) {
    Ray r = Ray::FromPoints(Vec3(0.0f, 0.0f, 0.0f), Vec3(3.0f, 4.0f, 0.0f));
    EXPECT_NEAR(r.direction.Length(), 1.0f, kEps);
    EXPECT_NEAR(r.direction.x, 0.6f, kEps);
    EXPECT_NEAR(r.direction.y, 0.8f, kEps);

    Vec3 landed = r.PointAt(5.0f); 
    EXPECT_NEAR(landed.x, 3.0f, kEps);
    EXPECT_NEAR(landed.y, 4.0f, kEps);
}