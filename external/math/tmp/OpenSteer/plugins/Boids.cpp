
#include <sstream>
#include "OpenSteer/SimpleVehicle.h"
#include "OpenSteer/OpenSteerDemo.h"
#include "OpenSteer/Proximity.h"
#include "OpenSteer/Color.h"
#include "OpenSteer/UnusedParameter.h"

#ifdef WIN32

#undef min
#undef max
#endif

#ifndef NO_LQ_BIN_STATS
#include <iomanip> 
#include <limits> 
#endif 

namespace {

    using namespace OpenSteer;

    typedef OpenSteer::AbstractProximityDatabase<AbstractVehicle*> ProximityDatabase;
    typedef OpenSteer::AbstractTokenForProximityDatabase<AbstractVehicle*> ProximityToken;

    class Boid : public OpenSteer::SimpleVehicle
    {
    public:

        typedef std::vector<Boid*> groupType;

        Boid (ProximityDatabase& pd)
        {

            proximityToken = NULL;
            newPD (pd);

            reset ();
        }

        ~Boid ()
        {

            delete proximityToken;
        }

        void reset (void)
        {

            SimpleVehicle::reset ();

            setMaxForce (27);

            setMaxSpeed (9);

            setSpeed (maxSpeed() * 0.3f);

            regenerateOrthonormalBasisUF (RandomUnitVector ());

            setPosition (RandomVectorInUnitRadiusSphere () * 20);

            proximityToken->updateForNewPosition (position());
        }

        void draw (void)
        {
            drawBasic3dSphericalVehicle (this, gGray70);

        }

        void update (const float currentTime, const float elapsedTime)
        {
            OPENSTEER_UNUSED_PARAMETER(currentTime);

            applySteeringForce (steerToFlock (), elapsedTime);

            sphericalWrapAround ();

            proximityToken->updateForNewPosition (position());
        }

        Vec3 steerToFlock (void)
        {

            const Vec3 avoidance = steerToAvoidObstacles (1.0f, obstacles);
            if (avoidance != Vec3::zero) return avoidance;

            const float separationRadius =  5.0f;
            const float separationAngle  = -0.707f;
            const float separationWeight =  12.0f;

            const float alignmentRadius = 7.5f;
            const float alignmentAngle  = 0.7f;
            const float alignmentWeight = 8.0f;

            const float cohesionRadius = 9.0f;
            const float cohesionAngle  = -0.15f;
            const float cohesionWeight = 8.0f;

            const float maxRadius = maxXXX (separationRadius,
                                            maxXXX (alignmentRadius,
                                                    cohesionRadius));

            neighbors.clear();
            proximityToken->findNeighbors (position(), maxRadius, neighbors);

    #ifndef NO_LQ_BIN_STATS

            size_t count = neighbors.size();
            if (maxNeighbors < count) maxNeighbors = count;
            if (minNeighbors > count) minNeighbors = count;
            totalNeighbors += count;
    #endif 

            const Vec3 separation = steerForSeparation (separationRadius,
                                                        separationAngle,
                                                        neighbors);
            const Vec3 alignment  = steerForAlignment  (alignmentRadius,
                                                        alignmentAngle,
                                                        neighbors);
            const Vec3 cohesion   = steerForCohesion   (cohesionRadius,
                                                        cohesionAngle,
                                                        neighbors);

            const Vec3 separationW = separation * separationWeight;
            const Vec3 alignmentW = alignment * alignmentWeight;
            const Vec3 cohesionW = cohesion * cohesionWeight;

            return separationW + alignmentW + cohesionW;
        }

        void sphericalWrapAround (void)
        {

            if (position().length() > worldRadius)
            {

                setPosition (position().sphericalWrapAround (Vec3::zero,
                                                             worldRadius));
                if (this == OpenSteerDemo::selectedVehicle)
                {
                    OpenSteerDemo::position3dCamera
                        (*OpenSteerDemo::selectedVehicle); 
                    OpenSteerDemo::camera.doNotSmoothNextMove ();
                }
            }
        }

        void regenerateLocalSpace (const Vec3& newVelocity,
                                   const float elapsedTime)
        {

            regenerateLocalSpaceForBanking (newVelocity, elapsedTime);

        }

        void regenerateLocalSpaceForTerrainFollowing  (const Vec3& newVelocity,
                                                       const float )
        {

            const Vec3 surfaceNormal = position().normalize();                       
            const Vec3 surfacePoint = surfaceNormal * 40.0f;                         

            const Vec3 newUp = surfaceNormal;
            const Vec3 newPos = surfacePoint;
            const Vec3 newVel = newVelocity.perpendicularComponent(newUp);
            const float newSpeed = newVel.length();
            const Vec3 newFor = newVel / newSpeed;

            setSpeed (newSpeed);
            setPosition (newPos);
            setUp (newUp);
            setForward (newFor);
            setUnitSideFromForwardAndUp ();
        }

