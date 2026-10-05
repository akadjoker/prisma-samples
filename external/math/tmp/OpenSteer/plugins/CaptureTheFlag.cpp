
#include <iomanip>
#include <string>
#include <sstream>
#include "OpenSteer/SimpleVehicle.h"
#include "OpenSteer/OpenSteerDemo.h"
#include "OpenSteer/Color.h"

namespace {

    using namespace OpenSteer;

    typedef std::vector<SphereObstacle*> SOG;  
    typedef SOG::const_iterator SOI;           

    class CtfBase : public SimpleVehicle
    {
    public:

        CtfBase () {reset ();}

        void reset (void);

        void draw (void);

        void annotateAvoidObstacle (const float minDistanceToCollision);

        void drawHomeBase (void);

        void randomizeStartingPositionAndHeading (void);
        enum seekerState {running, tagged, atGoal};

        Color bodyColor;

        bool avoiding;

        static void initializeObstacles (void);
        static void addOneObstacle (void);
        static void removeOneObstacle (void);
        float minDistanceToObstacle (const Vec3 point);
        static int obstacleCount;
        static const int maxObstacleCount;
        static SOG allObstacles;
    };

    class CtfSeeker : public CtfBase
    {
    public:

        CtfSeeker () {reset ();}

        void reset (void);

        void update (const float currentTime, const float elapsedTime);

        bool clearPathToGoal (void);

        Vec3 steeringForSeeker (void);
        void updateState (const float currentTime);
        void draw (void);
        Vec3 steerToEvadeAllDefenders (void);
        Vec3 XXXsteerToEvadeAllDefenders (void);
        void adjustObstacleAvoidanceLookAhead (const bool clearPath);
        void clearPathAnnotation (const float threshold,
                                  const float behindcThreshold,
                                  const Vec3& goalDirection);

        seekerState state;
        bool evading; 
        float lastRunningTime; 
    };

    class CtfEnemy : public CtfBase
    {
    public:

        CtfEnemy () {reset ();}

        void reset (void);

        void update (const float currentTime, const float elapsedTime);
    };

    const int CtfBase::maxObstacleCount = 100;

    const Vec3 gHomeBaseCenter (0, 0, 0);
    const float gHomeBaseRadius = 1.5;

    const float gMinStartRadius = 30;
    const float gMaxStartRadius = 40;

    const float gBrakingRate = 0.75;

    const Color evadeColor     (0.6f, 0.6f, 0.3f); 
    const Color seekColor      (0.3f, 0.6f, 0.6f); 
    const Color clearPathColor (0.3f, 0.6f, 0.3f); 

    const float gAvoidancePredictTimeMin  = 0.9f;
    const float gAvoidancePredictTimeMax  = 2;
    float gAvoidancePredictTime = gAvoidancePredictTimeMin;

    CtfSeeker* gSeeker = NULL;

    int resetCount = 0;

    CtfSeeker* ctfSeeker;
    const int ctfEnemyCount = 4;
    CtfEnemy* ctfEnemies [ctfEnemyCount];

    void CtfBase::reset (void)
    {
        SimpleVehicle::reset ();  

        setSpeed (3);             
        setMaxForce (3.0);        
        setMaxSpeed (3.0);        

        avoiding = false;         

        randomizeStartingPositionAndHeading ();  

        clearTrailHistory ();     
    }

    void CtfSeeker::reset (void)
    {
        CtfBase::reset ();
        bodyColor.set (0.4f, 0.4f, 0.6f); 
        gSeeker = this;
        state = running;
        evading = false;
    }

    void CtfEnemy::reset (void)
    {
        CtfBase::reset ();
        bodyColor.set (0.6f, 0.4f, 0.4f); 
    }

    void CtfBase::draw (void)
    {
        drawBasic2dCircularVehicle (this, bodyColor);
        drawTrail ();
    }

    void CtfBase::randomizeStartingPositionAndHeading (void)
    {

        const float rRadius = frandom2 (gMinStartRadius, gMaxStartRadius);
        const Vec3 randomOnRing = RandomUnitVectorOnXZPlane () * rRadius;
        setPosition (gHomeBaseCenter + randomOnRing);

        if (minDistanceToObstacle (position()) < radius()*5)
        {

            randomizeStartingPositionAndHeading ();
        }
        else
        {

            randomizeHeadingOnXZPlane ();
        }
    }

