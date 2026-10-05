
#include <iomanip>
#include <sstream>
#include <cassert>
#include "OpenSteer/OpenSteerDemo.h"
#include "OpenSteer/SimpleVehicle.h"
#include "OpenSteer/Color.h"
#include "OpenSteer/UnusedParameter.h"

#include "OpenSteer/PolylineSegmentedPathwaySegmentRadii.h"

#include "OpenSteer/QueryPathAlike.h"

#include "OpenSteer/QueryPathAlikeUtilities.h"

#include "OpenSteer/SegmentedPathAlikeUtilities.h"

#include "OpenSteer/Utilities.h"

#include "OpenSteer/StandardTypes.h"

#define OLDTERRAINMAP
#ifndef OLDTERRAINMAP
#include "OpenSteer/TerrainMap.h"
#endif

namespace {

    using namespace OpenSteer;

    class PointToRadiusMapping : public OpenSteer::DontExtractPathDistance {
    public:
        PointToRadiusMapping(): radius( 0.0f ) {}

        void setPointOnPathCenterLine( OpenSteer::Vec3 const& ) {}
        void setPointOnPathBoundary( OpenSteer::Vec3 const&  ) {}
        void setRadius( float r ) { radius = r; }
        void setTangent( OpenSteer::Vec3 const& ) {}
        void setSegmentIndex( OpenSteer::size_t ) {}
        void setDistancePointToPath( float  ) {}
        void setDistancePointToPathCenterLine( float ) {}
        void setDistanceOnPath( float  ) {}
        void setDistanceOnSegment( float ) {}

        float radius;
    };

    class PointToTangentMapping : public OpenSteer::DontExtractPathDistance {
    public:
        PointToTangentMapping() : tangent( OpenSteer::Vec3( 0.0f, 0.0f, 0.0f ) ) {}

        void setPointOnPathCenterLine( OpenSteer::Vec3 const& ) {}
        void setPointOnPathBoundary( OpenSteer::Vec3 const&  ) {}
        void setRadius( float ) {}
        void setTangent( OpenSteer::Vec3 const& t ) { tangent = t; }
        void setSegmentIndex( OpenSteer::size_t ) {}
        void setDistancePointToPath( float  ) {}
        void setDistancePointToPathCenterLine( float ) {}
        void setDistanceOnPath( float  ) {}
        void setDistanceOnSegment( float ) {}

        OpenSteer::Vec3 tangent;
    };

    class PointToPointOnCenterLineAndOutsideMapping : public OpenSteer::DontExtractPathDistance {
    public:
        PointToPointOnCenterLineAndOutsideMapping() : pointOnPathCenterLine( OpenSteer::Vec3( 0.0f, 0.0f, 0.0f ) ), distancePointToPathBoundary( 0.0f ) {}

        void setPointOnPathCenterLine( OpenSteer::Vec3 const& point) { pointOnPathCenterLine = point; }
        void setPointOnPathBoundary( OpenSteer::Vec3 const& ) {}
        void setRadius( float ) {}
        void setTangent( OpenSteer::Vec3 const& ) {}
        void setSegmentIndex( OpenSteer::size_t ) {}
        void setDistancePointToPath( float d ) { distancePointToPathBoundary = d; }
        void setDistancePointToPathCenterLine( float ) {}
        void setDistanceOnPath( float  ) {}
        void setDistanceOnSegment( float ) {}    

        OpenSteer::Vec3 pointOnPathCenterLine;
        float distancePointToPathBoundary;
    };

    class PointToOutsideMapping : public OpenSteer::DontExtractPathDistance {
    public:
        PointToOutsideMapping() : distancePointToPathBoundary( 0.0f ) {}

        void setPointOnPathCenterLine( OpenSteer::Vec3 const& ) {}
        void setPointOnPathBoundary( OpenSteer::Vec3 const&  ) {}
        void setRadius( float ) {}
        void setTangent( OpenSteer::Vec3 const& ) {}
        void setSegmentIndex( OpenSteer::size_t ) {}
        void setDistancePointToPath( float d ) { distancePointToPathBoundary = d; }
        void setDistancePointToPathCenterLine( float ) {}
        void setDistanceOnPath( float  ) {}
        void setDistanceOnSegment( float ) {}    

        float distancePointToPathBoundary;
    };

    class PointToSegmentIndexMapping : public OpenSteer::DontExtractPathDistance {
    public:
        PointToSegmentIndexMapping() : segmentIndex( 0 ) {}

        void setPointOnPathCenterLine( OpenSteer::Vec3 const& ) {}
        void setPointOnPathBoundary( OpenSteer::Vec3 const&  ) {}
        void setRadius( float ) {}
        void setTangent( OpenSteer::Vec3 const& ) {}
        void setSegmentIndex( OpenSteer::size_t i ) { segmentIndex = i; }
        void setDistancePointToPath( float  ) {}
        void setDistancePointToPathCenterLine( float ) {}
        void setDistanceOnPath( float  ) {}
        void setDistanceOnSegment( float ) {}    

        OpenSteer::size_t segmentIndex;
    };

    float mapPointToRadius( OpenSteer::PolylineSegmentedPathwaySegmentRadii const& pathway, OpenSteer::Vec3 const& point ) {
        PointToRadiusMapping mapping;
        OpenSteer::mapPointToPathAlike( pathway, point, mapping );
        return mapping.radius;
    }

    OpenSteer::Vec3 mapPointToTangent( OpenSteer::PolylineSegmentedPathwaySegmentRadii const& pathway, OpenSteer::Vec3 const& point ) {
        PointToTangentMapping mapping;
        OpenSteer::mapPointToPathAlike( pathway, point, mapping );
        return mapping.tangent;
    }

    bool isInsidePathSegment( OpenSteer::PolylineSegmentedPathwaySegmentRadii const& pathway,  
                              OpenSteer::PolylineSegmentedPathwaySegmentRadii::size_type segmentIndex, 
                              OpenSteer::Vec3 const& point ) {
        assert( pathway.isValid() && "pathway isn't valid." );
        assert( segmentIndex < pathway.segmentCount() && "segmentIndex out of range." );

        float const segmentDistance = pathway.mapPointToSegmentDistance( segmentIndex, point );
        OpenSteer::Vec3 const pointOnSegmentCenterLine = pathway.mapSegmentDistanceToPoint( segmentIndex, segmentDistance );
        float const segmentRadiusAtPoint = pathway.mapSegmentDistanceToRadius( segmentIndex, segmentDistance );

        float const distancePointToPointOnSegmentCenterLine = (point - pointOnSegmentCenterLine).length();

        return distancePointToPointOnSegmentCenterLine < segmentRadiusAtPoint;
    }

    OpenSteer::Vec3 mapPointAndDirectionToTangent( OpenSteer::PolylineSegmentedPathwaySegmentRadii const& pathway, OpenSteer::Vec3 const& point, int direction ) {
        assert( ( ( 1 == direction ) || ( -1 == direction ) ) && "direction must be 1 or -1." );
        typedef OpenSteer::PolylineSegmentedPathwaySegmentRadii::size_type size_type;

        PointToSegmentIndexMapping mapping;
        OpenSteer::mapPointToPathAlike( pathway, point, mapping );
        size_type segmentIndex = mapping.segmentIndex;
        size_type nextSegmentIndex = segmentIndex;
        if ( 0 < direction ) {
            nextSegmentIndex = OpenSteer::nextSegment( pathway, segmentIndex );
        } else {
            nextSegmentIndex = OpenSteer::previousSegment( pathway, segmentIndex );
        }

        if ( isInsidePathSegment( pathway, nextSegmentIndex, point ) ) {
            segmentIndex = nextSegmentIndex;
        }

        return pathway.mapSegmentDistanceToTangent( segmentIndex, 0.0f ) * static_cast< float >( direction );

    }

    bool isNearWaypoint( OpenSteer::PolylineSegmentedPathwaySegmentRadii const& pathway, OpenSteer::Vec3 const& point ) {
        assert( pathway.isValid() && "pathway must be valid." );

        typedef OpenSteer::PolylineSegmentedPathwaySegmentRadii::size_type size_type;

        size_type pointIndex = 0;

        OpenSteer::Vec3 pointPathwayPointVector = point - pathway.point( pointIndex );
        float pointPathwayPointDistance = pointPathwayPointVector.dot( pointPathwayPointVector );
        if ( pointPathwayPointDistance < OpenSteer::square( pathway.segmentRadius( pointIndex ) ) ) {
            return true;
        }

        size_type const maxInnerPointIndex = pathway.pointCount() - 2;
        for ( pointIndex = 1; pointIndex <= maxInnerPointIndex; ++pointIndex ) {
            pointPathwayPointVector = point - pathway.point( pointIndex );
            pointPathwayPointDistance = pointPathwayPointVector.dot( pointPathwayPointVector );
            if ( ( pointPathwayPointDistance < OpenSteer::square( pathway.segmentRadius( pointIndex ) ) ) ||
                 ( pointPathwayPointDistance < OpenSteer::square( pathway.segmentRadius( pointIndex - 1) ) ) ) {
                return true;
            }
        }

        pointPathwayPointVector = point - pathway.point( pointIndex );
        pointPathwayPointDistance = pointPathwayPointVector.dot( pointPathwayPointVector );
        if ( pointPathwayPointDistance < OpenSteer::square( pathway.segmentRadius( pointIndex - 1 ) ) ) {
            return true;
        }

        return false;

    }