        void newPD (ProximityDatabase& pd)
        {

            delete proximityToken;

            proximityToken = pd.allocateToken (this);
        }

        static ObstacleGroup obstacles;

        ProximityToken* proximityToken;

        static AVGroup neighbors;

        static float worldRadius;

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

    #ifndef NO_LQ_BIN_STATS
            static size_t minNeighbors, maxNeighbors, totalNeighbors;
    #endif 
    };

    AVGroup Boid::neighbors;
    float Boid::worldRadius = 50.0f;
    ObstacleGroup Boid::obstacles;
    #ifndef NO_LQ_BIN_STATS
    size_t Boid::minNeighbors, Boid::maxNeighbors, Boid::totalNeighbors;
    #endif 

    class BoidsPlugIn : public PlugIn
    {
    public:

        const char* name (void) {return "Boids";}

        float selectionOrderSortKey (void) {return 0.03f;}

        virtual ~BoidsPlugIn() {} 

        void open (void)
        {

            cyclePD = -1;
            nextPD ();

            population = 0;
            for (int i = 0; i < 200; i++) addBoidToFlock ();

            OpenSteerDemo::init3dCamera (*OpenSteerDemo::selectedVehicle);
            OpenSteerDemo::camera.mode = Camera::cmFixed;
            OpenSteerDemo::camera.fixedDistDistance = OpenSteerDemo::cameraTargetDistance;
            OpenSteerDemo::camera.fixedDistVOffset = 0;
            OpenSteerDemo::camera.lookdownDistance = 20;
            OpenSteerDemo::camera.aimLeadTime = 0.5;
            OpenSteerDemo::camera.povOffset.set (0, 0.5, -2);

            initObstacles ();
        }

        void update (const float currentTime, const float elapsedTime)
        {
    #ifndef NO_LQ_BIN_STATS
            Boid::maxNeighbors = Boid::totalNeighbors = 0;
            Boid::minNeighbors = std::numeric_limits<int>::max();
    #endif 

            for (iterator i = flock.begin(); i != flock.end(); i++)
            {
                (**i).update (currentTime, elapsedTime);
            }
        }

        void redraw (const float currentTime, const float elapsedTime)
        {

            AbstractVehicle* selected = OpenSteerDemo::selectedVehicle;

            AbstractVehicle* nearMouse = OpenSteerDemo::vehicleNearestToMouse ();

            OpenSteerDemo::updateCamera (currentTime, elapsedTime, selected);

            for (iterator i = flock.begin(); i != flock.end(); i++) (**i).draw ();

            OpenSteerDemo::drawCircleHighlightOnVehicle (nearMouse, 1, gGray70);

            OpenSteerDemo::drawCircleHighlightOnVehicle (selected, 1, gGray50);

            std::ostringstream status;
            status << "[F1/F2] " << population << " boids";
            status << "\n[F3]    PD type: ";
            switch (cyclePD)
            {
            case 0: status << "LQ bin lattice"; break;
            case 1: status << "brute force";    break;
            }
            status << "\n[F4]    Obstacles: ";
            switch (constraint)
            {
            case none:
                status << "none (wrap-around at sphere boundary)" ; break;
            case insideSphere:
                status << "inside a sphere" ; break;
            case outsideSphere:
                status << "inside a sphere, outside another" ; break;
            case outsideSpheres:
                status << "inside a sphere, outside several" ; break;
            case outsideSpheresNoBig:
                status << "outside several spheres, with wrap-around" ; break;
            case rectangle:
                status << "inside a sphere, with a rectangle" ; break;
            case rectangleNoBig:
                status << "a rectangle, with wrap-around" ; break;
            case outsideBox:
                status << "inside a sphere, outside a box" ; break;
            case insideBox:
                status << "inside a box" ; break;
            }
            status << std::endl;
            const float h = drawGetWindowHeight ();
            const Vec3 screenLocation (10, h-50, 0);
            draw2dTextAt2dLocation (status, screenLocation, gGray80, drawGetWindowWidth(), drawGetWindowHeight());

            drawObstacles ();
        }

        void close (void)
        {

            while (population > 0) removeBoidFromFlock ();

            delete pd;
            pd = NULL;
        }

        void reset (void)
        {

            for (iterator i = flock.begin(); i != flock.end(); i++) (**i).reset();

            OpenSteerDemo::position3dCamera (*OpenSteerDemo::selectedVehicle);

            OpenSteerDemo::camera.doNotSmoothNextMove ();
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
                    const float div = 10.0f;
                    const Vec3 divisions (div, div, div);
                    const float diameter = Boid::worldRadius * 1.1f * 2;
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

            for (iterator i=flock.begin(); i!=flock.end(); i++) (**i).newPD(*pd);

            delete oldPD;
        }