    void CtfEnemy::update (const float currentTime, const float elapsedTime)
    {

        const float seekerToGoalDist = Vec3::distance (gHomeBaseCenter,
                                                       gSeeker->position());
        const float adjustedDistance = seekerToGoalDist - radius()-gHomeBaseRadius;
        const float seekerToGoalTime = ((adjustedDistance < 0 ) ?
                                        0 :
                                        (adjustedDistance/gSeeker->speed()));
        const float maxPredictionTime = seekerToGoalTime * 0.9f;

        Vec3 steer (0, 0, 0);
        if (gSeeker->state == running)
        {
            const Vec3 avoidance =
                steerToAvoidObstacles (gAvoidancePredictTimeMin,
                                       (ObstacleGroup&) allObstacles);

            avoiding = (avoidance == Vec3::zero);

            if (avoiding)
                steer = steerForPursuit (*gSeeker, maxPredictionTime);
            else
                steer = avoidance;
        }
        else
        {
            applyBrakingForce (gBrakingRate, elapsedTime);
        }
        applySteeringForce (steer, elapsedTime);

        annotationVelocityAcceleration ();
        recordTrailVertex (currentTime, position());

        const float seekerToMeDist = Vec3::distance (position(), 
                                                     gSeeker->position());
        const float sumOfRadii = radius() + gSeeker->radius();
        if (seekerToMeDist < sumOfRadii)
        {
            if (gSeeker->state == running) gSeeker->state = tagged;

            if (gSeeker->state == tagged)
            {
                const Color color (0.8f, 0.5f, 0.5f);
                annotationXZDisk (sumOfRadii,
                            (position() + gSeeker->position()) / 2,
                            color,
                            20);
            }
        }
    }

    bool CtfSeeker::clearPathToGoal (void)
    {
        const float sideThreshold = radius() * 8.0f;
        const float behindThreshold = radius() * 2.0f;

        const Vec3 goalOffset = gHomeBaseCenter - position();
        const float goalDistance = goalOffset.length ();
        const Vec3 goalDirection = goalOffset / goalDistance;

        const bool goalIsAside = isAside (gHomeBaseCenter, 0.5);

        bool xxxReturn = true;

        for (int i = 0; i < ctfEnemyCount; i++)
        {

            const CtfEnemy& e = *ctfEnemies[i];
            const float eDistance = Vec3::distance (position(), e.position());
            const float timeEstimate = 0.3f * eDistance / e.speed(); 
            const Vec3 eFuture = e.predictFuturePosition (timeEstimate);
            const Vec3 eOffset = eFuture - position();
            const float alongCorridor = goalDirection.dot (eOffset);
            const bool inCorridor = ((alongCorridor > -behindThreshold) && 
                                     (alongCorridor < goalDistance));
            const float eForwardDistance = forward().dot (eOffset);

            annotationXZCircle (e.radius(), eFuture, clearPathColor, 20); 

            if (inCorridor)
            {
                const Vec3 perp = eOffset - (goalDirection * alongCorridor);
                const float acrossCorridor = perp.length();
                if (acrossCorridor < sideThreshold)
                {

                    const float eFront = eForwardDistance + e.radius ();

                    const bool eIsBehind = eFront < -behindThreshold;
                    const bool eIsWayBehind = eFront < (-2 * behindThreshold);
                    const bool safeToTurnTowardsGoal =
                        ((eIsBehind && goalIsAside) || eIsWayBehind);

                    if (! safeToTurnTowardsGoal)
                    {

                        annotationLine (position(), e.position(), clearPathColor);

                        xxxReturn = false;
                    }
                }
            }
        }

            clearPathAnnotation (sideThreshold, behindThreshold, goalDirection);
        return xxxReturn;
    }

    void CtfSeeker::clearPathAnnotation (const float sideThreshold,
                                         const float behindThreshold,
                                         const Vec3& goalDirection)
    {
        const Vec3 behindSide = side() * sideThreshold;
        const Vec3 behindBack = forward() * -behindThreshold;
        const Vec3 pbb = position() + behindBack;
        const Vec3 gun = localRotateForwardToSide (goalDirection);
        const Vec3 gn = gun * sideThreshold;
        const Vec3 hbc = gHomeBaseCenter;
        annotationLine (pbb + gn,         hbc + gn,         clearPathColor);
        annotationLine (pbb - gn,         hbc - gn,         clearPathColor);
        annotationLine (hbc - gn,         hbc + gn,         clearPathColor);
        annotationLine (pbb - behindSide, pbb + behindSide, clearPathColor);
    }

    void CtfBase::annotateAvoidObstacle (const float minDistanceToCollision)
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

