#ifndef MATHC_INCLUDE_MATHC_H
#define MATHC_INCLUDE_MATHC_H

#include <cstdint>
#include <ostream>

namespace Math
{

    constexpr float PI = 3.14159265358979323846f;
    constexpr float DEG2RAD = PI / 180.0f;
    constexpr float RAD2DEG = 180.0f / PI;
    constexpr float EPSILON = 1e-6f;

    struct Vec2
    {
        union
        {
            struct
            {
                float x, y;
            };
            float v[2];
        };

        Vec2();
        Vec2(float x, float y);
        explicit Vec2(float scalar);

        float &operator[](int index);
        const float &operator[](int index) const;

        Vec2 operator+(const Vec2 &other) const;
        Vec2 operator-(const Vec2 &other) const;
        Vec2 operator*(const Vec2 &other) const;
        Vec2 operator/(const Vec2 &other) const;

        Vec2 operator*(float scalar) const;
        Vec2 operator/(float scalar) const;

        Vec2 &operator+=(const Vec2 &other);
        Vec2 &operator-=(const Vec2 &other);
        Vec2 &operator*=(const Vec2 &other);
        Vec2 &operator/=(const Vec2 &other);
        Vec2 &operator*=(float scalar);
        Vec2 &operator/=(float scalar);

        Vec2 operator-() const;

        bool operator==(const Vec2 &other) const;
        bool operator!=(const Vec2 &other) const;

        float LengthSquared() const;
        float Length() const;
        Vec2 Normalized() const;
        Vec2 NormalizedSafe() const;
        void Normalize();
        void NormalizeSafe();
        float Dot(const Vec2 &other) const;
        float Cross(const Vec2 &other) const;

        float Angle() const;
        float AngleDeg() const;
        Vec2 Rotate(float angleRad) const;
        Vec2 RotateDeg(float angleDeg) const;

        static Vec2 FromAngle(float angleRad);
        static Vec2 FromAngleDeg(float angleDeg);
        static float AngleBetween(const Vec2 &a, const Vec2 &b);
        static float AngleBetweenDeg(const Vec2 &a, const Vec2 &b);
        static float Dot(const Vec2 &a, const Vec2 &b);
        static float Cross(const Vec2 &a, const Vec2 &b);
        static float Distance(const Vec2 &a, const Vec2 &b);
        static float DistanceSquared(const Vec2 &a, const Vec2 &b);
        static Vec2 Lerp(const Vec2 &a, const Vec2 &b, float t);
        static Vec2 Min(const Vec2 &a, const Vec2 &b);
        static Vec2 Max(const Vec2 &a, const Vec2 &b);
        static Vec2 Clamp(const Vec2 &v, const Vec2 &lo, const Vec2 &hi);
        static const Vec2 Zero;
        static const Vec2 One;
        static const Vec2 UnitX;
        static const Vec2 UnitY;
    };

    Vec2 operator*(float scalar, const Vec2 &v);
    inline Vec2 operator+(const Vec2 &v, float scalar) { return v + Vec2(scalar); }
    inline Vec2 operator+(float scalar, const Vec2 &v) { return Vec2(scalar) + v; }
    inline Vec2 operator-(const Vec2 &v, float scalar) { return v - Vec2(scalar); }
    inline Vec2 operator-(float scalar, const Vec2 &v) { return Vec2(scalar) - v; }
    std::ostream &operator<<(std::ostream &os, const Vec2 &v);

    struct Vec4;

    struct Vec3
    {
        union
        {
            struct
            {
                float x, y, z;
            };
            float v[3];
        };

        Vec3();
        Vec3(float x, float y, float z);
        explicit Vec3(float scalar);
        Vec3(const Vec2 &xy, float z);
        Vec3(const Vec4 &v);

        float &operator[](int index);
        const float &operator[](int index) const;

        Vec3 operator+(const Vec3 &other) const;
        Vec3 operator-(const Vec3 &other) const;
        Vec3 operator*(const Vec3 &other) const;
        Vec3 operator/(const Vec3 &other) const;

        Vec3 operator*(float scalar) const;
        Vec3 operator/(float scalar) const;

