
#ifndef OPENSTEER_POLYLINESEGMENTEDPATHWAYSEGMENTRADII_H
#define OPENSTEER_POLYLINESEGMENTEDPATHWAYSEGMENTRADII_H

#include <vector>

#include "OpenSteer/SegmentedPathway.h"

#include "OpenSteer/PolylineSegmentedPath.h"

#include "OpenSteer/QueryPathAlikeBaseDataExtractionPolicies.h"

#include "OpenSteer/Vec3.h"

#include "OpenSteer/Vec3Utilities.h"

namespace OpenSteer {

    class PolylineSegmentedPathwaySegmentRadii : public SegmentedPathway {
    public:
        PolylineSegmentedPathwaySegmentRadii();

        PolylineSegmentedPathwaySegmentRadii( size_type numOfPoints,
                                              Vec3 const points[],
                                              float const radii[],
                                              bool closedCycle );
        PolylineSegmentedPathwaySegmentRadii( PolylineSegmentedPathwaySegmentRadii const& other );
        virtual ~PolylineSegmentedPathwaySegmentRadii();

        PolylineSegmentedPathwaySegmentRadii& operator=( PolylineSegmentedPathwaySegmentRadii other );

        void swap( PolylineSegmentedPathwaySegmentRadii& other );

        void movePoints( size_type startIndex,
                         size_type numOfPoints,
                         Vec3 const points[] );

        void setPathway( size_type numOfPoints,
                         Vec3 const points[],
                         float const radii[],
                         bool closedCycle );

        float segmentRadius( size_type segmentIndex ) const;

        void setSegmentRadius( size_type segmentIndex, float r );

        void setSegmentRadii( size_type startIndex,
                              size_type numOfRadii,
                              float const radii[] );

        virtual bool isValid() const;
        virtual Vec3 mapPointToPath (const Vec3& point,
                                     Vec3& tangent,
                                     float& outside) const;
		virtual Vec3 mapPathDistanceToPoint (float pathDistance) const;
		virtual float mapPointToPathDistance (const Vec3& point) const;
        virtual bool isCyclic() const;
        virtual float length() const;

        virtual size_type pointCount() const;
        virtual Vec3 point( size_type pointIndex ) const;  

        virtual size_type segmentCount() const;
        virtual float segmentLength( size_type segmentIndex ) const;
        virtual Vec3 segmentStart( size_type segmentIndex ) const;
        virtual Vec3 segmentEnd( size_type segmentIndex ) const;
        virtual float mapPointToSegmentDistance( size_type segmentIndex, 
                                                 Vec3 const& point ) const;
        virtual Vec3 mapSegmentDistanceToPoint( size_type segmentIndex, 
                                                float segmentDistance ) const;
        virtual float mapSegmentDistanceToRadius( size_type segmentIndex, 
                                                  float distanceOnSegment ) const;
        virtual Vec3 mapSegmentDistanceToTangent( size_type segmentIndex, 
                                                  float segmentDistance ) const;

        virtual void mapDistanceToSegmentPointAndTangentAndRadius( size_type segmentIndex,
                                                                   float segmentDistance,
                                                                   Vec3& pointOnPath,
                                                                   Vec3& tangent,
                                                                   float& radius ) const;

        virtual void mapPointToSegmentDistanceAndPointAndTangentAndRadius( size_type segmentIndex,
                                                                           Vec3 const& point,
                                                                           float& distance,
                                                                           Vec3& pointOnPath,
                                                                           Vec3& tangent,
                                                                           float& radius) const;

    private:
        PolylineSegmentedPath path_;
        std::vector< float > segmentRadii_; 
    }; 

    inline void swap( PolylineSegmentedPathwaySegmentRadii& lhs, 
               PolylineSegmentedPathwaySegmentRadii& rhs ) {
        lhs.swap( rhs );
    }

    template<>
    class PointToPathAlikeBaseDataExtractionPolicy< PolylineSegmentedPathwaySegmentRadii > {
    public:

        static void extract( PolylineSegmentedPathwaySegmentRadii const& pathAlike,
                             PolylineSegmentedPathwaySegmentRadii::size_type segmentIndex,
                             Vec3 const& point, 
                             float& segmentDistance, 
                             float& radius, 
                             float& distancePointToPath, 
                             Vec3& pointOnPathCenterLine, 
                             Vec3& tangent ) {
            pathAlike.mapPointToSegmentDistanceAndPointAndTangentAndRadius( segmentIndex, point, segmentDistance, pointOnPathCenterLine, tangent, radius );
            distancePointToPath = distance( point, pointOnPathCenterLine ) - radius;
        }

    }; 

    template<>
    class DistanceToPathAlikeBaseDataExtractionPolicy< PolylineSegmentedPathwaySegmentRadii > {
    public:
        static void extract( PolylineSegmentedPathwaySegmentRadii const& pathAlike,
                             PolylineSegmentedPathwaySegmentRadii::size_type segmentIndex,
                             float segmentDistance, 
                             Vec3& pointOnPathCenterLine, 
                             Vec3& tangent, 
                             float& radius )  {
            pathAlike.mapDistanceToSegmentPointAndTangentAndRadius( segmentIndex, segmentDistance, pointOnPathCenterLine, tangent, radius );     
        }

    }; 
} 

#endif 