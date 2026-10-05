
#ifndef OPENSTEER_SEGMENTEDPATH_H
#define OPENSTEER_SEGMENTEDPATH_H

#include "OpenSteer/Path.h"

#include "OpenSteer/StandardTypes.h"

namespace OpenSteer {

    class SegmentedPath : public Path {
    public:
        typedef size_t size_type;

        virtual ~SegmentedPath() = 0;

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

        virtual Vec3 mapSegmentDistanceToTangent( size_type segmentIndex, 
                                                  float segmentDistance ) const = 0;

        virtual void mapDistanceToSegmentPointAndTangent( size_type segmentIndex,
                                                          float distance,
                                                          Vec3& pointOnPath,
                                                          Vec3& tangent ) const = 0;

        virtual void mapPointToSegmentDistanceAndPointAndTangent( size_type segmentIndex,
                                                                  Vec3 const& point,
                                                                  float& distance,
                                                                  Vec3& pointOnPath,
                                                                  Vec3& tangent ) const = 0;

    protected:

    }; 

} 

#endif 