
#ifndef OPENSTEER_QUERYPATHALIKEMAPPINGS_H
#define OPENSTEER_QUERYPATHALIKEMAPPINGS_H

#include "OpenSteer/QueryPathAlikeUtilities.h"

#include "OpenSteer/Vec3.h"

#include "OpenSteer/StandardTypes.h"

namespace OpenSteer {

    class PointToPathMapping 
        : public DontExtractPathDistance {

    public:
        PointToPathMapping() : pointOnPathCenterLine( 0.0f, 0.0f, 0.0f ), tangent( 0.0f, 0.0f, 0.0f ), distancePointToPath( 0.0f ) {}

        void setPointOnPathCenterLine( Vec3 const& point ) {
            pointOnPathCenterLine = point;
        }
        void setPointOnPathBoundary( Vec3 const& ) {

        }
        void setRadius( float ) {}
        void setTangent( Vec3 const& t) {
            tangent = t;
        }
        void setSegmentIndex( size_t ) {}
        void setDistancePointToPath( float distance ) {
            distancePointToPath = distance;
        }
        void setDistancePointToPathCenterLine( float ) {}
        void setDistanceOnPath( float ) {}
        void setDistanceOnSegment( float ) {}

        Vec3 pointOnPathCenterLine;

        Vec3 tangent;
        float distancePointToPath;

    }; 

    class PathDistanceToPointMapping 
        :  public DontExtractPathDistance {

    public:

        void setPointOnPathCenterLine( Vec3 const& vec ){
            pointOnPathCenterLine = vec;
        }
        void setRadius( float ) {}
        void setTangent( Vec3 const& ){}
        void setSegmentIndex( size_t ){}
        void setDistanceOnPath( float ){}
        void setDistanceOnSegment( float ){}

        Vec3 pointOnPathCenterLine; 

    }; 

    class PointToPathDistanceMapping
        : public ExtractPathDistance {
    public:
        PointToPathDistanceMapping() : distanceOnPath( 0.0f ) {}

        void setPointOnPathCenterLine( Vec3 const& ) {}
        void setPointOnPathBoundary( Vec3 const&  ) {}
        void setRadius( float ) {}
        void setTangent( Vec3 const& ) {}
        void setSegmentIndex( size_t ) {}
        void setDistancePointToPath( float  ) {}
        void setDistancePointToPathCenterLine( float ) {}
        void setDistanceOnPath( float distance ) {
            distanceOnPath = distance;
        }
        void setDistanceOnSegment( float ) {}

        float distanceOnPath;
    }; 

} 

#endif 