        void handleFunctionKeys (int keyNumber)
        {
            switch (keyNumber)
            {
            case 1:  addBoidToFlock ();         break;
            case 2:  removeBoidFromFlock ();    break;
            case 3:  nextPD ();                 break;
            case 4:  nextBoundaryCondition ();  break;
            case 5:  printLQbinStats ();        break;
            }
        }

        void printLQbinStats (void)
        {
    #ifndef NO_LQ_BIN_STATS
            int min, max; float average;
            Boid& aBoid = **(flock.begin());
            aBoid.proximityToken->getBinPopulationStats (min, max, average);
            std::cout << std::setprecision (2)
                      << std::setiosflags (std::ios::fixed);
            std::cout << "Bin populations: min, max, average: "
                      << min << ", " << max << ", " << average
                      << " (non-empty bins)" << std::endl; 
            std::cout << "Boid neighbors:  min, max, average: "
                      << Boid::minNeighbors << ", "
                      << Boid::maxNeighbors << ", "
                      << ((float)Boid::totalNeighbors) / ((float)population)
                      << std::endl;
    #endif 
        }

        void printMiniHelpForFunctionKeys (void)
        {
            std::ostringstream message;
            message << "Function keys handled by ";
            message << '"' << name() << '"' << ':' << std::ends;
            OpenSteerDemo::printMessage (message);
            OpenSteerDemo::printMessage ("  F1     add a boid to the flock.");
            OpenSteerDemo::printMessage ("  F2     remove a boid from the flock.");
            OpenSteerDemo::printMessage ("  F3     use next proximity database.");
            OpenSteerDemo::printMessage ("  F4     next flock boundary condition.");
            OpenSteerDemo::printMessage ("");
        }

        void addBoidToFlock (void)
        {
            population++;
            Boid* boid = new Boid (*pd);
            flock.push_back (boid);
            if (population == 1) OpenSteerDemo::selectedVehicle = boid;
        }

        void removeBoidFromFlock (void)
        {
            if (population > 0)
            {

                const Boid* boid = flock.back();
                flock.pop_back();
                population--;

                if (boid == OpenSteerDemo::selectedVehicle)
                    OpenSteerDemo::selectedVehicle = NULL;

                delete boid;
            }
        }

        const AVGroup& allVehicles (void) {return (const AVGroup&)flock;}

        Boid::groupType flock;
        typedef Boid::groupType::const_iterator iterator;

        ProximityDatabase* pd;

        int population;

        int cyclePD;

        enum ConstraintType {none, insideSphere,
                             outsideSphere, outsideSpheres, outsideSpheresNoBig,
                             rectangle, rectangleNoBig,
                             outsideBox, insideBox};

        ConstraintType constraint;

        void nextBoundaryCondition (void)
        {
            constraint = (ConstraintType) ((int) constraint + 1);
            updateObstacles ();
        }

        class SO : public SphereObstacle
        {void draw (const bool filled, const Color& color, const Vec3& vp) const
            {drawSphereObstacle (*this, 10.0f, filled, color, vp);}};

        class RO : public RectangleObstacle
        {void draw (const bool, const Color& color, const Vec3&) const
            {tempDrawRectangle (*this, color);}};

        class BO : public BoxObstacle
        {void draw (const bool, const Color& color, const Vec3&) const
            {tempDrawBox (*this, color);}};

        RO bigRectangle;
        BO outsideBigBox, insideBigBox;
        SO insideBigSphere, outsideSphere0, outsideSphere1, outsideSphere2,
           outsideSphere3, outsideSphere4, outsideSphere5, outsideSphere6;

        void initObstacles (void)
        {
            constraint = none;

            insideBigSphere.radius = Boid::worldRadius;
            insideBigSphere.setSeenFrom (Obstacle::inside);

            outsideSphere0.radius = Boid::worldRadius * 0.5f;

            const float r = Boid::worldRadius * 0.33f;
            outsideSphere1.radius = r;
            outsideSphere2.radius = r;
            outsideSphere3.radius = r;
            outsideSphere4.radius = r;
            outsideSphere5.radius = r;
            outsideSphere6.radius = r;

            const float p = Boid::worldRadius * 0.5f;
            const float m = -p;
            const float z = 0.0f;
            outsideSphere1.center.set (p, z, z);
            outsideSphere2.center.set (m, z, z);
            outsideSphere3.center.set (z, p, z);
            outsideSphere4.center.set (z, m, z);
            outsideSphere5.center.set (z, z, p);
            outsideSphere6.center.set (z, z, m);

            const Vec3 tiltF = Vec3 (1.0f, 1.0f, 0.0f).normalize ();
            const Vec3 tiltS (0.0f, 0.0f, 1.0f);
            const Vec3 tiltU = Vec3 (-1.0f, 1.0f, 0.0f).normalize ();

            bigRectangle.width = 50.0f;
            bigRectangle.height = 80.0f;
            bigRectangle.setSeenFrom (Obstacle::both);
            bigRectangle.setForward (tiltF);
            bigRectangle.setSide (tiltS);
            bigRectangle.setUp (tiltU);

            outsideBigBox.width = 50.0f;
            outsideBigBox.height = 80.0f;
            outsideBigBox.depth = 20.0f;
            outsideBigBox.setForward (tiltF);
            outsideBigBox.setSide (tiltS);
            outsideBigBox.setUp (tiltU);

            insideBigBox = outsideBigBox;
            insideBigBox.setSeenFrom (Obstacle::inside);

            updateObstacles ();
        }

