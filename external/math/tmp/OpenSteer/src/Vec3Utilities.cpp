
#include "OpenSteer/Vec3Utilities.h"

#include <cassert>

#include "OpenSteer/Utilities.h"

OpenSteer::Vec3
OpenSteer::nearestPointOnSegment( const Vec3& point,
                                  const Vec3& segmentPoint0,
                                  const Vec3& segmentPoint1 )
{

    Vec3 const local( point - segmentPoint0 );

    Vec3 const segment( segmentPoint1 - segmentPoint0 );
    float const segmentLength( segment.length() );

    assert( 0 != segmentLength && "Segment mustn't be of length zero." );

    Vec3 const segmentNormalized( segment / segmentLength ); 
    float segmentProjection = segmentNormalized.dot (local);

    segmentProjection = clamp( segmentProjection, 0.0f, segmentLength );

    Vec3 result( segmentNormalized * segmentProjection );
    result +=  segmentPoint0;
    return result;    

}

float 
OpenSteer::pointToSegmentDistance ( const Vec3& point,
                                    const Vec3& segmentPoint0,
                                    const Vec3& segmentPoint1)
{
    return distance( point, nearestPointOnSegment( point, segmentPoint0, segmentPoint1 ) );
}