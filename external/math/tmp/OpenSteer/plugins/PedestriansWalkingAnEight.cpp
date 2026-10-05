
#include <iomanip>
#include <sstream>
#include "OpenSteer/PolylineSegmentedPathwaySingleRadius.h"
#include "OpenSteer/SimpleVehicle.h"
#include "OpenSteer/OpenSteerDemo.h"
#include "OpenSteer/Proximity.h"
#include "OpenSteer/Color.h"

namespace {

    using namespace OpenSteer;

    typedef AbstractProximityDatabase<AbstractVehicle*> ProximityDatabase;
    typedef AbstractTokenForProximityDatabase<AbstractVehicle*> ProximityToken;

    int const gPedestrianStartCount = 1; 

    PolylineSegmentedPathwaySingleRadius* getTestPath (void);
    PolylineSegmentedPathwaySingleRadius* gTestPath = NULL;
    ObstacleGroup gObstacles;
    Vec3 gEndpoint0;
    Vec3 gEndpoint1;
    bool gUseDirectedPathFollowing = true;

    bool gWanderSwitch = true;

    class Pedestrian : public SimpleVehicle
    {
public:

        typedef std::vector<Pedestrian*> groupType;

        Pedestrian (ProximityDatabase& pd)
        {

            proximityToken = NULL;
            newPD (pd);

            reset ();
        }

        virtual ~Pedestrian ()
        {

            delete proximityToken;
        }

        void reset (void)
        {

            SimpleVehicle::reset ();

            setMaxSpeed (2.0);
            setMaxForce (8.0);

            setSpeed (0);

            setRadius (0.5); 

            path = getTestPath ();

            const float d = path->length() * frandom01();
            const float r = path->radius();
            const Vec3 randomOffset = randomVectorOnUnitRadiusXZDisk () * r;
            setPosition (path->mapPathDistanceToPoint (d) + randomOffset);

            randomizeHeadingOnXZPlane ();

            pathDirection = (frandom01() > 0.5) ? -1 : +1;

            setTrailParameters (3, 60);

            proximityToken->updateForNewPosition (position());
        }

        void update (const float currentTime, const float elapsedTime)
        {

            applySteeringForce (determineCombinedSteering (elapsedTime),
                                elapsedTime);

            if (gUseDirectedPathFollowing)
            {
                const Color darkRed (0.7f, 0, 0);
                float const pathRadius = path->radius();

                if (Vec3::distance (position(), gEndpoint0) < pathRadius )
                {
                    pathDirection = +1;
                    annotationXZCircle (pathRadius, gEndpoint0, darkRed, 20);
                }
                if (Vec3::distance (position(), gEndpoint1) < pathRadius )
                {
                    pathDirection = -1;
                    annotationXZCircle (pathRadius, gEndpoint1, darkRed, 20);
                }
            }

            annotationVelocityAcceleration (5, 0);
            recordTrailVertex (currentTime, position());

            proximityToken->updateForNewPosition (position());
        }

        Vec3 determineCombinedSteering (const float elapsedTime)
        {

            Vec3 steeringForce = forward();

            const float leakThrough = 0.1f;

            Vec3 obstacleAvoidance;
            if (leakThrough < frandom01())
            {
                const float oTime = 6; 

                obstacleAvoidance = steerToAvoidObstacles (oTime, gObstacles);

            }

            if (obstacleAvoidance != Vec3::zero)
            {
                steeringForce += obstacleAvoidance;
            }
            else
            {

                Vec3 collisionAvoidance;
                const float caLeadTime = 3;

                const float maxRadius = caLeadTime * maxSpeed() * 2;
                neighbors.clear();
                proximityToken->findNeighbors (position(), maxRadius, neighbors);

                if (leakThrough < frandom01())
                    collisionAvoidance =
                        steerToAvoidNeighbors (caLeadTime, neighbors) * 10;

                if (collisionAvoidance != Vec3::zero)
                {
                    steeringForce += collisionAvoidance;
                }
                else
                {

                    if (gWanderSwitch)
                        steeringForce += steerForWander (elapsedTime);

                    const float pfLeadTime = 3;
                    const Vec3 pathFollow =
                        (gUseDirectedPathFollowing ?
                         steerToFollowPath (pathDirection, pfLeadTime, *path) :
                         steerToStayOnPath (pfLeadTime, *path));

                    steeringForce += pathFollow * 0.5;
                }
            }

            return steeringForce.setYtoZero ();
        }

        void draw (void)
        {
            drawBasic2dCircularVehicle (this, gGray50);
            drawTrail ();
        }

