#include "mathc.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#if defined(__GNUC__) || defined(__clang__)
#define MATHC_SIMD 1
typedef float SimdF4 __attribute__((vector_size(16)));
#else
#define MATHC_SIMD 0
#endif

namespace Math
{

#if MATHC_SIMD
    namespace
    {

        inline SimdF4 LoadV4(const Vec4 &v)
        {
            SimdF4 r;
            std::memcpy(&r, &v, sizeof(r));
            return r;
        }
        inline Vec4 StoreV4(SimdF4 s)
        {
            Vec4 r;
            std::memcpy(&r, &s, sizeof(r));
            return r;
        }
    }
#endif

    const Vec2 Vec2::Zero = Vec2(0.0f, 0.0f);
    const Vec2 Vec2::One = Vec2(1.0f, 1.0f);
    const Vec2 Vec2::UnitX = Vec2(1.0f, 0.0f);
    const Vec2 Vec2::UnitY = Vec2(0.0f, 1.0f);

    Vec2::Vec2() : x(0.0f), y(0.0f) {}
    Vec2::Vec2(float x, float y) : x(x), y(y) {}
    Vec2::Vec2(float scalar) : x(scalar), y(scalar) {}

    float &Vec2::operator[](int index) { return v[index]; }
    const float &Vec2::operator[](int index) const { return v[index]; }

    Vec2 Vec2::operator+(const Vec2 &o) const { return Vec2(x + o.x, y + o.y); }
    Vec2 Vec2::operator-(const Vec2 &o) const { return Vec2(x - o.x, y - o.y); }
    Vec2 Vec2::operator*(const Vec2 &o) const { return Vec2(x * o.x, y * o.y); }
    Vec2 Vec2::operator/(const Vec2 &o) const { return Vec2(x / o.x, y / o.y); }

    Vec2 Vec2::operator*(float s) const { return Vec2(x * s, y * s); }
    Vec2 Vec2::operator/(float s) const
    {
        float inv = 1.0f / s;
        return Vec2(x * inv, y * inv);
    }

    Vec2 &Vec2::operator+=(const Vec2 &o)
    {
        x += o.x;
        y += o.y;
        return *this;
    }
    Vec2 &Vec2::operator-=(const Vec2 &o)
    {
        x -= o.x;
        y -= o.y;
        return *this;
    }
    Vec2 &Vec2::operator*=(const Vec2 &o)
    {
        x *= o.x;
        y *= o.y;
        return *this;
    }
    Vec2 &Vec2::operator/=(const Vec2 &o)
    {
        x /= o.x;
        y /= o.y;
        return *this;
    }
    Vec2 &Vec2::operator*=(float s)
    {
        x *= s;
        y *= s;
        return *this;
    }
    Vec2 &Vec2::operator/=(float s)
    {
        float inv = 1.0f / s;
        x *= inv;
        y *= inv;
        return *this;
    }

    Vec2 Vec2::operator-() const { return Vec2(-x, -y); }

    bool Vec2::operator==(const Vec2 &o) const
    {
        return std::fabs(x - o.x) < EPSILON && std::fabs(y - o.y) < EPSILON;
    }
    bool Vec2::operator!=(const Vec2 &o) const { return !(*this == o); }

    float Vec2::LengthSquared() const { return x * x + y * y; }
    float Vec2::Length() const { return std::sqrt(LengthSquared()); }

    Vec2 Vec2::Normalized() const
    {
        float inv = 1.0f / std::sqrt(LengthSquared());
        return Vec2(x * inv, y * inv);
    }

    Vec2 Vec2::NormalizedSafe() const
    {
        float lenSq = std::max(LengthSquared(), EPSILON * EPSILON);
        float inv = 1.0f / std::sqrt(lenSq);
        return Vec2(x * inv, y * inv);
    }

    void Vec2::Normalize() { *this = Normalized(); }
    void Vec2::NormalizeSafe() { *this = NormalizedSafe(); }

    float Vec2::Dot(const Vec2 &o) const { return x * o.x + y * o.y; }

    float Vec2::Cross(const Vec2 &o) const { return x * o.y - y * o.x; }

    float Vec2::Angle() const { return std::atan2(y, x); }
    float Vec2::AngleDeg() const { return Angle() * RAD2DEG; }

    Vec2 Vec2::Rotate(float angleRad) const
    {
        float s = std::sin(angleRad);
        float c = std::cos(angleRad);
        return Vec2(x * c - y * s, x * s + y * c);
    }
    Vec2 Vec2::RotateDeg(float angleDeg) const { return Rotate(angleDeg * DEG2RAD); }

    Vec2 Vec2::FromAngle(float angleRad) { return Vec2(std::cos(angleRad), std::sin(angleRad)); }
    Vec2 Vec2::FromAngleDeg(float angleDeg) { return FromAngle(angleDeg * DEG2RAD); }

    float Vec2::AngleBetween(const Vec2 &a, const Vec2 &b)
    {
        return std::atan2(a.Cross(b), a.Dot(b));
    }
    float Vec2::AngleBetweenDeg(const Vec2 &a, const Vec2 &b) { return AngleBetween(a, b) * RAD2DEG; }

    float Vec2::Dot(const Vec2 &a, const Vec2 &b) { return a.Dot(b); }
    float Vec2::Cross(const Vec2 &a, const Vec2 &b) { return a.Cross(b); }
    float Vec2::Distance(const Vec2 &a, const Vec2 &b) { return (a - b).Length(); }
    float Vec2::DistanceSquared(const Vec2 &a, const Vec2 &b) { return (a - b).LengthSquared(); }
    Vec2 Vec2::Lerp(const Vec2 &a, const Vec2 &b, float t) { return a + (b - a) * t; }
    Vec2 Vec2::Min(const Vec2 &a, const Vec2 &b) { return Vec2(a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y); }
    Vec2 Vec2::Max(const Vec2 &a, const Vec2 &b) { return Vec2(a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y); }
    Vec2 Vec2::Clamp(const Vec2 &v, const Vec2 &lo, const Vec2 &hi) { return Min(Max(v, lo), hi); }

    Vec2 operator*(float scalar, const Vec2 &v) { return v * scalar; }
    std::ostream &operator<<(std::ostream &os, const Vec2 &v)
    {
        return os << "(" << v.x << ", " << v.y << ")";
    }

    const Vec3 Vec3::Zero = Vec3(0.0f, 0.0f, 0.0f);
    const Vec3 Vec3::One = Vec3(1.0f, 1.0f, 1.0f);
    const Vec3 Vec3::UnitX = Vec3(1.0f, 0.0f, 0.0f);
    const Vec3 Vec3::UnitY = Vec3(0.0f, 1.0f, 0.0f);
    const Vec3 Vec3::UnitZ = Vec3(0.0f, 0.0f, 1.0f);

    Vec3::Vec3() : x(0.0f), y(0.0f), z(0.0f) {}
    Vec3::Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
    Vec3::Vec3(float scalar) : x(scalar), y(scalar), z(scalar) {}
    Vec3::Vec3(const Vec2 &xy, float z) : x(xy.x), y(xy.y), z(z) {}

    float &Vec3::operator[](int index) { return v[index]; }
    const float &Vec3::operator[](int index) const { return v[index]; }