    OpenSteer::Vec3 mapPointToPointOnCenterLineAndOutside( OpenSteer::PolylineSegmentedPathwaySegmentRadii const& pathway, OpenSteer::Vec3 const& point, float& outside ) {
        PointToPointOnCenterLineAndOutsideMapping mapping;
        OpenSteer::mapPointToPathAlike( pathway, point, mapping );
        outside = mapping.distancePointToPathBoundary;
        return mapping.pointOnPathCenterLine;
    }

    float mapPointToOutside( OpenSteer::PolylineSegmentedPathwaySegmentRadii const& pathway, OpenSteer::Vec3 const& point ) {
        PointToOutsideMapping mapping;
        OpenSteer::mapPointToPathAlike( pathway, point, mapping);
        return mapping.distancePointToPathBoundary;    
    }

    bool isInsidePathway( OpenSteer::PolylineSegmentedPathwaySegmentRadii const& pathway, OpenSteer::Vec3 const& point ) {
        return 0.0f > mapPointToOutside( pathway, point );
    }

    OpenSteer::PolylineSegmentedPathwaySegmentRadii::size_type mapPointToSegmentIndex(  OpenSteer::PolylineSegmentedPathwaySegmentRadii const& pathway, 
                                                                                        OpenSteer::Vec3 const& point ) {
        PointToSegmentIndexMapping mapping;
        OpenSteer::mapPointToPathAlike( pathway, point, mapping );
        return mapping.segmentIndex;
    }

    #ifdef OLDTERRAINMAP

    class TerrainMap
    {
    public:

        TerrainMap (const Vec3& c, float x, float z, int r)
            : center(c),
              xSize(x),
              zSize(z),
              resolution(r),
              outsideValue (false),
			  map(resolution * resolution)
        {
            map.reserve (resolution * resolution);
        }

        ~TerrainMap ()
        {
        }

        void clear (void)
        {
            for (int i = 0; i < resolution; i++)
                for (int j = 0; j < resolution; j++)
                    setMapBit (i, j, 0);
        }

        bool getMapBit (int i, int j) const
        {
            return map[mapAddress(i, j)];
        }

        bool setMapBit (int i, int j, bool value)
        {
            return map[mapAddress(i, j)] = value;
        }

        bool getMapValue (const Vec3& point) const
        {
            const Vec3 local = point - center;
            const Vec3 localXZ = local.setYtoZero();

            const float hxs = xSize/2;
            const float hzs = zSize/2;

            const float x = localXZ.x;
            const float z = localXZ.z;

            const bool out = (x > +hxs) || (x < -hxs) || (z > +hzs) || (z < -hzs);

            if (out) 
            {
                return outsideValue;
            }
            else
            {
                const float r = (float) resolution; 
                const int i = (int) remapInterval (x, -hxs, hxs, 0.0f, r);
                const int j = (int) remapInterval (z, -hzs, hzs, 0.0f, r);
                return getMapBit (i, j);
            }
        }

        void xxxDrawMap (void)
        {
            const float xs = xSize/(float)resolution;
            const float zs = zSize/(float)resolution;
            const Vec3 alongRow (xs, 0, 0);
            const Vec3 nextRow (-xSize, 0, zs);
            Vec3 g ((xSize - xs) / -2, 0, (zSize - zs) / -2);
            g += center;
            for (int j = 0; j < resolution; j++)
            {
                for (int i = 0; i < resolution; i++)
                {
                    if (getMapBit (i, j))
                    {

                        const float rockHeight = 0;
                        const Vec3 v1 (+xs/2, rockHeight, +zs/2);
                        const Vec3 v2 (+xs/2, rockHeight, -zs/2);
                        const Vec3 v3 (-xs/2, rockHeight, -zs/2);
                        const Vec3 v4 (-xs/2, rockHeight, +zs/2);

                        const Color orangeRockColor (0.5f, 0.2f, 0.0f);
                        drawQuadrangle (g+v1, g+v2, g+v3, g+v4, orangeRockColor);

                    } 
                    g += alongRow;
                }
                g += nextRow;
            }
        }

        float minSpacing (void) const
        {
            return minXXX (xSize, zSize) / (float)resolution;
        }

        bool scanLocalXZRectangle (const AbstractLocalSpace& localSpace,
                                   float xMin, float xMax,
                                   float zMin, float zMax) const
        {
            const float spacing = minSpacing() / 2;

            for (float x = xMin; x < xMax; x += spacing)
            {
                for (float z = zMin; z < zMax; z += spacing)
                {
                    const Vec3 sample (x, 0, z);
                    const Vec3 global = localSpace.globalizePosition (sample);
                    if (getMapValue (global)) return true;
                }
            }
            return false;
        }

        int scanXZray (const Vec3& origin,
                       const Vec3& sampleSpacing,
                       const int sampleCount) const
        {
            Vec3 samplePoint (origin);

            for (int i = 1; i <= sampleCount; i++)
            {
                samplePoint += sampleSpacing;
                if (getMapValue (samplePoint)) return i;
            }

            return 0;
        }

        int cellwidth (void) const {return resolution;}  
        int cellheight (void) const {return resolution;}  
        bool isPassable (const Vec3& point) const {return ! getMapValue (point);}

        Vec3 center;
        float xSize;
        float zSize;
        int resolution;

        bool outsideValue;

    private:

        int mapAddress (int i, int j) const {return i + (j * resolution);}

        std::vector<bool> map;
    };
    #endif

    typedef PolylineSegmentedPathwaySegmentRadii GCRoute;

    class MapDriver : public SimpleVehicle
    {
    public:

        MapDriver () : map (makeMap ()), path (makePath ())
        {
            reset ();

            sumOfCollisionFreeTimes = 0;
            countOfCollisionFreeTimes = 0;

            collisionLastTime = false;
            timeOfLastCollision = OpenSteerDemo::clock.getTotalSimulationTime ();

            totalDistance = 0;
            totalTime = 0;

            pathFollowTime = 0;
            pathFollowOffTime = 0;

            stuckCount = 0;
            stuckCycleCount = 0;
            stuckOffPathCount = 0;
            lapsStarted = 0;
            lapsFinished = 0;
            hintGivenCount = 0;
            hintTakenCount = 0;

            pathFollowDirection = 1;

            curvedSteering = true;
            incrementalSteering = true;

            setTrailParameters (10, 200);
        }

        ~MapDriver ()
        {
            delete (map);
            delete (path);
        }

        void reset (void)
        {

            SimpleVehicle::reset ();

            setSpeed (0);

            setMaxSpeed (20);

            setMaxForce (maxSpeed () * 0.4f);

            halfWidth = 1.0f;
            halfLength = 1.5f;

            adjustVehicleRadiusForSpeed ();

            annotateAvoid = Vec3::zero;

            clearTrailHistory ();

            stuck = false;

            qqqLastNearestObstacle = Vec3::zero;

            baseLookAheadTime = 3;

            if (demoSelect == 2)
            {
                lapsStarted++;
                const float s = worldSize;
                const float d = (float) pathFollowDirection;
                setPosition (Vec3 (s * d * 0.6f, 0, s * -0.4f));
                regenerateOrthonormalBasisUF (Vec3::side * d);
            }

            resetStuckCycleDetection ();

            currentSteering = Vec3::zero;

            dtZero = false;

            QQQoaJustScraping = false;

        }

        void update (const float currentTime, const float elapsedTime)
        {

            dtZero = (elapsedTime == 0);

            adjustVehicleRadiusForSpeed ();

            annoteMaxRelSpeed = annoteMaxRelSpeedCurve = annoteMaxRelSpeedPath = 1;

            Vec3 steering;
            const bool offPath = !bodyInsidePath ();
            if (stuck || offPath || detectImminentCollision ())
            {

                applyBrakingForce ((curvedSteering?3.0f:2.0f), elapsedTime); 

                if (offPath && !stuck && (demoSelect == 2)) stuckOffPathCount++;
                stuck = true;

                resetSmoothedAcceleration ();
                currentSteering = Vec3::zero;
            }
            else
            {

                const Vec3 avoid = annotateAvoid = 
                    steerToAvoidObstaclesOnMap (lookAheadTimeOA (),
                                                *map,
                                                hintForObstacleAvoidance ());
                const bool needToAvoid = avoid != Vec3::zero;

                if (needToAvoid)
                {

                    const float targetSpeed =((curvedSteering && QQQoaJustScraping)
                                              ? maxSpeedForCurvature () : 0);
                    annoteMaxRelSpeed = targetSpeed / maxSpeed ();
                    const float avoidWeight = 3 + (3 * relativeSpeed ()); 
                    steering = avoid * avoidWeight;
                    steering += steerForTargetSpeed (targetSpeed);
                }
                else
                {

                    steering = steerForTargetSpeed (maxSpeedForCurvature ());

                    if (demoSelect == 1)
                    {
                        const Vec3 wander = steerForWander (elapsedTime);
                        const Vec3 flat = wander.setYtoZero ();
                        const Vec3 weighted = flat.truncateLength (maxForce()) * 6;
                        const Vec3 a = position() + Vec3 (0, 0.2f, 0);
                        annotationLine (a, a + (weighted * 0.3f), gWhite);
                        steering += weighted;
                    }

                    if (demoSelect == 2)
                    {
                        const Vec3 pf = steerToFollowPath (pathFollowDirection,
                                                           lookAheadTimePF (),
                                                           *path);
                        if (pf != Vec3::zero)
                        {

                            if (pf.dot (forward()) < 0)
                                steering = pf;
                            else
                                steering = pf + steering;
                        }
                        else
                        {

                            const Vec3 pathHeading = mapPointAndDirectionToTangent( *path, position(), pathFollowDirection ); 
                            {
                                const Vec3 b = (position () +
                                                (up () * 0.2f) +
                                                (forward () * halfLength * 1.4f));
                                const float l = 2;
                                annotationLine (b, b + (forward ()  * l), gCyan);
                                annotationLine (b, b + (pathHeading * l), gCyan);
                            }
                            steering += (steerTowardHeading(pathHeading) *
                                         ( isNearWaypoint( *path, position() )  ?
                                          0.5f : 0.1f));
                        }
                    }
                }
            }

            if (!stuck)
            {

                if (incrementalSteering)
                    steering = convertAbsoluteToIncrementalSteering (steering,
                                                                     elapsedTime);

                steering = adjustSteeringForMinimumTurningRadius (steering);
            }

            applySteeringForce (steering, elapsedTime);
            collectReliabilityStatistics (currentTime, elapsedTime);

            if (demoSelect == 2)
            {
                const bool circles = weAreGoingInCircles ();
                if (circles && !stuck) stuckCycleCount++;
                if (circles) stuck = true;
                annotationCircleOrDisk (0.5, up(), smoothedPosition (),
                                        gWhite, 12, circles, false);
            }

            perFrameAnnotation ();
            recordTrailVertex (currentTime, position());
        }

