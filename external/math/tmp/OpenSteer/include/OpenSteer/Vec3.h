
#ifndef OPENSTEER_VEC3_H
#define OPENSTEER_VEC3_H

#include "OpenSteer/Utilities.h"  

namespace OpenSteer {

    class Vec3
    {
    public:

        float x, y, z;

        Vec3 (void): x( 0.0f ), y( 0.0f ), z( 0.0f ) {}
        Vec3 (float X, float Y, float Z) : x( X ), y( Y ), z( Z ) {}

        Vec3 operator+ (const Vec3& v) const {return Vec3 (x+v.x, y+v.y, z+v.z);}

        Vec3 operator- (const Vec3& v) const {return Vec3 (x-v.x, y-v.y, z-v.z);}

        Vec3 operator- (void) const {return Vec3 (-x, -y, -z);}

        Vec3 operator* (const float s) const {return Vec3 (x * s, y * s, z * s);}

        Vec3 operator/ (const float s) const {return Vec3 (x / s, y / s, z / s);}

        float dot (const Vec3& v) const {return (x * v.x) + (y * v.y) + (z * v.z);}

        float length (void) const {return sqrtXXX (lengthSquared ());}

        float lengthSquared (void) const {return this->dot (*this);}

        Vec3 normalize (void) const
        {

            const float len = length ();
            return (len>0) ? (*this)/len : (*this);
        }

        void cross(const Vec3& a, const Vec3& b)
        {
            *this = Vec3 ((a.y * b.z) - (a.z * b.y),
                          (a.z * b.x) - (a.x * b.z),
                          (a.x * b.y) - (a.y * b.x));
        }

        Vec3 operator= (const Vec3& v) {x=v.x; y=v.y; z=v.z; return *this;}

        Vec3 set (const float _x, const float _y, const float _z)
        {x = _x; y = _y; z = _z; return *this;}

        Vec3 operator+= (const Vec3& v) {return *this = (*this + v);}

        Vec3 operator-= (const Vec3& v) {return *this = (*this - v);}

        Vec3 operator*= (const float& s) {return *this = (*this * s);}

        Vec3 operator/=( float d ) { return *this = (*this / d);  }

        bool operator== (const Vec3& v) const {return x==v.x && y==v.y && z==v.z;}
        bool operator!= (const Vec3& v) const {return !(*this == v);}

        static float distance (const Vec3& a, const Vec3& b){ return(a-b).length();}

        inline Vec3 parallelComponent (const Vec3& unitBasis) const
        {
            const float projection = this->dot (unitBasis);
            return unitBasis * projection;
        }

        inline Vec3 perpendicularComponent (const Vec3& unitBasis) const
        {
            return (*this) - parallelComponent (unitBasis);
        }

        Vec3 truncateLength (const float maxLength) const
        {
            const float maxLengthSquared = maxLength * maxLength;
            const float vecLengthSquared = this->lengthSquared ();
            if (vecLengthSquared <= maxLengthSquared)
                return *this;
            else
                return (*this) * (maxLength / sqrtXXX (vecLengthSquared));
        }

        Vec3 setYtoZero (void) const {return Vec3 (this->x, 0, this->z);}

        Vec3 rotateAboutGlobalY (float angle) const 
        {
            const float s = sinXXX (angle);
            const float c = cosXXX (angle);
            return Vec3 ((this->x * c) + (this->z * s),
                         (this->y),
                         (this->z * c) - (this->x * s));
        }

        Vec3 rotateAboutGlobalY (float angle, float& sin, float& cos) const 
        {

            if (sin==0 && cos==0)
            {
                sin = sinXXX (angle);
                cos = cosXXX (angle);
            }
            return Vec3 ((this->x * cos) + (this->z * sin),
                         (this->y),
                         (this->z * cos) - (this->x * sin));
        }

        Vec3 sphericalWrapAround (const Vec3& center, float radius)
        {
            const Vec3 offset = *this - center;
            const float r = offset.length();
            if (r > radius)
                return *this + ((offset/r) * radius * -2);
            else
                return *this;
        }

        static const Vec3 zero;
        static const Vec3 side;
        static const Vec3 up;
        static const Vec3 forward;
    };

    inline Vec3 operator* (float s, const Vec3& v) {return v*s;}

	inline Vec3 crossProduct(const Vec3& a, const Vec3& b)
	{
		Vec3 result((a.y * b.z) - (a.z * b.y),
					(a.z * b.x) - (a.x * b.z),
					(a.x * b.y) - (a.y * b.x));
		return result;
	}

#ifndef NOT_OPENSTEERDEMO  

    inline std::ostream& operator<< (std::ostream& o, const Vec3& v)
    {
        return o << "(" << v.x << "," << v.y << "," << v.z << ")";
    }

#endif 

    Vec3 RandomVectorInUnitRadiusSphere (void);

    Vec3 randomVectorOnUnitRadiusXZDisk (void);

    inline Vec3 RandomUnitVector (void)
    {
        return RandomVectorInUnitRadiusSphere().normalize();
    }

    inline Vec3 RandomUnitVectorOnXZPlane (void)
    {
        return RandomVectorInUnitRadiusSphere().setYtoZero().normalize();
    }

    Vec3 vecLimitDeviationAngleUtility (const bool insideOrOutside,
                                        const Vec3& source,
                                        const float cosineOfConeAngle,
                                        const Vec3& basis);

    inline Vec3 limitMaxDeviationAngle (const Vec3& source,
                                        const float cosineOfConeAngle,
                                        const Vec3& basis)
    {
        return vecLimitDeviationAngleUtility (true, 
                                              source,
                                              cosineOfConeAngle,
                                              basis);
    }

    inline Vec3 limitMinDeviationAngle (const Vec3& source,
                                        const float cosineOfConeAngle,
                                        const Vec3& basis)
    {    
        return vecLimitDeviationAngleUtility (false, 
                                              source,
                                              cosineOfConeAngle,
                                              basis);
    }

    inline float distanceFromLine (const Vec3& point,
                                   const Vec3& lineOrigin,
                                   const Vec3& lineUnitTangent)
    {
        const Vec3 offset = point - lineOrigin;
        const Vec3 perp = offset.perpendicularComponent (lineUnitTangent);
        return perp.length();
    }

    Vec3 findPerpendicularIn3d (const Vec3& direction);

} 

#endif 