    Vec3 Vec3::operator+(const Vec3 &o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
    Vec3 Vec3::operator-(const Vec3 &o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
    Vec3 Vec3::operator*(const Vec3 &o) const { return Vec3(x * o.x, y * o.y, z * o.z); }
    Vec3 Vec3::operator/(const Vec3 &o) const { return Vec3(x / o.x, y / o.y, z / o.z); }

    Vec3 Vec3::operator*(float s) const { return Vec3(x * s, y * s, z * s); }
    Vec3 Vec3::operator/(float s) const
    {
        float inv = 1.0f / s;
        return Vec3(x * inv, y * inv, z * inv);
    }

    Vec3 &Vec3::operator+=(const Vec3 &o)
    {
        x += o.x;
        y += o.y;
        z += o.z;
        return *this;
    }
    Vec3 &Vec3::operator-=(const Vec3 &o)
    {
        x -= o.x;
        y -= o.y;
        z -= o.z;
        return *this;
    }
    Vec3 &Vec3::operator*=(const Vec3 &o)
    {
        x *= o.x;
        y *= o.y;
        z *= o.z;
        return *this;
    }
    Vec3 &Vec3::operator/=(const Vec3 &o)
    {
        x /= o.x;
        y /= o.y;
        z /= o.z;
        return *this;
    }
    Vec3 &Vec3::operator*=(float s)
    {
        x *= s;
        y *= s;
        z *= s;
        return *this;
    }
    Vec3 &Vec3::operator/=(float s)
    {
        float inv = 1.0f / s;
        x *= inv;
        y *= inv;
        z *= inv;
        return *this;
    }

    Vec3 Vec3::operator-() const { return Vec3(-x, -y, -z); }

    bool Vec3::operator==(const Vec3 &o) const
    {
        return std::fabs(x - o.x) < EPSILON && std::fabs(y - o.y) < EPSILON && std::fabs(z - o.z) < EPSILON;
    }
    bool Vec3::operator!=(const Vec3 &o) const { return !(*this == o); }

    float Vec3::LengthSquared() const { return x * x + y * y + z * z; }
    float Vec3::Length() const { return std::sqrt(LengthSquared()); }

    Vec3 Vec3::Normalized() const
    {
        float inv = 1.0f / std::sqrt(LengthSquared());
        return Vec3(x * inv, y * inv, z * inv);
    }

    Vec3 Vec3::NormalizedSafe() const
    {
        float lenSq = std::max(LengthSquared(), EPSILON * EPSILON);
        float inv = 1.0f / std::sqrt(lenSq);
        return Vec3(x * inv, y * inv, z * inv);
    }

    void Vec3::Normalize() { *this = Normalized(); }
    void Vec3::NormalizeSafe() { *this = NormalizedSafe(); }

    float Vec3::Dot(const Vec3 &o) const { return x * o.x + y * o.y + z * o.z; }

    Vec3 Vec3::Cross(const Vec3 &o) const
    {
        return Vec3(y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x);
    }

    Vec2 Vec3::xy() const { return Vec2(x, y); }

    Vec3 Vec3::RotateX(float angleRad) const
    {
        float s = std::sin(angleRad);
        float c = std::cos(angleRad);
        return Vec3(x, y * c - z * s, y * s + z * c);
    }
    Vec3 Vec3::RotateY(float angleRad) const
    {
        float s = std::sin(angleRad);
        float c = std::cos(angleRad);
        return Vec3(x * c + z * s, y, -x * s + z * c);
    }
    Vec3 Vec3::RotateZ(float angleRad) const
    {
        float s = std::sin(angleRad);
        float c = std::cos(angleRad);
        return Vec3(x * c - y * s, x * s + y * c, z);
    }
    Vec3 Vec3::RotateXDeg(float angleDeg) const { return RotateX(angleDeg * DEG2RAD); }
    Vec3 Vec3::RotateYDeg(float angleDeg) const { return RotateY(angleDeg * DEG2RAD); }
    Vec3 Vec3::RotateZDeg(float angleDeg) const { return RotateZ(angleDeg * DEG2RAD); }

    float Vec3::AngleBetween(const Vec3 &a, const Vec3 &b)
    {
        return std::atan2(a.Cross(b).Length(), a.Dot(b));
    }
    float Vec3::AngleBetweenDeg(const Vec3 &a, const Vec3 &b) { return AngleBetween(a, b) * RAD2DEG; }

    float Vec3::Dot(const Vec3 &a, const Vec3 &b) { return a.Dot(b); }
    Vec3 Vec3::Cross(const Vec3 &a, const Vec3 &b) { return a.Cross(b); }
    float Vec3::Distance(const Vec3 &a, const Vec3 &b) { return (a - b).Length(); }
    float Vec3::DistanceSquared(const Vec3 &a, const Vec3 &b) { return (a - b).LengthSquared(); }
    Vec3 Vec3::Lerp(const Vec3 &a, const Vec3 &b, float t) { return a + (b - a) * t; }
    Vec3 Vec3::Min(const Vec3 &a, const Vec3 &b)
    {
        return Vec3(a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.z < b.z ? a.z : b.z);
    }
    Vec3 Vec3::Max(const Vec3 &a, const Vec3 &b)
    {
        return Vec3(a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y, a.z > b.z ? a.z : b.z);
    }
    Vec3 Vec3::Clamp(const Vec3 &v, const Vec3 &lo, const Vec3 &hi) { return Min(Max(v, lo), hi); }

    Vec3 operator*(float scalar, const Vec3 &v) { return v * scalar; }
    std::ostream &operator<<(std::ostream &os, const Vec3 &v)
    {
        return os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
    }

    const Vec4 Vec4::Zero = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    const Vec4 Vec4::One = Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    const Vec4 Vec4::UnitX = Vec4(1.0f, 0.0f, 0.0f, 0.0f);
    const Vec4 Vec4::UnitY = Vec4(0.0f, 1.0f, 0.0f, 0.0f);
    const Vec4 Vec4::UnitZ = Vec4(0.0f, 0.0f, 1.0f, 0.0f);
    const Vec4 Vec4::UnitW = Vec4(0.0f, 0.0f, 0.0f, 1.0f);

    Vec4::Vec4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
    Vec4::Vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    Vec4::Vec4(float scalar) : x(scalar), y(scalar), z(scalar), w(scalar) {}
    Vec4::Vec4(const Vec3 &xyz, float w) : x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}
    Vec4::Vec4(const Vec2 &xy, float z, float w) : x(xy.x), y(xy.y), z(z), w(w) {}

    float &Vec4::operator[](int index) { return v[index]; }
    const float &Vec4::operator[](int index) const { return v[index]; }

#if MATHC_SIMD
    Vec4 Vec4::operator+(const Vec4 &o) const { return StoreV4(LoadV4(*this) + LoadV4(o)); }
    Vec4 Vec4::operator-(const Vec4 &o) const { return StoreV4(LoadV4(*this) - LoadV4(o)); }
    Vec4 Vec4::operator*(const Vec4 &o) const { return StoreV4(LoadV4(*this) * LoadV4(o)); }
    Vec4 Vec4::operator/(const Vec4 &o) const { return StoreV4(LoadV4(*this) / LoadV4(o)); }
    Vec4 Vec4::operator*(float s) const { return StoreV4(LoadV4(*this) * SimdF4{s, s, s, s}); }
    Vec4 Vec4::operator/(float s) const { return StoreV4(LoadV4(*this) / SimdF4{s, s, s, s}); }
#else
    Vec4 Vec4::operator+(const Vec4 &o) const { return Vec4(x + o.x, y + o.y, z + o.z, w + o.w); }
    Vec4 Vec4::operator-(const Vec4 &o) const { return Vec4(x - o.x, y - o.y, z - o.z, w - o.w); }
    Vec4 Vec4::operator*(const Vec4 &o) const { return Vec4(x * o.x, y * o.y, z * o.z, w * o.w); }
    Vec4 Vec4::operator/(const Vec4 &o) const { return Vec4(x / o.x, y / o.y, z / o.z, w / o.w); }
    Vec4 Vec4::operator*(float s) const { return Vec4(x * s, y * s, z * s, w * s); }
    Vec4 Vec4::operator/(float s) const
    {
        float inv = 1.0f / s;
        return Vec4(x * inv, y * inv, z * inv, w * inv);
    }
#endif

    Vec4 &Vec4::operator+=(const Vec4 &o) { return *this = *this + o; }
    Vec4 &Vec4::operator-=(const Vec4 &o) { return *this = *this - o; }
    Vec4 &Vec4::operator*=(const Vec4 &o) { return *this = *this * o; }
    Vec4 &Vec4::operator/=(const Vec4 &o) { return *this = *this / o; }
    Vec4 &Vec4::operator*=(float s) { return *this = *this * s; }
    Vec4 &Vec4::operator/=(float s) { return *this = *this / s; }

    Vec4 Vec4::operator-() const { return Vec4(-x, -y, -z, -w); }

    bool Vec4::operator==(const Vec4 &o) const
    {
        return std::fabs(x - o.x) < EPSILON && std::fabs(y - o.y) < EPSILON &&
               std::fabs(z - o.z) < EPSILON && std::fabs(w - o.w) < EPSILON;
    }
    bool Vec4::operator!=(const Vec4 &o) const { return !(*this == o); }

    float Vec4::LengthSquared() const { return x * x + y * y + z * z + w * w; }
    float Vec4::Length() const { return std::sqrt(LengthSquared()); }

    Vec4 Vec4::Normalized() const
    {
        float inv = 1.0f / std::sqrt(LengthSquared());
        return *this * inv;
    }

    Vec4 Vec4::NormalizedSafe() const
    {
        float lenSq = std::max(LengthSquared(), EPSILON * EPSILON);
        float inv = 1.0f / std::sqrt(lenSq);
        return *this * inv;
    }

    void Vec4::Normalize() { *this = Normalized(); }
    void Vec4::NormalizeSafe() { *this = NormalizedSafe(); }

    float Vec4::Dot(const Vec4 &o) const { return x * o.x + y * o.y + z * o.z + w * o.w; }

    Vec2 Vec4::xy() const { return Vec2(x, y); }
    Vec3 Vec4::xyz() const { return Vec3(x, y, z); }

    float Vec4::AngleBetween(const Vec4 &a, const Vec4 &b)
    {
        float d = a.Dot(b) / (a.Length() * b.Length());
        d = std::max(-1.0f, std::min(1.0f, d));
        return std::acos(d);
    }
    float Vec4::AngleBetweenDeg(const Vec4 &a, const Vec4 &b) { return AngleBetween(a, b) * RAD2DEG; }

    float Vec4::Dot(const Vec4 &a, const Vec4 &b) { return a.Dot(b); }
    float Vec4::Distance(const Vec4 &a, const Vec4 &b) { return (a - b).Length(); }
    float Vec4::DistanceSquared(const Vec4 &a, const Vec4 &b) { return (a - b).LengthSquared(); }
    Vec4 Vec4::Lerp(const Vec4 &a, const Vec4 &b, float t) { return a + (b - a) * t; }
    Vec4 Vec4::Min(const Vec4 &a, const Vec4 &b)
    {
        return Vec4(a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y,
                     a.z < b.z ? a.z : b.z, a.w < b.w ? a.w : b.w);
    }
    Vec4 Vec4::Max(const Vec4 &a, const Vec4 &b)
    {
        return Vec4(a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y,
                     a.z > b.z ? a.z : b.z, a.w > b.w ? a.w : b.w);
    }
    Vec4 Vec4::Clamp(const Vec4 &v, const Vec4 &lo, const Vec4 &hi) { return Min(Max(v, lo), hi); }

    Vec4 operator*(float scalar, const Vec4 &v) { return v * scalar; }
    std::ostream &operator<<(std::ostream &os, const Vec4 &v)
    {
        return os << "(" << v.x << ", " << v.y << ", " << v.z << ", " << v.w << ")";
    }

    Mat2::Mat2() : col0(1.0f, 0.0f), col1(0.0f, 1.0f) {}
    Mat2::Mat2(float scalar) : col0(scalar, 0.0f), col1(0.0f, scalar) {}
    Mat2::Mat2(float m00, float m01, float m10, float m11) : col0(m00, m01), col1(m10, m11) {}
    Mat2::Mat2(const Vec2 &c0, const Vec2 &c1) : col0(c0), col1(c1) {}

    Vec2 &Mat2::operator[](int col) { return (&col0)[col]; }
    const Vec2 &Mat2::operator[](int col) const { return (&col0)[col]; }
    float *Mat2::Data() { return &col0.x; }
    const float *Mat2::Data() const { return &col0.x; }

    Mat2 Mat2::operator+(const Mat2 &o) const { return Mat2(col0 + o.col0, col1 + o.col1); }
    Mat2 Mat2::operator-(const Mat2 &o) const { return Mat2(col0 - o.col0, col1 - o.col1); }
    Mat2 Mat2::operator*(const Mat2 &o) const { return Mat2(*this * o.col0, *this * o.col1); }
    Mat2 Mat2::operator*(float s) const { return Mat2(col0 * s, col1 * s); }
    Vec2 Mat2::operator*(const Vec2 &v) const { return col0 * v.x + col1 * v.y; }

    bool Mat2::operator==(const Mat2 &o) const { return col0 == o.col0 && col1 == o.col1; }
    bool Mat2::operator!=(const Mat2 &o) const { return !(*this == o); }

    Mat2 Mat2::Transposed() const { return Mat2(col0.x, col1.x, col0.y, col1.y); }
    float Mat2::Determinant() const { return col0.Cross(col1); }

    Mat2 Mat2::Inverse() const
    {
        float invDet = 1.0f / Determinant();
        return Mat2(col1.y * invDet, -col0.y * invDet, -col1.x * invDet, col0.x * invDet);
    }

    Mat2 Mat2::InverseSafe() const
    {
        float det = Determinant();
        float safeDet = std::copysign(std::max(std::fabs(det), EPSILON), det);
        float invDet = 1.0f / safeDet;
        return Mat2(col1.y * invDet, -col0.y * invDet, -col1.x * invDet, col0.x * invDet);
    }

    Vec2 Mat2::GetScale() const
    {
        float detSign = Determinant() < 0.0f ? -1.0f : 1.0f;
        return Vec2(col0.Length(), detSign * col1.Length());
    }

    float Mat2::GetRotation() const { return std::atan2(col0.y, col0.x); }

    float Mat2::GetSkew() const
    {
        float detSign = Determinant() < 0.0f ? -1.0f : 1.0f;
        float d = col0.Normalized().Dot(col1.Normalized() * detSign);
        d = std::max(-1.0f, std::min(1.0f, d)); 
        return std::acos(d) - PI * 0.5f;
    }

    Mat2 Mat2::Identity() { return Mat2(); }
    Mat2 Mat2::Scale(float sx, float sy) { return Mat2(sx, 0.0f, 0.0f, sy); }
    Mat2 Mat2::Scale(const Vec2 &s) { return Scale(s.x, s.y); }

    Mat2 Mat2::Rotation(float angleRad)
    {
        float s = std::sin(angleRad);
        float c = std::cos(angleRad);
        return Mat2(c, s, -s, c);
    }

    Mat2 Mat2::SkewX(float angleRad) { return Mat2(1.0f, 0.0f, std::tan(angleRad), 1.0f); }

    Mat2 Mat2::SkewY(float angleRad) { return Mat2(1.0f, std::tan(angleRad), 0.0f, 1.0f); }

    Mat2 Mat2::FromScaleRotationSkew(const Vec2 &scale, float rotationRad, float skewRad)
    {
        float cr = std::cos(rotationRad), sr = std::sin(rotationRad);
        float crs = std::cos(rotationRad + skewRad), srs = std::sin(rotationRad + skewRad);
        return Mat2(cr * scale.x, sr * scale.x, -srs * scale.y, crs * scale.y);
    }

    Mat2 operator*(float scalar, const Mat2 &m) { return m * scalar; }
    std::ostream &operator<<(std::ostream &os, const Mat2 &m)
    {
        return os << "[" << m.col0 << ", " << m.col1 << "]";
    }

#ifndef MATHC_NO_EXTRA
    Transform2D::Transform2D() : basis(), origin(0.0f, 0.0f) {}
    Transform2D::Transform2D(const Mat2 &basis, const Vec2 &origin) : basis(basis), origin(origin) {}

    Vec2 Transform2D::TransformPoint(const Vec2 &p) const { return basis * p + origin; }
    Vec2 Transform2D::TransformVector(const Vec2 &v) const { return basis * v; }

    void Transform2D::TransformPoints(const Vec2 *in, Vec2 *out, int count) const
    {
        for (int i = 0; i < count; i++)
            out[i] = TransformPoint(in[i]);
    }

    Transform2D Transform2D::operator*(const Transform2D &o) const
    {
        return Transform2D(basis * o.basis, basis * o.origin + origin);
    }

    bool Transform2D::operator==(const Transform2D &o) const { return basis == o.basis && origin == o.origin; }
    bool Transform2D::operator!=(const Transform2D &o) const { return !(*this == o); }

    Transform2D Transform2D::Inverse() const
    {
        Mat2 invBasis = basis.Inverse();
        return Transform2D(invBasis, invBasis * (-origin));
    }

    Transform2D Transform2D::InverseSafe() const
    {
        Mat2 invBasis = basis.InverseSafe();
        return Transform2D(invBasis, invBasis * (-origin));
    }

    Transform2D Transform2D::RigidInverse() const
    {
        Mat2 invBasis = basis.Transposed();
        return Transform2D(invBasis, invBasis * (-origin));
    }

    Transform2D Transform2D::Identity() { return Transform2D(); }
    Transform2D Transform2D::Translation(const Vec2 &t) { return Transform2D(Mat2::Identity(), t); }
    Transform2D Transform2D::Rotation(float angleRad) { return Transform2D(Mat2::Rotation(angleRad), Vec2::Zero); }
    Transform2D Transform2D::ScaleBy(const Vec2 &s) { return Transform2D(Mat2::Scale(s), Vec2::Zero); }
    Transform2D Transform2D::Rigid(const Vec2 &translation, float rotationRad)
    {
        return Transform2D(Mat2::Rotation(rotationRad), translation);
    }

    Transform2D Transform2D::RotationAround(const Vec2 &pivot, float angleRad)
    {
        Mat2 r = Mat2::Rotation(angleRad);
        return Transform2D(r, pivot - r * pivot);
    }

    Transform2D Transform2D::ScaleAround(const Vec2 &pivot, const Vec2 &scale)
    {
        Mat2 s = Mat2::Scale(scale);
        return Transform2D(s, pivot - s * pivot);
    }

    Transform2D Transform2D::FromTRS(const Vec2 &translation, float rotationRad, const Vec2 &scale,
                                      float skewXRad, float skewYRad, const Vec2 &pivot)
    {
        Mat2 basis = Mat2::Rotation(rotationRad) * Mat2::SkewY(skewYRad) * Mat2::SkewX(skewXRad) * Mat2::Scale(scale);
        return Transform2D(basis, translation - basis * pivot);
    }

    std::ostream &operator<<(std::ostream &os, const Transform2D &t)
    {
        return os << "{basis: " << t.basis << ", origin: " << t.origin << "}";
    }
#endif 

    Mat3::Mat3() : col0(1.0f, 0.0f, 0.0f), col1(0.0f, 1.0f, 0.0f), col2(0.0f, 0.0f, 1.0f) {}
    Mat3::Mat3(float scalar) : col0(scalar, 0.0f, 0.0f), col1(0.0f, scalar, 0.0f), col2(0.0f, 0.0f, scalar) {}
    Mat3::Mat3(float m00, float m01, float m02, float m10, float m11, float m12, float m20, float m21, float m22)
        : col0(m00, m01, m02), col1(m10, m11, m12), col2(m20, m21, m22)
    {
    }
    Mat3::Mat3(const Vec3 &c0, const Vec3 &c1, const Vec3 &c2) : col0(c0), col1(c1), col2(c2) {}
#ifndef MATHC_NO_EXTRA
    Mat3::Mat3(const Transform2D &t)
        : col0(t.basis.col0.x, t.basis.col0.y, 0.0f), col1(t.basis.col1.x, t.basis.col1.y, 0.0f),
          col2(t.origin.x, t.origin.y, 1.0f)
    {
    }
#endif 

    Vec3 &Mat3::operator[](int col) { return (&col0)[col]; }
    const Vec3 &Mat3::operator[](int col) const { return (&col0)[col]; }
    float *Mat3::Data() { return &col0.x; }
    const float *Mat3::Data() const { return &col0.x; }

    Mat3 Mat3::operator+(const Mat3 &o) const { return Mat3(col0 + o.col0, col1 + o.col1, col2 + o.col2); }
    Mat3 Mat3::operator-(const Mat3 &o) const { return Mat3(col0 - o.col0, col1 - o.col1, col2 - o.col2); }
    Mat3 Mat3::operator*(const Mat3 &o) const { return Mat3(*this * o.col0, *this * o.col1, *this * o.col2); }
    Mat3 Mat3::operator*(float s) const { return Mat3(col0 * s, col1 * s, col2 * s); }
    Vec3 Mat3::operator*(const Vec3 &v) const { return col0 * v.x + col1 * v.y + col2 * v.z; }

    bool Mat3::operator==(const Mat3 &o) const { return col0 == o.col0 && col1 == o.col1 && col2 == o.col2; }
    bool Mat3::operator!=(const Mat3 &o) const { return !(*this == o); }

    Mat3 Mat3::Transposed() const
    {
        return Mat3(col0.x, col1.x, col2.x, col0.y, col1.y, col2.y, col0.z, col1.z, col2.z);
    }

    float Mat3::Determinant() const { return col0.Dot(col1.Cross(col2)); }

    Mat3 Mat3::Inverse() const
    {
        Vec3 r0 = col1.Cross(col2);
        Vec3 r1 = col2.Cross(col0);
        Vec3 r2 = col0.Cross(col1);
        float invDet = 1.0f / col0.Dot(r0);
        return Mat3(r0.x * invDet, r1.x * invDet, r2.x * invDet,
                     r0.y * invDet, r1.y * invDet, r2.y * invDet,
                     r0.z * invDet, r1.z * invDet, r2.z * invDet);
    }

    Mat3 Mat3::InverseSafe() const
    {
        Vec3 r0 = col1.Cross(col2);
        Vec3 r1 = col2.Cross(col0);
        Vec3 r2 = col0.Cross(col1);
        float det = col0.Dot(r0);
        float safeDet = std::copysign(std::max(std::fabs(det), EPSILON), det);
        float invDet = 1.0f / safeDet;
        return Mat3(r0.x * invDet, r1.x * invDet, r2.x * invDet,
                     r0.y * invDet, r1.y * invDet, r2.y * invDet,
                     r0.z * invDet, r1.z * invDet, r2.z * invDet);
    }

    Vec2 Mat3::TransformPoint2D(const Vec2 &p) const { return (*this * Vec3(p, 1.0f)).xy(); }
    Vec2 Mat3::TransformVector2D(const Vec2 &v) const { return (*this * Vec3(v, 0.0f)).xy(); }

    Mat3 Mat3::Identity() { return Mat3(); }
    Mat3 Mat3::Scale(const Vec3 &s) { return Mat3(s.x, 0.0f, 0.0f, 0.0f, s.y, 0.0f, 0.0f, 0.0f, s.z); }

    Mat3 Mat3::RotationX(float angleRad)
    {
        float s = std::sin(angleRad), c = std::cos(angleRad);
        return Mat3(1.0f, 0.0f, 0.0f, 0.0f, c, s, 0.0f, -s, c);
    }
    Mat3 Mat3::RotationY(float angleRad)
    {
        float s = std::sin(angleRad), c = std::cos(angleRad);
        return Mat3(c, 0.0f, -s, 0.0f, 1.0f, 0.0f, s, 0.0f, c);
    }
    Mat3 Mat3::RotationZ(float angleRad)
    {
        float s = std::sin(angleRad), c = std::cos(angleRad);
        return Mat3(c, s, 0.0f, -s, c, 0.0f, 0.0f, 0.0f, 1.0f);
    }

    Mat3 operator*(float scalar, const Mat3 &m) { return m * scalar; }
    std::ostream &operator<<(std::ostream &os, const Mat3 &m)
    {
        return os << "[" << m.col0 << ", " << m.col1 << ", " << m.col2 << "]";
    }

    Mat4::Mat4()
        : col0(1.0f, 0.0f, 0.0f, 0.0f), col1(0.0f, 1.0f, 0.0f, 0.0f),
          col2(0.0f, 0.0f, 1.0f, 0.0f), col3(0.0f, 0.0f, 0.0f, 1.0f)
    {
    }
    Mat4::Mat4(float scalar)
        : col0(scalar, 0.0f, 0.0f, 0.0f), col1(0.0f, scalar, 0.0f, 0.0f),
          col2(0.0f, 0.0f, scalar, 0.0f), col3(0.0f, 0.0f, 0.0f, scalar)
    {
    }
    Mat4::Mat4(float m00, float m01, float m02, float m03, float m10, float m11, float m12, float m13,
               float m20, float m21, float m22, float m23, float m30, float m31, float m32, float m33)
        : col0(m00, m01, m02, m03), col1(m10, m11, m12, m13), col2(m20, m21, m22, m23), col3(m30, m31, m32, m33)
    {
    }
    Mat4::Mat4(const Vec4 &c0, const Vec4 &c1, const Vec4 &c2, const Vec4 &c3)
        : col0(c0), col1(c1), col2(c2), col3(c3)
    {
    }
    Mat4::Mat4(const Mat3 &u)
        : col0(u.col0, 0.0f), col1(u.col1, 0.0f), col2(u.col2, 0.0f), col3(0.0f, 0.0f, 0.0f, 1.0f)
    {
    }
    Mat4::Mat4(const Mat3 &u, const Vec3 &t)
        : col0(u.col0, 0.0f), col1(u.col1, 0.0f), col2(u.col2, 0.0f), col3(t, 1.0f)
    {
    }

    Vec4 &Mat4::operator[](int col) { return (&col0)[col]; }
    const Vec4 &Mat4::operator[](int col) const { return (&col0)[col]; }
    float *Mat4::Data() { return &col0.x; }
    const float *Mat4::Data() const { return &col0.x; }

    Mat4 Mat4::operator+(const Mat4 &o) const { return Mat4(col0 + o.col0, col1 + o.col1, col2 + o.col2, col3 + o.col3); }
    Mat4 Mat4::operator-(const Mat4 &o) const { return Mat4(col0 - o.col0, col1 - o.col1, col2 - o.col2, col3 - o.col3); }
    Mat4 Mat4::operator*(const Mat4 &o) const
    {
        return Mat4(*this * o.col0, *this * o.col1, *this * o.col2, *this * o.col3);
    }
    Mat4 Mat4::operator*(float s) const { return Mat4(col0 * s, col1 * s, col2 * s, col3 * s); }
    Vec4 Mat4::operator*(const Vec4 &v) const { return col0 * v.x + col1 * v.y + col2 * v.z + col3 * v.w; }

    bool Mat4::operator==(const Mat4 &o) const
    {
        return col0 == o.col0 && col1 == o.col1 && col2 == o.col2 && col3 == o.col3;
    }
    bool Mat4::operator!=(const Mat4 &o) const { return !(*this == o); }

    Mat4 Mat4::Transposed() const
    {
        return Mat4(col0.x, col1.x, col2.x, col3.x, col0.y, col1.y, col2.y, col3.y,
                     col0.z, col1.z, col2.z, col3.z, col0.w, col1.w, col2.w, col3.w);
    }

    float Mat4::Determinant() const
    {
        const float *m = Data();
        float c0 = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
                   m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
        float c1 = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
                   m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
        float c2 = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
                   m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
        float c3 = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
                   m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
        return m[0] * c0 + m[1] * c1 + m[2] * c2 + m[3] * c3;
    }

    Mat4 Mat4::Inverse() const
    {
        const float *m = Data();
        float inv[16];

        inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
                 m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
        inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
                 m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
        inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
                 m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
        inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
                  m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
        inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
                 m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
        inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
                 m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
        inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
                 m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
        inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
                  m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
        inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
                 m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
        inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
                 m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
        inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
                  m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
        inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
                  m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
        inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
                 m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
        inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
                 m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
        inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
                  m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
        inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
                  m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

        float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
        float invDet = 1.0f / det;

        Mat4 result;
        float *r = result.Data();
        for (int i = 0; i < 16; i++)
            r[i] = inv[i] * invDet;
        return result;
    }

    Mat4 Mat4::InverseAffine() const
    {
        Mat3 invRotScale = UpperLeft3x3().Inverse();
        Vec3 t = GetTranslation();
        return Mat4(invRotScale, invRotScale * -t);
    }

    Mat3 Mat4::UpperLeft3x3() const { return Mat3(col0.xyz(), col1.xyz(), col2.xyz()); }
    Vec3 Mat4::GetTranslation() const { return col3.xyz(); }

    Vec3 Mat4::TransformPoint(const Vec3 &p) const { return (*this * Vec4(p, 1.0f)).xyz(); }
    Vec3 Mat4::TransformPointPerspective(const Vec3 &p) const
    {
        Vec4 r = *this * Vec4(p, 1.0f);
        return r.xyz() / r.w;
    }
    Vec3 Mat4::TransformVector(const Vec3 &v) const { return (*this * Vec4(v, 0.0f)).xyz(); }

    Mat4 Mat4::Identity() { return Mat4(); }
    Mat4 Mat4::Translation(const Vec3 &t) { return Mat4(Mat3::Identity(), t); }
    Mat4 Mat4::Scale(const Vec3 &s) { return Mat4(Mat3::Scale(s)); }
    Mat4 Mat4::RotationX(float angleRad) { return Mat4(Mat3::RotationX(angleRad)); }
    Mat4 Mat4::RotationY(float angleRad) { return Mat4(Mat3::RotationY(angleRad)); }
    Mat4 Mat4::RotationZ(float angleRad) { return Mat4(Mat3::RotationZ(angleRad)); }

    Mat4 Mat4::LookAt(const Vec3 &eye, const Vec3 &center, const Vec3 &up)
    {
        Vec3 f = (center - eye).Normalized();
        Vec3 s = f.Cross(up).Normalized();
        Vec3 u = s.Cross(f);

        return Mat4(
            s.x, u.x, -f.x, 0.0f,
            s.y, u.y, -f.y, 0.0f,
            s.z, u.z, -f.z, 0.0f,
            -s.Dot(eye), -u.Dot(eye), f.Dot(eye), 1.0f);
    }

    Mat4 Mat4::Perspective(float fovyRad, float aspect, float nearZ, float farZ)
    {
        float tanHalfFovy = std::tan(fovyRad * 0.5f);
        Mat4 r(0.0f);
        r.col0.x = 1.0f / (aspect * tanHalfFovy);
        r.col1.y = 1.0f / tanHalfFovy;
        r.col2.z = -(farZ + nearZ) / (farZ - nearZ);
        r.col2.w = -1.0f;
        r.col3.z = -(2.0f * farZ * nearZ) / (farZ - nearZ);
        return r;
    }

    Mat4 Mat4::Ortho(float left, float right, float bottom, float top, float nearZ, float farZ)
    {
        Mat4 r;
        r.col0.x = 2.0f / (right - left);
        r.col1.y = 2.0f / (top - bottom);
        r.col2.z = -2.0f / (farZ - nearZ);
        r.col3.x = -(right + left) / (right - left);
        r.col3.y = -(top + bottom) / (top - bottom);
        r.col3.z = -(farZ + nearZ) / (farZ - nearZ);
        return r;
    }

    Mat4 operator*(float scalar, const Mat4 &m) { return m * scalar; }
    std::ostream &operator<<(std::ostream &os, const Mat4 &m)
    {
        return os << "[" << m.col0 << ", " << m.col1 << ", " << m.col2 << ", " << m.col3 << "]";
    }

    Quaternion::Quaternion() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}
    Quaternion::Quaternion(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

    float &Quaternion::operator[](int index) { return v[index]; }
    const float &Quaternion::operator[](int index) const { return v[index]; }

#if MATHC_SIMD

    static inline SimdF4 LoadQ(const Quaternion &q)
    {
        SimdF4 r;
        std::memcpy(&r, &q, sizeof(r));
        return r;
    }
    static inline Quaternion StoreQ(SimdF4 s)
    {
        Quaternion r;
        std::memcpy(&r, &s, sizeof(r));
        return r;
    }
    Quaternion Quaternion::operator+(const Quaternion &o) const { return StoreQ(LoadQ(*this) + LoadQ(o)); }
    Quaternion Quaternion::operator-(const Quaternion &o) const { return StoreQ(LoadQ(*this) - LoadQ(o)); }
    Quaternion Quaternion::operator*(float s) const { return StoreQ(LoadQ(*this) * SimdF4{s, s, s, s}); }
#else
    Quaternion Quaternion::operator+(const Quaternion &o) const { return Quaternion(x + o.x, y + o.y, z + o.z, w + o.w); }
    Quaternion Quaternion::operator-(const Quaternion &o) const { return Quaternion(x - o.x, y - o.y, z - o.z, w - o.w); }
    Quaternion Quaternion::operator*(float s) const { return Quaternion(x * s, y * s, z * s, w * s); }
#endif

    Quaternion Quaternion::operator*(const Quaternion &o) const
    {
        return Quaternion(
            w * o.x + x * o.w + y * o.z - z * o.y,
            w * o.y - x * o.z + y * o.w + z * o.x,
            w * o.z + x * o.y - y * o.x + z * o.w,
            w * o.w - x * o.x - y * o.y - z * o.z);
    }

    Vec3 Quaternion::operator*(const Vec3 &v) const
    {
        Vec3 qv(x, y, z);
        Vec3 t = qv.Cross(v) * 2.0f;
        return v + t * w + qv.Cross(t);
    }

    Quaternion Quaternion::operator-() const { return Quaternion(-x, -y, -z, -w); }

    bool Quaternion::operator==(const Quaternion &o) const
    {
        return std::fabs(x - o.x) < EPSILON && std::fabs(y - o.y) < EPSILON &&
               std::fabs(z - o.z) < EPSILON && std::fabs(w - o.w) < EPSILON;
    }
    bool Quaternion::operator!=(const Quaternion &o) const { return !(*this == o); }

    float Quaternion::LengthSquared() const { return x * x + y * y + z * z + w * w; }
    float Quaternion::Length() const { return std::sqrt(LengthSquared()); }

    Quaternion Quaternion::Normalized() const
    {
        float inv = 1.0f / std::sqrt(LengthSquared());
        return *this * inv;
    }
    Quaternion Quaternion::NormalizedSafe() const
    {
        float lenSq = std::max(LengthSquared(), EPSILON * EPSILON);
        float inv = 1.0f / std::sqrt(lenSq);
        return *this * inv;
    }
    void Quaternion::Normalize() { *this = Normalized(); }
    void Quaternion::NormalizeSafe() { *this = NormalizedSafe(); }

    Quaternion Quaternion::Conjugate() const { return Quaternion(-x, -y, -z, w); }
    Quaternion Quaternion::Inverse() const
    {
        float invLenSq = 1.0f / LengthSquared();
        return Quaternion(-x * invLenSq, -y * invLenSq, -z * invLenSq, w * invLenSq);
    }
    float Quaternion::Dot(const Quaternion &o) const { return x * o.x + y * o.y + z * o.z + w * o.w; }

    Mat3 Quaternion::ToMat3() const
    {
        float xx = x * x, yy = y * y, zz = z * z;
        float xy = x * y, xz = x * z, yz = y * z;
        float wx = w * x, wy = w * y, wz = w * z;
        return Mat3(
            1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz), 2.0f * (xz - wy),
            2.0f * (xy - wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx),
            2.0f * (xz + wy), 2.0f * (yz - wx), 1.0f - 2.0f * (xx + yy));
    }
    Mat4 Quaternion::ToMat4() const { return Mat4(ToMat3()); }

