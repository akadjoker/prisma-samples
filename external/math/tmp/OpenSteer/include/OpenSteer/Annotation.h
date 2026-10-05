
#ifndef OPENSTEER_ANNOTATION_H
#define OPENSTEER_ANNOTATION_H

#ifndef NOT_OPENSTEERDEMO  
#include "OpenSteer/Draw.h"
#endif 
#include "OpenSteer/Vec3.h"
#include "OpenSteer/Color.h"

namespace OpenSteer {

    extern bool enableAnnotation;
    extern bool drawPhaseActive;

    inline bool annotationIsOn (void) {return enableAnnotation;}
    inline void setAnnotationOn (void) {enableAnnotation = true;}
    inline void setAnnotationOff (void) {enableAnnotation = false;}
    inline bool toggleAnnotationState (void) {return (enableAnnotation = !enableAnnotation);}

    template <class Super>
    class AnnotationMixin : public Super
    {
    public:

        AnnotationMixin ();

        virtual ~AnnotationMixin ();

        void recordTrailVertex (const float currentTime, const Vec3& position);

        void drawTrail (void) {drawTrail (grayColor (0.7f), gWhite);}
        void drawTrail  (const Color& trailColor, const Color& tickColor);

        void setTrailParameters (const float duration, const int vertexCount);

        void clearTrailHistory (void);

        void annotationLine (const Vec3& startPoint,
                             const Vec3& endPoint,
                             const Color& color) const;

        void annotationXZCircle (const float radius,
                                 const Vec3& center,
                                 const Color& color,
                                 const int segments) const
        {
            annotationXZCircleOrDisk (radius, center, color, segments, false);
        }

        void annotationXZDisk (const float radius,
                               const Vec3& center,
                               const Color& color,
                               const int segments) const
        {
            annotationXZCircleOrDisk (radius, center, color, segments, true);
        }

        void annotation3dCircle (const float radius,
                                 const Vec3& center,
                                 const Vec3& axis,
                                 const Color& color,
                                 const int segments) const
        {
            annotation3dCircleOrDisk (radius, center, axis, color, segments, false);
        }

        void annotation3dDisk (const float radius,
                               const Vec3& center,
                               const Vec3& axis,
                               const Color& color,
                               const int segments) const
        {
            annotation3dCircleOrDisk (radius, center, axis, color, segments, true);
        }

        void annotationXZCircleOrDisk (const float radius,
                                       const Vec3& center,
                                       const Color& color,
                                       const int segments,
                                       const bool filled) const
        {
            annotationCircleOrDisk (radius,
                                    Vec3::zero,
                                    center,
                                    color,
                                    segments,
                                    filled,
                                    false); 
        }

        void annotation3dCircleOrDisk (const float radius,
                                       const Vec3& center,
                                       const Vec3& axis,
                                       const Color& color,
                                       const int segments,
                                       const bool filled) const
        {
            annotationCircleOrDisk (radius,
                                    axis,
                                    center,
                                    color,
                                    segments,
                                    filled,
                                    true); 
        }

        void annotationCircleOrDisk (const float radius,
                                     const Vec3& axis,
                                     const Vec3& center,
                                     const Color& color,
                                     const int segments,
                                     const bool filled,
                                     const bool in3d) const;

    private:

        int trailVertexCount;       
        int trailIndex;             
        float trailDuration;        
        float trailSampleInterval;  
        float trailLastSampleTime;  
        int trailDottedPhase;       
        Vec3 curPosition;           
        Vec3* trailVertices;        
        char* trailFlags;           
    };

} 

template<class Super>
OpenSteer::AnnotationMixin<Super>::AnnotationMixin (void)
{
    trailVertices = NULL;
    trailFlags = NULL;

    setTrailParameters (5, 100);  
}

template<class Super>
OpenSteer::AnnotationMixin<Super>::~AnnotationMixin (void)
{
    delete[] trailVertices;
    delete[] trailFlags;
}

