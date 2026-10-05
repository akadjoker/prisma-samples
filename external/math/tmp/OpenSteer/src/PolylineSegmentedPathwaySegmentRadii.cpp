
#include "OpenSteer/PolylineSegmentedPathwaySegmentRadii.h"

#include <algorithm>

#include <functional>

#include "OpenSteer/QueryPathAlike.h"

#include "OpenSteer/QueryPathAlikeMappings.h"

#include "OpenSteer/QueryPathAlikeUtilities.h"

#include "OpenSteer/Utilities.h"

#include "OpenSteer/StandardTypes.h"

#include "OpenSteer/UnusedParameter.h"

namespace {

    typedef OpenSteer::size_t size_type;

    size_type radiiCount( size_type numOfPoints, bool closedCycle ) {
        return numOfPoints - ( closedCycle ? 0 : 1 );
    }

    template< typename Iterator >
        bool allRadiiNonNegative( Iterator first, Iterator last ) {
            for (Iterator i = first; i != last; ++i) if (*i < 0.0f) return false;
            return true;

        }

    bool allRadiiNonNegative( std::vector< float > const& radii ) {
        return allRadiiNonNegative( radii.begin(), radii.end() );
    }

} 

OpenSteer::PolylineSegmentedPathwaySegmentRadii::PolylineSegmentedPathwaySegmentRadii()
    : path_(), segmentRadii_( 0 )
{

}

OpenSteer::PolylineSegmentedPathwaySegmentRadii::PolylineSegmentedPathwaySegmentRadii( size_type numOfPoints,
                                                                                       Vec3 const points[],
                                                                                       float const radii[],
                                                                                       bool closedCycle )
    : path_( numOfPoints, points, closedCycle ), segmentRadii_( radii, radii + radiiCount( numOfPoints, closedCycle ) )
{
    assert( allRadiiNonNegative( segmentRadii_ ) && "All radii must be positive or zero." );
}

OpenSteer::PolylineSegmentedPathwaySegmentRadii::PolylineSegmentedPathwaySegmentRadii( PolylineSegmentedPathwaySegmentRadii const& other )
    : SegmentedPathway( other ), path_( other.path_ ), segmentRadii_( other.segmentRadii_ )
{
    assert( allRadiiNonNegative( segmentRadii_ ) && "All radii must be positive or zero." );    
}

OpenSteer::PolylineSegmentedPathwaySegmentRadii::~PolylineSegmentedPathwaySegmentRadii()
{

}

OpenSteer::PolylineSegmentedPathwaySegmentRadii& 
OpenSteer::PolylineSegmentedPathwaySegmentRadii::operator=( PolylineSegmentedPathwaySegmentRadii other )
{
    swap( other );
    return *this;
}

void 
OpenSteer::PolylineSegmentedPathwaySegmentRadii::swap( PolylineSegmentedPathwaySegmentRadii& other )
{
    path_.swap( other.path_ );
    segmentRadii_.swap( other.segmentRadii_ );
}

void 
OpenSteer::PolylineSegmentedPathwaySegmentRadii::movePoints( size_type startIndex,
                                                             size_type numOfPoints,
                                                             Vec3 const points[] )
{
    path_.movePoints( startIndex, numOfPoints, points );
}

void 
OpenSteer::PolylineSegmentedPathwaySegmentRadii::setPathway( size_type numOfPoints,
                                                             Vec3 const points[],
                                                             float const radii[],
                                                             bool closedCycle )
{
    assert( allRadiiNonNegative( radii, radii + radiiCount( numOfPoints, closedCycle ) ) && "All radii must be positive or zero." );
    path_.setPath( numOfPoints, points, closedCycle );
    segmentRadii_.assign( radii, radii + radiiCount( numOfPoints, closedCycle ) );
    shrinkToFit( segmentRadii_ );

}

float 
OpenSteer::PolylineSegmentedPathwaySegmentRadii::segmentRadius( size_type segmentIndex ) const
{
    assert( segmentIndex < segmentCount() && "segmentIndex out of range." );
    return segmentRadii_[ segmentIndex ];
}

void 
OpenSteer::PolylineSegmentedPathwaySegmentRadii::setSegmentRadius( size_type segmentIndex, float r )
{
    assert( segmentIndex < segmentCount() && "segmentIndex out of range." );
    assert( 0.0f <= r && "No negative radii allowed." );

    segmentRadii_[ segmentIndex ] = r;
}

void 
OpenSteer::PolylineSegmentedPathwaySegmentRadii::setSegmentRadii( size_type startIndex,
                                                                  size_type numOfRadii,
                                                                  float const radii[] )
{
    assert( startIndex < segmentCount() && "startIndex out of range." );
    assert( startIndex + numOfRadii <= segmentCount() && "Too many radii to set." );
    assert( allRadiiNonNegative( radii, radii + numOfRadii ) && "All radii must be positive or zero." );

    std::copy( radii, radii + numOfRadii, segmentRadii_.begin() + startIndex );
}

