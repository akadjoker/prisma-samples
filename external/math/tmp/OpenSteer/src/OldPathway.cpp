
#include "OpenSteer/OldPathway.h"

OpenSteer::Old::PolylinePathway::PolylinePathway (const int _pointCount,
                                                  const Vec3 _points[],
                                                  const float _radius,
                                                  const bool _cyclic)
{
    initialize (_pointCount, _points, _radius, _cyclic);
}

void 
OpenSteer::Old::PolylinePathway::initialize (const int _pointCount,
                                             const Vec3 _points[],
                                             const float _radius,
                                             const bool _cyclic)
{

    radius = _radius;
    cyclic = _cyclic;
    pointCount = _pointCount;
    if (cyclic) pointCount++;
    lengths = new float    [pointCount];
    points  = new Vec3 [pointCount];
    normals = new Vec3 [pointCount];

    for (int i = 0; i < pointCount; i++)
    {

        const bool closeCycle = cyclic && (i == pointCount-1);
        const int j = closeCycle ? 0 : i;
        points[i] = _points[j];
	}
	setupLengths();
}

void 
OpenSteer::Old::PolylinePathway::setupLengths ()
{
     totalPathLength = 0;

    for (int i = 0; i < pointCount; i++)
    {

        if (i > 0)
        {

            normals[i] = points[i] - points[i-1];
            lengths[i] = normals[i].length ();

            normals[i] *= 1 / lengths[i];

            totalPathLength += lengths[i];
        }
    }
}

void 
OpenSteer::Old::PolylinePathway::movePoints (const int _firstPoint,
                                             const int _numPoints,
                                             const Vec3 _points[])
{

    for (int i = _firstPoint; i < _firstPoint + _numPoints; i++)
    {
		int	j = i - _firstPoint;

        points[i] = _points[j];

        if (cyclic && i == 0)
		{
			points[pointCount-1] = _points[j];
		}
	}
	setupLengths();
}

OpenSteer::Vec3 
OpenSteer::Old::PolylinePathway::mapPointToPath (const Vec3& point,
                                                 Vec3& tangent,
                                                 float& outside)
{
    float d;
    float minDistance = FLT_MAX;
    Vec3 onPath;

    for (int i = 1; i < pointCount; i++)
    {
        segmentLength = lengths[i];
        segmentNormal = normals[i];
        d = pointToSegmentDistance (point, points[i-1], points[i]);
        if (d < minDistance)
        {
            minDistance = d;
            onPath = chosen;
            tangent = segmentNormal;
        }
    }

    outside = Vec3::distance (onPath, point) - radius;

    return onPath;
}

float 
OpenSteer::Old::PolylinePathway::mapPointToPathDistance (const Vec3& point)
{
    float d;
    float minDistance = FLT_MAX;
    float segmentLengthTotal = 0;
    float pathDistance = 0;

    for (int i = 1; i < pointCount; i++)
    {
        segmentLength = lengths[i];
        segmentNormal = normals[i];
        d = pointToSegmentDistance (point, points[i-1], points[i]);
        if (d < minDistance)
        {
            minDistance = d;
            pathDistance = segmentLengthTotal + segmentProjection;
        }
        segmentLengthTotal += segmentLength;
    }

    return pathDistance;
}

OpenSteer::Vec3 
OpenSteer::Old::PolylinePathway::mapPathDistanceToPoint (float pathDistance)
{

    float remaining = pathDistance;
    if (cyclic)
    {
        remaining = (float) fmod (pathDistance, totalPathLength);
    }
    else
    {
        if (pathDistance < 0) return points[0];
        if (pathDistance >= totalPathLength) return points [pointCount-1];
    }

    Vec3 result;
    for (int i = 1; i < pointCount; i++)
    {
        segmentLength = lengths[i];
        if (segmentLength < remaining)
        {
            remaining -= segmentLength;
        }
        else
        {
            float ratio = remaining / segmentLength;
            result = interpolate (ratio, points[i-1], points[i]);
            break;
        }
    }
    return result;
}

float 
OpenSteer::Old::PolylinePathway::pointToSegmentDistance (const Vec3& point,
                                                         const Vec3& ep0,
                                                         const Vec3& ep1)
{

    local = point - ep0;

    segmentProjection = segmentNormal.dot (local);

    if (segmentProjection < 0)
    {
        chosen = ep0;
        segmentProjection = 0;
        return Vec3::distance (point, ep0);
    }
    if (segmentProjection > segmentLength)
    {
        chosen = ep1;
        segmentProjection = segmentLength;
        return Vec3::distance (point, ep1);
    }

    chosen = segmentNormal * segmentProjection;
    chosen +=  ep0;
    return Vec3::distance (point, chosen);
}