        void adjustVehicleRadiusForSpeed (void)
        {
            const float minRadius = sqrtXXX(square(halfWidth)+square(halfLength));
            const float safetyMargin = (curvedSteering ?
                                        interpolate (relativeSpeed(), 0.0f, 1.5f) :
                                        0.0f);
            setRadius (minRadius + safetyMargin);
        }

        void collectReliabilityStatistics (const float currentTime,
                                           const float elapsedTime)
        {

            collisionDetected = map->scanLocalXZRectangle (*this,
                                                           -halfWidth, halfWidth,
                                                           -halfLength,halfLength);

            const float timeSinceLastCollision = currentTime - timeOfLastCollision;
            if (collisionDetected &&
                !collisionLastTime &&
                (timeSinceLastCollision > 1))
            {
                std::ostringstream message;
                message << "collision after "<<timeSinceLastCollision<<" seconds";
                OpenSteerDemo::printMessage (message);
                sumOfCollisionFreeTimes += timeSinceLastCollision;
                countOfCollisionFreeTimes++;
                timeOfLastCollision = currentTime;
            }
            collisionLastTime = collisionDetected;

            totalDistance += speed () * elapsedTime;
            totalTime += elapsedTime;

            if (demoSelect == 2)
            {
                pathFollowTime += elapsedTime;
                if (! bodyInsidePath ()) pathFollowOffTime += elapsedTime;
            }
        }

        Vec3 hintForObstacleAvoidance (void)
        {

            if (demoSelect != 2) return Vec3::zero;

            const Vec3 p = position ();
            const Vec3 pathHeading = mapPointAndDirectionToTangent( *path, p, pathFollowDirection ); 
            if (pathHeading.dot (forward ()) < 0.8f)
            {

                const Vec3 s = side () * halfWidth;
                const float f = halfLength * 2;
                annotationLine (p + s, p + s + (forward () * f), gBlack);
                annotationLine (p - s, p - s + (forward () * f), gBlack);
                annotationLine (p, p + (pathHeading * 5), gMagenta);
                return pathHeading;
            }
            else
            {

                const Vec3 obstacle = qqqLastNearestObstacle;
                const Vec3 o = obstacle + (up () * 0.1f);
                if (obstacle != Vec3::zero)
                {

                    float outside;
                    const Vec3 onPath = mapPointToPointOnCenterLineAndOutside( *path, obstacle, outside );
                    const Vec3 offset = onPath - obstacle;
                    const float offsetDistance = offset.length();

                    if (outside < 0)
                    {

                        float const segmentRadius = mapPointToRadius( *path, onPath );
                        const float w = halfWidth * 6;
                        const bool nearEdge = offsetDistance > w;
                        const bool wideEnough = segmentRadius > (w * 2);
                        if (nearEdge && wideEnough)
                        {
                            const float obstacleDistance = (obstacle - p).length();
                            const float range = speed () * lookAheadTimeOA ();
                            const float farThreshold = range * 0.8f;
                            const bool usableHint = obstacleDistance>farThreshold;
                            if (usableHint)
                            {
                                const Vec3 q = p + (offset.normalize() * 5);
                                annotationLine (p, q, gMagenta);
                                annotationCircleOrDisk (0.4f, up(), o, gWhite,
                                                        12, false, false);
                                return offset;
                            }
                        }
                    }
                    annotationCircleOrDisk (0.4f, up(), o, gBlack, 12,false,false);
                }
            }

            return Vec3::zero;
        }

        Vec3 steerToAvoidObstaclesOnMap (const float minTimeToCollision,
                                         const TerrainMap& map)
        {
            return steerToAvoidObstaclesOnMap (minTimeToCollision,
                                               map,
                                               Vec3::zero); 
        }