        Vec3 &operator+=(const Vec3 &other);
        Vec3 &operator-=(const Vec3 &other);
        Vec3 &operator*=(const Vec3 &other);
        Vec3 &operator/=(const Vec3 &other);
        Vec3 &operator*=(float scalar);
        Vec3 &operator/=(float scalar);

        Vec3 operator-() const;

        bool operator==(const Vec3 &other) const;
        bool operator!=(const Vec3 &other) const;

        float LengthSquared() const;
        float Length() const;
        Vec3 Normalized() const;
        Vec3 NormalizedSafe() const;
        void Normalize();
        void NormalizeSafe();
        float Dot(const Vec3 &other) const;
        Vec3 Cross(const Vec3 &other) const;

        Vec2 xy() const;
        Vec3 RotateX(float angleRad) const;
        Vec3 RotateY(float angleRad) const;
        Vec3 RotateZ(float angleRad) const;
        Vec3 RotateXDeg(float angleDeg) const;
        Vec3 RotateYDeg(float angleDeg) const;
        Vec3 RotateZDeg(float angleDeg) const;

        static float AngleBetween(const Vec3 &a, const Vec3 &b);
        static float AngleBetweenDeg(const Vec3 &a, const Vec3 &b);
        static float Dot(const Vec3 &a, const Vec3 &b);
        static Vec3 Cross(const Vec3 &a, const Vec3 &b);
        static float Distance(const Vec3 &a, const Vec3 &b);
        static float DistanceSquared(const Vec3 &a, const Vec3 &b);
        static Vec3 Lerp(const Vec3 &a, const Vec3 &b, float t);
        static Vec3 Min(const Vec3 &a, const Vec3 &b);
        static Vec3 Max(const Vec3 &a, const Vec3 &b);
        static Vec3 Clamp(const Vec3 &v, const Vec3 &lo, const Vec3 &hi);
        static const Vec3 Zero;
        static const Vec3 One;
        static const Vec3 UnitX;
        static const Vec3 UnitY;
        static const Vec3 UnitZ;
    };

    Vec3 operator*(float scalar, const Vec3 &v);
    inline Vec3 operator+(const Vec3 &v, float scalar) { return v + Vec3(scalar); }
    inline Vec3 operator+(float scalar, const Vec3 &v) { return Vec3(scalar) + v; }
    inline Vec3 operator-(const Vec3 &v, float scalar) { return v - Vec3(scalar); }
    inline Vec3 operator-(float scalar, const Vec3 &v) { return Vec3(scalar) - v; }
    std::ostream &operator<<(std::ostream &os, const Vec3 &v);

    struct alignas(16) Vec4
    {
        union
        {
            struct
            {
                float x, y, z, w;
            };
            float v[4];
        };

        Vec4();
        Vec4(float x, float y, float z, float w);
        explicit Vec4(float scalar);
        Vec4(const Vec3 &xyz, float w);
        Vec4(const Vec2 &xy, float z, float w);

        float &operator[](int index);
        const float &operator[](int index) const;

        Vec4 operator+(const Vec4 &other) const;
        Vec4 operator-(const Vec4 &other) const;
        Vec4 operator*(const Vec4 &other) const;
        Vec4 operator/(const Vec4 &other) const;

        Vec4 operator*(float scalar) const;
        Vec4 operator/(float scalar) const;

        Vec4 &operator+=(const Vec4 &other);
        Vec4 &operator-=(const Vec4 &other);
        Vec4 &operator*=(const Vec4 &other);
        Vec4 &operator/=(const Vec4 &other);
        Vec4 &operator*=(float scalar);
        Vec4 &operator/=(float scalar);

        Vec4 operator-() const;

        bool operator==(const Vec4 &other) const;
        bool operator!=(const Vec4 &other) const;

        float LengthSquared() const;
        float Length() const;
        Vec4 Normalized() const;
        Vec4 NormalizedSafe() const;
        void Normalize();
        void NormalizeSafe();
        float Dot(const Vec4 &other) const;

        Vec2 xy() const;
        Vec3 xyz() const;

