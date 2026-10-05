
#ifndef OPENSTEER_DRAW_H
#define OPENSTEER_DRAW_H

#include "OpenSteer/Vec3.h"
#include "OpenSteer/Color.h"
#include "OpenSteer/AbstractVehicle.h"
#include "OpenSteer/Obstacle.h"

namespace OpenSteer {

    void warnIfInUpdatePhase2( const char* name);

    extern bool updatePhaseActive;

    inline void warnIfInUpdatePhase (const char* name)
    {
        if (updatePhaseActive)
        {
            warnIfInUpdatePhase2 (name);
        }
    }

    typedef void (*drawTriangleRoutine) (const Vec3& a,
                                         const Vec3& b,
                                         const Vec3& c,
                                         const Color& color);

    void drawAxes  (const AbstractLocalSpace& localSpace,
                    const Vec3& size,
                    const Color& color);

    void drawBoxOutline  (const AbstractLocalSpace* localSpace,
                          const Vec3& size,
                          const Color& color);

    void drawXZCheckerboardGrid (const float size,
                                 const int subsquares,
                                 const Vec3& center,
                                 const Color& color1,
                                 const Color& color2);

    void drawXZLineGrid (const float size,
                         const int subsquares,
                         const Vec3& center,
                         const Color& color);

    void drawCircleOrDisk (const float radius,
                           const Vec3& axis,
                           const Vec3& center,
                           const Color& color,
                           const int segments,
                           const bool filled,
                           const bool in3d);

    void drawXZCircleOrDisk (const float radius,
                             const Vec3& center,
                             const Color& color,
                             const int segments,
                             const bool filled);

    void draw3dCircleOrDisk (const float radius,
                             const Vec3& center,
                             const Vec3& axis,
                             const Color& color,
                             const int segments,
                             const bool filled);

    inline void drawXZCircle (const float radius,
                              const Vec3& center,
                              const Color& color,
                              const int segments)
    {
        warnIfInUpdatePhase ("drawXZCircle");
        drawXZCircleOrDisk (radius, center, color, segments, false);
    }

    inline void drawXZDisk (const float radius,
                            const Vec3& center,
                            const Color& color,
                            const int segments)
    {
        warnIfInUpdatePhase ("drawXZDisk");
        drawXZCircleOrDisk (radius, center, color, segments, true);
    }

    inline void draw3dCircle (const float radius,
                              const Vec3& center,
                              const Vec3& axis,
                              const Color& color,
                              const int segments)
    {
        warnIfInUpdatePhase ("draw3dCircle");
        draw3dCircleOrDisk (radius, center, axis, color, segments, false);
    }

    inline void draw3dDisk (const float radius,
                            const Vec3& center,
                            const Vec3& axis,
                            const Color& color,
                            const int segments)
    {
        warnIfInUpdatePhase ("draw3dDisk");
        draw3dCircleOrDisk (radius, center, axis, color, segments, true);
    }

    void drawXZArc (const Vec3& start,
                    const Vec3& center,
                    const float arcLength,
                    const int segments,
                    const Color& color);

    void drawSphere (const Vec3 center,
                     const float radius,
                     const float maxEdgeLength,
                     const bool filled,
                     const Color& color,
                     const bool drawFrontFacing = true,
                     const bool drawBackFacing = true,
                     const Vec3& viewpoint = Vec3::zero);

    void drawSphereObstacle (const SphereObstacle& so,
                             const float maxEdgeLength,
                             const bool filled,
                             const Color& color,
                             const Vec3& viewpoint);

    void drawReticle (float w, float h);

    void drawBasic2dCircularVehicle (const AbstractVehicle* bv,
                                     const Color& color);

    void drawBasic3dSphericalVehicle (const AbstractVehicle* bv,
                                      const Color& color);

    void drawBasic3dSphericalVehicle (drawTriangleRoutine, const AbstractVehicle* bv,
                                      const Color& color);

    void draw2dTextAt3dLocation (const char& text,
                                 const Vec3& location,
                                 const Color& color, float w, float h);

    void draw2dTextAt3dLocation (const std::ostringstream& text,
                                 const Vec3& location,
                                 const Color& color, float w, float h);

    void draw2dTextAt2dLocation (const char& text,
                                 const Vec3 location,
                                 const Color& color, float w, float h);

    void draw2dTextAt2dLocation (const std::ostringstream& text,
                                 const Vec3 location,
                                 const Color& color, float w, float h);

    void glVertexVec3 (const Vec3& v);

    void drawLine (const Vec3& startPoint,
                   const Vec3& endPoint,
                   const Color& color);

    void draw2dLine (const Vec3& startPoint,
                    const Vec3& endPoint,
                    const Color& color,
                    float w, float h);

    void drawLineAlpha (const Vec3& startPoint,
                        const Vec3& endPoint,
                        const Color& color,
                        const float alpha);

    void deferredDrawLine (const Vec3& startPoint,
                           const Vec3& endPoint,
                           const Color& color);

    void deferredDrawCircleOrDisk (const float radius,
                                   const Vec3& axis,
                                   const Vec3& center,
                                   const Color& color,
                                   const int segments,
                                   const bool filled,
                                   const bool in3d);

    void drawAllDeferredLines (void);
    void drawAllDeferredCirclesOrDisks (void);

    void drawTriangle (const Vec3& a,
                       const Vec3& b,
                       const Vec3& c,
                       const Color& color);

    void drawQuadrangle (const Vec3& a,
                         const Vec3& b,
                         const Vec3& c,
                         const Vec3& d,
                         const Color& color);

    void drawXZWideLine (const Vec3& startPoint,
                         const Vec3& endPoint,
                         const Color& color,
                         float width);

    void drawCameraLookAt (const Vec3& cameraPosition,
                           const Vec3& pointToLookAt,
                           const Vec3& up);

    void checkForDrawError (const char * locationDescription);

    Vec3 directionFromCameraToScreenPosition (int x, int y, int h);

} 

#endif 