#include <gtest/gtest.h>
#include "../mathc.h"

using Math::Plane;
using Math::Ray;
using Math::Vec3;

constexpr float kEps = 1e-4f;

TEST(Plane, DistanceToPointForXZGroundPlane) {

    Plane p(Vec3(0.0f, 1.0f, 0.0f), 0.0f);
    EXPECT_NEAR(p.DistanceToPoint(Vec3(5.0f, 0.0f, -3.0f)), 0.0f, kEps);
    EXPECT_NEAR(p.DistanceToPoint(Vec3(0.0f, 3.0f, 0.0f)), 3.0f, kEps);
    EXPECT_NEAR(p.DistanceToPoint(Vec3(0.0f, -2.0f, 0.0f)), -2.0f, kEps);
}

TEST(Plane, ConstructFromNormalAndPoint) {
    Vec3 pointOnPlane(0.0f, 5.0f, 0.0f);
    Plane p(Vec3(0.0f, 1.0f, 0.0f), pointOnPlane);
    EXPECT_NEAR(p.DistanceToPoint(pointOnPlane), 0.0f, kEps);
    EXPECT_NEAR(p.DistanceToPoint(Vec3(100.0f, 5.0f, -50.0f)), 0.0f, kEps); 
    EXPECT_NEAR(p.DistanceToPoint(Vec3(0.0f, 8.0f, 0.0f)), 3.0f, kEps);
}

TEST(Plane, ConstructFromThreePointsGivesCorrectNormalAndDistance) {

    Plane p(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f), Vec3(1.0f, 0.0f, 0.0f));
    EXPECT_NEAR(p.normal.x, 0.0f, kEps);
    EXPECT_NEAR(p.normal.y, 1.0f, kEps);
    EXPECT_NEAR(p.normal.z, 0.0f, kEps);
    EXPECT_NEAR(p.DistanceToPoint(Vec3(0.5f, 0.0f, 0.5f)), 0.0f, kEps);
}

TEST(Plane, ClosestPointProjectsOntoPlane) {
    Plane p(Vec3(0.0f, 1.0f, 0.0f), 0.0f);
    Vec3 above(2.0f, 7.0f, -1.0f);
    Vec3 closest = p.ClosestPoint(above);
    EXPECT_NEAR(closest.x, 2.0f, kEps);
    EXPECT_NEAR(closest.y, 0.0f, kEps);
    EXPECT_NEAR(closest.z, -1.0f, kEps);
    EXPECT_NEAR(p.DistanceToPoint(closest), 0.0f, kEps);
}

TEST(Plane, ClosestPointSupportsUnnormalizedNormal) {
    // 3x - 6 = 0 is x = 2. The projection must be independent of the
    // arbitrary scale (3) applied to the plane equation.
    Plane p(Vec3(3.0f, 0.0f, 0.0f), -6.0f);
    Vec3 closest = p.ClosestPoint(Vec3(8.0f, 4.0f, -1.0f));
    EXPECT_TRUE(closest == Vec3(2.0f, 4.0f, -1.0f));
    EXPECT_NEAR(p.DistanceToPoint(closest), 0.0f, kEps);
}

TEST(Plane, NormalizedPreservesGeometryWithUnscaledNormal) {
    Plane p(Vec3(0.0f, 2.0f, 0.0f), -10.0f); 
    Plane n = p.Normalized();
    EXPECT_NEAR(n.normal.Length(), 1.0f, kEps);

    Vec3 pt(3.0f, 5.0f, -2.0f);
    EXPECT_NEAR(p.DistanceToPoint(pt) * 0.5f, n.DistanceToPoint(pt), kEps);
}

TEST(Plane, IntersectRayHitsPerpendicular) {
    Plane p(Vec3(0.0f, 1.0f, 0.0f), 0.0f);
    Ray r(Vec3(0.0f, 10.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f));
    float t;
    ASSERT_TRUE(p.IntersectRay(r, t));
    EXPECT_NEAR(t, 10.0f, kEps);
    Vec3 hit = r.PointAt(t);
    EXPECT_NEAR(p.DistanceToPoint(hit), 0.0f, kEps);
}

TEST(Plane, IntersectRayMissesParallelRay) {
    Plane p(Vec3(0.0f, 1.0f, 0.0f), 0.0f);
    Ray r(Vec3(0.0f, 5.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f)); 
    float t;
    EXPECT_FALSE(p.IntersectRay(r, t));
}