        Vec3 steerToAvoidObstaclesOnMap (const float minTimeToCollision,
                                         const TerrainMap& map,
                                         const Vec3& steerHint)
        {
            const float spacing = map.minSpacing() / 2;
            const float maxSide = radius();
            const float maxForward = minTimeToCollision * speed();
            const int maxSamples = (int) (maxForward / spacing);
            const Vec3 step = forward () * spacing;
            const Vec3 fOffset = position ();
            Vec3 sOffset;
            float s = spacing / 2;

            const int infinity = 9999; 
            int nearestL = infinity;
            int nearestR = infinity;
            int nearestWL = infinity;
            int nearestWR = infinity;
            Vec3 nearestO;
            wingDrawFlagL = false;
            wingDrawFlagR = false;

            const bool hintGiven = steerHint != Vec3::zero;
            if (hintGiven && !dtZero) hintGivenCount++;
            if (hintGiven) annotationCircleOrDisk (halfWidth * 0.9f, up(),
                                                   position () + (up () * 0.2f),
                                                   gWhite, 12, false, false);

            QQQoaJustScraping = true;

            const float signedRadius = 1 / nonZeroCurvatureQQQ ();
            const Vec3 localCenterOfCurvature = side () * signedRadius;
            const Vec3 center = position () + localCenterOfCurvature;
            const float sign = signedRadius < 0 ? 1.0f : -1.0f;
            const float arcRadius = signedRadius * -sign;
            const float twoPi = 2 * OPENSTEER_M_PI;
            const float circumference = twoPi * arcRadius;
            const float rawLength = speed() * minTimeToCollision * sign;
            const float fracLimit = 1.0f / 6.0f;
            const float distLimit = circumference * fracLimit;
            const float arcLength = arcLengthLimit (rawLength, distLimit);
            const float arcAngle = twoPi * arcLength / circumference;

            if (curvedSteering)
            {
                if ((speed() * minTimeToCollision) > (circumference * fracLimit))
                {
                    const float q = twoPi * fracLimit;
                    const Vec3 fooz = position () - center;
                    const Vec3 booz = fooz.rotateAboutGlobalY (sign * q);
                    annotationLine (center, center + fooz, gRed);
                    annotationLine (center, center + booz, gRed);
                }
            }

        assert (spacing > 0);

            while (s < maxSide)
            {
                sOffset = side() * s;
                s += spacing;
                const Vec3 lOffset = fOffset + sOffset;
                const Vec3 rOffset = fOffset - sOffset;

                Vec3 lObsPos, rObsPos;

                const int L = (curvedSteering ? 
                               (int) (scanObstacleMap (lOffset,
                                                       center,
                                                       arcAngle,
                                                       maxSamples,
                                                       0,
                                                       gYellow,
                                                       gRed,
                                                       lObsPos)
                                      / spacing) :
                               map.scanXZray (lOffset, step, maxSamples));
                const int R = (curvedSteering ? 
                               (int) (scanObstacleMap (rOffset,
                                                        center,
                                                       arcAngle,
                                                       maxSamples,
                                                       0,
                                                       gYellow,
                                                       gRed,
                                                       rObsPos)
                                      / spacing) :
                               map.scanXZray (rOffset, step, maxSamples));

                if ((L > 0) && (L < nearestL))
                {
                    nearestL = L;
                    if (L < nearestR) nearestO = ((curvedSteering) ?
                                                  lObsPos :
                                                  lOffset + ((float)L * step));
                }
                if ((R > 0) && (R < nearestR))
                {
                    nearestR = R;
                    if (R < nearestL) nearestO = ((curvedSteering) ?
                                                  rObsPos :
                                                  rOffset + ((float)R * step));
                }

                if (!curvedSteering)
                {
                    annotateAvoidObstaclesOnMap (lOffset, L, step);
                    annotateAvoidObstaclesOnMap (rOffset, R, step);
                }

                if (curvedSteering)
                {

                    const bool outermost = s >= maxSide;
                    const bool eitherSide = (L > 0) || (R > 0);
                    if (!outermost && eitherSide) QQQoaJustScraping = false;
                }
            }
            qqqLastNearestObstacle = nearestO;

            {
                const int wingScans = 4;

                const Vec3 wingWidth = side() * wingSlope () * maxForward;

                const Color beforeColor (0.75f, 0.9f, 0.0f);  
                const Color afterColor  (0.9f,  0.5f, 0.0f);  

                for (int i=1; i<=wingScans; i++)
                {
                    const float fraction = (float)i / (float)wingScans;
                    const Vec3 endside = sOffset + (wingWidth * fraction);
                    const Vec3 corridorFront = forward() * maxForward;

                    for (int j = -1; j < 2; j+=2)
                    {
                        float k = (float)j; 
                        const Vec3 start = fOffset + (sOffset * k);
                        const Vec3 end = fOffset + corridorFront + (endside * k);
                        const Vec3 ray = end - start;
                        const float rayLength = ray.length();
                        const Vec3 step = ray * spacing / rayLength;
                        const int raySamples = (int) (rayLength / spacing);
                        const float endRadius =
                            wingSlope () * maxForward * fraction *
                            (signedRadius < 0 ? 1 : -1) * (j==1?1:-1);
                        Vec3 ignore;
                        const int scan = (curvedSteering ?
                                          (int) (scanObstacleMap (start,
                                                                  center,
                                                                  arcAngle,
                                                                  raySamples,
                                                                  endRadius,
                                                                  beforeColor,
                                                                  afterColor,
                                                                  ignore)
                                                 / spacing) :
                                          map.scanXZray (start, step, raySamples));

                        if (!curvedSteering)
                            annotateAvoidObstaclesOnMap (start,scan,step);

                        if (j==1) 
                        {
                            if ((scan > 0) && (scan < nearestWL)) nearestWL = scan;
                        }
                        else
                        {
                            if ((scan > 0) && (scan < nearestWR)) nearestWR = scan;
                        }
                    }
                }
                wingDrawFlagL = nearestWL != infinity;
                wingDrawFlagR = nearestWR != infinity;
            }

            savedNearestWR = (float) nearestWR;
            savedNearestR  = (float) nearestR;
            savedNearestL  = (float) nearestL;
            savedNearestWL = (float) nearestWL;

            const bool obstacleFreeC  = nearestL==infinity && nearestR==infinity;
            const bool obstacleFreeL  = nearestL==infinity && nearestWL==infinity;
            const bool obstacleFreeR  = nearestR==infinity && nearestWR==infinity;
            const bool obstacleFreeWL = nearestWL==infinity;
            const bool obstacleFreeWR = nearestWR==infinity;
            const bool obstacleFreeW  = obstacleFreeWL && obstacleFreeWR;

            const bool JS = curvedSteering && QQQoaJustScraping;
            const bool cancelJS = !obstacleFreeWL && !obstacleFreeWR;
            if (JS && cancelJS) QQQoaJustScraping = false;

            if (obstacleFreeC)
            {
                qqqLastNearestObstacle = Vec3::zero;
                annotationNoteOAClauseName ("obstacleFreeC");

                if (obstacleFreeWL || obstacleFreeWR || relativeSpeed () < 0.7f)
                    return Vec3::zero;
                else
                    return -forward ();
            }

            if (hintGiven && (minXXX ((float)nearestL, (float)nearestR) >
                              (maxSamples * 0.8f)))
            {
                annotationNoteOAClauseName ("nearest obstacle is way out there");
                annotationHintWasTaken ();
                if (steerHint.dot(side())>0) return side();else return -side();
            }

            const float maxCurvature = 1 / (minimumTurningRadius () * 1.2f);
            if (absXXX (curvature ()) > maxCurvature)
            {
                annotationNoteOAClauseName ("min turn radius");
                annotationCircleOrDisk (minimumTurningRadius () * 1.2f, up(),
                                        center, gBlue * 0.8f, 40, false, false);
                return side () * sign;
            }

            if (obstacleFreeL || obstacleFreeR)
                annotationNoteOAClauseName ("obstacle-free side");

            if (obstacleFreeL) return side();
            if (obstacleFreeR) return -side();

            if (obstacleFreeW)
            {
                annotationNoteOAClauseName ("obstacleFreeW");

                const bool same = absXXX (nearestL - nearestR) < 5; 

                if (same && hintGiven)
                {
                    annotationHintWasTaken ();
                    if (steerHint.dot(side())>0) return side();else return -side();
                }
                else
                {

                    if (nearestL > nearestR) return side(); else return -side();
                }
            }

            const bool equallyClear = absXXX (nearestWL-nearestWR) < 2; 
            if (equallyClear && hintGiven)
            {
                annotationNoteOAClauseName ("equallyClear");
                annotationHintWasTaken ();
                if (steerHint.dot(side()) > 0) return side(); else return -side();
            }

            annotationNoteOAClauseName ("wing less cluttered");
            if (nearestWL > nearestWR) return side(); else return -side();
        }

        void annotateAvoidObstaclesOnMap (const Vec3& scanOrigin,
                                          int scanIndex,
                                          const Vec3& scanStep)
        {
            if (scanIndex > 0)
            {
                const Vec3 hit = scanOrigin + (scanStep * (float) scanIndex);
                annotationLine (scanOrigin, hit, Color (0.7f, 0.3f, 0.3f));
            }
        }

        void annotationNoteOAClauseName (const char* clauseName)
        {
            OPENSTEER_UNUSED_PARAMETER(clauseName);

        }

        void annotationHintWasTaken (void)
        {
            if (!dtZero) hintTakenCount++;

            const float r = halfWidth * 0.9f;
            const Vec3 ff = forward () * r;
            const Vec3 ss = side () * r;
            const Vec3 pp = position () + (up () * 0.2f);
            annotationLine (pp + ff + ss, pp - ff + ss, gWhite);
            annotationLine (pp - ff - ss, pp - ff + ss, gWhite);
            annotationLine (pp - ff - ss, pp + ff - ss, gWhite);
            annotationLine (pp + ff + ss, pp + ff - ss, gWhite);

        }

        float scanObstacleMap (const Vec3& start,
                               const Vec3& center,
                               const float arcAngle,
                               const int segments,
                               const float endRadiusChange,
                               const Color& beforeColor,
                               const Color& afterColor,
                               Vec3& returnObstaclePosition)
        {

            Vec3 spoke = start - center;

            const float step = arcAngle / segments;

            float obstacleDistance = 0;
            returnObstaclePosition = Vec3::zero;

            const float startRadius = (endRadiusChange == 0) ? 0 : spoke.length(); 

            float sin=0, cos=0;
            Vec3 oldPoint = start;
            bool obstacleFound = false;
            for (int i = 0; i < segments; i++)
            {

                spoke = spoke.rotateAboutGlobalY (step, sin, cos);

                const float adjust = ((endRadiusChange == 0) ?
                                      1.0f :
                                      interpolate ((float)(i+1) / (float)segments,
                                                   1.0f,
                                                   (maxXXX (0,
                                                            (startRadius +
                                                             endRadiusChange))
                                                    / startRadius)));

                const Vec3 newPoint = center + (spoke * adjust);

                if (obstacleFound)
                {
                    annotationLine (oldPoint, newPoint, afterColor);
                }
                else
                {

                    const Vec3 offset = newPoint - oldPoint;
                    const float d2 = offset.length() * 2;

                    if (! map->isPassable (newPoint))
                    {
                        obstacleFound = true;
                        obstacleDistance = d2 * 0.5f * (i+1);
                        returnObstaclePosition = newPoint;
                    }
                    annotationLine (oldPoint, newPoint, beforeColor);
                }

                oldPoint = newPoint;
            }

            return obstacleDistance;
        }

        bool detectImminentCollision (void)
        {

            bool returnFlag = false;
            const float spacing = map->minSpacing() / 2;
            const float maxSide = halfWidth + spacing;
            const float minDistance = curvedSteering ? 2.0f : 2.5f; 
            const float predictTime = curvedSteering ? .75f : 1.3f; 
            const float maxForward =
                speed () * combinedLookAheadTime (predictTime, minDistance);
            const Vec3 step = forward () * spacing;
            float s = curvedSteering ? (spacing / 4) : (spacing / 2);

            const float signedRadius = 1 / nonZeroCurvatureQQQ ();
            const Vec3 localCenterOfCurvature = side () * signedRadius;
            const Vec3 center = position () + localCenterOfCurvature;
            const float sign = signedRadius < 0 ? 1.0f : -1.0f;
            const float arcRadius = signedRadius * -sign;
            const float twoPi = 2 * OPENSTEER_M_PI;
            const float circumference = twoPi * arcRadius;
            const Vec3 qqqLift (0, 0.2f, 0);
            Vec3 ignore;

            while (s < maxSide)
            {
                const Vec3 sOffset = side() * s;
                const Vec3 lOffset = position () + sOffset;
                const Vec3 rOffset = position () - sOffset;
                const float bevel = 0.3f;
                const float fraction = s / maxSide;
                const float scanDist = (halfLength +
                                        interpolate (fraction,
                                                     maxForward,
                                                     maxForward * bevel));
                const float angle = (scanDist * twoPi * sign) / circumference;
                const int samples = (int) (scanDist / spacing);
                const int L = (curvedSteering ?
                               (int) (scanObstacleMap (lOffset + qqqLift,
                                                       center,
                                                       angle,
                                                       samples,
                                                       0,
                                                       gMagenta,
                                                       gCyan,
                                                       ignore)
                                      / spacing) :
                               map->scanXZray (lOffset, step, samples));
                const int R = (curvedSteering ?
                               (int) (scanObstacleMap (rOffset + qqqLift,
                                                       center,
                                                       angle,
                                                       samples,
                                                       0,
                                                       gMagenta,
                                                       gCyan,
                                                       ignore)
                                      / spacing) :
                               map->scanXZray (rOffset, step, samples));

                returnFlag = returnFlag || (L > 0);
                returnFlag = returnFlag || (R > 0);

                if (! curvedSteering)
                {
                    const Vec3 d (step * (float) samples);
                    annotationLine (lOffset, lOffset + d, gWhite);
                    annotationLine (rOffset, rOffset + d, gWhite);
                }

                s += spacing;
            }
            return returnFlag;
        }