bool
OpenSteer::PolylineSegmentedPathwaySegmentRadii::isValid() const 
{
    return pointCount() > 1;
}

OpenSteer::Vec3 
OpenSteer::PolylineSegmentedPathwaySegmentRadii::mapPointToPath (const Vec3& point,
                                                                 Vec3& tangent,
                                                                 float& outside) const
{
    PointToPathMapping mapping;
    mapPointToPathAlike( *this, point, mapping );
    tangent = mapping.tangent;
    outside = mapping.distancePointToPath;
    return mapping.pointOnPathCenterLine;    
}

OpenSteer::Vec3 
OpenSteer::PolylineSegmentedPathwaySegmentRadii::mapPathDistanceToPoint (float pathDistance) const
{
    PathDistanceToPointMapping mapping;
    mapDistanceToPathAlike( *this, pathDistance, mapping );
    return mapping.pointOnPathCenterLine;    
}

float 
OpenSteer::PolylineSegmentedPathwaySegmentRadii::mapPointToPathDistance (const Vec3& point) const
{
    PointToPathDistanceMapping mapping;
    mapPointToPathAlike( *this, point, mapping );
    return mapping.distanceOnPath;    
}

bool 
OpenSteer::PolylineSegmentedPathwaySegmentRadii::isCyclic() const
{
    return path_.isCyclic();
}

float 
OpenSteer::PolylineSegmentedPathwaySegmentRadii::length() const
{
    return path_.length();
}

OpenSteer::SegmentedPathway::size_type 
OpenSteer::PolylineSegmentedPathwaySegmentRadii::pointCount() const 
{
    return path_.pointCount();
}

OpenSteer::Vec3 
OpenSteer::PolylineSegmentedPathwaySegmentRadii::point( size_type pointIndex ) const
{
    return path_.point( pointIndex );
}

OpenSteer::PolylineSegmentedPathwaySegmentRadii::size_type 
OpenSteer::PolylineSegmentedPathwaySegmentRadii::segmentCount() const
{
    return path_.segmentCount();
}

float 
OpenSteer::PolylineSegmentedPathwaySegmentRadii::segmentLength( size_type segmentIndex ) const
{
    return path_.segmentLength( segmentIndex );
}

OpenSteer::Vec3 
OpenSteer::PolylineSegmentedPathwaySegmentRadii::segmentStart( size_type segmentIndex ) const
{
    return path_.segmentStart( segmentIndex );
}

OpenSteer::Vec3 
OpenSteer::PolylineSegmentedPathwaySegmentRadii::segmentEnd( size_type segmentIndex ) const
{
    return path_.segmentEnd( segmentIndex );
}

float 
OpenSteer::PolylineSegmentedPathwaySegmentRadii::mapPointToSegmentDistance( size_type segmentIndex, 
                                                                            Vec3 const& point ) const
{
    return path_.mapPointToSegmentDistance( segmentIndex, point );
}

OpenSteer::Vec3 
OpenSteer::PolylineSegmentedPathwaySegmentRadii::mapSegmentDistanceToPoint( size_type segmentIndex, 
                                                                            float segmentDistance ) const
{
    return path_.mapSegmentDistanceToPoint( segmentIndex, segmentDistance );
}

float 
OpenSteer::PolylineSegmentedPathwaySegmentRadii::mapSegmentDistanceToRadius( size_type segmentIndex, 
                                                                             float distanceOnSegment ) const
{
    OPENSTEER_UNUSED_PARAMETER(distanceOnSegment);
    assert( segmentIndex < segmentCount() && "segmentIndex out of range." );
    return segmentRadii_[ segmentIndex ];
}

OpenSteer::Vec3 
OpenSteer::PolylineSegmentedPathwaySegmentRadii::mapSegmentDistanceToTangent( size_type segmentIndex, 
                                                                              float segmentDistance ) const
{
    return path_.mapSegmentDistanceToTangent( segmentIndex, segmentDistance );
}

void 
OpenSteer::PolylineSegmentedPathwaySegmentRadii::mapDistanceToSegmentPointAndTangentAndRadius( size_type segmentIndex,
                                                                                               float distance,
                                                                                               Vec3& pointOnPath,
                                                                                               Vec3& tangent,
                                                                                               float& radius ) const
{
    assert( segmentIndex < segmentCount() && "segmentIndex out of range." );
    path_.mapDistanceToSegmentPointAndTangent( segmentIndex, distance, pointOnPath, tangent );
    radius = segmentRadii_[ segmentIndex ];
}

void 
OpenSteer::PolylineSegmentedPathwaySegmentRadii::mapPointToSegmentDistanceAndPointAndTangentAndRadius( size_type segmentIndex,
                                                                                                       Vec3 const& point,
                                                                                                       float& distance,
                                                                                                       Vec3& pointOnPath,
                                                                                                       Vec3& tangent,
                                                                                                       float& radius) const
{
    path_.mapPointToSegmentDistanceAndPointAndTangent( segmentIndex, point, distance, pointOnPath, tangent );
    radius = segmentRadii_[ segmentIndex ];
}