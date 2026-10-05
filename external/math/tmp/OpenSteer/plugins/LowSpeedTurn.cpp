
#include <iomanip>
#include <sstream>
#include "OpenSteer/SimpleVehicle.h"
#include "OpenSteer/OpenSteerDemo.h"
#include "OpenSteer/Color.h"

namespace {

    using namespace OpenSteer;

    class LowSpeedTurn : public SimpleVehicle
    {
    public:

        LowSpeedTurn () {reset ();}

        void reset (void)
        {

            SimpleVehicle::reset ();

            setSpeed (startSpeed);

            setPosition (startX, 0, 0);

            setMaxForce (0.3f);

            setMaxSpeed (1.5f);

            startX += 2;

            startSpeed += 0.15f;

            setTrailParameters (15, 150);
        }

        void draw (void)
        {
            drawBasic2dCircularVehicle (this, gGray50);
            drawTrail ();
        }

        void update (const float currentTime, const float elapsedTime)
        {
            applySteeringForce (steering (), elapsedTime);

            annotationVelocityAcceleration ();
            recordTrailVertex (currentTime, position());
        }

        static void resetStarts (void)
        {
            startX = 0;
            startSpeed = 0;
        }

        Vec3 steering (void) {return Vec3 (1, 0, -1);}

        static float startX;
        static float startSpeed;
    };

    float LowSpeedTurn::startX;
    float LowSpeedTurn::startSpeed;

    const int lstCount = 5;
    const float lstLookDownDistance = 18;
    const Vec3 lstViewCenter (7, 0, -2);
    const Vec3 lstPlusZ (0, 0, 1);

    class LowSpeedTurnPlugIn : public PlugIn
    {
    public:

        const char* name (void) {return "Low Speed Turn";}

        float selectionOrderSortKey (void) {return 0.05f;}

        virtual ~LowSpeedTurnPlugIn() {}

        void open (void)
        {

            LowSpeedTurn::resetStarts ();
            for (int i = 0; i < lstCount; i++) all.push_back (new LowSpeedTurn);

            OpenSteerDemo::selectedVehicle = *all.begin();

            OpenSteerDemo::camera.mode = Camera::cmFixed;
            OpenSteerDemo::camera.fixedUp = lstPlusZ;
            OpenSteerDemo::camera.fixedTarget = lstViewCenter;
            OpenSteerDemo::camera.fixedPosition = lstViewCenter;
            OpenSteerDemo::camera.fixedPosition.y += lstLookDownDistance;
            OpenSteerDemo::camera.lookdownDistance = lstLookDownDistance;
            OpenSteerDemo::camera.fixedDistVOffset = OpenSteerDemo::camera2dElevation;
            OpenSteerDemo::camera.fixedDistDistance = OpenSteerDemo::cameraTargetDistance;
        }

        void update (const float currentTime, const float elapsedTime)
        {

            for (iterator i = all.begin(); i != all.end(); i++)
            {
                (**i).update (currentTime, elapsedTime);
            }
        }

        void redraw (const float currentTime, const float elapsedTime)
        {

            AbstractVehicle* selected = OpenSteerDemo::selectedVehicle;

            AbstractVehicle* nearMouse = OpenSteerDemo::vehicleNearestToMouse ();

            OpenSteerDemo::updateCamera (currentTime, elapsedTime, selected);

            OpenSteerDemo::gridUtility (selected->position());

            for (iterator i = all.begin(); i != all.end(); i++)
            {

                LowSpeedTurn& agent = **i;
                agent.draw ();

                const Color textColor (0.8f, 0.8f, 1.0f);
                const Vec3 textOffset (0, 0.25f, 0);
                const Vec3 textPosition = agent.position() + textOffset;
                std::ostringstream annote;
                annote << std::setprecision (2)
                       << std::setiosflags (std::ios::fixed)
                       << agent.speed()
                       << std::ends;
                draw2dTextAt3dLocation (annote, textPosition, textColor, drawGetWindowWidth(), drawGetWindowHeight());
            }

            OpenSteerDemo::highlightVehicleUtility (nearMouse);
        }

        void close (void)
        {
            for (iterator i = all.begin(); i!=all.end(); i++) delete (*i);
            all.clear ();
        }

        void reset (void)
        {

            LowSpeedTurn::resetStarts ();
            for (iterator i = all.begin(); i!=all.end(); i++) (**i).reset();
        }

        const AVGroup& allVehicles (void) {return (const AVGroup&) all;}

        std::vector<LowSpeedTurn*> all; 
        typedef std::vector<LowSpeedTurn*>::const_iterator iterator;
    };

    LowSpeedTurnPlugIn gLowSpeedTurnPlugIn;

} 