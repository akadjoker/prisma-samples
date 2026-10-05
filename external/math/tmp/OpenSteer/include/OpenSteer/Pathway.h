
#ifndef OPENSTEER_PATHWAY_H
#define OPENSTEER_PATHWAY_H

namespace OpenSteer {

    class Vec3;

    class Pathway {
    public:
        virtual ~Pathway() = 0;

        virtual bool isValid() const = 0;

		virtual Vec3 mapPointToPath (const Vec3& point,
                                     Vec3& tangent,
                                     float& outside) const = 0;

		virtual Vec3 mapPathDistanceToPoint (float pathDistance) const = 0;

		virtual float mapPointToPathDistance (const Vec3& point) const = 0;

        virtual bool isCyclic() const = 0;

        virtual float length() const = 0;

    protected:

    }; 

} 

#endif 