        void annotatePathFollowing (const Vec3& future,
                                    const Vec3& onPath,
                                    const Vec3& target,
                                    const float outside)
        {
            const Color yellow (1, 1, 0);
            const Color lightOrange (1.0f, 0.5f, 0.0f);
            const Color darkOrange  (0.6f, 0.3f, 0.0f);
            const Color yellowOrange (1.0f, 0.75f, 0.0f);

            annotationLine (position(), future, yellow);

            annotationLine (position(), target, yellowOrange);

            const Vec3 boundaryOffset = (onPath - future).normalize() * outside;
            const Vec3 onPathBoundary = future + boundaryOffset;
            annotationLine (onPath, onPathBoundary, darkOrange);
            annotationLine (onPathBoundary, future, lightOrange);
        }

        void annotateAvoidCloseNeighbor (const AbstractVehicle& other,
                                         const float )
                                         {

            const float headOn = forward().dot(other.forward()) < 0;
            const Color green (0.4f, 0.8f, 0.1f);
            const Color red (1, 0.1f, 0);
            const Color color = headOn ? red : green;
            const char* string = headOn ? "OUCH!" : "pardon me";
            const Vec3 location = position() + Vec3 (0, 0.5f, 0);
            if (OpenSteer::annotationIsOn())
                draw2dTextAt3dLocation (*string, location, color, drawGetWindowWidth(), drawGetWindowHeight());
                                         }

                                         void annotateAvoidNeighbor (const AbstractVehicle& threat,
                                                                     const float ,
                                                                     const Vec3& ourFuture,
                                                                     const Vec3& threatFuture)
                                         {
                                             const Color green (0.15f, 0.6f, 0.0f);

                                             annotationLine (position(), ourFuture, green);
                                             annotationLine (threat.position(), threatFuture, green);
                                             annotationLine (ourFuture, threatFuture, gRed);
                                             annotationXZCircle (radius(), ourFuture,    green, 12);
                                             annotationXZCircle (radius(), threatFuture, green, 12);
                                         }

                                         void annotateAvoidObstacle (const float minDistanceToCollision)
                                         {
                                             const Vec3 boxSide = side() * radius();
                                             const Vec3 boxFront = forward() * minDistanceToCollision;
                                             const Vec3 FR = position() + boxFront - boxSide;
                                             const Vec3 FL = position() + boxFront + boxSide;
                                             const Vec3 BR = position()            - boxSide;
                                             const Vec3 BL = position()            + boxSide;
                                             const Color white (1,1,1);
                                             annotationLine (FR, FL, white);
                                             annotationLine (FL, BL, white);
                                             annotationLine (BL, BR, white);
                                             annotationLine (BR, FR, white);
                                         }

                                         void newPD (ProximityDatabase& pd)
                                         {

                                             delete proximityToken;

                                             proximityToken = pd.allocateToken (this);
                                         }

                                         ProximityToken* proximityToken;

                                         static AVGroup neighbors;

                                         PolylineSegmentedPathwaySingleRadius* path;

                                         int pathDirection;
    };

AVGroup Pedestrian::neighbors;

PolylineSegmentedPathwaySingleRadius* getTestPath (void)
{
    if (gTestPath == NULL)
    {
        const float pathRadius = 2;

        const PolylineSegmentedPathwaySingleRadius::size_type pathPointCount = 16;

        const Vec3 pathPoints[pathPointCount] = {
            Vec3( -12.678730011f, 0.0144290002063f, 0.523285984993f ),
            Vec3( -10.447640419f, 0.0149269998074f, -3.44138407707f ),
            Vec3( -5.88988399506f, 0.0128290001303f, -4.1717581749f ),
            Vec3( 0.941263973713f, 0.00330199999735f, 0.350513994694f ),
            Vec3( 5.83484792709f, -0.00407700007781f, 6.56243610382f ),
            Vec3( 11.0144147873f, -0.0111180003732f, 10.175157547f ),
            Vec3( 15.9621419907f, -0.0129949999973f, 8.82364273071f ),
            Vec3( 18.697883606f, -0.0102310003713f, 2.42084693909f ),
            Vec3( 16.0552558899f, -0.00506500015035f, -3.57153511047f ),
            Vec3( 10.5450153351f, 0.00284500000998f, -9.92683887482f ),
            Vec3( 5.88374519348f, 0.00683500012383f, -8.51393127441f ),
            Vec3( 3.17790007591f, 0.00419700006023f, -2.33129906654f ),
            Vec3( 1.94371795654f, 0.00101799995173f, 2.78656601906f ),
            Vec3( -1.04967498779f, 0.000867999973707f, 5.57114219666f ),
            Vec3( -7.58111476898f, 0.00634300010279f, 6.13661909103f ),
            Vec3( -12.4111375809f, 0.0108559997752f, 3.5670940876f )
        };

        gEndpoint0 = pathPoints[0];
        gEndpoint1 = pathPoints[pathPointCount-1];

        gTestPath = new PolylineSegmentedPathwaySingleRadius (pathPointCount,
                                                              pathPoints,
                                                              pathRadius,
                                                              false);
    }
    return gTestPath;
}

class PedestrianPlugIn : public PlugIn
{
public:

    const char* name (void) {return "Pedestrians Walking an Eight";}

    float selectionOrderSortKey (void) {return 98.0f;}

    virtual ~PedestrianPlugIn() {}

    void open (void)
    {

        cyclePD = -1;
        nextPD ();

        population = 0;
        for (int i = 0; i < gPedestrianStartCount; i++) addPedestrianToCrowd ();

        Pedestrian& firstPedestrian = **crowd.begin();
        OpenSteerDemo::init3dCamera (firstPedestrian);
        OpenSteerDemo::camera.mode = Camera::cmFixedDistanceOffset;
        OpenSteerDemo::camera.fixedTarget.set (15, 0, 30);
        OpenSteerDemo::camera.fixedPosition.set (15, 70, -70);
    }

    void update (const float currentTime, const float elapsedTime)
    {

        for (iterator i = crowd.begin(); i != crowd.end(); i++)
        {
            (**i).update (currentTime, elapsedTime);
        }
    }

    void redraw (const float currentTime, const float elapsedTime)
    {

        AbstractVehicle* selected = OpenSteerDemo::selectedVehicle;

        AbstractVehicle* nearMouse = OpenSteerDemo::vehicleNearestToMouse ();

        OpenSteerDemo::updateCamera (currentTime, elapsedTime, selected);

        if (OpenSteerDemo::selectedVehicle) gridCenter = selected->position();
        OpenSteerDemo::gridUtility (gridCenter);

        for (iterator i = crowd.begin(); i != crowd.end(); i++) (**i).draw (); 

        drawPathAndObstacles ();

        OpenSteerDemo::highlightVehicleUtility (nearMouse);

        serialNumberAnnotationUtility (selected, nearMouse);

        if (OpenSteerDemo::selectedVehicle && OpenSteer::annotationIsOn())
        {
            const Color color (0.8f, 0.8f, 1.0f);
            const Vec3 textOffset (0, 0.25f, 0);
            const Vec3 textPosition = selected->position() + textOffset;
            const Vec3 camPosition = OpenSteerDemo::camera.position();
            const float camDistance = Vec3::distance (selected->position(),
                                                      camPosition);
            const char* spacer = "      ";
            std::ostringstream annote;
            annote << std::setprecision (2);
            annote << std::setiosflags (std::ios::fixed);
            annote << spacer << "1: speed: " << selected->speed() << std::endl;
            annote << std::setprecision (1);
            annote << spacer << "2: cam dist: " << camDistance << std::endl;
            annote << spacer << "3: no third thing" << std::ends;
            draw2dTextAt3dLocation (annote, textPosition, color, drawGetWindowWidth(), drawGetWindowHeight());
        }

        std::ostringstream status;
        status << "[F1/F2] Crowd size: " << population;
        status << "\n[F3] PD type: ";
        switch (cyclePD)
        {
            case 0: status << "LQ bin lattice"; break;
            case 1: status << "brute force";    break;
        }
        status << "\n[F4] ";
        if (gUseDirectedPathFollowing)
            status << "Directed path following.";
        else
            status << "Stay on the path.";
        status << "\n[F5] Wander: ";
        if (gWanderSwitch) status << "yes"; else status << "no";
        status << std::endl;
        const float h = drawGetWindowHeight ();
        const Vec3 screenLocation (10, h-50, 0);
        draw2dTextAt2dLocation (status, screenLocation, gGray80, drawGetWindowWidth(), drawGetWindowHeight());
    }

