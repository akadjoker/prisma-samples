
#ifndef OPENSTEER_ABSTRACTVEHICLE_H
#define OPENSTEER_ABSTRACTVEHICLE_H

#include "OpenSteer/LocalSpace.h"

#include <vector>

namespace OpenSteer {

    class AbstractVehicle : public AbstractLocalSpace 
    {
    public:
        virtual ~AbstractVehicle() {  }

        virtual float mass (void) const = 0;
        virtual float setMass (float) = 0;

        virtual float radius (void) const = 0;
        virtual float setRadius (float) = 0;

        virtual Vec3 velocity (void) const = 0;

        virtual float speed (void) const = 0;
        virtual float setSpeed (float) = 0;

        typedef std::vector<AbstractVehicle*> group;
        typedef group::const_iterator iterator;    

        virtual Vec3 predictFuturePosition (const float predictionTime) const = 0;

        virtual float maxForce (void) const = 0;
        virtual float setMaxForce (float) = 0;

        virtual float maxSpeed (void) const = 0;
        virtual float setMaxSpeed (float) = 0;

		virtual void update(const float currentTime, const float elapsedTime) = 0;
    };

    typedef AbstractVehicle::group AVGroup;
    typedef AbstractVehicle::iterator AVIterator;

} 

#endif 