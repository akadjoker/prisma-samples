
#include "OpenSteer/SimpleVehicle.h"
#include "OpenSteer/OpenSteerDemo.h"
#include "OpenSteer/Color.h"

namespace {

    using namespace OpenSteer;

    class MpBase : public SimpleVehicle
    {
    public:

        MpBase () {reset ();}

        void reset (void)
        {
            SimpleVehicle::reset (); 
            setSpeed (0);            
            setMaxForce (5.0);       
            setMaxSpeed (3.0);       
            clearTrailHistory ();    
            gaudyPursuitAnnotation = true; 
        }

        void draw (void)
        {
            drawBasic2dCircularVehicle (this, bodyColor);
            drawTrail ();
        }

        Color bodyColor;
    };

    class MpWanderer : public MpBase
    {
    public:

        MpWanderer () {reset ();}

        void reset (void)
        {
            MpBase::reset ();
            bodyColor.set (0.4f, 0.6f, 0.4f); 
        }

        void update (const float currentTime, const float elapsedTime)
        {
            const Vec3 wander2d = steerForWander (elapsedTime).setYtoZero ();
            const Vec3 steer = forward() + (wander2d * 3);
            applySteeringForce (steer, elapsedTime);

            recordTrailVertex (currentTime, position());
        }

    };

    class MpPursuer : public MpBase
    {
    public:

        MpPursuer (MpWanderer* w) {wanderer = w; reset ();}

        void reset (void)
        {
            MpBase::reset ();
            bodyColor.set (0.6f, 0.4f, 0.4f); 
            randomizeStartingPositionAndHeading ();
        }

        void update (const float currentTime, const float elapsedTime)
        {

            const float d = Vec3::distance (position(), wanderer->position());
            const float r = radius() + wanderer->radius();
            if (d < r) reset ();

            const float maxTime = 20; 
            applySteeringForce (steerForPursuit (*wanderer, maxTime), elapsedTime);

            recordTrailVertex (currentTime, position());
        }

        void randomizeStartingPositionAndHeading (void)
        {

            const float inner = 20;
            const float outer = 30;
            const float radius = frandom2 (inner, outer);
            const Vec3 randomOnRing = RandomUnitVectorOnXZPlane () * radius;
            setPosition (wanderer->position() + randomOnRing);

            randomizeHeadingOnXZPlane ();
        }

        MpWanderer* wanderer;
    };

    class MpPlugIn : public PlugIn
    {
    public:

        const char* name (void) {return "Multiple Pursuit";}

        float selectionOrderSortKey (void) {return 0.04f;}

        virtual ~MpPlugIn() {} 

        void open (void)
        {

            wanderer = new MpWanderer;
            allMP.push_back (wanderer);

            const int pursuerCount = 30;
            for (int i = 0; i < pursuerCount; i++)
                allMP.push_back (new MpPursuer (wanderer));
            pBegin = allMP.begin() + 1;  
            pEnd = allMP.end();          

            OpenSteerDemo::selectedVehicle = wanderer;
            OpenSteerDemo::camera.mode = Camera::cmStraightDown;
            OpenSteerDemo::camera.fixedDistDistance = OpenSteerDemo::cameraTargetDistance;
            OpenSteerDemo::camera.fixedDistVOffset = OpenSteerDemo::camera2dElevation;
        }

        void update (const float currentTime, const float elapsedTime)
        {

            wanderer->update (currentTime, elapsedTime);

            for (iterator i = pBegin; i != pEnd; i++)
            {
                ((MpPursuer&) (**i)).update (currentTime, elapsedTime);
            }
        }

        void redraw (const float currentTime, const float elapsedTime)
        {

            AbstractVehicle* selected = OpenSteerDemo::selectedVehicle;

            AbstractVehicle* nearMouse = OpenSteerDemo::vehicleNearestToMouse ();

            OpenSteerDemo::updateCamera (currentTime, elapsedTime, selected);

            OpenSteerDemo::gridUtility (selected->position());

            for (iterator i = allMP.begin(); i != pEnd; i++) (**i).draw ();

            OpenSteerDemo::highlightVehicleUtility (nearMouse);
            OpenSteerDemo::circleHighlightVehicleUtility (selected);
        }

        void close (void)
        {

            delete (wanderer);
            for (iterator i = pBegin; i != pEnd; i++) delete ((MpPursuer*)*i);
            allMP.clear();
        }

        void reset (void)
        {

            wanderer->reset ();
            for (iterator i = pBegin; i != pEnd; i++) ((MpPursuer&)(**i)).reset ();

            OpenSteerDemo::camera.doNotSmoothNextMove ();
            OpenSteerDemo::camera.resetLocalSpace ();
        }

        const AVGroup& allVehicles (void) {return (const AVGroup&) allMP;}

        std::vector<MpBase*> allMP;
        typedef std::vector<MpBase*>::const_iterator iterator;
        iterator pBegin, pEnd;

        MpWanderer* wanderer;
    };

    MpPlugIn gMpPlugIn;

} 