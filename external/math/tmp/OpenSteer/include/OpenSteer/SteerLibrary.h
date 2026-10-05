
#ifndef OPENSTEER_STEERLIBRARY_H
#define OPENSTEER_STEERLIBRARY_H

#include "OpenSteer/AbstractVehicle.h"
#include "OpenSteer/Pathway.h"
#include "OpenSteer/Obstacle.h"
#include "OpenSteer/Utilities.h"

#include "Color.h"

namespace OpenSteer {

    template <class Super>
    class SteerLibraryMixin : public Super
    {
    public:
        using Super::velocity;
        using Super::maxSpeed;
        using Super::speed;
        using Super::radius;
        using Super::maxForce;
        using Super::forward;
        using Super::position;
        using Super::side;
        using Super::up;
        using Super::predictFuturePosition;

    public:

        SteerLibraryMixin ()
        {

            reset ();
        }

        void reset (void)
        {

            WanderSide = 0;
            WanderUp = 0;

            gaudyPursuitAnnotation = false;
        }

        float WanderSide;
        float WanderUp;
        Vec3 steerForWander (float dt);

        Vec3 steerForSeek (const Vec3& target);

        Vec3 steerForFlee (const Vec3& target);

        Vec3 xxxsteerForFlee (const Vec3& target);
        Vec3 xxxsteerForSeek (const Vec3& target);

        Vec3 steerToFollowPath (const int direction,
                                const float predictionTime,
                                Pathway& path);
        Vec3 steerToStayOnPath (const float predictionTime, Pathway& path);

        Vec3 steerToAvoidObstacle (const float minTimeToCollision,
                                   const Obstacle& obstacle);

        Vec3 steerToAvoidObstacles (const float minTimeToCollision,
                                    const ObstacleGroup& obstacles);

        Vec3 steerToAvoidNeighbors (const float minTimeToCollision,
                                    const AVGroup& others);

        float predictNearestApproachTime (AbstractVehicle& otherVehicle);

        float computeNearestApproachPositions (AbstractVehicle& otherVehicle,
                                               float time);

        Vec3 hisPositionAtNearestApproach;
        Vec3 ourPositionAtNearestApproach;

        Vec3 steerToAvoidCloseNeighbors (const float minSeparationDistance,
                                         const AVGroup& others);

        bool inBoidNeighborhood (const AbstractVehicle& otherVehicle,
                                 const float minDistance,
                                 const float maxDistance,
                                 const float cosMaxAngle);

        Vec3 steerForSeparation (const float maxDistance,
                                 const float cosMaxAngle,
                                 const AVGroup& flock);

        Vec3 steerForAlignment (const float maxDistance,
                                const float cosMaxAngle,
                                const AVGroup& flock);

        Vec3 steerForCohesion (const float maxDistance,
                               const float cosMaxAngle,
                               const AVGroup& flock);

        Vec3 steerForPursuit (const AbstractVehicle& quarry);

        Vec3 steerForPursuit (const AbstractVehicle& quarry,
                              const float maxPredictionTime);

        bool gaudyPursuitAnnotation;

        Vec3 steerForEvasion (const AbstractVehicle& menace,
                              const float maxPredictionTime);

        Vec3 steerForTargetSpeed (const float targetSpeed);

        bool isAhead (const Vec3& target) const {return isAhead (target, 0.707f);};
        bool isAside (const Vec3& target) const {return isAside (target, 0.707f);};
        bool isBehind (const Vec3& target) const {return isBehind (target, -0.707f);};

        bool isAhead (const Vec3& target, float cosThreshold) const
        {
            const Vec3 targetDirection = (target - position ()).normalize ();
            return forward().dot(targetDirection) > cosThreshold;
        };
        bool isAside (const Vec3& target, float cosThreshold) const
        {
            const Vec3 targetDirection = (target - position ()).normalize ();
            const float dp = forward().dot(targetDirection);
            return (dp < cosThreshold) && (dp > -cosThreshold);
        };
        bool isBehind (const Vec3& target, float cosThreshold) const
        {
            const Vec3 targetDirection = (target - position()).normalize ();
            return forward().dot(targetDirection) < cosThreshold;
        };