        Vec3 predictFuturePosition (const float predictionTime) const
        {
            if (curvedSteering)
            {

                const float signedRadius = 1 / nonZeroCurvatureQQQ ();
                const Vec3 localCenterOfCurvature = side () * signedRadius;
                const Vec3 center = position () + localCenterOfCurvature;
                const float sign = signedRadius < 0 ? 1.0f : -1.0f;
                const float arcRadius = signedRadius * -sign;
                const float twoPi = 2 * OPENSTEER_M_PI;
                const float circumference = twoPi * arcRadius;
                const float rawLength = speed() * predictionTime * sign;
                const float arcLength = arcLengthLimit (rawLength,
                                                        circumference * 0.25f);
                const float arcAngle = twoPi * arcLength / circumference;

                const Vec3 spoke = position () - center;
                const Vec3 newSpoke = spoke.rotateAboutGlobalY (arcAngle);
                const Vec3 prediction = newSpoke + center;

                const Color futurePositionColor (0.5f, 0.5f, 0.6f);
                annotationXZArc (position (), center, arcLength, 20, 
                                 futurePositionColor);
                return prediction;
            }
            else
            {
                return position() + (velocity() * predictionTime);
            }
        }

        float arcLengthLimit (const float length, const float limit) const
        {
            if (length > 0)
                return minXXX (length, limit);
            else
                return -minXXX (-length, limit);
        }

        Vec3 steerToFollowPath (const int direction,
                                const float predictionTime,
                                GCRoute& path)
        {
            if (curvedSteering)
                return steerToFollowPathCurve (direction, predictionTime, path);
            else
                return steerToFollowPathLinear (direction, predictionTime, path);
        }

        Vec3 steerToFollowPathLinear (const int direction,
                                      const float predictionTime,
                                      GCRoute& path)
        {

            const float pathDistanceOffset = direction * predictionTime * speed();

            const Vec3 futurePosition = predictFuturePosition (predictionTime);

            const float nowPathDistance =
                path.mapPointToPathDistance (position ());

            const Vec3 pathHeading = mapPointToTangent( path, position() ) * static_cast< float >( direction );
            const bool correctDirection = pathHeading.dot (forward ()) > 0;

            float futureOutside;
            const Vec3 onPath = mapPointToPointOnCenterLineAndOutside( path, futurePosition, futureOutside ); 

            float nowOutside;
            const Vec3 nowOnPath = mapPointToPointOnCenterLineAndOutside( path, position(), nowOutside );  

            const float m = -radius ();
            const bool whollyInside = (futureOutside < m) && (nowOutside < m);
            if (whollyInside && correctDirection)
            {

                return Vec3::zero;
            }
            else
            {

                const float targetPathDistance = (nowPathDistance + 
                                                  (pathDistanceOffset *
                                                   (correctDirection ? 1 : 0.1f)));
                Vec3 target = path.mapPathDistanceToPoint (targetPathDistance);

                const int ip =  static_cast< int >( mapPointToSegmentIndex( path, position() ) ); 
                const int it =  static_cast< int >( mapPointToSegmentIndex( path, target ) ); 

                Vec3 const ipTangent = path.mapSegmentDistanceToTangent( ip, 0.0f );

                Vec3 const itTangent = path.mapSegmentDistanceToTangent( it, 0.0f );
                if (((ip + direction) == it) &&
                    (   itTangent.dot( ipTangent ) < -0.1f ) )
                {
                    const float newTargetPathDistance =
                        nowPathDistance + (pathDistanceOffset * 2);
                    target = path.mapPathDistanceToPoint (newTargetPathDistance);
                }

                annotatePathFollowing (futurePosition,onPath,target,futureOutside);

                if (nowOutside > 0) return steerForSeek (nowOnPath);

                const Vec3 seek = steerForSeek (target).truncateLength(maxForce());

                if ((nowOutside < 0) && (futureOutside > 0))
                    return (seek.perpendicularComponent (forward ()) -
                            (forward () * maxForce ()));
                else
                    return seek;
            }
        }

        Vec3 steerToFollowPathCurve (const int direction,
                                     const float predictionTime,
                                     GCRoute& path)
        {

            const Vec3 futurePosition = predictFuturePosition (predictionTime);

            float futureOutside;
            const Vec3 onPath =  mapPointToPointOnCenterLineAndOutside( path, futurePosition, futureOutside ); 
            const Vec3 pathHeading =  mapPointAndDirectionToTangent( path, onPath, direction ); 
            const Vec3 rawBraking = forward () * maxForce () * -1;
            const Vec3 braking = ((futureOutside < 0) ? Vec3::zero : rawBraking);

            float nowOutside;
            Vec3 nowTangent;
            const Vec3 p = position ();
            const Vec3 nowOnPath = path.mapPointToPath (p, nowTangent, nowOutside);
            nowTangent *= (float)direction;
            const float alignedness = nowTangent.dot (forward ());

            if (alignedness < 0)
            {
                annotationLine (p, p + (nowTangent * 10), gCyan);

                if (alignedness < -0.707f)
                {
                    const Vec3 towardCenter = nowOnPath - p;
                    const Vec3 turn = (towardCenter.dot (side ()) > 0 ?
                                       side () * maxForce () :
                                       side () * maxForce () * -1);
                    return (turn + rawBraking);
                }
                else
                {
                    return (steerTowardHeading(pathHeading).
                            perpendicularComponent(forward()) + braking);
                }
            }

            if (futureOutside < -(radius () + 1.0f)) 
            {

                return Vec3::zero;
            }
            else
            {

                annotationLine (futurePosition, futurePosition+pathHeading, gRed);
                annotatePathFollowing (futurePosition, onPath,
                                       position(), futureOutside);

                if (  isNearWaypoint( path, onPath )  && (futureOutside > 0))
                {

                    annotationCircleOrDisk (0.5f, up(), futurePosition,
                                            gRed, 8, false, false);
                    return steerTowardHeading (pathHeading) + braking;
                }
                else
                {

                    const Vec3 pathSide = localRotateForwardToSide (pathHeading);
                    const Vec3 towardFP = futurePosition - onPath;
                    const float whichSide = (pathSide.dot(towardFP)<0)?1.0f :-1.0f;
                    return (side () * maxForce () * whichSide) + braking;
                }
            }
        }

        void perFrameAnnotation (void)
        {
            const Vec3 p = position();

            annotationCircleOrDisk (radius(), up(), p, gBlack, 32, false, false);

            if (!curvedSteering)
            {
                const float corLength = speed() * lookAheadTimeOA ();
                if (corLength > halfLength)
                {
                    const Vec3 corFront = forward() * corLength;
                    const Vec3 corBack = Vec3::zero; 
                    const Vec3 corSide  = side() * radius();
                    const Vec3 c1 = p + corSide + corBack;
                    const Vec3 c2 = p + corSide + corFront;
                    const Vec3 c3 = p - corSide + corFront;
                    const Vec3 c4 = p - corSide + corBack;
                    const Color color = ((annotateAvoid!=Vec3::zero)?gRed:gYellow);
                    annotationLine (c1, c2, color);
                    annotationLine (c2, c3, color);
                    annotationLine (c3, c4, color);

                    const Vec3 wingWidth = side () * wingSlope () * corLength;
                    const Vec3 wingTipL = c2 + wingWidth;
                    const Vec3 wingTipR = c3 - wingWidth;
                    const Color wingColor (gOrange);
                    if (wingDrawFlagL) annotationLine (c2, wingTipL, wingColor);
                    if (wingDrawFlagL) annotationLine (c1, wingTipL, wingColor);
                    if (wingDrawFlagR) annotationLine (c3, wingTipR, wingColor);
                    if (wingDrawFlagR) annotationLine (c4, wingTipR, wingColor);
                }
            }

            const Vec3 above = position () + Vec3 (0, 0.2f, 0);
            const Vec3 accel = smoothedAcceleration () * 5 / maxForce ();
            const Color aColor (0.4f, 0.4f, 0.8f);
            annotationLine (above, above + accel, aColor);
        }

        void draw (void)
        {

            Color                     bodyColor( gBlack );
            if (stuck)               bodyColor = gYellow;
            if (! bodyInsidePath ()) bodyColor = gOrange;
            if (collisionDetected)   bodyColor = gRed;

            const Vec3 p = position();
            const Vec3 bbSide = side() * halfWidth;
            const Vec3 bbFront = forward() * halfLength;
            const Vec3 bbHeight (0, 0.1f, 0);
            drawQuadrangle (p - bbFront + bbSide + bbHeight,
                            p + bbFront + bbSide + bbHeight,
                            p + bbFront - bbSide + bbHeight,
                            p - bbFront - bbSide + bbHeight,
                            bodyColor);

            const Color darkGreen (0, 0.6f, 0);
            drawTrail (darkGreen, gBlack);
        }

