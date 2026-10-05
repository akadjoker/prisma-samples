// Proves the distributable header works with MATHC_NO_EXTRA, which removes
// Transform2D/Ray/Plane/Box/Frustum from both its declarations and definitions.
#define MATHC_NO_EXTRA
#define MATHC_IMPLEMENTATION
#include "../dist/mathc.h"
#include <cstdio>

using namespace Math;

int main() {
    Vec4 a(1, 2, 3, 4), b(5, 6, 7, 8);
    Mat4 m = Mat4::Translation(Vec3(1, 0, 0)) * Mat4::RotationY(0.5f);
    Quaternion q = Quaternion::FromAxisAngle(Vec3(0, 1, 0), 0.5f);
    Vec3 p = m.TransformPoint(q * Vec3(1, 0, 0));

    printf("core-only build OK: dot=%.3f p=(%.3f,%.3f,%.3f)\n", a.Dot(b), p.x, p.y, p.z);
    return 0;
}