        virtual void annotateAvoidObstacle (const float )
        {
        }

        virtual void annotatePathFollowing (const Vec3& ,
                                            const Vec3& ,
                                            const Vec3& ,
                                            const float )
        {
        }

        virtual void annotateAvoidCloseNeighbor (const AbstractVehicle& ,
                                                 const float )
        {
        }

        virtual void annotateAvoidNeighbor (const AbstractVehicle& ,
                                            const float ,
                                            const Vec3& ,
                                            const Vec3& )
        {
        }
    };

} 

template<class Super>
OpenSteer::Vec3
OpenSteer::SteerLibraryMixin<Super>::
steerForWander (float dt)
{

    const float speed = 12.0f * dt; 
    WanderSide = scalarRandomWalk (WanderSide, speed, -1, +1);
    WanderUp   = scalarRandomWalk (WanderUp,   speed, -1, +1);

    return (side() * WanderSide) + (up() * WanderUp);
}

template<class Super>
OpenSteer::Vec3
OpenSteer::SteerLibraryMixin<Super>::
steerForSeek (const Vec3& target)
{
    const Vec3 desiredVelocity = target - position();
    return desiredVelocity - velocity();
}

template<class Super>
OpenSteer::Vec3
OpenSteer::SteerLibraryMixin<Super>::
steerForFlee (const Vec3& target)
{
    const Vec3 desiredVelocity = position - target;
    return desiredVelocity - velocity();
}

template<class Super>
OpenSteer::Vec3
OpenSteer::SteerLibraryMixin<Super>::
xxxsteerForFlee (const Vec3& target)
{

    const Vec3 offset = position() - target;
    const Vec3 desiredVelocity = offset.truncateLength (maxSpeed ()); 
    return desiredVelocity - velocity();
}

template<class Super>
OpenSteer::Vec3
OpenSteer::SteerLibraryMixin<Super>::
xxxsteerForSeek (const Vec3& target)
{

    const Vec3 offset = target - position();
    const Vec3 desiredVelocity = offset.truncateLength (maxSpeed ()); 
    return desiredVelocity - velocity();
}

template<class Super>
OpenSteer::Vec3
OpenSteer::SteerLibraryMixin<Super>::
steerToStayOnPath (const float predictionTime, Pathway& path)
{

    const Vec3 futurePosition = predictFuturePosition (predictionTime);

    Vec3 tangent;
    float outside;
    const Vec3 onPath = path.mapPointToPath (futurePosition,
                                             tangent,     
                                             outside);    

    if (outside < 0)
    {

        return Vec3::zero;
    }
    else
    {

        annotatePathFollowing (futurePosition, onPath, onPath, outside);
        return steerForSeek (onPath);
    }
}

template<class Super>
OpenSteer::Vec3
OpenSteer::SteerLibraryMixin<Super>::
steerToFollowPath (const int direction,
                   const float predictionTime,
                   Pathway& path)
{

    const float pathDistanceOffset = direction * predictionTime * speed();

    const Vec3 futurePosition = predictFuturePosition (predictionTime);

    const float nowPathDistance =
        path.mapPointToPathDistance (position ());
    const float futurePathDistance =
        path.mapPointToPathDistance (futurePosition);

    const bool rightway = ((pathDistanceOffset > 0) ?
                           (nowPathDistance < futurePathDistance) :
                           (nowPathDistance > futurePathDistance));

    Vec3 tangent;
    float outside;
    const Vec3 onPath = path.mapPointToPath (futurePosition,

                                             tangent,
                                             outside);

    if ((outside < 0) && rightway)
    {

        return Vec3::zero;
    }
    else
    {

        float const targetPathDistance = nowPathDistance + pathDistanceOffset;
        Vec3 const target = path.mapPathDistanceToPoint (targetPathDistance);

        annotatePathFollowing (futurePosition, onPath, target, outside);

        return steerForSeek (target);
    }
}