        static float AngleBetween(const Vec4 &a, const Vec4 &b);
        static float AngleBetweenDeg(const Vec4 &a, const Vec4 &b);
        static float Dot(const Vec4 &a, const Vec4 &b);
        static float Distance(const Vec4 &a, const Vec4 &b);
        static float DistanceSquared(const Vec4 &a, const Vec4 &b);
        static Vec4 Lerp(const Vec4 &a, const Vec4 &b, float t);
        static Vec4 Min(const Vec4 &a, const Vec4 &b);
        static Vec4 Max(const Vec4 &a, const Vec4 &b);
        static Vec4 Clamp(const Vec4 &v, const Vec4 &lo, const Vec4 &hi);
        static const Vec4 Zero;
        static const Vec4 One;
        static const Vec4 UnitX;
        static const Vec4 UnitY;
        static const Vec4 UnitZ;
        static const Vec4 UnitW;
    };

    Vec4 operator*(float scalar, const Vec4 &v);
    inline Vec4 operator+(const Vec4 &v, float scalar) { return v + Vec4(scalar); }
    inline Vec4 operator+(float scalar, const Vec4 &v) { return Vec4(scalar) + v; }
    inline Vec4 operator-(const Vec4 &v, float scalar) { return v - Vec4(scalar); }
    inline Vec4 operator-(float scalar, const Vec4 &v) { return Vec4(scalar) - v; }
    std::ostream &operator<<(std::ostream &os, const Vec4 &v);

    struct Mat2
    {
        Vec2 col0, col1;

        Mat2();
        explicit Mat2(float scalar);
        Mat2(float m00, float m01, float m10, float m11);
        Mat2(const Vec2 &col0, const Vec2 &col1);

        Vec2 &operator[](int col);
        const Vec2 &operator[](int col) const;
        float *Data();
        const float *Data() const;

        Mat2 operator+(const Mat2 &other) const;
        Mat2 operator-(const Mat2 &other) const;
        Mat2 operator*(const Mat2 &other) const;
        Mat2 operator*(float scalar) const;
        Vec2 operator*(const Vec2 &v) const;

        Mat2 &operator+=(const Mat2 &other);
        Mat2 &operator-=(const Mat2 &other);
        Mat2 &operator*=(float scalar);

        bool operator==(const Mat2 &other) const;
        bool operator!=(const Mat2 &other) const;

        Mat2 Transposed() const;
        float Determinant() const;
        Mat2 Inverse() const;
        Mat2 InverseSafe() const;

        Vec2 GetScale() const;
        float GetRotation() const;
        float GetSkew() const;

        static Mat2 Identity();
        static Mat2 Scale(float sx, float sy);
        static Mat2 Scale(const Vec2 &s);
        static Mat2 Rotation(float angleRad);
        static Mat2 SkewX(float angleRad);
        static Mat2 SkewY(float angleRad);

        static Mat2 FromScaleRotationSkew(const Vec2 &scale, float rotationRad, float skewRad);
    };

    Mat2 operator*(float scalar, const Mat2 &m);
    std::ostream &operator<<(std::ostream &os, const Mat2 &m);

#ifndef MATHC_NO_EXTRA
    struct Transform2D
    {
        Mat2 basis;
        Vec2 origin;

        Transform2D();
        Transform2D(const Mat2 &basis, const Vec2 &origin);

        Vec2 TransformPoint(const Vec2 &p) const;
        Vec2 TransformVector(const Vec2 &v) const;

        void TransformPoints(const Vec2 *in, Vec2 *out, int count) const;

        Transform2D operator*(const Transform2D &other) const;

        bool operator==(const Transform2D &other) const;
        bool operator!=(const Transform2D &other) const;

        Transform2D Inverse() const;
        Transform2D InverseSafe() const;

        Transform2D RigidInverse() const;

        static Transform2D Identity();
        static Transform2D Translation(const Vec2 &t);
        static Transform2D Rotation(float angleRad);
        static Transform2D ScaleBy(const Vec2 &s);
        static Transform2D Rigid(const Vec2 &translation, float rotationRad);

        static Transform2D RotationAround(const Vec2 &pivot, float angleRad);
        static Transform2D ScaleAround(const Vec2 &pivot, const Vec2 &scale);

        static Transform2D FromTRS(const Vec2 &translation, float rotationRad, const Vec2 &scale,
                                    float skewXRad, float skewYRad, const Vec2 &pivot = Vec2::Zero);
    };

    std::ostream &operator<<(std::ostream &os, const Transform2D &t);
#endif

    struct Mat4;

    struct Mat3
    {
        Vec3 col0, col1, col2;