    Vec3 Quaternion::ToEulerAngles() const
    {
        const float sinYaw = 2.0f * (w * y - z * x);
        if (sinYaw >= 0.999999f)
            return Vec3(0.0f, PI * 0.5f, -2.0f * std::atan2(x, w));
        if (sinYaw <= -0.999999f)
            return Vec3(0.0f, -PI * 0.5f, 2.0f * std::atan2(x, w));

        return Vec3(std::atan2(2.0f * (w * x + y * z), 1.0f - 2.0f * (x * x + y * y)),
                    std::asin(sinYaw),
                    std::atan2(2.0f * (w * z + x * y), 1.0f - 2.0f * (y * y + z * z)));
    }

    Quaternion Quaternion::Identity() { return Quaternion(); }

    Quaternion Quaternion::FromAxisAngle(const Vec3 &axis, float angleRad)
    {
        float half = angleRad * 0.5f;
        float s = std::sin(half);
        return Quaternion(axis.x * s, axis.y * s, axis.z * s, std::cos(half));
    }

    Quaternion Quaternion::FromEulerAngles(float pitchX, float yawY, float rollZ)
    {
        float cx = std::cos(pitchX * 0.5f), sx = std::sin(pitchX * 0.5f);
        float cy = std::cos(yawY * 0.5f), sy = std::sin(yawY * 0.5f);
        float cz = std::cos(rollZ * 0.5f), sz = std::sin(rollZ * 0.5f);

        return Quaternion(
            sx * cy * cz - cx * sy * sz,
            cx * sy * cz + sx * cy * sz,
            cx * cy * sz - sx * sy * cz,
            cx * cy * cz + sx * sy * sz);
    }