template<class Super>
OpenSteer::Vec3
OpenSteer::SteerLibraryMixin<Super>::
steerToAvoidObstacle (const float minTimeToCollision,
                      const Obstacle& obstacle)
{
    const Vec3 avoidance = obstacle.steerToAvoid (*this, minTimeToCollision);

    if (avoidance != Vec3::zero)
        annotateAvoidObstacle (minTimeToCollision * speed());

    return avoidance;
}

template<class Super>
OpenSteer::Vec3
OpenSteer::SteerLibraryMixin<Super>::
steerToAvoidObstacles (const float minTimeToCollision,
                       const ObstacleGroup& obstacles)
{
    const Vec3 avoidance = Obstacle::steerToAvoidObstacles (*this,
                                                            minTimeToCollision,
                                                            obstacles);

    if (avoidance != Vec3::zero)
        annotateAvoidObstacle (minTimeToCollision * speed());

    return avoidance;
}

template<class Super>
OpenSteer::Vec3
OpenSteer::SteerLibraryMixin<Super>::
steerToAvoidNeighbors (const float minTimeToCollision,
                       const AVGroup& others)
{

    const Vec3 separation = steerToAvoidCloseNeighbors (0, others);
    if (separation != Vec3::zero) return separation;

    float steer = 0;
    AbstractVehicle* threat = NULL;

    float minTime = minTimeToCollision;

    Vec3 xxxThreatPositionAtNearestApproach;
    Vec3 xxxOurPositionAtNearestApproach;

    for (AVIterator i = others.begin(); i != others.end(); i++)
    {
        AbstractVehicle& other = **i;
        if (&other != this)
        {	

            const float collisionDangerThreshold = radius() * 2;

            const float time = predictNearestApproachTime (other);

            if ((time >= 0) && (time < minTime))
            {

                if (computeNearestApproachPositions (other, time)
                    < collisionDangerThreshold)
                {
                    minTime = time;
                    threat = &other;
                    xxxThreatPositionAtNearestApproach
                        = hisPositionAtNearestApproach;
                    xxxOurPositionAtNearestApproach
                        = ourPositionAtNearestApproach;
                }
            }
        }
    }

    if (threat != NULL)
    {

        float parallelness = forward().dot(threat->forward());
        float angle = 0.707f;

        if (parallelness < -angle)
        {

            Vec3 offset = xxxThreatPositionAtNearestApproach - position();
            float sideDot = offset.dot(side());
            steer = (sideDot > 0) ? -1.0f : 1.0f;
        }
        else
        {
            if (parallelness > angle)
            {

                Vec3 offset = threat->position() - position();
                float sideDot = offset.dot(side());
                steer = (sideDot > 0) ? -1.0f : 1.0f;
            }
            else
            {

                if (threat->speed() <= speed())
                {
                    float sideDot = side().dot(threat->velocity());
                    steer = (sideDot > 0) ? -1.0f : 1.0f;
                }
            }
        }

        annotateAvoidNeighbor (*threat,
                               steer,
                               xxxOurPositionAtNearestApproach,
                               xxxThreatPositionAtNearestApproach);
    }

    return side() * steer;
}

template<class Super>
float
OpenSteer::SteerLibraryMixin<Super>::
predictNearestApproachTime (AbstractVehicle& otherVehicle)
{

    const Vec3 myVelocity = velocity();
    const Vec3 otherVelocity = otherVehicle.velocity();
    const Vec3 relVelocity = otherVelocity - myVelocity;
    const float relSpeed = relVelocity.length();

    if (relSpeed == 0) return 0;

    const Vec3 relTangent = relVelocity / relSpeed;

    const Vec3 relPosition = position() - otherVehicle.position();
    const float projection = relTangent.dot(relPosition);

    return projection / relSpeed;
}

