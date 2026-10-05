
#ifndef OPENSTEER_SIMPLEVEHICLE_H
#define OPENSTEER_SIMPLEVEHICLE_H

#include "OpenSteer/AbstractVehicle.h"
#include "OpenSteer/SteerLibrary.h"
#include "OpenSteer/Annotation.h"

namespace OpenSteer {

    typedef LocalSpaceMixin<AbstractVehicle> SimpleVehicle_1;

    typedef AnnotationMixin<SimpleVehicle_1> SimpleVehicle_2;

    typedef SteerLibraryMixin<SimpleVehicle_2> SimpleVehicle_3;

    class SimpleVehicle : public SimpleVehicle_3
    {
    public:

        SimpleVehicle ();

        ~SimpleVehicle ();

        void reset (void)
        {

            resetLocalSpace ();

            SimpleVehicle_3::reset ();

            setMass (1);          
            setSpeed (0);         

            setRadius (0.5f);     

            setMaxForce (0.1f);   
            setMaxSpeed (1.0f);   

            resetSmoothedPosition ();
            resetSmoothedCurvature ();
            resetSmoothedAcceleration ();
        }

        float mass (void) const {return _mass;}
        float setMass (float m) {return _mass = m;}

        Vec3 velocity (void) const {return forward() * _speed;}

        float speed (void) const {return _speed;}
        float setSpeed (float s) {return _speed = s;}

        float radius (void) const {return _radius;}
        float setRadius (float m) {return _radius = m;}

        float maxForce (void) const {return _maxForce;}
        float setMaxForce (float mf) {return _maxForce = mf;}

        float maxSpeed (void) const {return _maxSpeed;}
        float setMaxSpeed (float ms) {return _maxSpeed = ms;}

        float relativeSpeed (void) const {return speed () / maxSpeed ();}

        void applySteeringForce (const Vec3& force, const float deltaTime);

        virtual void regenerateLocalSpace (const Vec3& newVelocity,
                                           const float elapsedTime);

        void regenerateLocalSpaceForBanking (const Vec3& newVelocity,
                                             const float elapsedTime);

        virtual Vec3 adjustRawSteeringForce (const Vec3& force,
                                             const float deltaTime);

        void applyBrakingForce (const float rate, const float deltaTime);

        Vec3 predictFuturePosition (const float predictionTime) const;

        float curvature (void) const {return _curvature;}

        float smoothedCurvature (void) {return _smoothedCurvature;}
        float resetSmoothedCurvature (float value = 0)
        {
            _lastForward = Vec3::zero;
            _lastPosition = Vec3::zero;
            return _smoothedCurvature = _curvature = value;
        }
        Vec3 smoothedAcceleration (void) {return _smoothedAcceleration;}
        Vec3 resetSmoothedAcceleration (const Vec3& value = Vec3::zero)
        {
            return _smoothedAcceleration = value;
        }
        Vec3 smoothedPosition (void) {return _smoothedPosition;}
        Vec3 resetSmoothedPosition (const Vec3& value = Vec3::zero)
        {
            return _smoothedPosition = value;
        }

        int serialNumber;
        static int serialNumberCounter;

        void annotationVelocityAcceleration (float maxLengthA, float maxLengthV);
        void annotationVelocityAcceleration (float maxLength)
            {annotationVelocityAcceleration (maxLength, maxLength);}
        void annotationVelocityAcceleration (void)
            {annotationVelocityAcceleration (3, 3);}

        void randomizeHeadingOnXZPlane (void)
        {
            setUp (Vec3::up);
            setForward (RandomUnitVectorOnXZPlane ());
            setSide (localRotateForwardToSide (forward()));
        }

    private:

        float _mass;       

        float _radius;     

        float _speed;      

        float _maxForce;   

        float _maxSpeed;   

        float _curvature;
        Vec3 _lastForward;
        Vec3 _lastPosition;
        Vec3 _smoothedPosition;
        float _smoothedCurvature;
        Vec3 _smoothedAcceleration;

        void measurePathCurvature (const float elapsedTime);
    };

} 

#endif 