    Vec3 CtfSeeker::steerToEvadeAllDefenders (void)
    {
        Vec3 evade (0, 0, 0);
        const float goalDistance = Vec3::distance (gHomeBaseCenter, position());

        for (int i = 0; i < ctfEnemyCount; i++)
        {
            const CtfEnemy& e = *ctfEnemies[i];
            const Vec3 eOffset = e.position() - position();
            const float eDistance = eOffset.length();

            const float eForwardDistance = forward().dot (eOffset);
            const float behindThreshold = radius() * 2;
            const bool behind = eForwardDistance < behindThreshold;
            if ((!behind) || (eDistance < 5))
            {
                if (eDistance < (goalDistance * 1.2)) 
                {

                    const float timeEstimate = 0.15f * eDistance / e.speed();
                    const Vec3 future =
                        e.predictFuturePosition (timeEstimate);

                    annotationXZCircle (e.radius(), future, evadeColor, 20); 

                    const Vec3 offset = future - position();
                    const Vec3 lateral = offset.perpendicularComponent (forward());
                    const float d = lateral.length();
                    const float weight = -1000 / (d * d);
                    evade += (lateral / d) * weight;
                }
            }
        }
        return evade;
    }

    Vec3 CtfSeeker::XXXsteerToEvadeAllDefenders (void)
    {

        Vec3 evade (0, 0, 0);
        for (int i = 0; i < ctfEnemyCount; i++)
        {
            const CtfEnemy& e = *ctfEnemies[i];
            const Vec3 eOffset = e.position() - position();
            const float eDistance = eOffset.length();

            const float timeEstimate = 0.5f * eDistance / e.speed(); 
            const Vec3 eFuture = e.predictFuturePosition (timeEstimate);

            annotationXZCircle (e.radius(), eFuture, evadeColor, 20);

            const Vec3 flee = xxxsteerForFlee (eFuture);

            const float eForwardDistance = forward().dot (eOffset);
            const float behindThreshold = radius() * -2;

            const float distanceWeight = 4 / eDistance;
            const float forwardWeight = ((eForwardDistance > behindThreshold) ?
                                         1.0f : 0.5f);

            const Vec3 adjustedFlee = flee * distanceWeight * forwardWeight;

            evade += adjustedFlee;
        }
        return evade;
    }

    Vec3 CtfSeeker::steeringForSeeker (void)
    {

        const bool clearPath = clearPathToGoal ();
        adjustObstacleAvoidanceLookAhead (clearPath);
        const Vec3 obstacleAvoidance =
            steerToAvoidObstacles (gAvoidancePredictTime,
                                   (ObstacleGroup&) allObstacles);

        avoiding = (obstacleAvoidance != Vec3::zero);

        if (avoiding)
        {

            return obstacleAvoidance;
        }
        else
        {

            const Vec3 seek = xxxsteerForSeek (gHomeBaseCenter);
            if (clearPath)
            {

                Vec3 s = limitMaxDeviationAngle (seek, 0.707f, forward());

                annotationLine (position(), position() + (s * 0.2f), seekColor);
                return s;
            }
            else
            {
                if (0) 
                {

                    const Vec3 evade = steerToEvadeAllDefenders ();
                    const Vec3 steer = 
                        seek + limitMaxDeviationAngle (evade, 0.5f, forward());

                    annotationLine (position(),position()+(steer*0.2f),evadeColor);
                    return steer;
                }
                else

                {
                    const Vec3 evade = XXXsteerToEvadeAllDefenders ();
                    const Vec3 steer = limitMaxDeviationAngle (seek + evade,
                                                               0.707f, forward());

                    annotationLine (position(),position()+seek, gRed);
                    annotationLine (position(),position()+evade, gGreen);

                    annotationLine (position(),position()+(steer*0.2f),evadeColor);
                    return steer;
                }
            }
        }
    }

    void CtfSeeker::adjustObstacleAvoidanceLookAhead (const bool clearPath)
    {
        if (clearPath)
        {
            evading = false;
            const float goalDistance = Vec3::distance (gHomeBaseCenter,position());
            const bool headingTowardGoal = isAhead (gHomeBaseCenter, 0.98f);
            const bool isNear = (goalDistance/speed()) < gAvoidancePredictTimeMax;
            const bool useMax = headingTowardGoal && !isNear;
            gAvoidancePredictTime =
                (useMax ? gAvoidancePredictTimeMax : gAvoidancePredictTimeMin);
        }
        else
        {
            evading = true;
            gAvoidancePredictTime = gAvoidancePredictTimeMin;
        }
    }

