
#ifndef OPENSTEER_QUERYPATHALIKE_H
#define OPENSTEER_QUERYPATHALIKE_H

#include <limits>

#include "OpenSteer/Vec3.h"

#include "OpenSteer/Vec3Utilities.h"

#include "OpenSteer/Utilities.h"

#include "OpenSteer/QueryPathAlikeBaseDataExtractionPolicies.h"

#ifdef _MSC_VER
#undef min
#undef max
#endif

namespace OpenSteer {

    template< class PathAlike, class Mapping, class BaseDataExtractionPolicy = PointToPathAlikeBaseDataExtractionPolicy< PathAlike > >
    class PointToPathAlikeMapping {
    public:

        static void map( PathAlike const& pathAlike, Vec3 const& queryPoint, Mapping& mapping ) {
            float minDistancePointToPath = std::numeric_limits< float >::max();
            mapping.setDistanceOnPathFlag( 0.0f );

            typedef typename PathAlike::size_type size_type;
            size_type const segmentCount = pathAlike.segmentCount();
            for ( size_type segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex ) {

                float segmentDistance = 0.0f;
                float radius = 0.0f;
                float distancePointToPath = 0.0f;
                Vec3 pointOnPathCenterLine( 0.0f, 0.0f, 0.0f );
                Vec3 tangent( 0.0f, 0.0f, 0.0f );

                BaseDataExtractionPolicy::extract( pathAlike, segmentIndex, queryPoint, segmentDistance, radius, distancePointToPath, pointOnPathCenterLine, tangent );

                if ( distancePointToPath < minDistancePointToPath ) {
                    minDistancePointToPath = distancePointToPath;
                    mapping.setPointOnPathCenterLine( pointOnPathCenterLine );
                    mapping.setPointOnPathBoundary( pointOnPathCenterLine + ( ( queryPoint - pointOnPathCenterLine ).normalize() * radius ) );
                    mapping.setRadius( radius );
                    mapping.setTangent( tangent );
                    mapping.setSegmentIndex( segmentIndex );
                    mapping.setDistancePointToPath( distancePointToPath );
                    mapping.setDistancePointToPathCenterLine( distancePointToPath + radius );
                    mapping.setDistanceOnPath( mapping.distanceOnPathFlag() + segmentDistance );
                    mapping.setDistanceOnSegment( segmentDistance );
                }

                mapping.setDistanceOnPathFlag( mapping.distanceOnPathFlag() + pathAlike.segmentLength( segmentIndex ) );
            }
        }

    }; 

    template< class PathAlike, class Mapping >
    void mapPointToPathAlike( PathAlike const& pathAlike, Vec3 const& point, Mapping& mapping ) {
        PointToPathAlikeMapping< PathAlike, Mapping >::map( pathAlike, point, mapping );
    }

    template< class PathAlike, class Mapping, class BaseDataExtractionPolicy = DistanceToPathAlikeBaseDataExtractionPolicy< PathAlike > > 
    class DistanceToPathAlikeMapping {
    public:

        static void map( PathAlike const& pathAlike, float distanceOnPath, Mapping& mapping ) {
            float const pathLength = pathAlike.length();

            if ( pathAlike.isCyclic() ) {
                distanceOnPath = modulo( distanceOnPath, pathLength );       
            }
            distanceOnPath = clamp( distanceOnPath, 0.0f, pathLength );

            float remainingDistance = distanceOnPath;
            typedef typename PathAlike::size_type size_type;
            size_type segmentIndex = 0;        
            size_type const maxSegmentIndex = pathAlike.segmentCount() - 1;
            while( ( segmentIndex < maxSegmentIndex ) && 
                   ( remainingDistance > pathAlike.segmentLength( segmentIndex ) ) ) {
                remainingDistance -= pathAlike.segmentLength( segmentIndex );
                ++segmentIndex;
            }

            Vec3 pointOnPathCenterLine( 0.0f, 0.0f, 0.0f );
            Vec3 tangent( 0.0f, 0.0f, 0.0f );
            float radius = 0.0f;
            BaseDataExtractionPolicy::extract( pathAlike, segmentIndex, remainingDistance, pointOnPathCenterLine, tangent, radius );

            mapping.setPointOnPathCenterLine( pointOnPathCenterLine );
            mapping.setRadius( radius );
            mapping.setTangent( tangent );
            mapping.setSegmentIndex( segmentIndex );
            mapping.setDistanceOnPath( distanceOnPath );
            mapping.setDistanceOnSegment( remainingDistance );            
        }

    }; 

    template< class PathAlike, class Mapping >
    void mapDistanceToPathAlike( PathAlike const& pathAlike, float distance, Mapping& mapping ) {
        DistanceToPathAlikeMapping< PathAlike, Mapping >::map( pathAlike, distance, mapping );
    }

} 

#endif 