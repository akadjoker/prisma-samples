
#include "OpenSteer/SimpleVehicle.h"
#include <algorithm>

int OpenSteer::SimpleVehicle::serialNumberCounter = 0;

OpenSteer::SimpleVehicle::SimpleVehicle (void)
{

    reset ();

    serialNumber = serialNumberCounter++;
}

OpenSteer::SimpleVehicle::~SimpleVehicle (void)
{
}

OpenSteer::Vec3 
OpenSteer::SimpleVehicle::adjustRawSteeringForce (const Vec3& force,
                                                  const float )
{
    const float maxAdjustedSpeed = 0.2f * maxSpeed ();

    if ((speed () > maxAdjustedSpeed) || (force == Vec3::zero))
    {
        return force;
    }
    else
    {
        const float range = speed() / maxAdjustedSpeed;

        const float cosine = interpolate (pow (range, 20), 1.0f, -1.0f);
        return limitMaxDeviationAngle (force, cosine, forward());
    }
}

void 
OpenSteer::SimpleVehicle::applyBrakingForce (const float rate, const float deltaTime)
{
    const float rawBraking = speed () * rate;
    const float clipBraking = ((rawBraking < maxForce ()) ?
                               rawBraking :
                               maxForce ());

    setSpeed (speed () - (clipBraking * deltaTime));
}

void 
OpenSteer::SimpleVehicle::applySteeringForce (const Vec3& force,
                                              const float elapsedTime)
{

    const Vec3 adjustedForce = adjustRawSteeringForce (force, elapsedTime);

    const Vec3 clippedForce = adjustedForce.truncateLength (maxForce ());

    Vec3 newAcceleration = (clippedForce / mass());
    Vec3 newVelocity = velocity();

    if (elapsedTime > 0)
    {
        const float smoothRate = clip (9 * elapsedTime, 0.15f, 0.4f);
        blendIntoAccumulator (smoothRate,
                              newAcceleration,
                              _smoothedAcceleration);
    }

    newVelocity += _smoothedAcceleration * elapsedTime;

    newVelocity = newVelocity.truncateLength (maxSpeed ());

    setSpeed (newVelocity.length());

    setPosition (position() + (newVelocity * elapsedTime));

    regenerateLocalSpace (newVelocity, elapsedTime);

    measurePathCurvature (elapsedTime);

    blendIntoAccumulator (elapsedTime * 0.06f, 
                          position (),
                          _smoothedPosition);
}

void 
OpenSteer::SimpleVehicle::regenerateLocalSpace (const Vec3& newVelocity,
                                                const float )
{

    if (speed() > 0) regenerateOrthonormalBasisUF (newVelocity / speed());
}

void 
OpenSteer::SimpleVehicle::regenerateLocalSpaceForBanking (const Vec3& newVelocity,
                                                          const float elapsedTime)
{

    const Vec3 globalUp (0, 0.2f, 0);

    const Vec3 accelUp = _smoothedAcceleration * 0.05f;

    const Vec3 bankUp = accelUp + globalUp;

    const float smoothRate = elapsedTime * 3;
    Vec3 tempUp = up();
    blendIntoAccumulator (smoothRate, bankUp, tempUp);
    setUp (tempUp.normalize());

    if (speed() > 0) regenerateOrthonormalBasisUF (newVelocity / speed());
}

void 
OpenSteer::SimpleVehicle::measurePathCurvature (const float elapsedTime)
{
    if (elapsedTime > 0)
    {
        const Vec3 dP = _lastPosition - position ();
        const Vec3 dF = (_lastForward - forward ()) / dP.length ();
        const Vec3 lateral = dF.perpendicularComponent (forward ());
        const float sign = (lateral.dot (side ()) < 0) ? 1.0f : -1.0f;
        _curvature = lateral.length() * sign;
        blendIntoAccumulator (elapsedTime * 4.0f,
                              _curvature,
                              _smoothedCurvature);
        _lastForward = forward ();
        _lastPosition = position ();
    }
}

void 
OpenSteer::SimpleVehicle::annotationVelocityAcceleration (float maxLengthA, 
                                                          float maxLengthV)
{
    const float desat = 0.4f;
    const float aScale = maxLengthA / maxForce ();
    const float vScale = maxLengthV / maxSpeed ();
    const Vec3& p = position();
    const Color aColor (desat, desat, 1); 
    const Color vColor (    1, desat, 1); 

    annotationLine (p, p + (velocity ()           * vScale), vColor);
    annotationLine (p, p + (_smoothedAcceleration * aScale), aColor);
}

OpenSteer::Vec3 
OpenSteer::SimpleVehicle::predictFuturePosition (const float predictionTime) const
{
    return position() + (velocity() * predictionTime);
}