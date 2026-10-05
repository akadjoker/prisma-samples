
#include "OpenSteer/Camera.h"
#include "OpenSteer/Draw.h"

OpenSteer::Camera::Camera (void)
{
    reset ();
}

void 
OpenSteer::Camera::reset (void)
{

    resetLocalSpace ();

    target = Vec3::zero;

    vehicleToTrack = NULL;

    aimLeadTime = 1;

    smoothNextMove = false;

    smoothMoveSpeed = 1.5f;

    mode = cmFixed;

    fixedDistDistance = 1;
    fixedDistVOffset = 0;

    lookdownDistance = 30;

    fixedPosition.set (75, 75, 75);
    fixedTarget = Vec3::zero;
    fixedUp = Vec3::up;

    fixedLocalOffset.set (5, 5, -5);

    povOffset.set (0, 1, -3);
}

void 
OpenSteer::Camera::update (const float ,
                        const float elapsedTime,
                        const bool simulationPaused)
{

    const AbstractVehicle& v = *vehicleToTrack;
    const bool noVehicle = vehicleToTrack == NULL;

    Vec3 newPosition = position();
    Vec3 newTarget = target;
    Vec3 newUp = up();

    const float antiLagTime = simulationPaused ? 0 : 1 / smoothMoveSpeed;

    const float predictionTime = aimLeadTime + antiLagTime;

    switch (mode)
    {
    case cmFixed:
        newPosition = fixedPosition;
        newTarget = fixedTarget;
        newUp = fixedUp;
        break;

    case cmFixedDistanceOffset:
        if (noVehicle) break;
        newUp = Vec3::up; 
        newTarget = v.predictFuturePosition (predictionTime);
        newPosition = constDistHelper (elapsedTime);
        break;

    case cmStraightDown:
        if (noVehicle) break;
        newUp = v.forward();
        newTarget = v.predictFuturePosition (predictionTime);
        newPosition = newTarget;
        newPosition.y += lookdownDistance;
        break;

    case cmFixedLocalOffset:
        if (noVehicle) break;
        newUp = v.up();
        newTarget = v.predictFuturePosition (predictionTime);
        newPosition = v.globalizePosition (fixedLocalOffset);
        break;

    case cmOffsetPOV:
        {
            if (noVehicle) break;
            newUp = v.up();
            const Vec3 futurePosition = v.predictFuturePosition (antiLagTime);
            const Vec3 globalOffset = v.globalizeDirection (povOffset);
            newPosition = futurePosition + globalOffset;

            const float L = 10;
            newTarget = newPosition + (v.forward() * L);
            break;
        }
    default:
        break;
    }

    smoothCameraMove (newPosition, newTarget, newUp, elapsedTime);

    drawCameraLookAt (position(), target, up());
}

void 
OpenSteer::Camera::smoothCameraMove (const Vec3& newPosition,
                                     const Vec3& newTarget,
                                     const Vec3& newUp,
                                     const float elapsedTime)
{
    if (smoothNextMove)
    {
        const float smoothRate = elapsedTime * smoothMoveSpeed;

        Vec3 tempPosition = position();
        Vec3 tempUp = up();
        blendIntoAccumulator (smoothRate, newPosition, tempPosition);
        blendIntoAccumulator (smoothRate, newTarget,   target);
        blendIntoAccumulator (smoothRate, newUp,       tempUp);
        setPosition (tempPosition);
        setUp (tempUp);

        if (up() == Vec3::zero)
            setUp (Vec3::up);
        else
            setUp (up().normalize ());
    }
    else
    {
        smoothNextMove = true;

        setPosition (newPosition);
        target   = newTarget;
        setUp (newUp);
    }
}

OpenSteer::Vec3 
OpenSteer::Camera::constDistHelper (const float )
{

    const bool constrainUp = (fixedDistVOffset != 0);

    const Vec3 adjustedPosition (position().x,
                                 (constrainUp) ? target.y : position().y,
                                 position().z);
    const Vec3 offset = adjustedPosition - target;

    const float distance = offset.length();

    if (distance == 0)
    {
        return position();
    }
    else
    {

        const Vec3 unitOffset = offset / distance;

        const float xxxDistance = sqrtXXX (square (fixedDistDistance) -
                                           square (fixedDistVOffset));
        const Vec3 newOffset = unitOffset * xxxDistance;

        return target + newOffset + Vec3 (0, fixedDistVOffset, 0);
    }
}

void 
OpenSteer::Camera::selectNextMode (void)
{
    mode = successorMode (mode);
    if (mode >= cmEndMode) mode = successorMode (cmStartMode);
}

OpenSteer::Camera::cameraMode 
OpenSteer::Camera::successorMode (const cameraMode cm) const
{
    return (cameraMode)(((int)cm) + 1);
}

char const* 
OpenSteer::Camera::modeName (void)
{
    switch (mode)
    {
    case  cmFixed:               return "static";                break;
    case  cmFixedDistanceOffset: return "fixed distance offset"; break;
    case  cmFixedLocalOffset:    return "fixed local offset";    break;
    case  cmOffsetPOV:           return "offset POV";            break;
    case  cmStraightDown:        return "straight down";         break;
    default:                     return "?";
    }
}

void 
OpenSteer::Camera::mouseAdjustOffset (const Vec3& adjustment)
{

    const AbstractVehicle& v = *vehicleToTrack;

    switch (mode)
    {
    case cmFixed:
        {
            const Vec3 offset = fixedPosition - fixedTarget;
            const Vec3 adjusted = mouseAdjustPolar (adjustment, offset);
            fixedPosition = fixedTarget + adjusted;
            break;
        }
    case cmFixedDistanceOffset:
        {

            const Vec3 offset = position() - target;
            const Vec3 adjusted = mouseAdjustPolar (adjustment, offset);

            setPosition (target + adjusted);
            fixedDistDistance = adjusted.length();

            fixedDistVOffset = position().y - target.y;

            break;
        }
    case cmStraightDown:
        {
            const Vec3 offset (0, 0, lookdownDistance);
            const Vec3 adjusted = mouseAdjustPolar (adjustment, offset);
            lookdownDistance = adjusted.z;
            break;
        }
    case cmFixedLocalOffset:
        {
            const Vec3 offset = v.globalizeDirection (fixedLocalOffset);
            const Vec3 adjusted = mouseAdjustPolar (adjustment, offset);
            fixedLocalOffset = v.localizeDirection (adjusted);
            break;
        }
    case cmOffsetPOV:
        {

            const Vec3 offset = v.globalizeDirection (povOffset);
            const Vec3 adjusted = mouseAdjustOrtho (adjustment, offset);
            povOffset = v.localizeDirection (adjusted);
            break;
        }
    default:
        break;
    }
}

OpenSteer::Vec3 
OpenSteer::Camera::mouseAdjust2 (const bool polar,
                                 const Vec3& adjustment,
                                 const Vec3& offsetToAdjust)
{

    Vec3 result = offsetToAdjust;

    const float oldLength = result.length ();
    const float rate = polar ? oldLength : 1;
    result += xxxls().side() * (adjustment.x * rate);
    result += xxxls().up()   * (adjustment.y * rate);
    if (polar)
    {
        const float newLength = result.length ();
        result *= oldLength / newLength;
    }

    if (polar)
        result *= (1 + adjustment.z);
    else
        result += xxxls().forward() * adjustment.z;

    return result;
}