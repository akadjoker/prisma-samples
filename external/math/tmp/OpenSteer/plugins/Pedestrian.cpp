
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

    int const gPedestrianStartCount = 100;

    PolylineSegmentedPathwaySingleRadius* getTestPath (void);
    PolylineSegmentedPathwaySingleRadius* gTestPath = NULL;
    SphereObstacle gObstacle1;
    SphereObstacle gObstacle2;
    ObstacleGroup gObstacles;
    Vec3 gEndpoint0;
    Vec3 gEndpoint1;
    bool gUseDirectedPathFollowing = true;

    RectangleObstacle gObstacle3 (7,7);

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

            const PolylineSegmentedPathwaySingleRadius::size_type pathPointCount = 7;
            const float size = 30;
            const float top = 2 * size;
            const float gap = 1.2f * size;
            const float out = 2 * size;
            const float h = 0.5;
            const Vec3 pathPoints[pathPointCount] =
                {Vec3 (h+gap-out,     0,  h+top-out),  
                 Vec3 (h+gap,         0,  h+top),      
                 Vec3 (h+gap+(top/2), 0,  h+top/2),    
                 Vec3 (h+gap,         0,  h),          
                 Vec3 (h,             0,  h),          
                 Vec3 (h,             0,  h+top),      
                 Vec3 (h+gap,         0,  h+top/2)};   

            gObstacle1.center = interpolate (0.2f, pathPoints[0], pathPoints[1]);
            gObstacle2.center = interpolate (0.5f, pathPoints[2], pathPoints[3]);
            gObstacle1.radius = 3;
            gObstacle2.radius = 5;
            gObstacles.push_back (&gObstacle1);
            gObstacles.push_back (&gObstacle2);

            gObstacles.push_back (&gObstacle3);

            gObstacle3.setForward (Vec3(1,1,0).normalize());
            gObstacle3.setSide (0,0,1);
            gObstacle3.setUp (Vec3(-1,1,0).normalize());
            gObstacle3.setPosition (20, 0, h);

            gObstacle3.setSeenFrom (Obstacle::both);

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

        const char* name (void) {return "Pedestrians";}

        float selectionOrderSortKey (void) {return 0.02f;}

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
                    Vec3 np(0,0,0);
                    if (nearMouse)
                        np = nearMouse->position();

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

            drawXZCircle (gObstacle1.radius, gObstacle1.center, gWhite, 40);
            drawXZCircle (gObstacle2.radius, gObstacle2.center, gWhite, 40);

            {
                float w = gObstacle3.width * 0.5f;
                Vec3 p = gObstacle3.position ();
                Vec3 s = gObstacle3.side ();
                drawLine (p + (s * w), p + (s * -w), gWhite);

                Vec3 v1 = gObstacle3.globalizePosition (Vec3 (w, w, 0));
                Vec3 v2 = gObstacle3.globalizePosition (Vec3 (-w, w, 0));
                Vec3 v3 = gObstacle3.globalizePosition (Vec3 (-w, -w, 0));
                Vec3 v4 = gObstacle3.globalizePosition (Vec3 (w, -w, 0));

                drawLine (v1, v2, gWhite);
                drawLine (v2, v3, gWhite);
                drawLine (v3, v4, gWhite);
                drawLine (v4, v1, gWhite);
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