template<class Super>
float
OpenSteer::SteerLibraryMixin<Super>::
computeNearestApproachPositions (AbstractVehicle& otherVehicle,
                                 float time)
{
    const Vec3    myTravel =       forward () *       speed () * time;
    const Vec3 otherTravel = otherVehicle.forward () * otherVehicle.speed () * time;

    const Vec3    myFinal =       position () +    myTravel;
    const Vec3 otherFinal = otherVehicle.position () + otherTravel;

    ourPositionAtNearestApproach = myFinal;
    hisPositionAtNearestApproach = otherFinal;

    return Vec3::distance (myFinal, otherFinal);
}

template<class Super>
OpenSteer::Vec3
OpenSteer::SteerLibraryMixin<Super>::
steerToAvoidCloseNeighbors (const float minSeparationDistance,
                            const AVGroup& others)
{

    for (AVIterator i = others.begin(); i != others.end(); i++)    
    {
        AbstractVehicle& other = **i;
        if (&other != this)
        {
            const float sumOfRadii = radius() + other.radius();
            const float minCenterToCenter = minSeparationDistance + sumOfRadii;
            const Vec3 offset = other.position() - position();
            const float currentDistance = offset.length();

            if (currentDistance < minCenterToCenter)
            {
                annotateAvoidCloseNeighbor (other, minSeparationDistance);
                return (-offset).perpendicularComponent (forward());
            }
        }
    }

    return Vec3::zero;
}

template<class Super>
bool
OpenSteer::SteerLibraryMixin<Super>::
inBoidNeighborhood (const AbstractVehicle& otherVehicle,
                    const float minDistance,
                    const float maxDistance,
                    const float cosMaxAngle)
{
    if (&otherVehicle == this)
    {
        return false;
    }
    else
    {
        const Vec3 offset = otherVehicle.position() - position();
        const float distanceSquared = offset.lengthSquared ();

        if (distanceSquared < (minDistance * minDistance))
        {
            return true;
        }
        else
        {

            if (distanceSquared > (maxDistance * maxDistance))
            {
                return false;
            }
            else
            {

                const Vec3 unitOffset = offset / sqrt (distanceSquared);
                const float forwardness = forward().dot (unitOffset);
                return forwardness > cosMaxAngle;
            }
        }
    }
}

template<class Super>
OpenSteer::Vec3
OpenSteer::SteerLibraryMixin<Super>::
steerForSeparation (const float maxDistance,
                    const float cosMaxAngle,
                    const AVGroup& flock)
{

    Vec3 steering;
    int neighbors = 0;

    AVIterator flockEndIter = flock.end();
    for (AVIterator otherVehicle = flock.begin(); otherVehicle != flockEndIter; ++otherVehicle )
    {
        if (inBoidNeighborhood (**otherVehicle, radius()*3, maxDistance, cosMaxAngle))
        {

            const Vec3 offset = (**otherVehicle).position() - position();
            const float distanceSquared = offset.dot(offset);
            steering += (offset / -distanceSquared);

            ++neighbors;
        }
    }

    steering = steering.normalize();

    return steering;
}

template<class Super>
OpenSteer::Vec3
OpenSteer::SteerLibraryMixin<Super>::
steerForAlignment (const float maxDistance,
                   const float cosMaxAngle,
                   const AVGroup& flock)
{

    Vec3 steering;
    int neighbors = 0;

    for (AVIterator otherVehicle = flock.begin(); otherVehicle != flock.end(); otherVehicle++)
    {
        if (inBoidNeighborhood (**otherVehicle, radius()*3, maxDistance, cosMaxAngle))
        {

            steering += (**otherVehicle).forward();

            neighbors++;
        }
    }

    if (neighbors > 0) steering = ((steering / (float)neighbors) - forward()).normalize();

    return steering;
}