        void annotatePathFollowing (const Vec3& future,
                                    const Vec3& onPath,
                                    const Vec3& target,
                                    const float outside)
        {
            const Color toTargetColor (gGreen * 0.6f);
            const Color insidePathColor (gCyan * 0.6f);
            const Color outsidePathColor (gBlue * 0.6f);
            const Color futurePositionColor (0.5f, 0.5f, 0.6f);

            if (!curvedSteering)
                annotationLine (position(), future, futurePositionColor);

            annotationLine (position(), target, toTargetColor);

            const float o = outside + radius () + (curvedSteering ? 1.0f : 0.0f);
            const Vec3 boundaryOffset = ((onPath - future).normalize() * o);

            const Vec3 onPathBoundary = future + boundaryOffset;
            annotationLine (onPath, onPathBoundary, insidePathColor);
            annotationLine (onPathBoundary, future, outsidePathColor);
        }

        void drawMap (void)
        {
    #ifdef OLDTERRAINMAP
            const float xs = map->xSize/(float)map->resolution;
            const float zs = map->zSize/(float)map->resolution;
            const Vec3 alongRow (xs, 0, 0);
            const Vec3 nextRow (-map->xSize, 0, zs);
            Vec3 g ((map->xSize - xs) / -2, 0, (map->zSize - zs) / -2);
            g += map->center;
            for (int j = 0; j < map->resolution; j++)
            {
                for (int i = 0; i < map->resolution; i++)
                {
                    if (map->getMapBit (i, j))
                    {

                        const float rockHeight = 0;
                        const Vec3 v1 (+xs/2, rockHeight, +zs/2);
                        const Vec3 v2 (+xs/2, rockHeight, -zs/2);
                        const Vec3 v3 (-xs/2, rockHeight, -zs/2);
                        const Vec3 v4 (-xs/2, rockHeight, +zs/2);

                        const Color orangeRockColor (0.5f, 0.2f, 0.0f);
                        drawQuadrangle (g+v1, g+v2, g+v3, g+v4, orangeRockColor);

                    } 
                    g += alongRow;
                }
                g += nextRow;
            }
    #else
    #endif
        }

        void drawPath (void)
        {
            const Color pathColor (0, 0.5f, 0.5f);
            const Color sandColor (0.8f, 0.7f, 0.5f);
            const Color color = interpolate (0.1f, sandColor, pathColor);

            const Vec3 down (0, -0.1f, 0);
            for ( OpenSteer::size_t i = 1; i < path->pointCount(); ++i )
            {
                const Vec3 endPoint0 = path->point( i ) + down;
                const Vec3 endPoint1 = path->point( i - 1 ) + down;

                const float legWidth = path->segmentRadius( i - 1 );

                drawXZWideLine (endPoint0, endPoint1, color, legWidth * 2);
                drawLine (path->point( i ), path->point( i - 1 ), pathColor);
                drawXZDisk (legWidth, endPoint0, color, 24);
                drawXZDisk (legWidth, endPoint1, color, 24);

            }
        }

        GCRoute* makePath (void)
        {

            const float m = worldSize * 0.4f; 
            const float n = worldSize / 8;    
            const float o = worldSize * 2;    

            const Vec3 p (0,   0, m);
            const Vec3 q (0,   0, m-n);
            const Vec3 r (-m,  0, 0);
            const Vec3 s (2*n, 0, 0);
            const Vec3 t (o,   0, 0);
            const Vec3 u (-o,  0, 0);
            const Vec3 v (n,   0, 0);
            const Vec3 w (0, 0, 0);

            const Vec3 a (t-p);
            const Vec3 b (s+v-p);
            const Vec3 c (s-q);
            const Vec3 d (s+q);
            const Vec3 e (s-v+p);
            const Vec3 f (p-w);
            const Vec3 g (r-w);
            const Vec3 h (-p-w);
            const Vec3 i (u-p);

            const int pathPointCount = 9;
            const Vec3 pathPoints[pathPointCount] = {a, b, c, d, e, f, g, h, i};
            const float k = 10.0f;
            const float pathRadii[pathPointCount] = {k, k, k, k, k, k, k, k, k};
            return new GCRoute (pathPointCount, pathPoints, pathRadii, false);
        }

        TerrainMap* makeMap (void)
        {
    #ifdef OLDTERRAINMAP
            return new TerrainMap (Vec3::zero,
                                   worldSize,
                                   worldSize,
                                   (int)worldSize + 1);
    #else
            return new TerrainMap (worldSize, worldSize, 1);
    #endif
        }

        bool handleExitFromMap (void)
        {
            if (demoSelect == 2)
            {

                const float px = position ().x;
                const float fx = forward ().x;
                const float ws = worldSize * 0.51f; 
                if (((fx > 0) && (px > ws)) || ((fx < 0) && (px < -ws)))
                {

                    lapsStarted++;
                    lapsFinished++;

                    const Vec3 camOffsetBefore =
                        OpenSteerDemo::camera.position() - position ();

                    setPosition ((((px < 0) ? 1 : -1) *
                                  ((worldSize * 0.5f) +
                                   (speed() * lookAheadTimePF ()))),
                                 position ().y,
                                 position ().z);

                    resetStuckCycleDetection ();

                    OpenSteerDemo::camera.target = position ();
                    OpenSteerDemo::camera.setPosition (position () + camOffsetBefore);

                    OpenSteerDemo::camera.doNotSmoothNextMove ();

                    clearTrailHistory ();

                    return true; 
                }
            }
            else
            {

                if (position().length() > worldDiag) reset();
            }
            return false;
        }

        float wingSlope (void)
        {
            return interpolate (relativeSpeed (),
                                (curvedSteering ? 0.3f : 0.35f),
                                0.06f);
        }

        void resetStuckCycleDetection (void)
        {
            resetSmoothedPosition (position () + (forward () * -80)); 
        }

        bool weAreGoingInCircles (void)
        {
            const Vec3 offset = smoothedPosition () - position ();
            return offset.length () < 10;
        }

        float lookAheadTimeOA (void) const
        {
            const float minTime = (baseLookAheadTime *
                                   (curvedSteering ?
                                    interpolate (relativeSpeed(), 0.4f, 0.7f) :
                                    0.66f));
            return combinedLookAheadTime (minTime, 3);
        }

        float lookAheadTimePF (void) const
        {
            return combinedLookAheadTime (baseLookAheadTime, 3);
        }

        float combinedLookAheadTime (float minTime, float minDistance) const
        {
            if (speed () == 0) return 0;
            return maxXXX (minTime, minDistance / speed ());
        }

        bool bodyInsidePath (void)
        {
            if (demoSelect == 2)
            {
                const Vec3 bbSide = side () * halfWidth;
                const Vec3 bbFront = forward () * halfLength;
                return (  isInsidePathway( *path, position () - bbFront + bbSide ) &&
                          isInsidePathway( *path, position () + bbFront + bbSide ) &&
                          isInsidePathway( *path, position () + bbFront - bbSide ) &&
                          isInsidePathway( *path, position () - bbFront - bbSide ) );
            }
            return true;
        }

        Vec3 convertAbsoluteToIncrementalSteering (const Vec3& absolute,
                                                   const float elapsedTime)
        {
            const Vec3 curved = convertLinearToCurvedSpaceGlobal (absolute);
            blendIntoAccumulator (elapsedTime * 8.0f, curved, currentSteering);
            {

                const Vec3 u (0, 0.5, 0);
                const Vec3 p = position ();
                annotationLine (p + u, p + u + absolute, gRed);
                annotationLine (p + u, p + u + curved, gYellow);
                annotationLine (p + u*2, p + u*2 + currentSteering, gGreen);
            }
            return currentSteering;
        }

        Vec3 convertLinearToCurvedSpaceGlobal (const Vec3& linear)
        {
            const Vec3 trimmedLinear = linear.truncateLength (maxForce ());

            const float signedRadius = 1 / (nonZeroCurvatureQQQ()  * 1);
            const Vec3 localCenterOfCurvature = side () * signedRadius;
            const Vec3 center = position () + localCenterOfCurvature;
            const float sign = signedRadius < 0 ? 1.0f : -1.0f;
            const float arcLength = trimmedLinear.dot (forward ());

            const float arcRadius = signedRadius * -sign;
            const float twoPi = 2 * OPENSTEER_M_PI;
            const float circumference = twoPi * arcRadius;
            const float arcAngle = twoPi * arcLength / circumference;

            const Vec3 initialSpoke = position () - center;

            const Vec3 spoke = initialSpoke.rotateAboutGlobalY (arcAngle * sign);

            const Vec3 fromCenter = -localCenterOfCurvature.normalize ();
            const float dRadius = trimmedLinear.dot (fromCenter);
            const float radiusChangeFactor = (dRadius + arcRadius) / arcRadius;
            const Vec3 resultLocation = center + (spoke * radiusChangeFactor);
            {
                const Vec3 center = position () + localCenterOfCurvature;
                annotationXZArc (position (), center, speed () * sign * -3,
                                 20, gWhite);
            }

            return resultLocation - position ();
        }

        float minimumTurningRadius () const {return 5.0f;}

        Vec3 adjustSteeringForMinimumTurningRadius (const Vec3& steering)
        {
            const float maxCurvature = 1 / (minimumTurningRadius () * 1.1f);

            if (absXXX (curvature ()) > maxCurvature)
            {

                const float signedRadius = 1 / nonZeroCurvatureQQQ ();
                const float sign = signedRadius < 0 ? 1.0f : -1.0f;
                const Vec3 thrust = steering.parallelComponent (forward ());
                const Vec3 trimmed = thrust.truncateLength (maxForce ());
                const Vec3 widenOut = side () * maxForce () * sign;
                {

                    const Vec3 localCenterOfCurvature = side () * signedRadius;
                    const Vec3 center = position () + localCenterOfCurvature;
                    annotationCircleOrDisk (minimumTurningRadius (), up(),
                                            center, gBlue, 40, false, false);
                }
                return trimmed + widenOut;
            }

            return steering;
        }