    Quaternion Quaternion::FromMat3(const Mat3 &m)
    {
        float m00 = m.col0.x, m01 = m.col0.y, m02 = m.col0.z;
        float m10 = m.col1.x, m11 = m.col1.y, m12 = m.col1.z;
        float m20 = m.col2.x, m21 = m.col2.y, m22 = m.col2.z;

        float fourXSquaredMinus1 = m00 - m11 - m22;
        float fourYSquaredMinus1 = m11 - m00 - m22;
        float fourZSquaredMinus1 = m22 - m00 - m11;
        float fourWSquaredMinus1 = m00 + m11 + m22;

        int biggestIndex = 0;
        float fourBiggestSquaredMinus1 = fourWSquaredMinus1;
        if (fourXSquaredMinus1 > fourBiggestSquaredMinus1)
        {
            fourBiggestSquaredMinus1 = fourXSquaredMinus1;
            biggestIndex = 1;
        }
        if (fourYSquaredMinus1 > fourBiggestSquaredMinus1)
        {
            fourBiggestSquaredMinus1 = fourYSquaredMinus1;
            biggestIndex = 2;
        }
        if (fourZSquaredMinus1 > fourBiggestSquaredMinus1)
        {
            fourBiggestSquaredMinus1 = fourZSquaredMinus1;
            biggestIndex = 3;
        }

        float biggestVal = std::sqrt(fourBiggestSquaredMinus1 + 1.0f) * 0.5f;
        float mult = 0.25f / biggestVal;

        switch (biggestIndex)
        {
        case 1:
            return Quaternion(biggestVal, (m01 + m10) * mult, (m20 + m02) * mult, (m12 - m21) * mult);
        case 2:
            return Quaternion((m01 + m10) * mult, biggestVal, (m12 + m21) * mult, (m20 - m02) * mult);
        case 3:
            return Quaternion((m20 + m02) * mult, (m12 + m21) * mult, biggestVal, (m01 - m10) * mult);
        default:
            return Quaternion((m12 - m21) * mult, (m20 - m02) * mult, (m01 - m10) * mult, biggestVal);
        }
    }