        Mat3();
        explicit Mat3(float scalar);
        Mat3(float m00, float m01, float m02, float m10, float m11, float m12, float m20, float m21, float m22);
        Mat3(const Vec3 &col0, const Vec3 &col1, const Vec3 &col2);
        Mat3(const Mat4 &m);
#ifndef MATHC_NO_EXTRA

        explicit Mat3(const Transform2D &t);
#endif

        Vec3 &operator[](int col);
        const Vec3 &operator[](int col) const;
        float *Data();
        const float *Data() const;

        Mat3 operator+(const Mat3 &other) const;
        Mat3 operator-(const Mat3 &other) const;
        Mat3 operator*(const Mat3 &other) const;
        Mat3 operator*(float scalar) const;
        Vec3 operator*(const Vec3 &v) const;

        Mat3 &operator+=(const Mat3 &other);
        Mat3 &operator-=(const Mat3 &other);
        Mat3 &operator*=(float scalar);

        bool operator==(const Mat3 &other) const;
        bool operator!=(const Mat3 &other) const;

        Mat3 Transposed() const;
        float Determinant() const;
        Mat3 Inverse() const;
        Mat3 InverseSafe() const;

        Vec2 TransformPoint2D(const Vec2 &p) const;

        Vec2 TransformVector2D(const Vec2 &v) const;

        static Mat3 Identity();
        static Mat3 Scale(const Vec3 &s);
        static Mat3 RotationX(float angleRad);
        static Mat3 RotationY(float angleRad);
        static Mat3 RotationZ(float angleRad);
    };

    Mat3 operator*(float scalar, const Mat3 &m);
    std::ostream &operator<<(std::ostream &os, const Mat3 &m);

    struct alignas(16) Mat4
    {
        Vec4 col0, col1, col2, col3;

        Mat4();
        explicit Mat4(float scalar);
        Mat4(float m00, float m01, float m02, float m03, float m10, float m11, float m12, float m13,
             float m20, float m21, float m22, float m23, float m30, float m31, float m32, float m33);
        Mat4(const Vec4 &col0, const Vec4 &col1, const Vec4 &col2, const Vec4 &col3);

        explicit Mat4(const Mat3 &upperLeft);
        Mat4(const Mat3 &upperLeft, const Vec3 &translation);

        Vec4 &operator[](int col);
        const Vec4 &operator[](int col) const;
        float *Data();
        const float *Data() const;

        Mat4 operator+(const Mat4 &other) const;
        Mat4 operator-(const Mat4 &other) const;
        Mat4 operator*(const Mat4 &other) const;
        Mat4 operator*(float scalar) const;
        Vec4 operator*(const Vec4 &v) const;

        Mat4 &operator+=(const Mat4 &other);
        Mat4 &operator-=(const Mat4 &other);
        Mat4 &operator*=(float scalar);

        bool operator==(const Mat4 &other) const;
        bool operator!=(const Mat4 &other) const;

        Mat4 Transposed() const;
        float Determinant() const;
        Mat4 Inverse() const;

        Mat4 InverseAffine() const;

        Mat3 UpperLeft3x3() const;
        Vec3 GetTranslation() const;

        Vec3 TransformPoint(const Vec3 &p) const;

        Vec3 TransformPointPerspective(const Vec3 &p) const;

        Vec3 TransformVector(const Vec3 &v) const;

        static Mat4 Identity();
        static Mat4 Translation(const Vec3 &t);
        static Mat4 Scale(const Vec3 &s);
        static Mat4 RotationX(float angleRad);
        static Mat4 RotationY(float angleRad);
        static Mat4 RotationZ(float angleRad);

        static Mat4 LookAt(const Vec3 &eye, const Vec3 &center, const Vec3 &up);

        static Mat4 Perspective(float fovyRad, float aspect, float near, float far);
        static Mat4 Ortho(float left, float right, float bottom, float top, float near, float far);
    };

    Mat4 operator*(float scalar, const Mat4 &m);
    std::ostream &operator<<(std::ostream &os, const Mat4 &m);

    struct alignas(16) Quaternion
    {
        union
        {
            struct
            {
                float x, y, z, w;
            };
            float v[4];
        };

        Quaternion();
        Quaternion(float x, float y, float z, float w);
        Quaternion(float w, const Vec3 &xyz);

