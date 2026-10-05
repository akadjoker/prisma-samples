
#include "OpenSteer/Obstacle.h"

OpenSteer::Vec3 
OpenSteer::Obstacle::steerToAvoid (const AbstractVehicle& vehicle,
                                   const float minTimeToCollision) const
{

    PathIntersection pi;
    findIntersectionWithVehiclePath (vehicle, pi);

    return pi.steerToAvoidIfNeeded (vehicle, minTimeToCollision);
}

OpenSteer::Vec3
OpenSteer::Obstacle::
steerToAvoidObstacles (const AbstractVehicle& vehicle,
                       const float minTimeToCollision,
                       const ObstacleGroup& obstacles)
{
    PathIntersection nearest, next;

    firstPathIntersectionWithObstacleGroup (vehicle, obstacles, nearest, next);

    return nearest.steerToAvoidIfNeeded (vehicle, minTimeToCollision);
}

void
OpenSteer::Obstacle::
firstPathIntersectionWithObstacleGroup (const AbstractVehicle& vehicle,
                                        const ObstacleGroup& obstacles,
                                        PathIntersection& nearest,
                                        PathIntersection& next)
{

    next.intersect = false;
    nearest.intersect = false;
    for (ObstacleIterator o = obstacles.begin(); o != obstacles.end(); o++)
    {

        (**o).findIntersectionWithVehiclePath (vehicle, next);

        const bool firstFound = !nearest.intersect;
        const bool nearestFound = (next.intersect &&
                                   (next.distance < nearest.distance));
        if (firstFound || nearestFound) nearest = next;
    }
}

OpenSteer::Vec3 
OpenSteer::Obstacle::PathIntersection::
steerToAvoidIfNeeded (const AbstractVehicle& vehicle,
                      const float minTimeToCollision) const
{

    const float minDistanceToCollision = minTimeToCollision * vehicle.speed();
    if (intersect && (distance < minDistanceToCollision))
    {

        Vec3 lateral = steerHint.perpendicularComponent (vehicle.forward ());
        return lateral.normalize () * vehicle.maxForce ();
    }
    else
    {
        return Vec3::zero;
    }
}

void 
OpenSteer::
SphereObstacle::
findIntersectionWithVehiclePath (const AbstractVehicle& vehicle,
                                 PathIntersection& pi) const
{

    float b, c, d, p, q, s;
    Vec3 lc;

    pi.intersect = false;

    lc = vehicle.localizePosition (center);
    pi.vehicleOutside = lc.length () > radius;

	if (pi.vehicleOutside && (seenFrom () == inside))
	{
		pi.intersect = true;
		pi.distance = 0.0f;
		pi.steerHint = (center - vehicle.position()).normalize();
		return;
	}

    const float r = radius + vehicle.radius();
    b = -2 * lc.z;
    c = square (lc.x) + square (lc.y) + square (lc.z) - square (r);
    d = (b * b) - (4 * c);

    if (d < 0) return;

    s = sqrtXXX (d);
    p = (-b + s) / 2;
    q = (-b - s) / 2;

    if ((p < 0) && (q < 0)) return; 

    pi.intersect = true;
    pi.obstacle = this;
    pi.distance =
        ((p > 0) && (q > 0)) ?

        ((p < q) ? p : q) :

        (seenFrom () == outside ?

         0.0f :

         ((p > 0) ? p : q));
    pi.surfacePoint =
        vehicle.position() + (vehicle.forward() * pi.distance);
    pi.surfaceNormal = (pi.surfacePoint-center).normalize();
    switch (seenFrom ())
    {
    case outside:
        pi.steerHint = pi.surfaceNormal;
        break;
    case inside:
        pi.steerHint = -pi.surfaceNormal;
        break;
    case both:
        pi.steerHint = pi.surfaceNormal * (pi.vehicleOutside ? 1.0f : -1.0f);
        break;
    }
}

void 
OpenSteer::
BoxObstacle::
findIntersectionWithVehiclePath (const AbstractVehicle& vehicle,
                                 PathIntersection& pi) const
{

    const float w = width; 
    const float h = height;
    const float d = depth;
    const Vec3 s = side (); 
    const Vec3 u = up ();
    const Vec3 f = forward ();
    const Vec3 p = position ();
    const Vec3 hw = s * (0.5f * width); 
    const Vec3 hh = u * (0.5f * height);
    const Vec3 hd = f * (0.5f * depth);
    const seenFromState sf = seenFrom ();

    RectangleObstacle r1 (w, h,  s,  u,  f, p + hd, sf); 
    RectangleObstacle r2 (w, h, -s,  u, -f, p - hd, sf); 
    RectangleObstacle r3 (d, h, -f,  u,  s, p + hw, sf); 
    RectangleObstacle r4 (d, h,  f,  u, -s, p - hw, sf); 
    RectangleObstacle r5 (w, d,  s, -f,  u, p + hh, sf); 
    RectangleObstacle r6 (w, d, -s, -f, -u, p - hh, sf); 

    ObstacleGroup faces;
    faces.push_back (&r1);
    faces.push_back (&r2);
    faces.push_back (&r3);
    faces.push_back (&r4);
    faces.push_back (&r5);
    faces.push_back (&r6);

    PathIntersection next;
    firstPathIntersectionWithObstacleGroup (vehicle, faces, pi, next);

    if (pi.intersect)
    {
        pi.obstacle = this;
        pi.steerHint = ((pi.surfacePoint - position ()).normalize () *
                        (pi.vehicleOutside ? 1.0f : -1.0f));
    }
}

void 
OpenSteer::
PlaneObstacle::
findIntersectionWithVehiclePath (const AbstractVehicle& vehicle,
                                 PathIntersection& pi) const
{

    pi.intersect = false;

    const Vec3 lp =  localizePosition (vehicle.position ());
    const Vec3 ld = localizeDirection (vehicle.forward ());

    if (ld.dot (Vec3::forward) == 0.0f) return;

    if ((lp.z > 0.0f) && (ld.z > 0.0f)) return;
    if ((lp.z < 0.0f) && (ld.z < 0.0f)) return;

    if ((seenFrom () == outside) && (lp.z < 0.0f)) return;
    if ((seenFrom () == inside)  && (lp.z > 0.0f)) return;

    const float ix = lp.x - (ld.x * lp.z / ld.z);
    const float iy = lp.y - (ld.y * lp.z / ld.z);
    const Vec3 planeIntersection (ix, iy, 0.0f);

    if (!xyPointInsideShape (planeIntersection, vehicle.radius ())) return;

    const Vec3 localXYradial = planeIntersection.normalize ();
    const Vec3 radial = globalizeDirection (localXYradial);
    const float sideSign = (lp.z > 0.0f) ? +1.0f : -1.0f;
    const Vec3 opposingNormal = forward () * sideSign;
    pi.intersect = true;
    pi.obstacle = this;
    pi.distance = (lp - planeIntersection).length ();
    pi.steerHint = opposingNormal + radial; 
    pi.surfacePoint = globalizePosition (planeIntersection);
    pi.surfaceNormal = opposingNormal;
    pi.vehicleOutside = lp.z > 0.0f;
}

bool 
OpenSteer::
RectangleObstacle::
xyPointInsideShape (const Vec3& point, float radius) const
{
    const float w = radius + (width * 0.5f);
    const float h = radius + (height * 0.5f);
    return !((point.x >  w) || (point.x < -w) || (point.y >  h) || (point.y < -h));
}