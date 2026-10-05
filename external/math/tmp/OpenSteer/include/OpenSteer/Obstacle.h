
#ifndef OPENSTEER_OBSTACLE_H
#define OPENSTEER_OBSTACLE_H

#include "OpenSteer/Vec3.h"
#include "OpenSteer/LocalSpace.h"
#include "OpenSteer/AbstractVehicle.h"

namespace OpenSteer {

    class Color;

    class AbstractObstacle
    {
    public:

        virtual ~AbstractObstacle() {  }

        virtual Vec3 steerToAvoid (const AbstractVehicle& v,
                                   const float minTimeToCollision) const = 0;

        class PathIntersection
        {
        public:
            bool intersect; 
            float distance; 
            Vec3 surfacePoint; 
            Vec3 surfaceNormal; 
            Vec3 steerHint; 
            bool vehicleOutside; 
            const AbstractObstacle* obstacle; 

            Vec3 steerToAvoidIfNeeded (const AbstractVehicle& vehicle,
                                       const float minTimeToCollision) const;

        };

        virtual void
        findIntersectionWithVehiclePath (const AbstractVehicle& vehicle,
                                         PathIntersection& pi)
            const
            = 0 ;

        virtual void draw (const bool filled,
                           const Color& color,
                           const Vec3& viewpoint)
            const
            = 0 ;

        enum seenFromState {outside, inside, both};
        virtual seenFromState seenFrom (void) const = 0;
        virtual void setSeenFrom (seenFromState s) = 0;
    };

    typedef std::vector<AbstractObstacle*> ObstacleGroup;
    typedef ObstacleGroup::const_iterator ObstacleIterator;

    class Obstacle : public AbstractObstacle
    {
    public:

        Obstacle (void) : _seenFrom (outside) {}

        virtual ~Obstacle() {  }

        Vec3 steerToAvoid (const AbstractVehicle& v,
                           const float minTimeToCollision)
            const;

        static Vec3 steerToAvoidObstacles (const AbstractVehicle& vehicle,
                                           const float minTimeToCollision,
                                           const ObstacleGroup& obstacles);

        static void
        firstPathIntersectionWithObstacleGroup (const AbstractVehicle& vehicle,
                                                const ObstacleGroup& obstacles,
                                                PathIntersection& nearest,
                                                PathIntersection& next);

        void draw (const bool, const Color&, const Vec3&) const {}

        seenFromState seenFrom (void) const {return _seenFrom;}
        void setSeenFrom (seenFromState s) {_seenFrom = s;}
    private:
        seenFromState _seenFrom;
    };

    class SphereObstacle : public Obstacle
    {
    public:
        float radius;
        Vec3 center;

        SphereObstacle (float r, Vec3 c) : radius(r), center (c) {}
        SphereObstacle (void) : radius(1), center (Vec3::zero) {}

        virtual ~SphereObstacle() {  }

        void findIntersectionWithVehiclePath (const AbstractVehicle& vehicle,
                                              PathIntersection& pi)
            const;
    };

     typedef LocalSpaceMixin<Obstacle> LocalSpaceObstacle;

    class BoxObstacle : public LocalSpaceObstacle
    {
    public:
        float width;  
        float height; 
        float depth;  

        BoxObstacle (float w, float h, float d) : width(w), height(h), depth(d) {}
        BoxObstacle (void) :  width(1.0f), height(1.0f), depth(1.0f) {}

        virtual ~BoxObstacle() {  }

        void findIntersectionWithVehiclePath (const AbstractVehicle& vehicle,
                                              PathIntersection& pi)
            const;
    };

    class PlaneObstacle : public LocalSpaceObstacle
    {
    public:

        PlaneObstacle (void) {}
        PlaneObstacle (const Vec3& s,
                       const Vec3& u,
                       const Vec3& f,
                       const Vec3& p)
        : LocalSpaceObstacle( s, u, f, p )
        {

        }

        void findIntersectionWithVehiclePath (const AbstractVehicle& vehicle,
                                              PathIntersection& pi)
            const;

        virtual bool xyPointInsideShape (const Vec3& ,
                                         float ) const
        {
            return true; 
        }
    };

    class RectangleObstacle : public PlaneObstacle
    {
    public:
        float width;  
        float height; 

        RectangleObstacle (float w, float h) : width(w), height(h) {}
        RectangleObstacle (void) :  width(1.0f), height(1.0f) {}
        RectangleObstacle (float w, float h, const Vec3& s,
                           const Vec3& u, const Vec3& f, const Vec3& p,
                           seenFromState sf) 
            : PlaneObstacle( s, u, f, p ), width(w), height(h)
        {

            setSeenFrom (sf);
        }

        virtual ~RectangleObstacle() {  }

        bool xyPointInsideShape (const Vec3& point, float radius) const;
    };

} 

#endif 