    Quaternion Quaternion::FromTo(const Vec3 &from, const Vec3 &to)
    {
        float d = from.Dot(to);
        if (d > 1.0f - EPSILON)
            return Quaternion::Identity();
        if (d < -1.0f + EPSILON)
        {
            Vec3 axis = Vec3::UnitX.Cross(from);
            if (axis.LengthSquared() < EPSILON)
                axis = Vec3::UnitY.Cross(from);
            return Quaternion::FromAxisAngle(axis.Normalized(), PI);
        }
        Vec3 axis = from.Cross(to);
        float s = std::sqrt((1.0f + d) * 2.0f);
        float invS = 1.0f / s;
        return Quaternion(axis.x * invS, axis.y * invS, axis.z * invS, s * 0.5f);
    }

    Quaternion Quaternion::LookRotation(const Vec3 &forward, const Vec3 &up)
    {
        Vec3 f = forward.Normalized();
        Vec3 r = up.Cross(f).Normalized();
        Vec3 u = f.Cross(r);
        return FromMat3(Mat3(r, u, f));
    }

    float Quaternion::Dot(const Quaternion &a, const Quaternion &b) { return a.Dot(b); }

    Quaternion Quaternion::Lerp(const Quaternion &a, const Quaternion &b, float t)
    {
        Quaternion bb = a.Dot(b) < 0.0f ? -b : b;
        return (a * (1.0f - t) + bb * t).Normalized();
    }

