
#ifndef OPENSTEER_SEGMENTEDPATHWAY_H
#define OPENSTEER_SEGMENTEDPATHWAY_H

#include "OpenSteer/Pathway.h"

#include "OpenSteer/StandardTypes.h"

namespace OpenSteer {

    class Vec3;

    class SegmentedPathway: public Pathway {
    public:
        typedef size_t size_type;

        virtual ~SegmentedPathway() = 0;

        virtual size_type pointCount() const = 0;

        virtual Vec3 point( size_type pointIndex ) const = 0;

        virtual size_type segmentCount() const = 0;

        virtual float segmentLength( size_type segmentIndex ) const = 0;

        virtual Vec3 segmentStart( size_type segmentIndex ) const = 0;

        virtual Vec3 segmentEnd( size_type segmentIndex ) const = 0;

        virtual float mapPointToSegmentDistance( size_type segmentIndex, 
                                                 Vec3 const& point ) const = 0;

        virtual Vec3 mapSegmentDistanceToPoint( size_type segmentIndex, 
                                                float segmentDistance ) const = 0;

        virtual float mapSegmentDistanceToRadius( size_type segmentIndex, 
                                                 float distanceOnSegment ) const = 0;

        virtual Vec3 mapSegmentDistanceToTangent( size_type segmentIndex, 
                                                  float segmentDistance ) const = 0;

        virtual void mapDistanceToSegmentPointAndTangentAndRadius( size_type segmentIndex,
                                                                   float segmentDistance,
                                                                   Vec3& pointOnPath,
                                                                   Vec3& tangent,
                                                                   float& radius ) const = 0;        

        virtual void mapPointToSegmentDistanceAndPointAndTangentAndRadius( size_type segmentIndex,
                                                                           Vec3 const& point,
                                                                           float& distance,
                                                                           Vec3& pointOnPath,
                                                                           Vec3& tangent,
                                                                           float& radius) const = 0;

    protected:

    }; 

} 

#endif 