template<class Super>
void 
OpenSteer::AnnotationMixin<Super>::setTrailParameters (const float duration, 
                                                       const int vertexCount)
{

    trailDuration = duration;
    trailVertexCount = vertexCount;

    trailIndex = 0;
    trailLastSampleTime = 0;
    trailSampleInterval = trailDuration / trailVertexCount;
    trailDottedPhase = 1;

    delete[] trailVertices;
    trailVertices = new Vec3[trailVertexCount];

    delete[] trailFlags;
    trailFlags = new char[trailVertexCount];

    for (int i = 0; i < trailVertexCount; i++) trailFlags[i] = 0;
}

template<class Super>
void 
OpenSteer::AnnotationMixin<Super>::clearTrailHistory (void)
{

    setTrailParameters (trailDuration, trailVertexCount);
}

template<class Super>
void 
OpenSteer::AnnotationMixin<Super>::recordTrailVertex (const float currentTime,
                                                      const Vec3& position)
{
    const float timeSinceLastTrailSample = currentTime - trailLastSampleTime;
    if (timeSinceLastTrailSample > trailSampleInterval)
    {
        trailIndex = (trailIndex + 1) % trailVertexCount;
        trailVertices [trailIndex] = position;
        trailDottedPhase = (trailDottedPhase + 1) % 2;
        const int tick = (floorXXX (currentTime) >
                          floorXXX (trailLastSampleTime));
        trailFlags [trailIndex] = trailDottedPhase | (tick ? '\2' : '\0');
        trailLastSampleTime = currentTime;
    }
    curPosition = position;
}

template<class Super>
void 
OpenSteer::AnnotationMixin<Super>::drawTrail (const Color& trailColor,
                                              const Color& tickColor)
{
    if (enableAnnotation)
    {
        int index = trailIndex;
        for (int j = 0; j < trailVertexCount; j++)
        {

            const int next = (index + 1) % trailVertexCount;

            const int tick = ((trailFlags [index] & 2) ||
                              (trailFlags [next] & 2));
            const Color color = tick ? tickColor : trailColor;

            if (trailFlags [index] & 1)
            {
                if (j == 0)
                {

                    drawLineAlpha (curPosition,
                                   trailVertices [index],
                                   color,
                                   1);
                }
                else
                {

                    const float minO = 0.05f; 
                    const float fraction = (float) j / trailVertexCount;
                    const float opacity = (fraction * (1 - minO)) + minO;
                    drawLineAlpha (trailVertices [index],
                                   trailVertices [next],
                                   color,
                                   opacity);
                }
            }
            index = next;
        }
    }
}

#ifndef NOT_OPENSTEERDEMO  
template<class Super>
void 
OpenSteer::AnnotationMixin<Super>::annotationLine (const Vec3& startPoint,
                                                   const Vec3& endPoint,
                                                   const Color& color) const
{
    if (enableAnnotation)
    {
        if (drawPhaseActive)
        {
            drawLine (startPoint, endPoint, color);
        }
        else
        {
            deferredDrawLine (startPoint, endPoint, color);
        }
    }
}
#else
template<class Super> void OpenSteer::AnnotationMixin<Super>::annotationLine
 (const Vec3&, const Vec3&, const Vec3&) const {}
#endif 

#ifndef NOT_OPENSTEERDEMO  
template<class Super>
void 
OpenSteer::AnnotationMixin<Super>::annotationCircleOrDisk (const float radius,
                                                           const Vec3& axis,
                                                           const Vec3& center,
                                                           const Color& color,
                                                           const int segments,
                                                           const bool filled,
                                                           const bool in3d) const
{
    if (enableAnnotation)
    {
        if (drawPhaseActive)
        {
            drawCircleOrDisk (radius, axis, center, color,
                              segments, filled, in3d);
        }
        else
        {
            deferredDrawCircleOrDisk (radius, axis, center, color,
                                      segments, filled, in3d);
        }
    }
}
#else
template<class Super>
void OpenSteer::AnnotationMixin<Super>::annotationCircleOrDisk
(const float, const Vec3&, const Vec3&, const Vec3&, const int,
 const bool, const bool) const {}
#endif 

#endif 