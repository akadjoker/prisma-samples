
#ifndef OPENSTEER_POLYLINESEGMENTEDPATH_H
#define OPENSTEER_POLYLINESEGMENTEDPATH_H

#include <vector>

#include "OpenSteer/SegmentedPath.h"

#include "OpenSteer/QueryPathAlikeBaseDataExtractionPolicies.h"

#include "OpenSteer/Vec3.h"

#include "OpenSteer/Vec3Utilities.h"

namespace OpenSteer {

    class PolylineSegmentedPath : public SegmentedPath {
    public:

        PolylineSegmentedPath();

        PolylineSegmentedPath( size_type numOfPoints,
                               Vec3 const newPoints[],
                               bool closedCycle );

        PolylineSegmentedPath( PolylineSegmentedPath const& other );

        virtual ~PolylineSegmentedPath();

        PolylineSegmentedPath& operator=( PolylineSegmentedPath other );

        void swap( PolylineSegmentedPath& other );

        void setPath( size_type numOfPoints,
                      Vec3 const newPoints[],
                      bool closedCycle );

        void movePoints( size_type startIndex,
                         size_type numOfPoints,
                         Vec3 const newPoints[]);

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
        virtual Vec3 mapSegmentDistanceToTangent( size_type segmentIndex, 
                                                  float segmentDistance ) const;

        virtual void mapDistanceToSegmentPointAndTangent( size_type segmentIndex,
                                                          float distance,
                                                          Vec3& pointOnPath,
                                                          Vec3& tangent ) const;

        virtual void mapPointToSegmentDistanceAndPointAndTangent( size_type segmentIndex,
                                                                  Vec3 const& point,
                                                                  float& distance,
                                                                  Vec3& pointOnPath,
                                                                  Vec3& tangent ) const;

    private:
        std::vector< Vec3 > points_;
        std::vector< Vec3 > segmentTangents_;
        std::vector< float > segmentLengths_;
        bool closedCycle_;
    }; 

    inline void swap( PolylineSegmentedPath& lhs, PolylineSegmentedPath& rhs ) {
        lhs.swap( rhs );
    }

    template<>
    class PointToPathAlikeBaseDataExtractionPolicy< PolylineSegmentedPath > {
    public:

        static void extract( PolylineSegmentedPath const& pathAlike,
                             PolylineSegmentedPath::size_type segmentIndex,
                             Vec3 const& point, 
                             float& segmentDistance, 
                             float&, 
                             float& distancePointToPath, 
                             Vec3& pointOnPathCenterLine, 
                             Vec3& tangent ) {
            pathAlike.mapPointToSegmentDistanceAndPointAndTangent( segmentIndex, point, segmentDistance, pointOnPathCenterLine, tangent );
            distancePointToPath = distance( point, pointOnPathCenterLine );
        }

    }; 

    template<>
    class DistanceToPathAlikeBaseDataExtractionPolicy< PolylineSegmentedPath > {
    public:
        static void extract( PolylineSegmentedPath const& pathAlike,
                             PolylineSegmentedPath::size_type segmentIndex,
                             float segmentDistance, 
                             Vec3& pointOnPathCenterLine, 
                             Vec3& tangent, 
                             float&  )  {
            pathAlike.mapDistanceToSegmentPointAndTangent( segmentIndex, segmentDistance, pointOnPathCenterLine, tangent );     
        }

    }; 

} 

#endif 