    Quaternion Quaternion::Slerp(const Quaternion &a, const Quaternion &b, float t)
    {
        float d = a.Dot(b);
        Quaternion bb = b;
        if (d < 0.0f)
        {
            bb = -b;
            d = -d;
        }
        if (d > 1.0f - EPSILON)
            return (a * (1.0f - t) + bb * t).Normalized();

        float theta0 = std::acos(d);
        float theta = theta0 * t;
        float sinTheta0 = std::sin(theta0);
        float sinTheta = std::sin(theta);
        float ratio = sinTheta / sinTheta0; 
        float s0 = std::cos(theta) - d * ratio;
        float s1 = ratio;
        return a * s0 + bb * s1;
    }

    Quaternion operator*(float scalar, const Quaternion &q) { return q * scalar; }
    std::ostream &operator<<(std::ostream &os, const Quaternion &q)
    {
        return os << "(" << q.x << ", " << q.y << ", " << q.z << ", " << q.w << ")";
    }

#ifndef MATHC_NO_EXTRA
    Ray::Ray() : origin(0.0f, 0.0f, 0.0f), direction(0.0f, 0.0f, 1.0f) {}
    Ray::Ray(const Vec3 &o, const Vec3 &d) : origin(o), direction(d) {}
    Vec3 Ray::PointAt(float t) const { return origin + direction * t; }
    Ray Ray::FromPoints(const Vec3 &a, const Vec3 &b) { return Ray(a, (b - a).Normalized()); }