template<class Super>
OpenSteer::Vec3
OpenSteer::SteerLibraryMixin<Super>::
steerForCohesion (const float maxDistance,
                  const float cosMaxAngle,
                  const AVGroup& flock)
{

    Vec3 steering;
    int neighbors = 0;

    for (AVIterator otherVehicle = flock.begin(); otherVehicle != flock.end(); otherVehicle++)
    {
        if (inBoidNeighborhood (**otherVehicle, radius()*3, maxDistance, cosMaxAngle))
        {

            steering += (**otherVehicle).position();

            neighbors++;
        }
    }

    if (neighbors > 0) steering = ((steering / (float)neighbors) - position()).normalize();

    return steering;
}

template<class Super>
OpenSteer::Vec3
OpenSteer::SteerLibraryMixin<Super>::
steerForPursuit (const AbstractVehicle& quarry)
{
    return steerForPursuit (quarry, FLT_MAX);
}

template<class Super>
OpenSteer::Vec3
OpenSteer::SteerLibraryMixin<Super>::
steerForPursuit (const AbstractVehicle& quarry,
                 const float maxPredictionTime)
{

    const Vec3 offset = quarry.position() - position();
    const float distance = offset.length ();
    const Vec3 unitOffset = offset / distance;

    const float parallelness = forward().dot (quarry.forward());

    const float forwardness = forward().dot (unitOffset);

    const float directTravelTime = distance / speed ();
    const int f = intervalComparison (forwardness,  -0.707f, 0.707f);
    const int p = intervalComparison (parallelness, -0.707f, 0.707f);

    float timeFactor = 0; 
    Color color;           

    switch (f)
    {
    case +1:
        switch (p)
        {
        case +1:          
            timeFactor = 4;
            color = gBlack;
            break;
        case 0:           
            timeFactor = 1.8f;
            color = gGray50;
            break;
        case -1:          
            timeFactor = 0.85f;
            color = gWhite;
            break;
        }
        break;
    case 0:
        switch (p)
        {
        case +1:          
            timeFactor = 1;
            color = gRed;
            break;
        case 0:           
            timeFactor = 0.8f;
            color = gYellow;
            break;
        case -1:          
            timeFactor = 4;
            color = gGreen;
            break;
        }
        break;
    case -1:
        switch (p)
        {
        case +1:          
            timeFactor = 0.5f;
            color= gCyan;
            break;
        case 0:           
            timeFactor = 2;
            color= gBlue;
            break;
        case -1:          
            timeFactor = 2;
            color = gMagenta;
            break;
        }
        break;
    }

    const float et = directTravelTime * timeFactor;

    const float etl = (et > maxPredictionTime) ? maxPredictionTime : et;

    const Vec3 target = quarry.predictFuturePosition (etl);

    this->annotationLine (position(),
                          target,
                          gaudyPursuitAnnotation ? color : gGray40);

    return steerForSeek (target);
}

template<class Super>
OpenSteer::Vec3
OpenSteer::SteerLibraryMixin<Super>::
steerForEvasion (const AbstractVehicle& menace,
                 const float maxPredictionTime)
{

    const Vec3 offset = this->menace.position - position;
    const float distance = offset.length ();

    const float roughTime = distance / menace.speed();
    const float predictionTime = ((roughTime > maxPredictionTime) ?
                                  maxPredictionTime :
                                  roughTime);

    const Vec3 target = menace.predictFuturePosition (predictionTime);

    return steerForFlee (target);
}

template<class Super>
OpenSteer::Vec3
OpenSteer::SteerLibraryMixin<Super>::
steerForTargetSpeed (const float targetSpeed)
{
    const float mf = maxForce ();
    const float speedError = targetSpeed - speed ();
    return forward () * clip (speedError, -mf, +mf);
}

#endif 