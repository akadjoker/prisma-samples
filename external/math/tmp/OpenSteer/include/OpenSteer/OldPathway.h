
#ifndef OPENSTEER_OLD_PATHWAY_H
#define OPENSTEER_OLD_PATHWAY_H

#include "OpenSteer/Vec3.h"

namespace OpenSteer {

    namespace Old {

        class Pathway
        {
        public:
            Pathway() { }
            virtual ~Pathway() { }

            virtual Vec3 mapPointToPath (const Vec3& point,
                Vec3& tangent,
                float& outside) = 0;

            virtual Vec3 mapPathDistanceToPoint (float pathDistance) = 0;

            virtual float mapPointToPathDistance (const Vec3& point) = 0;

            bool isInsidePath (const Vec3& point)
            {
                float outside; Vec3 tangent;
                mapPointToPath (point, tangent, outside);
                return outside < 0;
            }

            float howFarOutsidePath (const Vec3& point)
            {
                float outside; Vec3 tangent;
                mapPointToPath (point, tangent, outside);
                return outside;
            }
        };

        class PolylinePathway: public virtual Pathway
        {
        public:

            int pointCount;
            Vec3* points;
            float radius;
            bool cyclic;

            PolylinePathway (void) {}
            virtual ~PolylinePathway() { }

            PolylinePathway (const int _pointCount,
                const Vec3 _points[],
                const float _radius,
                const bool _cyclic);

            void initialize (const int _pointCount,
                const Vec3 _points[],
                const float _radius,
                const bool _cyclic);

            void 
            setupLengths ();

            void movePoints (const int _firstPoint,
                            const int _numPoints,
                            const Vec3 _points[]);

            Vec3 mapPointToPath (const Vec3& point, Vec3& tangent, float& outside);

            float mapPointToPathDistance (const Vec3& point);

            Vec3 mapPathDistanceToPoint (float pathDistance);

            float pointToSegmentDistance (const Vec3& point,
                const Vec3& ep0,
                const Vec3& ep1);

            float getTotalPathLength (void) {return totalPathLength;};

            float segmentLength;
            float segmentProjection;
            Vec3 local;
            Vec3 chosen;
            Vec3 segmentNormal;

            float* lengths;
            Vec3* normals;
            float totalPathLength;
        };

    } 

} 

#endif 