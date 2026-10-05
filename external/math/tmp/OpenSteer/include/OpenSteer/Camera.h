
#ifndef OPENSTEER_CAMERA_H
#define OPENSTEER_CAMERA_H

#include "OpenSteer/LocalSpace.h"
#include "OpenSteer/AbstractVehicle.h"

namespace OpenSteer {

    class Camera : public LocalSpace
    {
    public:

        Camera ();
        virtual ~Camera() {  }

        void reset (void);

        Vec3 target;

        const AbstractVehicle* vehicleToTrack;

        float aimLeadTime;

        void update (const float currentTime,
                     const float elapsedTime,
                     const bool simulationPaused);
        void update (const float currentTime, const float elapsedTime)
        {update (currentTime, elapsedTime, false);};

        Vec3 constDistHelper (const float elapsedTime);

        void smoothCameraMove (const Vec3& newPosition,
                               const Vec3& newTarget,
                               const Vec3& newUp,
                               const float elapsedTime);

        void doNotSmoothNextMove (void) {smoothNextMove = false;};

        bool smoothNextMove;
        float smoothMoveSpeed;

        void mouseAdjustOffset (const Vec3& adjustment);
        Vec3 mouseAdjust2 (const bool polar,
                           const Vec3& adjustment,
                           const Vec3& offsetToAdjust);
        Vec3 mouseAdjustPolar (const Vec3& adjustment,
                               const Vec3& offsetToAdjust)
        {return mouseAdjust2 (true, adjustment, offsetToAdjust);};
        Vec3 mouseAdjustOrtho (const Vec3& adjustment,
                               const Vec3& offsetToAdjust)
        {return mouseAdjust2 (false, adjustment, offsetToAdjust);};

        LocalSpace ls;
        const LocalSpace& xxxls (void)
        {ls.regenerateOrthonormalBasis (target - position(), up()); return ls;}

        enum cameraMode 
            {

                cmStartMode,

                cmFixed,

                cmStraightDown,

                cmFixedDistanceOffset,

                cmFixedLocalOffset,

                cmOffsetPOV,

                cmEndMode
            };

        cameraMode mode;

        char const* modeName (void);

        void selectNextMode (void);

        cameraMode successorMode (const cameraMode cm) const;

        Vec3 fixedPosition;
        Vec3 fixedTarget;
        Vec3 fixedUp;

        float fixedDistDistance;             
        float fixedDistVOffset;              

        float lookdownDistance;             

        Vec3 fixedLocalOffset;

        Vec3 povOffset;
    };

} 

#endif 