    void CtfSeeker::updateState (const float currentTime)
    {

        if (state == running)
        {
            const float baseDistance = Vec3::distance (position(),gHomeBaseCenter);
            if (baseDistance < (radius() + gHomeBaseRadius)) state = atGoal;
        }

        if (state == running)
        {
            lastRunningTime = currentTime;
        }
        else
        {
            const float resetDelay = 4;
            const float resetTime = lastRunningTime + resetDelay;
            if (currentTime > resetTime) 
            {

                OpenSteerDemo::queueDelayedResetPlugInXXX ();
            }
        }
    }

    void CtfSeeker::draw (void)
    {

        CtfBase::draw();

        std::string seekerStateString("");
        switch (state)
        {
        case running:
            if (avoiding)
                seekerStateString = "avoid obstacle";
            else if (evading)
                seekerStateString = "seek and evade";
            else
                seekerStateString = "seek goal";
            break;
        case tagged: seekerStateString = "tagged"; break;
        case atGoal: seekerStateString = "reached goal"; break;
        }

        const Vec3 textOrigin = position() + Vec3 (0, 0.25, 0);
        std::ostringstream annote;
        annote << seekerStateString << std::endl;
        annote << std::setprecision(2) << std::setiosflags(std::ios::fixed)
               << speed() << std::ends;
        draw2dTextAt3dLocation (annote, textOrigin, gWhite, drawGetWindowWidth(), drawGetWindowHeight());

        std::ostringstream status;
        status << seekerStateString << std::endl;
        status << obstacleCount << " obstacles [F1/F2]" << std::endl;
        status << resetCount << " restarts" << std::ends;
        const float h = drawGetWindowHeight ();
        const Vec3 screenLocation (10, h-50, 0);
        draw2dTextAt2dLocation (status, screenLocation, gGray80, drawGetWindowWidth(), drawGetWindowHeight());
    }

    void CtfSeeker::update (const float currentTime, const float elapsedTime)
    {

        updateState (currentTime);

        Vec3 steer (0, 0, 0);
        if (state == running)
        {
            steer = steeringForSeeker ();
        }
        else
        {
            applyBrakingForce (gBrakingRate, elapsedTime);
        }
        applySteeringForce (steer, elapsedTime);

        annotationVelocityAcceleration ();
        recordTrailVertex (currentTime, position());
    }

    int CtfBase::obstacleCount = -1; 
    SOG CtfBase::allObstacles;

    #define testOneObstacleOverlap(radius, center)               \
    {                                                            \
        float d = Vec3::distance (c, center);                    \
        float clearance = d - (r + (radius));                    \
        if (minClearance > clearance) minClearance = clearance;  \
    }

    void CtfBase::initializeObstacles (void)
    {

        if (obstacleCount == -1)
        {
            obstacleCount = 0;
            for (int i = 0; i < (maxObstacleCount * 0.4); i++) addOneObstacle ();
        }
    }

    void CtfBase::addOneObstacle (void)
    {
        if (obstacleCount < maxObstacleCount)
        {

            float r;
            Vec3 c;
            float minClearance;
            const float requiredClearance = gSeeker->radius() * 4; 
            do
            {
                r = frandom2 (1.5, 4);
                c = randomVectorOnUnitRadiusXZDisk () * gMaxStartRadius * 1.1f;
                minClearance = FLT_MAX;

                for (SOI so = allObstacles.begin(); so != allObstacles.end(); so++)
                {
                    testOneObstacleOverlap ((**so).radius, (**so).center);
                }

                testOneObstacleOverlap (gHomeBaseRadius - requiredClearance,
                                        gHomeBaseCenter);
            }
            while (minClearance < requiredClearance);

            allObstacles.push_back (new SphereObstacle (r, c));
            obstacleCount++;
        }
    }

    float CtfBase::minDistanceToObstacle (const Vec3 point)
    {
        float r = 0;
        Vec3 c = point;
        float minClearance = FLT_MAX;
        for (SOI so = allObstacles.begin(); so != allObstacles.end(); so++)
        {
            testOneObstacleOverlap ((**so).radius, (**so).center);
        }
        return minClearance;
    }

    void CtfBase::removeOneObstacle (void)
    {
        if (obstacleCount > 0)
        {
            obstacleCount--;
            allObstacles.pop_back();
        }
    }

    class CtfPlugIn : public PlugIn
    {
    public:

        const char* name (void) {return "Capture the Flag";}

        float selectionOrderSortKey (void) {return 0.01f;}