        float nonZeroCurvatureQQQ (void) const
        {
            const float c = curvature ();
            const float minCurvature = 1.0f / 100000.0f; 
            const bool tooSmall = (c < minCurvature) && (c > -minCurvature);
            return (tooSmall ? minCurvature: c);
        }

        float maxSpeedForCurvature ()
        {
            float maxRelativeSpeed = 1;

            if (curvedSteering)
            {

                const float absC = absXXX (curvature ());
                const float maxC = 1 / minimumTurningRadius ();
                const float relativeCurvature = sqrtXXX (clip (absC/maxC, 0, 1));

                const float curveSpeed = interpolate (relativeCurvature,1.0f,0.1f);
                annoteMaxRelSpeedCurve = curveSpeed;

                if (demoSelect != 2)
                {
                    maxRelativeSpeed = curveSpeed;
                }
                else
                {

                    const Vec3 pathHeading =  mapPointAndDirectionToTangent( *path, position(), pathFollowDirection ); 

                    const float parallelness = pathHeading.dot (forward ());

                    const float mw = 0.2f;
                    const float headingSpeed = ((parallelness < 0) ? mw :
                                                interpolate (parallelness,
                                                             mw, 1.0f));
                    maxRelativeSpeed = minXXX (curveSpeed, headingSpeed);
                    annoteMaxRelSpeedPath = headingSpeed;
                }
            }
            annoteMaxRelSpeed = maxRelativeSpeed;
            return maxSpeed () * maxRelativeSpeed;
        }

        Vec3 steerTowardHeading (const Vec3& desiredGlobalHeading)
        {
            const Vec3 headingError = desiredGlobalHeading - forward ();
            return headingError.normalize () * maxForce ();
        }

        void annotationXZArc (const Vec3& start,
                              const Vec3& center,
                              const float arcLength,
                              const int segments,
                              const Color& color) const
        {

            Vec3 spoke = start - center;

            const float radius = spoke.length ();
            const float twoPi = 2 * OPENSTEER_M_PI;
            const float circumference = twoPi * radius;
            const float arcAngle = twoPi * arcLength / circumference;
            const float step = arcAngle / segments;

            float sin=0, cos=0;
            for (int i = 0; i < segments; i++)
            {
                const Vec3 old = spoke + center;

                spoke = spoke.rotateAboutGlobalY (step, sin, cos);

                annotationLine (spoke + center, old, color);
            }
        }

        TerrainMap* map;

        GCRoute* path;

        int pathFollowDirection;

        float baseLookAheadTime;

        float halfWidth;
        float halfLength;

        bool collisionDetected;
        bool collisionLastTime;
        float timeOfLastCollision;
        float sumOfCollisionFreeTimes;
        int countOfCollisionFreeTimes;

        float totalDistance;
        float totalTime;

        float pathFollowTime;
        float pathFollowOffTime;

        bool dtZero;

        Vec3 annotateAvoid;
        bool wingDrawFlagL, wingDrawFlagR;

        bool stuck;
        int stuckCount;
        int stuckCycleCount;
        int stuckOffPathCount;

        Vec3 qqqLastNearestObstacle;

        int lapsStarted;
        int lapsFinished;

        bool QQQoaJustScraping;

        int hintGivenCount;
        int hintTakenCount;

        Vec3 currentSteering;

        bool curvedSteering;
        bool incrementalSteering;

        static float savedNearestWR, savedNearestR, savedNearestL, savedNearestWL;

        float annoteMaxRelSpeed, annoteMaxRelSpeedCurve, annoteMaxRelSpeedPath;

        static int demoSelect;

        static float worldSize;
        static float worldDiag;
    };

    float MapDriver::worldSize = 200;
    float MapDriver::worldDiag = sqrtXXX (square (worldSize) / 2);

    int MapDriver::demoSelect = 2;

    float MapDriver::savedNearestWR = 0;
    float MapDriver::savedNearestR = 0;
    float MapDriver::savedNearestL = 0;
    float MapDriver::savedNearestWL = 0;

    class MapDrivePlugIn : public PlugIn
    {
    public:

        const char* name (void) {return "Driving through map based obstacles";}

        float selectionOrderSortKey (void) {return 0.07f;}

        virtual ~MapDrivePlugIn() {}

        void open (void)
        {

            vehicle = new MapDriver ();
            vehicles.push_back (vehicle);
            OpenSteerDemo::selectedVehicle = vehicle;

            usePathFences = true; 

            useRandomRocks = true;

            initCamDist = 30;
            initCamElev = 15;
            OpenSteerDemo::init2dCamera (*vehicle, initCamDist, initCamElev);

            OpenSteerDemo::camera.lookdownDistance = 50;

            OpenSteerDemo::camera.fixedPosition.set (145, 145, 145);
            OpenSteerDemo::camera.fixedTarget.set (40, 0, 40);
            OpenSteerDemo::camera.fixedUp = Vec3::up;

            reset ();
        }

        void update (const float currentTime, const float elapsedTime)
        {

            vehicle->update (currentTime, elapsedTime);

            if (vehicle->handleExitFromMap ()) regenerateMap ();

            if (vehicle->stuck && (vehicle->relativeSpeed () < 0.001f))
            {
                vehicle->stuckCount++;
                reset();
            }
        }

        void redraw (const float currentTime, const float elapsedTime)
        {

            OpenSteerDemo::updateCamera (currentTime, elapsedTime, vehicle);

            const float s = MapDriver::worldSize * 2;
            const float u = -0.2f;
            drawQuadrangle (Vec3 (+s, u, +s),
                            Vec3 (+s, u, -s),
                            Vec3 (-s, u, -s),
                            Vec3 (-s, u, +s),
                            Color (0.8f, 0.7f, 0.5f)); 

            vehicle->drawMap ();
            if (vehicle->demoSelect == 2) vehicle->drawPath ();

            vehicle->draw ();

            const float tick = 2;
            drawLine (Vec3 (tick, 0, 0), Vec3 (-tick, 0, 0), gGreen);
            drawLine (Vec3 (0, 0, tick), Vec3 (0, 0, -tick), gGreen);

            const float metersPerMile = 1609.344f;
            const float secondsPerHour = 3600;
            const float MPSperMPH = metersPerMile / secondsPerHour;

            std::ostringstream status;
            status << "Speed: "
                   << (int) vehicle->speed () << " mps ("
                   << (int) (vehicle->speed () / MPSperMPH) << " mph)"
                   << ", average: "
                   << std::setprecision (1) << std::setiosflags (std::ios::fixed)
                   << vehicle->totalDistance / vehicle->totalTime

                   << " mps\n\n";
            status << "collisions avoided for "
                   << (int)(OpenSteerDemo::clock.getTotalSimulationTime () -
                            vehicle->timeOfLastCollision)
                   << " seconds";
            if (vehicle->countOfCollisionFreeTimes > 0)
            {
                status << "\nmean time between collisions: "
                       << (int) (vehicle->sumOfCollisionFreeTimes /
                                 vehicle->countOfCollisionFreeTimes)
                       << " ("
                       << (int)vehicle->sumOfCollisionFreeTimes
                       << "/"
                       << (int)vehicle->countOfCollisionFreeTimes
                       << ")";
            }

            status << "\n\nStuck count: " << vehicle->stuckCount << " (" 
                   << vehicle->stuckCycleCount << " cycles, "
                   << vehicle->stuckOffPathCount << " off path)";
            status << "\n\n[F1] ";
            if (1 == vehicle->demoSelect) status << "wander, ";
            if (2 == vehicle->demoSelect) status << "follow path, ";
            status << "avoid obstacle";

            if (2 == vehicle->demoSelect)
            {
                status << "\n[F2] path following direction: ";
                if (vehicle->pathFollowDirection>0)status<<"+1";else status<<"-1";
                status << "\n[F3] path fence: ";
                if (usePathFences) status << "on"; else status << "off";
            }

            status << "\n[F4] rocks: ";
            if (useRandomRocks) status << "on"; else status << "off";
            status << "\n[F5] prediction: ";
            if (vehicle->curvedSteering)
                status << "curved"; else status << "linear";
            if (2 == vehicle->demoSelect)
            {
                status << "\n\nLap " << vehicle->lapsStarted
                       << " (completed: "
                       << ((vehicle->lapsStarted < 2) ? 0 :
                           (int) (100 * ((float) vehicle->lapsFinished /
                                         (float) (vehicle->lapsStarted - 1))))
                       << "%)";

                status << "\nHints given: " << vehicle->hintGivenCount
                       << ", taken: " << vehicle->hintTakenCount;
            }
            status << "\n";
            qqqRange ("WR ", vehicle->savedNearestWR, status);
            qqqRange ("R  ", vehicle->savedNearestR,  status);
            qqqRange ("L  ", vehicle->savedNearestL,  status);
            qqqRange ("WL ", vehicle->savedNearestWL, status);
            status << std::ends;
            const float h = drawGetWindowHeight ();
            const Vec3 screenLocation (10, h-50, 0);
            const Color color (0.15f, 0.15f, 0.5f);
            draw2dTextAt2dLocation (status, screenLocation, color, drawGetWindowWidth(), drawGetWindowHeight());

            {
                const float v = 5;
                const float m = 10;
                const float w = drawGetWindowWidth ();
                const float f = w - (2 * m);
                const float s = vehicle->relativeSpeed ();

                const float l = vehicle->annoteMaxRelSpeed;
                draw2dLine (Vec3 (m+(f*l), v-3, 0), Vec3 (m+(f*l),v+3, 0), gBlack, drawGetWindowWidth(), drawGetWindowHeight());

                if (l!=0)
                {
                    const float c = vehicle->annoteMaxRelSpeedCurve;
                    const float p = vehicle->annoteMaxRelSpeedPath;
                    draw2dLine (Vec3(m+(f*c), v+1, 0), Vec3(w-m, v+1, 0), gRed, drawGetWindowWidth(), drawGetWindowHeight());
                    draw2dLine (Vec3(m+(f*p), v-2, 0), Vec3(w-m, v-1, 0), gGreen, drawGetWindowWidth(), drawGetWindowHeight());
                }

                draw2dLine (Vec3 (m, v, 0), Vec3 (m + (f * s), v, 0), gWhite, drawGetWindowWidth(), drawGetWindowHeight());

                draw2dLine (Vec3 (m,       v, 0), Vec3 (m,      v-2, 0), gWhite, drawGetWindowWidth(), drawGetWindowHeight());
                draw2dLine (Vec3 (w-m,     v, 0), Vec3 (w-m,    v-2, 0), gWhite, drawGetWindowWidth(), drawGetWindowHeight());
            }
        }

