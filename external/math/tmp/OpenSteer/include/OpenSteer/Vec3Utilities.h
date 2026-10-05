
#ifndef OPENSTEER_VEC3UTILITIES_H
#define OPENSTEER_VEC3UTILITIES_H

#include "OpenSteer/Vec3.h"

#include "OpenSteer/StandardTypes.h"

#include "OpenSteer/Utilities.h"

namespace OpenSteer {

    OpenSteer::Vec3  nearestPointOnSegment( const Vec3& point,
                                            const Vec3& segmentPoint0,
                                            const Vec3& segmentPoint1 );

    float pointToSegmentDistance( const Vec3& point,
                                  const Vec3& segmentPoint0,
                                  const Vec3& segmentPoint1);

    inline float distance (const Vec3& a, const Vec3& b) {
        return (a-b).length();
    } 

    inline
    bool
    equalsRelative( Vec3 const& lhs, 
                     Vec3 const& rhs, 
                     float const& tolerance = std::numeric_limits< float >::epsilon()  ) {
        return equalsRelative( lhs.x, rhs.x, tolerance ) && equalsRelative( lhs.y, rhs.y ) && equalsRelative( lhs.z, rhs.z );
    }

} 

#endif 