        virtual ~CtfPlugIn() {} 

        void open (void)
        {

            ctfSeeker = new CtfSeeker;
            all.push_back (ctfSeeker);

            for (int i = 0; i<ctfEnemyCount; i++)
            {
                ctfEnemies[i] = new CtfEnemy;
                all.push_back (ctfEnemies[i]);
            }

            OpenSteerDemo::init2dCamera (*ctfSeeker);
            OpenSteerDemo::camera.mode = Camera::cmFixedDistanceOffset;
            OpenSteerDemo::camera.fixedTarget.set (15, 0, 0);
            OpenSteerDemo::camera.fixedPosition.set (80, 60, 0);

            CtfBase::initializeObstacles ();
        }

        void update (const float currentTime, const float elapsedTime)
        {

            ctfSeeker->update (currentTime, elapsedTime);

            for (int i = 0; i < ctfEnemyCount; i++)
            {
                ctfEnemies[i]->update (currentTime, elapsedTime);
            }
        }

        void redraw (const float currentTime, const float elapsedTime)
        {

            AbstractVehicle* selected = OpenSteerDemo::selectedVehicle;

            AbstractVehicle* nearMouse = OpenSteerDemo::vehicleNearestToMouse ();

            OpenSteerDemo::updateCamera (currentTime, elapsedTime, selected);

            const Vec3 goalOffset = gHomeBaseCenter-OpenSteerDemo::camera.position();
            const Vec3 goalDirection = goalOffset.normalize ();
            const Vec3 cameraForward = OpenSteerDemo::camera.xxxls().forward();
            const float goalDot = cameraForward.dot (goalDirection);
            const float blend = remapIntervalClip (goalDot, 1, 0, 0.5, 0);
            const Vec3 gridCenter = interpolate (blend,
                                                 selected->         position(),
                                                 gHomeBaseCenter);
            OpenSteerDemo::gridUtility (gridCenter);

            ctfSeeker->draw();
            drawObstacles ();
            drawHomeBase();

            for (int i = 0; i < ctfEnemyCount; i++) ctfEnemies[i]->draw ();

            OpenSteerDemo::highlightVehicleUtility (nearMouse);
        }

        void close (void)
        {

            delete (ctfSeeker);
            ctfSeeker = NULL;

            for (int i = 0; i < ctfEnemyCount; i++)
            {
                delete (ctfEnemies[i]);
                ctfEnemies[i] = NULL;
            }

            all.clear();
        }

        void reset (void)
        {

            resetCount++;

            ctfSeeker->reset ();
            for (int i = 0; i<ctfEnemyCount; i++) ctfEnemies[i]->reset ();

            OpenSteerDemo::position2dCamera (*ctfSeeker);

            OpenSteerDemo::camera.doNotSmoothNextMove ();
        }

        void handleFunctionKeys (int keyNumber)
        {
            switch (keyNumber)
            {
            case 1: CtfBase::addOneObstacle ();    break;
            case 2: CtfBase::removeOneObstacle (); break;
            }
        }

        void printMiniHelpForFunctionKeys (void)
        {
            std::ostringstream message;
            message << "Function keys handled by ";
            message << '"' << name() << '"' << ':' << std::ends;
            OpenSteerDemo::printMessage (message);
            OpenSteerDemo::printMessage ("  F1     add one obstacle.");
            OpenSteerDemo::printMessage ("  F2     remove one obstacle.");
            OpenSteerDemo::printMessage ("");
        }

        const AVGroup& allVehicles (void) {return (const AVGroup&) all;}

        void drawHomeBase (void)
        {
            const Vec3 up (0, 0.01f, 0);
            const Color atColor (0.3f, 0.3f, 0.5f);
            const Color noColor = gGray50;
            const bool reached = ctfSeeker->state == CtfSeeker::atGoal;
            const Color baseColor = (reached ? atColor : noColor);
            drawXZDisk (gHomeBaseRadius,    gHomeBaseCenter, baseColor, 40);
            drawXZDisk (gHomeBaseRadius/15, gHomeBaseCenter+up, gBlack, 20);
        }

        void drawObstacles (void)
        {
            const Color color (0.8f, 0.6f, 0.4f);
            const SOG& allSO = CtfBase::allObstacles;
            for (SOI so = allSO.begin(); so != allSO.end(); so++)
            {
                drawXZCircle ((**so).radius, (**so).center, color, 40);
            }
        }

        std::vector<CtfBase*> all;
    };

    CtfPlugIn gCtfPlugIn;

} 