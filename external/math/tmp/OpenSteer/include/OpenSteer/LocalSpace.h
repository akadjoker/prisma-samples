
#ifndef OPENSTEER_LOCALSPACE_H
#define OPENSTEER_LOCALSPACE_H

#include "OpenSteer/Vec3.h"

namespace OpenSteer {

    class AbstractLocalSpace
    {
    public:
        virtual ~AbstractLocalSpace() {  }

        virtual Vec3 side (void) const = 0;
        virtual Vec3 setSide (Vec3 s) = 0;
        virtual Vec3 up (void) const = 0;
        virtual Vec3 setUp (Vec3 u) = 0;
        virtual Vec3 forward (void) const = 0;
        virtual Vec3 setForward (Vec3 f) = 0;
        virtual Vec3 position (void) const = 0;
        virtual Vec3 setPosition (Vec3 p) = 0;

        virtual bool rightHanded (void) const = 0;

        virtual void resetLocalSpace (void) = 0;

        virtual Vec3 localizeDirection (const Vec3& globalDirection) const = 0;

        virtual Vec3 localizePosition (const Vec3& globalPosition) const = 0;

        virtual Vec3 globalizePosition (const Vec3& localPosition) const = 0;

        virtual Vec3 globalizeDirection (const Vec3& localDirection) const = 0;

        virtual void setUnitSideFromForwardAndUp (void) = 0;

        virtual void regenerateOrthonormalBasisUF (const Vec3& newUnitForward) = 0;

        virtual void regenerateOrthonormalBasis (const Vec3& newForward) = 0;

        virtual void regenerateOrthonormalBasis (const Vec3& newForward,
                                                 const Vec3& newUp) = 0;

        virtual Vec3 localRotateForwardToSide (const Vec3& v) const = 0;
        virtual Vec3 globalRotateForwardToSide (const Vec3& globalForward) const=0;
    };

    template <class Super>
    class LocalSpaceMixin : public Super
    {

    private:

        Vec3 _side;     
        Vec3 _up;       
        Vec3 _forward;  
        Vec3 _position; 

    public:

        Vec3 side     (void) const {return _side;};
        Vec3 up       (void) const {return _up;};
        Vec3 forward  (void) const {return _forward;};
        Vec3 position (void) const {return _position;};
        Vec3 setSide     (Vec3 s) {return _side = s;};
        Vec3 setUp       (Vec3 u) {return _up = u;};
        Vec3 setForward  (Vec3 f) {return _forward = f;};
        Vec3 setPosition (Vec3 p) {return _position = p;};
        Vec3 setSide     (float x, float y, float z){return _side.set    (x,y,z);};
        Vec3 setUp       (float x, float y, float z){return _up.set      (x,y,z);};
        Vec3 setForward  (float x, float y, float z){return _forward.set (x,y,z);};
        Vec3 setPosition (float x, float y, float z){return _position.set(x,y,z);};

        bool rightHanded (void) const {return true;}

        LocalSpaceMixin (void)
        {
            resetLocalSpace ();
        };

        LocalSpaceMixin (const Vec3& Side,
                         const Vec3& Up,
                         const Vec3& Forward,
                         const Vec3& Position)
            : _side( Side ), _up( Up ), _forward( Forward ), _position( Position ) {}

        LocalSpaceMixin (const Vec3& Up,
                         const Vec3& Forward,
                         const Vec3& Position)
            : _side(), _up( Up ), _forward( Forward ), _position( Position )
        {
            setUnitSideFromForwardAndUp ();
        }

        virtual ~LocalSpaceMixin() {  }

        void resetLocalSpace (void)
        {
            _forward.set (0, 0, 1);
            _side = localRotateForwardToSide (_forward);
            _up.set (0, 1, 0);
            _position.set (0, 0, 0);
        };

        Vec3 localizeDirection (const Vec3& globalDirection) const
        {

            return Vec3 (globalDirection.dot (_side),
                         globalDirection.dot (_up),
                         globalDirection.dot (_forward));
        };

        Vec3 localizePosition (const Vec3& globalPosition) const
        {

            Vec3 globalOffset = globalPosition - _position;

            return localizeDirection (globalOffset);
        };

        Vec3 globalizePosition (const Vec3& localPosition) const
        {
            return _position + globalizeDirection (localPosition);
        };

        Vec3 globalizeDirection (const Vec3& localDirection) const
        {
            return ((_side    * localDirection.x) +
                    (_up      * localDirection.y) +
                    (_forward * localDirection.z));
        };

        void setUnitSideFromForwardAndUp (void)
        {

            if (rightHanded())
                _side.cross (_forward, _up);
            else
                _side.cross (_up, _forward);
            _side = _side.normalize ();
        }

        void regenerateOrthonormalBasisUF (const Vec3& newUnitForward)
        {
            _forward = newUnitForward;

            setUnitSideFromForwardAndUp ();

            if (rightHanded())
                _up.cross (_side, _forward);
            else
                _up.cross (_forward, _side);
        }

        void regenerateOrthonormalBasis (const Vec3& newForward)
        {
            regenerateOrthonormalBasisUF (newForward.normalize());
        }

        void regenerateOrthonormalBasis (const Vec3& newForward,
                                         const Vec3& newUp)
        {
            _up = newUp;
            regenerateOrthonormalBasis (newForward.normalize());
        }

        Vec3 localRotateForwardToSide (const Vec3& v) const
        {
            return Vec3 (rightHanded () ? -v.z : +v.z,
                         v.y,
                         v.x);
        }

        Vec3 globalRotateForwardToSide (const Vec3& globalForward) const
        {
            const Vec3 localForward = localizeDirection (globalForward);
            const Vec3 localSide = localRotateForwardToSide (localForward);
            return globalizeDirection (localSide);
        }
    };

    typedef LocalSpaceMixin<AbstractLocalSpace> LocalSpace;

    const LocalSpace gGlobalSpace;

} 

#endif 