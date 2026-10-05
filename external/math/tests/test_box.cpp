#include <cmath>
#include <gtest/gtest.h>
#include "../mathc.h"

using Math::Box;
using Math::Mat4;
using Math::Ray;
using Math::Vec3;

constexpr float kEps = 1e-4f;

TEST(Box, CenterExtentsSize) {
    Box b(Vec3(-1.0f, -2.0f, -3.0f), Vec3(3.0f, 4.0f, 5.0f));
    EXPECT_NEAR(b.Center().x, 1.0f, kEps);
    EXPECT_NEAR(b.Center().y, 1.0f, kEps);
    EXPECT_NEAR(b.Center().z, 1.0f, kEps);
    EXPECT_NEAR(b.Extents().x, 2.0f, kEps);
    EXPECT_NEAR(b.Size().x, 4.0f, kEps);
}

TEST(Box, Contains) {
    Box b(Vec3(0.0f), Vec3(10.0f));
    EXPECT_TRUE(b.Contains(Vec3(5.0f, 5.0f, 5.0f)));
    EXPECT_TRUE(b.Contains(Vec3(0.0f, 0.0f, 0.0f))); 
    EXPECT_TRUE(b.Contains(Vec3(10.0f, 10.0f, 10.0f)));
    EXPECT_FALSE(b.Contains(Vec3(11.0f, 5.0f, 5.0f)));
    EXPECT_FALSE(b.Contains(Vec3(5.0f, -1.0f, 5.0f)));
}

TEST(Box, Intersects) {
    Box a(Vec3(0.0f), Vec3(5.0f));
    Box overlapping(Vec3(3.0f), Vec3(8.0f));
    Box touching(Vec3(5.0f), Vec3(10.0f)); 
    Box separate(Vec3(6.0f), Vec3(10.0f));

    EXPECT_TRUE(a.Intersects(overlapping));
    EXPECT_TRUE(a.Intersects(touching));
    EXPECT_FALSE(a.Intersects(separate));
}

TEST(Box, Union) {
    Box a(Vec3(0.0f), Vec3(5.0f));
    Box b(Vec3(-2.0f, 1.0f, 3.0f), Vec3(3.0f, 8.0f, 6.0f));
    Box u = a.Union(b);
    EXPECT_NEAR(u.min.x, -2.0f, kEps);
    EXPECT_NEAR(u.min.y, 0.0f, kEps);
    EXPECT_NEAR(u.max.y, 8.0f, kEps);
    EXPECT_NEAR(u.max.z, 6.0f, kEps);
}

TEST(Box, ExpandedByPointAndAmount) {
    Box b(Vec3(0.0f), Vec3(5.0f));
    Box withPoint = b.Expanded(Vec3(10.0f, -3.0f, 2.0f));
    EXPECT_NEAR(withPoint.max.x, 10.0f, kEps);
    EXPECT_NEAR(withPoint.min.y, -3.0f, kEps);

    Box grown = b.Expanded(1.0f);
    EXPECT_NEAR(grown.min.x, -1.0f, kEps);
    EXPECT_NEAR(grown.max.x, 6.0f, kEps);
}

TEST(Box, EmptyBoxUnionWithAnyBoxGivesThatBox) {
    Box b(Vec3(1.0f, 2.0f, 3.0f), Vec3(4.0f, 5.0f, 6.0f));
    Box u = Box::Empty.Union(b);
    EXPECT_NEAR(u.min.x, b.min.x, kEps);
    EXPECT_NEAR(u.max.z, b.max.z, kEps);
}

TEST(Box, FromPoints) {
    Vec3 pts[4] = {Vec3(1, -2, 3), Vec3(-5, 4, 0), Vec3(2, 2, 8), Vec3(0, 0, -1)};
    Box b = Box::FromPoints(pts, 4);
    EXPECT_NEAR(b.min.x, -5.0f, kEps);
    EXPECT_NEAR(b.min.y, -2.0f, kEps);
    EXPECT_NEAR(b.min.z, -1.0f, kEps);
    EXPECT_NEAR(b.max.x, 2.0f, kEps);
    EXPECT_NEAR(b.max.y, 4.0f, kEps);
    EXPECT_NEAR(b.max.z, 8.0f, kEps);
}

TEST(Box, GetCornersMatchesIrrlichtOrdering) {
    Box b(Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 2.0f, 3.0f));
    Vec3 corners[8];
    b.GetCorners(corners);

    EXPECT_TRUE(corners[0] == Vec3(0, 0, 0));
    EXPECT_TRUE(corners[1] == Vec3(0, 2, 0));
    EXPECT_TRUE(corners[2] == Vec3(0, 0, 3));
    EXPECT_TRUE(corners[3] == Vec3(0, 2, 3));
    EXPECT_TRUE(corners[4] == Vec3(1, 0, 0));
    EXPECT_TRUE(corners[5] == Vec3(1, 2, 0));
    EXPECT_TRUE(corners[6] == Vec3(1, 0, 3));
    EXPECT_TRUE(corners[7] == Vec3(1, 2, 3));

    for (int i = 0; i < 8; i++) {
        EXPECT_TRUE(b.Contains(corners[i]));
    }
}

TEST(Box, IntersectRayHitsFrontFace) {
    Box b(Vec3(-1.0f), Vec3(1.0f));
    Ray r(Vec3(0.0f, 0.0f, -5.0f), Vec3(0.0f, 0.0f, 1.0f));
    float tmin, tmax;
    ASSERT_TRUE(b.IntersectRay(r, tmin, tmax));
    EXPECT_NEAR(tmin, 4.0f, kEps); 
    EXPECT_NEAR(tmax, 6.0f, kEps); 
}

TEST(Box, IntersectRayMissesEntirely) {
    Box b(Vec3(-1.0f), Vec3(1.0f));
    Ray r(Vec3(10.0f, 10.0f, -5.0f), Vec3(0.0f, 0.0f, 1.0f)); 
    float tmin, tmax;
    EXPECT_FALSE(b.IntersectRay(r, tmin, tmax));
}

TEST(Box, IntersectRayOriginInsideBox) {
    Box b(Vec3(-1.0f), Vec3(1.0f));
    Ray r(Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f));
    float tmin, tmax;
    ASSERT_TRUE(b.IntersectRay(r, tmin, tmax));
    EXPECT_LT(tmin, 0.0f);  
    EXPECT_NEAR(tmax, 1.0f, kEps);
}

TEST(Box, TransformedEnclosesRotatedCorners) {
    Box b(Vec3(-1.0f, -1.0f, -1.0f), Vec3(1.0f, 1.0f, 1.0f));
    Mat4 m = Mat4::RotationY(Math::PI / 4.0f); 
    Box t = b.Transformed(m);

    EXPECT_NEAR(t.max.x, std::sqrt(2.0f), kEps);
    EXPECT_NEAR(t.max.z, std::sqrt(2.0f), kEps);
    EXPECT_NEAR(t.max.y, 1.0f, kEps); 

    Vec3 corner = m.TransformPoint(Vec3(1.0f, 1.0f, 1.0f));
    EXPECT_TRUE(t.Contains(corner));
}