    std::ostream &operator<<(std::ostream &os, const Ray &r)
    {
        return os << "{origin: " << r.origin << ", direction: " << r.direction << "}";
    }

    Plane::Plane() : normal(0.0f, 1.0f, 0.0f), d(0.0f) {}
    Plane::Plane(const Vec3 &n, float dist) : normal(n), d(dist) {}
    Plane::Plane(const Vec3 &n, const Vec3 &pointOnPlane) : normal(n), d(-n.Dot(pointOnPlane)) {}
    Plane::Plane(const Vec3 &a, const Vec3 &b, const Vec3 &c)
        : normal((b - a).Cross(c - a).Normalized()), d(-normal.Dot(a))
    {
    }

    float Plane::DistanceToPoint(const Vec3 &p) const { return normal.Dot(p) + d; }
    Vec3 Plane::ClosestPoint(const Vec3 &p) const
    {
        // The plane normal is not required to be unit length. Divide by its
        // squared length so this is the orthogonal projection for a scaled
        // plane equation too.
        return p - normal * (DistanceToPoint(p) / normal.LengthSquared());
    }

    Plane Plane::Normalized() const
    {
        float invLen = 1.0f / normal.Length();
        return Plane(normal * invLen, d * invLen);
    }

    bool Plane::IntersectRay(const Ray &ray, float &outT) const
    {
        float denom = normal.Dot(ray.direction);
        if (std::fabs(denom) < EPSILON)
            return false;
        outT = -(normal.Dot(ray.origin) + d) / denom;
        return true;
    }

    Plane Plane::FromPoints(const Vec3 &a, const Vec3 &b, const Vec3 &c) { return Plane(a, b, c); }