        void updateObstacles (void)
        {

            Boid::obstacles.clear ();

            switch (constraint)
            {
            default:

                constraint = none;
            case none:
                break;
            case insideSphere:
                Boid::obstacles.push_back (&insideBigSphere);
                break;
            case outsideSphere:
                Boid::obstacles.push_back (&insideBigSphere);
                Boid::obstacles.push_back (&outsideSphere0);
                break;
            case outsideSpheres:
                Boid::obstacles.push_back (&insideBigSphere);
            case outsideSpheresNoBig:
                Boid::obstacles.push_back (&outsideSphere1);
                Boid::obstacles.push_back (&outsideSphere2);
                Boid::obstacles.push_back (&outsideSphere3);
                Boid::obstacles.push_back (&outsideSphere4);
                Boid::obstacles.push_back (&outsideSphere5);
                Boid::obstacles.push_back (&outsideSphere6);
                break;
            case rectangle:
                Boid::obstacles.push_back (&insideBigSphere);
                Boid::obstacles.push_back (&bigRectangle);
            case rectangleNoBig:
                Boid::obstacles.push_back (&bigRectangle);
                break;
            case outsideBox:
                Boid::obstacles.push_back (&insideBigSphere);
                Boid::obstacles.push_back (&outsideBigBox);
                break;
            case insideBox:
                Boid::obstacles.push_back (&insideBigBox);
                break;
            }
        }

        void drawObstacles (void)
        {
            for (ObstacleIterator o = Boid::obstacles.begin();
                 o != Boid::obstacles.end();
                 o++)
            {
                (**o).draw (false, 
                            ((*o == &insideBigSphere) ?
                             Color (0.2f, 0.2f, 0.4f) :
                             Color (0.1f, 0.1f, 0.2f)),
                            OpenSteerDemo::camera.position ());
            }
        }

        static void tempDrawRectangle (const RectangleObstacle& rect, const Color& color)
        {
            float w = rect.width / 2;
            float h = rect.height / 2;

            Vec3 v1 = rect.globalizePosition (Vec3 ( w,  h, 0));
            Vec3 v2 = rect.globalizePosition (Vec3 (-w,  h, 0));
            Vec3 v3 = rect.globalizePosition (Vec3 (-w, -h, 0));
            Vec3 v4 = rect.globalizePosition (Vec3 ( w, -h, 0));

            drawLine (v1, v2, color);
            drawLine (v2, v3, color);
            drawLine (v3, v4, color);
            drawLine (v4, v1, color);
        }

        static void tempDrawBox (const BoxObstacle& box, const Color& color)
        {
            const float w = box.width / 2;
            const float h = box.height / 2;
            const float d = box.depth / 2;

            const Vec3 v1 = box.globalizePosition (Vec3 ( w,  h,  d));
            const Vec3 v2 = box.globalizePosition (Vec3 (-w,  h,  d));
            const Vec3 v3 = box.globalizePosition (Vec3 (-w, -h,  d));
            const Vec3 v4 = box.globalizePosition (Vec3 ( w, -h,  d));

            const Vec3 v5 = box.globalizePosition (Vec3 ( w,  h, -d));
            const Vec3 v6 = box.globalizePosition (Vec3 (-w,  h, -d));
            const Vec3 v7 = box.globalizePosition (Vec3 (-w, -h, -d));
            const Vec3 v8 = box.globalizePosition (Vec3 ( w, -h, -d));

            drawLine (v1, v2, color);
            drawLine (v2, v3, color);
            drawLine (v3, v4, color);
            drawLine (v4, v1, color);

            drawLine (v5, v6, color);
            drawLine (v6, v7, color);
            drawLine (v7, v8, color);
            drawLine (v8, v5, color);

            drawLine (v1, v5, color);
            drawLine (v2, v6, color);
            drawLine (v3, v7, color);
            drawLine (v4, v8, color);
        }
    };

    BoidsPlugIn gBoidsPlugIn;

} 