
#include <iomanip>
#include <sstream>
#include "OpenSteer/SimpleVehicle.h"
#include "OpenSteer/OpenSteerDemo.h"
#include "OpenSteer/Color.h"

namespace {

    using namespace OpenSteer;

    class OneTurning : public SimpleVehicle
    {
    public:

        OneTurning () {reset ();}

        void reset (void)
        {
            SimpleVehicle::reset (); 
            setSpeed (1.5f);         
            setMaxForce (0.3f);      
            setMaxSpeed (5);         
            clearTrailHistory ();    
        }

        void update (const float currentTime, const float elapsedTime)
        {
            applySteeringForce (Vec3 (-2, 0, -3), elapsedTime);
            annotationVelocityAcceleration ();
            recordTrailVertex (currentTime, position());
        }

        void draw (void)
        {
            drawBasic2dCircularVehicle (this, gGray50);
            drawTrail ();
        }
    };

    class OneTurningPlugIn : public PlugIn
    {
    public:

        const char* name (void) {return "One Turning Away";}

        float selectionOrderSortKey (void) {return 0.06f;}

        virtual ~OneTurningPlugIn() {}

        void open (void)
        {
            gOneTurning = new OneTurning;
            OpenSteerDemo::selectedVehicle = gOneTurning;
            theVehicle.push_back (gOneTurning);

            OpenSteerDemo::init2dCamera (*gOneTurning);
            OpenSteerDemo::camera.setPosition (10,
                                               OpenSteerDemo::camera2dElevation,
                                               10);
            OpenSteerDemo::camera.fixedPosition.set (40, 40, 40);
        }

        void update (const float currentTime, const float elapsedTime)
        {

            gOneTurning->update (currentTime, elapsedTime);
        }

        void redraw (const float currentTime, const float elapsedTime)
        {

            OpenSteerDemo::gridUtility (gOneTurning->position());

            gOneTurning->draw ();

            std::ostringstream annote;
            annote << std::setprecision (2) << std::setiosflags (std::ios::fixed);
            annote << "      speed: " << gOneTurning->speed() << std::ends;
            draw2dTextAt3dLocation (annote, gOneTurning->position(), gRed, drawGetWindowWidth(), drawGetWindowHeight());
            draw2dTextAt3dLocation (*"start", Vec3::zero, gGreen, drawGetWindowWidth(), drawGetWindowHeight());

            OpenSteerDemo::updateCamera (currentTime, elapsedTime, gOneTurning);

        }

        void close (void)
        {
            theVehicle.clear ();
            delete (gOneTurning);
            gOneTurning = NULL;
        }

        void reset (void)
        {

            gOneTurning->reset ();
        }

        const AVGroup& allVehicles (void) {return (const AVGroup&) theVehicle;}

        OneTurning* gOneTurning;
        std::vector<OneTurning*> theVehicle; 
    };

    OneTurningPlugIn gOneTurningPlugIn;

} 