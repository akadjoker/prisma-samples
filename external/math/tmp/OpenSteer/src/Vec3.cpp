
#include "OpenSteer/Vec3.h"

const OpenSteer::Vec3 OpenSteer::Vec3::zero    (0, 0, 0);
const OpenSteer::Vec3 OpenSteer::Vec3::up      (0, 1, 0);
const OpenSteer::Vec3 OpenSteer::Vec3::forward (0, 0, 1);

const OpenSteer::Vec3 OpenSteer::Vec3::side    (-1, 0, 0);

OpenSteer::Vec3 
OpenSteer::RandomVectorInUnitRadiusSphere (void)
{
    Vec3 v;

    do
    {
        v.set ((frandom01()*2) - 1,
               (frandom01()*2) - 1,
               (frandom01()*2) - 1);
    }
    while (v.length() >= 1);

    return v;
}

OpenSteer::Vec3 
OpenSteer::randomVectorOnUnitRadiusXZDisk (void)
{
    Vec3 v;

    do
    {
        v.set ((frandom01()*2) - 1,
               0,
               (frandom01()*2) - 1);
    }
    while (v.length() >= 1);

    return v;
}

OpenSteer::Vec3 
OpenSteer::vecLimitDeviationAngleUtility (const bool insideOrOutside,
                                          const Vec3& source,
                                          const float cosineOfConeAngle,
                                          const Vec3& basis)
{

    float sourceLength = source.length();
    if (sourceLength == 0) return source;

    const Vec3 direction = source / sourceLength;
    float cosineOfSourceAngle = direction.dot (basis);

    if (insideOrOutside)
    {

	if (cosineOfSourceAngle >= cosineOfConeAngle) return source;
    }
    else
    {

	if (cosineOfSourceAngle <= cosineOfConeAngle) return source;
    }

    const Vec3 perp = source.perpendicularComponent (basis);

    const Vec3 unitPerp = perp.normalize ();

    float perpDist = sqrtXXX (1 - (cosineOfConeAngle * cosineOfConeAngle));
    const Vec3 c0 = basis * cosineOfConeAngle;
    const Vec3 c1 = unitPerp * perpDist;
    return (c0 + c1) * sourceLength;
}

OpenSteer::Vec3 
OpenSteer::findPerpendicularIn3d (const Vec3& direction)
{

    Vec3 quasiPerp;  
    Vec3 result;     

    const Vec3 i (1, 0, 0);
    const Vec3 j (0, 1, 0);
    const Vec3 k (0, 0, 1);

    const float id = i.dot (direction);
    const float jd = j.dot (direction);
    const float kd = k.dot (direction);

    if ((id <= jd) && (id <= kd))
    {
        quasiPerp = i;               
    }
    else
    {
        if ((jd <= id) && (jd <= kd))
            quasiPerp = j;           
        else
            quasiPerp = k;           
    }

    result.cross (direction, quasiPerp);
    return result;
}