        void qqqRange (char const* string, float range, std::ostringstream& status)
        {
            status << "\n" << string;
            if (range == 9999.0f) status << "--"; else status << (int) range;
        }

        void close (void)
        {
            vehicles.clear ();
            delete (vehicle);
        }

        void reset (void)
        {
            regenerateMap();

            vehicle->reset ();

            OpenSteerDemo::camera.doNotSmoothNextMove ();

            OpenSteerDemo::position2dCamera (*vehicle, initCamDist, initCamElev);
        }

        void handleFunctionKeys (int keyNumber)
        {
            switch (keyNumber)
            {
            case 1: selectNextDemo (); break;
            case 2: reversePathFollowDirection (); break;
            case 3: togglePathFences (); break;
            case 4: toggleRandomRocks (); break;
            case 5: toggleCurvedSteering (); break;

            case 6: 
                {
                    const float m = MapDriver::worldSize * 0.4f; 
                    const float n = MapDriver::worldSize / 8;    
                    const Vec3 q (0,   0, m-n);
                    const Vec3 s (2*n, 0, 0);
                    const Vec3 c (s-q);
                    const Vec3 d (s+q);
                    const int pathPointCount = 2;
                    const float pathRadii[pathPointCount] = {10, 10};
                    const Vec3 pathPoints[pathPointCount] = {c, d};
                    GCRoute r (pathPointCount, pathPoints, pathRadii, false);
                    drawPathFencesOnMap (*vehicle->map, r);
                    break;
                }
            }
        }

        void printMiniHelpForFunctionKeys (void)
        {
            std::ostringstream message;
            message << "Function keys handled by ";
            message << '"' << name() << '"' << ':' << std::ends;
            OpenSteerDemo::printMessage (message);
            OpenSteerDemo::printMessage ("  F1     select next driving demo.");
            OpenSteerDemo::printMessage ("  F2     reverse path following direction.");
            OpenSteerDemo::printMessage ("  F3     toggle path fences.");
            OpenSteerDemo::printMessage ("  F4     toggle random rock clumps.");
            OpenSteerDemo::printMessage ("  F5     toggle curved prediction.");
            OpenSteerDemo::printMessage ("");
        }

        void reversePathFollowDirection (void)
        {
            int& pfd = vehicle->pathFollowDirection;
            pfd = (pfd > 0) ? -1 : +1;
        }

        void togglePathFences (void)
        {
            usePathFences = ! usePathFences;
            reset ();
        }

        void toggleRandomRocks (void)
        {
            useRandomRocks = ! useRandomRocks;
            reset ();
        }

        void toggleCurvedSteering (void)
        {
            vehicle->curvedSteering = ! vehicle->curvedSteering;
            vehicle->incrementalSteering = ! vehicle->incrementalSteering;
            reset ();
        }

        void selectNextDemo (void)
        {
            std::ostringstream message;
            message << name() << ": ";
            switch (++vehicle->demoSelect)
            {
            default:
                vehicle->demoSelect = 0; 
            case 0:
                message << "obstacle avoidance and speed control";
                reset ();
                break;
            case 1: 
                message << "wander, obstacle avoidance and speed control";
                reset ();
                break;
            case 2: 
                message << "path following, obstacle avoidance and speed control";
                reset ();
                break;
            }
            message << std::ends;
            OpenSteerDemo::printMessage (message);
        }

        int irandom2 (int min, int max)
        {
            return (int) frandom2 ((float) min, (float) max);
        }

	void regenerateMap (void)
	{

	    vehicle->map->clear();
	    drawRandomClumpsOfRocksOnMap (*vehicle->map);
	    clearCenterOfMap (*vehicle->map);

	    if (vehicle->demoSelect < 2) drawBoundaryFencesOnMap (*vehicle->map);

	    if (vehicle->demoSelect == 2)
	    {
		const OpenSteer::size_t count = vehicle->path->segmentCount();
		const bool upstream = vehicle->pathFollowDirection > 0;
		const OpenSteer::size_t entryIndex = upstream ? 0 : count-1;
		const OpenSteer::size_t exitIndex  = upstream ? count-1 : 0;
		const float lastExitRadius = vehicle->path->segmentRadius( exitIndex );
		for (OpenSteer::size_t i = 0; i < count; i++)
		{
		    vehicle->path->setSegmentRadius( i, frandom2 (4, 19) );
		}
		vehicle->path->setSegmentRadius( entryIndex, lastExitRadius );
	    }

	    if (usePathFences && (vehicle->demoSelect == 2))
		drawPathFencesOnMap (*vehicle->map, *vehicle->path);
	}

        void drawRandomClumpsOfRocksOnMap (TerrainMap& map)
        {
            if (useRandomRocks)
            {
                const int spread = 4;
                const int r = map.cellwidth();
                const int k = irandom2 (50, 150);

                for (int p=0; p<k; p++)
                {
                    const int i = irandom2 (0, r - spread);
                    const int j = irandom2 (0, r - spread);
                    const int c = irandom2 (0, 10);

                    for (int q=0; q<c; q++)
                    {
                        const int m = irandom2 (0, spread);
                        const int n = irandom2 (0, spread);
    #ifdef OLDTERRAINMAP
                        map.setMapBit (i+m, j+n, 1);
    #else
                        map.setType (i+m, j+n, CellData::OBSTACLE);
    #endif
                    }
                }
            }
        }

        void drawBoundaryFencesOnMap (TerrainMap& map)
        {

            const int cw = map.cellwidth();
            const int ch = map.cellheight();

            const int r = cw - 1;
            const int a = cw >> 3;
            const int b = cw - a;
            const int o = cw >> 4;
            const int p = (cw - o) >> 1;
            const int q = (cw + o) >> 1;

            for (int i = 0; i < cw; i++)
            {
                for (int j = 0; j < ch; j++)
                {
                    const bool c = i>a && i<b && (i<p || i>q);
                    if (i==0 || j==0 || i==r || j==r || (c && (i==j || i+j==r))) 
    #ifdef OLDTERRAINMAP
                        map.setMapBit (i, j, 1);
    #else
                        map.setType (i, j, CellData::IMPASSABLE);
    #endif
                }
            }
        }

        void clearCenterOfMap (TerrainMap& map)
        {
            const int o = map.cellwidth() >> 4;
            const int p = (map.cellwidth() - o) >> 1;
            const int q = (map.cellwidth() + o) >> 1;
            for (int i = p; i <= q; i++)
                for (int j = p; j <= q; j++)
    #ifdef OLDTERRAINMAP
                    map.setMapBit (i, j, 0);
    #else
                    map.setType (i, j, CellData::CLEAR);
    #endif
        }

        void drawPathFencesOnMap (TerrainMap& map, GCRoute& path)
        {
    #ifdef OLDTERRAINMAP
            const float xs = map.xSize / (float)map.resolution;
            const float zs = map.zSize / (float)map.resolution;
            const Vec3 alongRow (xs, 0, 0);
            const Vec3 nextRow (-map.xSize, 0, zs);
            Vec3 g ((map.xSize - xs) / -2, 0, (map.zSize - zs) / -2);
            for (int j = 0; j < map.resolution; j++)
            {
                for (int i = 0; i < map.resolution; i++)
                {
                    const float outside = mapPointToOutside( path, g ); 
                    const float wallThickness = 1.0f;

                    if ((outside > 0) && (outside < wallThickness))
                        map.setMapBit (i, j, true);

                    if (outside > wallThickness) map.setMapBit (i, j, false);

                    g += alongRow;
                }
                g += nextRow;
            }
    #else
    #endif
        }

        const AVGroup& allVehicles (void) {return (const AVGroup&) vehicles;}

        MapDriver* vehicle;
        std::vector<MapDriver*> vehicles; 

        float initCamDist, initCamElev;

        bool usePathFences;
        bool useRandomRocks;
    };

    MapDrivePlugIn gMapDrivePlugIn;

} 