    std::ostream &operator<<(std::ostream &os, const Plane &p)
    {
        return os << "{normal: " << p.normal << ", d: " << p.d << "}";
    }

    const Box Box::Empty = Box(
        Vec3(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()),
        Vec3(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()));

    Box::Box() : min(Box::Empty.min), max(Box::Empty.max) {}
    Box::Box(const Vec3 &mn, const Vec3 &mx) : min(mn), max(mx) {}

    Vec3 Box::Center() const { return (min + max) * 0.5f; }
    Vec3 Box::Extents() const { return (max - min) * 0.5f; }
    Vec3 Box::Size() const { return max - min; }

    bool Box::Contains(const Vec3 &p) const
    {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
    }

    bool Box::Intersects(const Box &o) const
    {
        return min.x <= o.max.x && max.x >= o.min.x && min.y <= o.max.y && max.y >= o.min.y &&
               min.z <= o.max.z && max.z >= o.min.z;
    }

    Box Box::Union(const Box &o) const { return Box(Vec3::Min(min, o.min), Vec3::Max(max, o.max)); }
    Box Box::Expanded(const Vec3 &p) const { return Box(Vec3::Min(min, p), Vec3::Max(max, p)); }
    Box Box::Expanded(float amount) const { return Box(min - Vec3(amount), max + Vec3(amount)); }

    bool Box::IntersectRay(const Ray &ray, float &outTMin, float &outTMax) const
    {
        float tmin = std::numeric_limits<float>::lowest();
        float tmax = std::numeric_limits<float>::max();
        for (int i = 0; i < 3; i++)
        {
            float invD = 1.0f / ray.direction[i];
            float t0 = (min[i] - ray.origin[i]) * invD;
            float t1 = (max[i] - ray.origin[i]) * invD;
            if (invD < 0.0f)
                std::swap(t0, t1);
            tmin = t0 > tmin ? t0 : tmin;
            tmax = t1 < tmax ? t1 : tmax;
            if (tmax < tmin)
                return false;
        }
        outTMin = tmin;
        outTMax = tmax;
        return true;
    }

    Box Box::Transformed(const Mat4 &m) const
    {
        Vec3 corners[8] = {
            Vec3(min.x, min.y, min.z), Vec3(max.x, min.y, min.z),
            Vec3(min.x, max.y, min.z), Vec3(max.x, max.y, min.z),
            Vec3(min.x, min.y, max.z), Vec3(max.x, min.y, max.z),
            Vec3(min.x, max.y, max.z), Vec3(max.x, max.y, max.z)};
        Box result = Box::Empty;
        for (int i = 0; i < 8; i++)
            result = result.Expanded(m.TransformPoint(corners[i]));
        return result;
    }

    void Box::GetCorners(Vec3 outCorners[8]) const
    {
        outCorners[0] = Vec3(min.x, min.y, min.z);
        outCorners[1] = Vec3(min.x, max.y, min.z);
        outCorners[2] = Vec3(min.x, min.y, max.z);
        outCorners[3] = Vec3(min.x, max.y, max.z);
        outCorners[4] = Vec3(max.x, min.y, min.z);
        outCorners[5] = Vec3(max.x, max.y, min.z);
        outCorners[6] = Vec3(max.x, min.y, max.z);
        outCorners[7] = Vec3(max.x, max.y, max.z);
    }

    Box Box::FromCenterExtents(const Vec3 &center, const Vec3 &extents) { return Box(center - extents, center + extents); }

    Box Box::FromPoints(const Vec3 *points, int count)
    {
        Box result = Box::Empty;
        for (int i = 0; i < count; i++)
            result = result.Expanded(points[i]);
        return result;
    }

    std::ostream &operator<<(std::ostream &os, const Box &b)
    {
        return os << "{min: " << b.min << ", max: " << b.max << "}";
    }

    Frustum::Frustum() {}

    Frustum Frustum::FromViewProjection(const Mat4 &m)
    {
        Vec4 row0(m.col0.x, m.col1.x, m.col2.x, m.col3.x);
        Vec4 row1(m.col0.y, m.col1.y, m.col2.y, m.col3.y);
        Vec4 row2(m.col0.z, m.col1.z, m.col2.z, m.col3.z);
        Vec4 row3(m.col0.w, m.col1.w, m.col2.w, m.col3.w);

        Vec4 raw[6] = {
            row3 + row0, row3 - row0, 
            row3 + row1, row3 - row1, 
            row3 + row2, row3 - row2  
        };

        Frustum f;
        for (int i = 0; i < 6; i++)
        {
            Vec3 n(raw[i].x, raw[i].y, raw[i].z);
            f.planes[i] = Plane(n, raw[i].w).Normalized();
        }
        return f;
    }

    bool Frustum::ContainsPoint(const Vec3 &point) const
    {
        for (int i = 0; i < 6; i++)
            if (planes[i].DistanceToPoint(point) < 0.0f)
                return false;
        return true;
    }

    bool Frustum::IntersectsSphere(const Vec3 &center, float radius) const
    {
        for (int i = 0; i < 6; i++)
            if (planes[i].DistanceToPoint(center) < -radius)
                return false;
        return true;
    }

    bool Frustum::IntersectsBox(const Box &box) const
    {
        for (int i = 0; i < 6; i++)
        {
            const Plane &p = planes[i];
            Vec3 positive(
                p.normal.x >= 0.0f ? box.max.x : box.min.x,
                p.normal.y >= 0.0f ? box.max.y : box.min.y,
                p.normal.z >= 0.0f ? box.max.z : box.min.z);
            if (p.DistanceToPoint(positive) < 0.0f)
                return false;
        }
        return true;
    }
#endif 

    Vec3::Vec3(const Vec4 &v) : x(v.x), y(v.y), z(v.z) {}
    Mat3::Mat3(const Mat4 &m) : col0(m[0].xyz()), col1(m[1].xyz()), col2(m[2].xyz()) {}
    Quaternion::Quaternion(float w, const Vec3 &xyz) : x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}

    Mat2 &Mat2::operator+=(const Mat2 &o) { col0 += o.col0; col1 += o.col1; return *this; }
    Mat2 &Mat2::operator-=(const Mat2 &o) { col0 -= o.col0; col1 -= o.col1; return *this; }
    Mat2 &Mat2::operator*=(float s) { col0 *= s; col1 *= s; return *this; }
    Mat3 &Mat3::operator+=(const Mat3 &o) { col0 += o.col0; col1 += o.col1; col2 += o.col2; return *this; }
    Mat3 &Mat3::operator-=(const Mat3 &o) { col0 -= o.col0; col1 -= o.col1; col2 -= o.col2; return *this; }
    Mat3 &Mat3::operator*=(float s) { col0 *= s; col1 *= s; col2 *= s; return *this; }
    Mat4 &Mat4::operator+=(const Mat4 &o) { col0 += o.col0; col1 += o.col1; col2 += o.col2; col3 += o.col3; return *this; }
    Mat4 &Mat4::operator-=(const Mat4 &o) { col0 -= o.col0; col1 -= o.col1; col2 -= o.col2; col3 -= o.col3; return *this; }
    Mat4 &Mat4::operator*=(float s) { col0 *= s; col1 *= s; col2 *= s; col3 *= s; return *this; }
    Quaternion &Quaternion::operator+=(const Quaternion &o) { x += o.x; y += o.y; z += o.z; w += o.w; return *this; }
    Quaternion &Quaternion::operator-=(const Quaternion &o) { x -= o.x; y -= o.y; z -= o.z; w -= o.w; return *this; }

} 