        float &operator[](int index);
        const float &operator[](int index) const;

        Quaternion operator+(const Quaternion &other) const;
        Quaternion operator-(const Quaternion &other) const;
        Quaternion operator*(const Quaternion &other) const;
        Quaternion operator*(float scalar) const;
        Vec3 operator*(const Vec3 &v) const;

        Quaternion &operator+=(const Quaternion &other);
        Quaternion &operator-=(const Quaternion &other);

        Quaternion operator-() const;

        bool operator==(const Quaternion &other) const;
        bool operator!=(const Quaternion &other) const;

        float LengthSquared() const;
        float Length() const;
        Quaternion Normalized() const;
        Quaternion NormalizedSafe() const;
        void Normalize();
        void NormalizeSafe();

        Quaternion Conjugate() const;
        Quaternion Inverse() const;
        float Dot(const Quaternion &other) const;

        Mat3 ToMat3() const;
        Mat4 ToMat4() const;
        Vec3 ToEulerAngles() const;

        static Quaternion Identity();
        static Quaternion FromAxisAngle(const Vec3 &axis, float angleRad);

        static Quaternion FromEulerAngles(float pitchX, float yawY, float rollZ);
        static Quaternion FromMat3(const Mat3 &m);

        static Quaternion FromTo(const Vec3 &from, const Vec3 &to);
        static Quaternion LookRotation(const Vec3 &forward, const Vec3 &up);

        static float Dot(const Quaternion &a, const Quaternion &b);

        static Quaternion Lerp(const Quaternion &a, const Quaternion &b, float t);

        static Quaternion Slerp(const Quaternion &a, const Quaternion &b, float t);
    };

    Quaternion operator*(float scalar, const Quaternion &q);
    std::ostream &operator<<(std::ostream &os, const Quaternion &q);

#ifndef MATHC_NO_EXTRA
    struct Ray
    {
        Vec3 origin;
        Vec3 direction;

        Ray();
        Ray(const Vec3 &origin, const Vec3 &direction);

        Vec3 PointAt(float t) const;

        static Ray FromPoints(const Vec3 &a, const Vec3 &b);
    };

    std::ostream &operator<<(std::ostream &os, const Ray &r);

    struct Plane
    {
        Vec3 normal;
        float d;

        Plane();
        Plane(const Vec3 &normal, float d);
        Plane(const Vec3 &normal, const Vec3 &pointOnPlane);
        Plane(const Vec3 &a, const Vec3 &b, const Vec3 &c);

        float DistanceToPoint(const Vec3 &point) const;
        Vec3 ClosestPoint(const Vec3 &point) const;
        Plane Normalized() const;

        bool IntersectRay(const Ray &ray, float &outT) const;

        static Plane FromPoints(const Vec3 &a, const Vec3 &b, const Vec3 &c);
    };

    std::ostream &operator<<(std::ostream &os, const Plane &p);

    struct Box
    {
        Vec3 min, max;

        Box();
        Box(const Vec3 &min, const Vec3 &max);

        Vec3 Center() const;
        Vec3 Extents() const; 
        Vec3 Size() const;    

        bool Contains(const Vec3 &point) const;
        bool Intersects(const Box &other) const;
        Box Union(const Box &other) const;
        Box Expanded(const Vec3 &point) const;
        Box Expanded(float amount) const;

        bool IntersectRay(const Ray &ray, float &outTMin, float &outTMax) const;

        Box Transformed(const Mat4 &m) const;

        void GetCorners(Vec3 outCorners[8]) const;

        static Box FromCenterExtents(const Vec3 &center, const Vec3 &extents);
        static Box FromPoints(const Vec3 *points, int count);
        static const Box Empty;
    };

    std::ostream &operator<<(std::ostream &os, const Box &b);

    struct Frustum
    {
        Plane planes[6];

        enum PlaneIndex { Left = 0, Right = 1, Bottom = 2, Top = 3, Near = 4, Far = 5 };

        Frustum();

        static Frustum FromViewProjection(const Mat4 &viewProj);

        bool ContainsPoint(const Vec3 &point) const;
        bool IntersectsSphere(const Vec3 &center, float radius) const;
        bool IntersectsBox(const Box &box) const;
    };
#endif 

}

#endif