    void serialNumberAnnotationUtility (const AbstractVehicle* selected,
                                        const AbstractVehicle* nearMouse)
    {

        if (&selected && &nearMouse && OpenSteer::annotationIsOn())
        {
            for (iterator i = crowd.begin(); i != crowd.end(); i++)
            {
                AbstractVehicle* vehicle = *i;
                const float nearDistance = 6;
                const Vec3& vp = vehicle->position();
                Vec3 np = nearMouse? nearMouse->position() : Vec3(0,0,0);
                if ((Vec3::distance (vp, selected->position()) < nearDistance)
                    ||
                    (&nearMouse && (Vec3::distance (vp, np) < nearDistance)))
                {
                    std::ostringstream sn;
                    sn << "#"
                        << ((Pedestrian*)vehicle)->serialNumber
                        << std::ends;
                    const Color textColor (0.8f, 1, 0.8f);
                    const Vec3 textOffset (0, 0.25f, 0);
                    const Vec3 textPos = vehicle->position() + textOffset;
                    draw2dTextAt3dLocation (sn, textPos, textColor, drawGetWindowWidth(), drawGetWindowHeight());
                }
            }
        }
    }

    void drawPathAndObstacles (void)
    {
        typedef PolylineSegmentedPathwaySingleRadius::size_type size_type;

        const PolylineSegmentedPathwaySingleRadius& path = *getTestPath ();
        for (size_type i = 1; i < path.pointCount(); ++i ) {
            drawLine (path.point( i ), path.point( i-1) , gRed);
        }

    }

    void close (void)
    {

        while (population > 0) removePedestrianFromCrowd ();
    }

    void reset (void)
    {

        for (iterator i = crowd.begin(); i != crowd.end(); i++) (**i).reset ();

        OpenSteerDemo::position2dCamera (*OpenSteerDemo::selectedVehicle);

        OpenSteerDemo::camera.doNotSmoothNextMove ();
    }

    void handleFunctionKeys (int keyNumber)
    {
        switch (keyNumber)
        {
            case 1:  addPedestrianToCrowd ();                               break;
            case 2:  removePedestrianFromCrowd ();                          break;
            case 3:  nextPD ();                                             break;
            case 4: gUseDirectedPathFollowing = !gUseDirectedPathFollowing; break;
            case 5: gWanderSwitch = !gWanderSwitch;                         break;
        }
    }

    void printMiniHelpForFunctionKeys (void)
    {
        std::ostringstream message;
        message << "Function keys handled by ";
        message << '"' << name() << '"' << ':' << std::ends;
        OpenSteerDemo::printMessage (message);
        OpenSteerDemo::printMessage (message);
        OpenSteerDemo::printMessage ("  F1     add a pedestrian to the crowd.");
        OpenSteerDemo::printMessage ("  F2     remove a pedestrian from crowd.");
        OpenSteerDemo::printMessage ("  F3     use next proximity database.");
        OpenSteerDemo::printMessage ("  F4     toggle directed path follow.");
        OpenSteerDemo::printMessage ("  F5     toggle wander component on/off.");
        OpenSteerDemo::printMessage ("");
    }

    void addPedestrianToCrowd (void)
    {
        population++;
        Pedestrian* pedestrian = new Pedestrian (*pd);
        crowd.push_back (pedestrian);
        if (population == 1) OpenSteerDemo::selectedVehicle = pedestrian;
    }

    void removePedestrianFromCrowd (void)
    {
        if (population > 0)
        {

            const Pedestrian* pedestrian = crowd.back();
            crowd.pop_back();
            population--;

            if (pedestrian == OpenSteerDemo::selectedVehicle)
                OpenSteerDemo::selectedVehicle = NULL;

            delete pedestrian;
        }
    }

    void nextPD (void)
    {

        ProximityDatabase* oldPD = pd;

        const int totalPD = 2;
        switch (cyclePD = (cyclePD + 1) % totalPD)
        {
            case 0:
            {
                const Vec3 center;
                const float div = 20.0f;
                const Vec3 divisions (div, 1.0f, div);
                const float diameter = 80.0f; 
                const Vec3 dimensions (diameter, diameter, diameter);
                typedef LQProximityDatabase<AbstractVehicle*> LQPDAV;
                pd = new LQPDAV (center, dimensions, divisions);
                break;
            }
            case 1:
            {
                pd = new BruteForceProximityDatabase<AbstractVehicle*> ();
                break;
            }
        }

        for (iterator i=crowd.begin(); i!=crowd.end(); i++) (**i).newPD(*pd);

        delete oldPD;
    }

    const AVGroup& allVehicles (void) {return (const AVGroup&) crowd;}

    Pedestrian::groupType crowd;
    typedef Pedestrian::groupType::const_iterator iterator;

    Vec3 gridCenter;

    ProximityDatabase* pd;

    int population;

    int cyclePD;
};

PedestrianPlugIn gPedestrianPlugIn;

} 