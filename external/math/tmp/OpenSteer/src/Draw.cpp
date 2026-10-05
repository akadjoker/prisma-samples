
#include "OpenSteer/Draw.h"

#include <iomanip>
#include <sstream>

#include <glad/glad.h>

#include "OpenSteer/Vec3.h"

#include "OpenSteer/Utilities.h"

namespace {

    inline void iglVertexVec3 (const OpenSteer::Vec3& v)
    {
        glVertex3f (v.x, v.y, v.z);
    }

    void 
    checkForGLError (const char* locationDescription)
    {

        const int lastGlError = glGetError();
        if (lastGlError == GL_NO_ERROR) return;

        std::cerr << std::endl << "OpenSteerDemo: OpenGL error ";
        switch (lastGlError)
        {
        case GL_INVALID_ENUM:      std::cerr << "GL_INVALID_ENUM";      break;
        case GL_INVALID_VALUE:     std::cerr << "GL_INVALID_VALUE";     break;
        case GL_INVALID_OPERATION: std::cerr << "GL_INVALID_OPERATION"; break;
        case GL_STACK_OVERFLOW:    std::cerr << "GL_STACK_OVERFLOW";    break;
        case GL_STACK_UNDERFLOW:   std::cerr << "GL_STACK_UNDERFLOW";   break;
        case GL_OUT_OF_MEMORY:     std::cerr << "GL_OUT_OF_MEMORY";     break;
        }
        if (locationDescription) std::cerr << " in " << locationDescription;
        std::cerr << std::endl << std::endl << std::flush;

    }

    inline void iDrawLine (const OpenSteer::Vec3& startPoint,
                           const OpenSteer::Vec3& endPoint,
                           const OpenSteer::Color& color)
    {
        OpenSteer::warnIfInUpdatePhase ("iDrawLine");
        glColor3f (color.r(), color.g(), color.b());
        glBegin (GL_LINES);
        glVertexVec3 (startPoint);
        glVertexVec3 (endPoint);
        glEnd ();
    }

    inline void iDrawTriangle (const OpenSteer::Vec3& a,
                               const OpenSteer::Vec3& b,
                               const OpenSteer::Vec3& c,
                               const OpenSteer::Color& color)
    {
        OpenSteer::warnIfInUpdatePhase ("iDrawTriangle");
        glColor3f (color.r(), color.g(), color.b());
        glBegin (GL_TRIANGLES);
        {
            OpenSteer::glVertexVec3 (a);
            OpenSteer::glVertexVec3 (b);
            OpenSteer::glVertexVec3 (c);
        }
        glEnd ();
    }

    inline void iDrawQuadrangle (const OpenSteer::Vec3& a,
                                 const OpenSteer::Vec3& b,
                                 const OpenSteer::Vec3& c,
                                 const OpenSteer::Vec3& d,
                                 const OpenSteer::Color& color)
    {
        OpenSteer::warnIfInUpdatePhase ("iDrawQuadrangle");
        glColor3f (color.r(), color.g(), color.b());
        glBegin (GL_QUADS);
        {
            OpenSteer::glVertexVec3 (a);
            OpenSteer::glVertexVec3 (b);
            OpenSteer::glVertexVec3 (c);
            OpenSteer::glVertexVec3 (d);
        }
        glEnd ();
    }

    inline void beginDoubleSidedDrawing (void)
    {
        glPushAttrib (GL_ENABLE_BIT);
        glDisable (GL_CULL_FACE);
    }

    inline void endDoubleSidedDrawing (void)
    {
        glPopAttrib ();
    }

    inline GLint begin2dDrawing (float w, float h)
    {

        GLint originalMatrixMode;
        glGetIntegerv (GL_MATRIX_MODE, &originalMatrixMode);

        glMatrixMode (GL_PROJECTION);
        glPushMatrix ();
        glLoadIdentity ();

        glOrtho (0.0f, w, 0.0f, h, -1.0f, 1.0f);

        glMatrixMode (GL_MODELVIEW);
        glPushMatrix ();
        glLoadIdentity ();

        return originalMatrixMode;
    }

    inline void end2dDrawing (GLint originalMatrixMode)
    {

        glPopMatrix ();
        glMatrixMode (GL_PROJECTION);
        glPopMatrix ();

        glMatrixMode (originalMatrixMode);
    }

}   

void 
OpenSteer::glVertexVec3 (const Vec3& v)
{
    iglVertexVec3 (v);
}

void 
OpenSteer::warnIfInUpdatePhase2 (const char* name)
{
    std::ostringstream message;
    message << "use annotation (during simulation update, do not call ";
    message << name;
    message << ")";
    message << std::ends;
    std::cerr << message.str();       
}

void 
OpenSteer::drawLine (const Vec3& startPoint,
                     const Vec3& endPoint,
                     const Color& color)
{
    iDrawLine (startPoint, endPoint, color);
}

void 
OpenSteer::drawLineAlpha (const Vec3& startPoint,
                          const Vec3& endPoint,
                          const Color& color,
                          const float alpha)
{
    warnIfInUpdatePhase ("drawLineAlpha");
    glColor4f (color.r(), color.g(), color.b(), alpha);
    glBegin (GL_LINES);
    OpenSteer::glVertexVec3 (startPoint);
    OpenSteer::glVertexVec3 (endPoint);
    glEnd ();
}

void 
OpenSteer::drawTriangle (const Vec3& a,
                         const Vec3& b,
                         const Vec3& c,
                         const Color& color)
{
    iDrawTriangle (a, b, c, color);
}

void 
OpenSteer::drawQuadrangle (const Vec3& a,
                           const Vec3& b,
                           const Vec3& c,
                           const Vec3& d,
                           const Color& color)
{
    iDrawQuadrangle (a, b, c, d, color);
}

void 
OpenSteer::drawXZWideLine (const Vec3& startPoint,
                           const Vec3& endPoint,
                           const Color& color,
                           float width)
{
    warnIfInUpdatePhase ("drawXZWideLine");

    const Vec3 offset = endPoint - startPoint;
    const Vec3 along = offset.normalize();
    const Vec3 perp = gGlobalSpace.localRotateForwardToSide (along);
    const Vec3 radius = perp * width / 2;

    const Vec3 a = startPoint + radius;
    const Vec3 b = endPoint + radius;
    const Vec3 c = endPoint - radius;
    const Vec3 d = startPoint - radius;

    iDrawQuadrangle (a, b, c, d, color);
}

void 
OpenSteer::drawCircleOrDisk (const float radius,
                             const Vec3& axis,
                             const Vec3& center,
                             const Color& color,
                             const int segments,
                             const bool filled,
                             const bool in3d)
{
    LocalSpace ls;
    if (in3d)
    {

        const Vec3 unitAxis = axis.normalize ();
        const Vec3 unitPerp = findPerpendicularIn3d (axis).normalize ();
        ls.setUp (unitAxis);
        ls.setForward (unitPerp);
        ls.setPosition (center);
        ls.setUnitSideFromForwardAndUp ();
    }

    if (filled) beginDoubleSidedDrawing ();

    Vec3 pointOnCircle (radius, 0, 0);
    const float step = (2 * OPENSTEER_M_PI) / segments;

    glColor3f (color.r(), color.g(), color.b());

    glBegin (filled ? GL_TRIANGLE_FAN : GL_LINE_LOOP);

    if (filled) iglVertexVec3 (in3d ? ls.position() : center);

    float sin=0, cos=0;
    const int vertexCount = filled ? segments+1 : segments;
    for (int i = 0; i < vertexCount; i++)
    {

        iglVertexVec3 (in3d ?
                           ls.globalizePosition (pointOnCircle) :
                           (Vec3) (pointOnCircle + center));

        pointOnCircle = pointOnCircle.rotateAboutGlobalY (step, sin, cos);
    }

    glEnd ();
    if (filled) endDoubleSidedDrawing ();
}

void 
OpenSteer::draw3dCircleOrDisk (const float radius,
                               const Vec3& center,
                               const Vec3& axis,
                               const Color& color,
                               const int segments,
                               const bool filled)
{

    drawCircleOrDisk (radius, axis, center, color, segments, filled, true);
}

void 
OpenSteer::drawXZCircleOrDisk (const float radius,
                               const Vec3& center,
                               const Color& color,
                               const int segments,
                               const bool filled)
{

    drawCircleOrDisk (radius, Vec3::zero, center, color, segments, filled, false);
}

void 
OpenSteer::drawXZArc (const Vec3& start,
                      const Vec3& center,
                      const float arcLength,
                      const int segments,
                      const Color& color)
{
    warnIfInUpdatePhase ("drawXZArc");

    Vec3 spoke = start - center;

    const float radius = spoke.length ();
    const float twoPi = 2 * OPENSTEER_M_PI;
    const float circumference = twoPi * radius;
    const float arcAngle = twoPi * arcLength / circumference;
    const float step = arcAngle / segments;

    glColor3f (color.r(), color.g(), color.b());

    glBegin (GL_LINE_STRIP);

    float sin=0, cos=0;
    for (int i = 0; i < segments; i++)
    {

        iglVertexVec3 (spoke + center);

        spoke = spoke.rotateAboutGlobalY (step, sin, cos);
    }

    glEnd ();
}

void 
OpenSteer::drawBasic2dCircularVehicle (const AbstractVehicle* vehicle,
                                       const Color& color)
{

    const float x = 0.5f;
    const float y = sqrtXXX (1 - (x * x));

    const float r = vehicle->radius();
    const Vec3& p = vehicle->position();

    const Vec3 u = r * 0.05f * Vec3 (0, 1, 0); 
    const Vec3 f = r * vehicle->forward();
    const Vec3 s = r * vehicle->side() * x;
    const Vec3 b = r * vehicle->forward() * -y;

    beginDoubleSidedDrawing ();
    iDrawTriangle (p + f + u,
                   p + b - s + u,
                   p + b + s + u,
                   color);
    endDoubleSidedDrawing ();

    drawXZCircle (r, p + u, gWhite, 20);
}

void 
OpenSteer::drawBasic3dSphericalVehicle (const AbstractVehicle* vehicle,
                                        const Color& color)
{

    const float x = 0.5f;
    const float y = sqrtXXX (1 - (x * x));

    const float r = vehicle->radius();
    const Vec3& p = vehicle->position();

    const Vec3 f = r * vehicle->forward();
    const Vec3 s = r * vehicle->side() * x;
    const Vec3 u = r * vehicle->up() * x * 0.5f;
    const Vec3 b = r * vehicle->forward() * -y;

    const Vec3 nose   = p + f;
    const Vec3 side1  = p + b - s;
    const Vec3 side2  = p + b + s;
    const Vec3 top    = p + b + u;
    const Vec3 bottom = p + b - u;

    const float j = +0.05f;
    const float k = -0.05f;
    const Color color1 = color + Color(j, j, k);
    const Color color2 = color + Color(j, k, j);
    const Color color3 = color + Color(k, j, j);
    const Color color4 = color + Color(k, j, k);
    const Color color5 = color + Color(k, k, j);

    iDrawTriangle (nose,  side1,  top,    color1);  
    iDrawTriangle (nose,  top,    side2,  color2);  
    iDrawTriangle (nose,  bottom, side1,  color3);  
    iDrawTriangle (nose,  side2,  bottom, color4);  
    iDrawTriangle (side1, side2,  top,    color5);  
    iDrawTriangle (side2, side1,  bottom, color5);  
}

void 
OpenSteer::drawBasic3dSphericalVehicle (drawTriangleRoutine draw, const AbstractVehicle* vehicle,
                                        const Color& color)
{

    const float x = 0.5f;
    const float y = sqrtXXX (1 - (x * x));

    const float r = vehicle->radius();
    const Vec3& p = vehicle->position();

    const Vec3 f = r * vehicle->forward();
    const Vec3 s = r * vehicle->side() * x;
    const Vec3 u = r * vehicle->up() * x * 0.5f;
    const Vec3 b = r * vehicle->forward() * -y;

    const Vec3 nose   = p + f;
    const Vec3 side1  = p + b - s;
    const Vec3 side2  = p + b + s;
    const Vec3 top    = p + b + u;
    const Vec3 bottom = p + b - u;

    const float j = +0.05f;
    const float k = -0.05f;
    const Color color1 = color + Color (j, j, k);
    const Color color2 = color + Color (j, k, j);
    const Color color3 = color + Color (k, j, j);
    const Color color4 = color + Color (k, j, k);
    const Color color5 = color + Color (k, k, j);

    draw (nose,  side1,  top,    color1);  
    draw (nose,  top,    side2,  color2);  
    draw (nose,  bottom, side1,  color3);  
    draw (nose,  side2,  bottom, color4);  
    draw (side1, side2,  top,    color5);  
    draw (side2, side1,  bottom, color5);  
}

void 
OpenSteer::drawXZCheckerboardGrid (const float size,
                                   const int subsquares,
                                   const Vec3& center,
                                   const Color& color1,
                                   const Color& color2)
{
    const float half = size/2;
    const float spacing = size / subsquares;

    beginDoubleSidedDrawing ();
    {
        bool flag1 = false;
        float p = -half;
        Vec3 corner;
        for (int i = 0; i < subsquares; i++)
        {
            bool flag2 = flag1;
            float q = -half;
            for (int j = 0; j < subsquares; j++)
            {
                corner.set (p, 0, q);
                corner += center;
                iDrawQuadrangle (corner,
                                 corner + Vec3 (spacing, 0,       0),
                                 corner + Vec3 (spacing, 0, spacing),
                                 corner + Vec3 (0,       0, spacing),
                                 flag2 ? color1 : color2);
                flag2 = !flag2;
                q += spacing;
            }
            flag1 = !flag1;
            p += spacing;
        }
    }
    endDoubleSidedDrawing ();
}

void 
OpenSteer::drawXZLineGrid (const float size,
                           const int subsquares,
                           const Vec3& center,
                           const Color& color)
{
    warnIfInUpdatePhase ("drawXZLineGrid");

    const float half = size/2;
    const float spacing = size / subsquares;

    glColor3f (color.r(), color.g(), color.b());

    glBegin (GL_LINES);
    float q = -half;
    for (int i = 0; i < (subsquares + 1); i++)
    {
        const Vec3 x1 (q, 0, +half); 
        const Vec3 x2 (q, 0, -half);
        const Vec3 z1 (+half, 0, q); 
        const Vec3 z2 (-half, 0, q);

        iglVertexVec3 (x1 + center);
        iglVertexVec3 (x2 + center);
        iglVertexVec3 (z1 + center);
        iglVertexVec3 (z2 + center);

        q += spacing;
    }
    glEnd ();
}

void 
OpenSteer::drawAxes  (const AbstractLocalSpace& ls,
                      const Vec3& size,
                      const Color& color)
{
    const Vec3 x (size.x / 2, 0, 0);
    const Vec3 y (0, size.y / 2, 0);
    const Vec3 z (0, 0, size.z / 2);

    iDrawLine (ls.globalizePosition (x), ls.globalizePosition (x * -1), color);
    iDrawLine (ls.globalizePosition (y), ls.globalizePosition (y * -1), color);
    iDrawLine (ls.globalizePosition (z), ls.globalizePosition (z * -1), color);
}

void 
OpenSteer::drawBoxOutline  (const AbstractLocalSpace* localSpace,
                            const Vec3& size,
                            const Color& color)
{
    const Vec3 s = size / 2.0f;  

    const Vec3 a (+s.x, +s.y, +s.z);
    const Vec3 b (+s.x, -s.y, +s.z);
    const Vec3 c (-s.x, -s.y, +s.z);
    const Vec3 d (-s.x, +s.y, +s.z);

    const Vec3 e (+s.x, +s.y, -s.z);
    const Vec3 f (+s.x, -s.y, -s.z);
    const Vec3 g (-s.x, -s.y, -s.z);
    const Vec3 h (-s.x, +s.y, -s.z);

    const Vec3 A = localSpace->globalizePosition (a);
    const Vec3 B = localSpace->globalizePosition (b);
    const Vec3 C = localSpace->globalizePosition (c);
    const Vec3 D = localSpace->globalizePosition (d);

    const Vec3 E = localSpace->globalizePosition (e);
    const Vec3 F = localSpace->globalizePosition (f);
    const Vec3 G = localSpace->globalizePosition (g);
    const Vec3 H = localSpace->globalizePosition (h);

    iDrawLine (A, B, color);
    iDrawLine (B, C, color);
    iDrawLine (C, D, color);
    iDrawLine (D, A, color);

    iDrawLine (A, E, color);
    iDrawLine (B, F, color);
    iDrawLine (C, G, color);
    iDrawLine (D, H, color);

    iDrawLine (E, F, color);
    iDrawLine (F, G, color);
    iDrawLine (G, H, color);
    iDrawLine (H, E, color);
}

namespace {

    inline void drawCameraLookAtCheck (const OpenSteer::Vec3& cameraPosition,
                                       const OpenSteer::Vec3& pointToLookAt,
                                       const OpenSteer::Vec3& up)
    {
        const OpenSteer::Vec3 view = pointToLookAt - cameraPosition;
        const OpenSteer::Vec3 perp = view.perpendicularComponent (up);
        if (perp == OpenSteer::Vec3::zero)
            std::cerr << "OpenSteer - LookAt: degenerate camera";
    }

} 

void 
OpenSteer::drawCameraLookAt (const Vec3& cameraPosition,
                             const Vec3& pointToLookAt,
                             const Vec3& up_)
{

    drawCameraLookAtCheck (cameraPosition, pointToLookAt, up_);

    glLoadIdentity();
    float matrix2[16];

    Vec3 forward = pointToLookAt - cameraPosition;
    forward = forward.normalize();
    Vec3 side;
    side.cross(forward, up_);
    side = side.normalize();
    Vec3 up;
    up.cross(side, forward);

    matrix2[0] = side.x;
    matrix2[4] = side.y;
    matrix2[8] = side.z;
    matrix2[12] = 0.f;
    matrix2[1] = up.x;
    matrix2[5] = up.y;
    matrix2[9] = up.z;
    matrix2[13] = 0.f;
    matrix2[2] = -forward.x;
    matrix2[6] = -forward.y;
    matrix2[10] = -forward.z;
    matrix2[14] = 0.f;
    matrix2[3] = matrix2[7] = matrix2[11] = 0.f;
    matrix2[15] = 1.f;

    glLoadMatrixf(matrix2);
    glTranslatef(-cameraPosition.x, -cameraPosition.y, -cameraPosition.z);
}

void 
OpenSteer::draw2dLine (const Vec3& startPoint,
                       const Vec3& endPoint,
                       const Color& color, 
                       float w, float h)
{
    const GLint originalMatrixMode = begin2dDrawing (w, h);

    iDrawLine (startPoint, endPoint, color);

    end2dDrawing (originalMatrixMode);
}

void 
OpenSteer::drawReticle (float w, float h)
{
    const int a = 10;
    const int b = 30;

    draw2dLine (Vec3 (w+a, h,   0), Vec3 (w+b, h,   0), gWhite, w, h);
    draw2dLine (Vec3 (w,   h+a, 0), Vec3 (w,   h+b, 0), gWhite, w, h);
    draw2dLine (Vec3 (w-a, h,   0), Vec3 (w-b, h,   0), gWhite, w, h);
    draw2dLine (Vec3 (w,   h-a, 0), Vec3 (w,   h-b, 0), gWhite, w, h);

    glLineWidth (3);
    draw2dLine (Vec3 (w+a, h,   0), Vec3 (w+b, h,   0), gBlack, w, h);
    draw2dLine (Vec3 (w,   h+a, 0), Vec3 (w,   h+b, 0), gBlack, w, h);
    draw2dLine (Vec3 (w-a, h,   0), Vec3 (w-b, h,   0), gBlack, w, h);
    draw2dLine (Vec3 (w,   h-a, 0), Vec3 (w,   h-b, 0), gBlack, w, h);
    glLineWidth (1);
}

void 
OpenSteer::checkForDrawError (const char * locationDescription)
{
    checkForGLError (locationDescription);
}

OpenSteer::Vec3 
OpenSteer::directionFromCameraToScreenPosition (int x, int y, int h)
{
    return Vec3(1, 0, 0);

#if 0

    GLint vp[4];
    GLdouble mMat[16], pMat[16];
    glGetIntegerv (GL_VIEWPORT, vp);
    glGetDoublev (GL_MODELVIEW_MATRIX, mMat);
    glGetDoublev (GL_PROJECTION_MATRIX, pMat);
    GLdouble un0x, un0y, un0z, un1x, un1y, un1z;

    gluUnProject (x, h-y, 0, mMat, pMat, vp, &un0x, &un0y, &un0z);
    gluUnProject (x, h-y, 1, mMat, pMat, vp, &un1x, &un1y, &un1z);

    const Vec3 diffNearFar (static_cast<float>(un1x-un0x), static_cast<float>(un1y-un0y), static_cast<float>(un1z-un0z));
    const Vec3 direction = diffNearFar.normalize ();
    return direction;
#endif
}

namespace {

    class DeferredLine
    {
    public:

        static void addToBuffer (const OpenSteer::Vec3& s,
                                 const OpenSteer::Vec3& e,
                                 const OpenSteer::Color& c)
        {
            DeferredLine dl;
            dl.startPoint = s;
            dl.endPoint = e;
            dl.color = c;

            lines.push_back (dl);
        }

        static void drawAll (void)
        {

            for (DeferredLines::iterator i = lines.begin();
                 i < lines.end();
                 i++)
            {
                DeferredLine& dl = *i;
                iDrawLine (dl.startPoint, dl.endPoint, dl.color);
            }

            lines.clear ();
        }

        typedef std::vector<DeferredLine> DeferredLines;

    private:

        OpenSteer::Vec3 startPoint;
        OpenSteer::Vec3 endPoint;
        OpenSteer::Color color;

        static DeferredLines lines;
    };

DeferredLine::DeferredLines DeferredLine::lines;

} 

void 
OpenSteer::deferredDrawLine (const Vec3& startPoint,
                             const Vec3& endPoint,
                             const Color& color)
{
    DeferredLine::addToBuffer (startPoint, endPoint, color);
}

void 
OpenSteer::drawAllDeferredLines (void)
{
    DeferredLine::drawAll ();
}

namespace {

    class DeferredCircle
    {
    public:

        static void addToBuffer (const float radius,
                                 const OpenSteer::Vec3& axis,
                                 const OpenSteer::Vec3& center,
                                 const OpenSteer::Color& color,
                                 const int segments,
                                 const bool filled,
                                 const bool in3d)
        {
            DeferredCircle dc;
            dc.radius   = radius;
            dc.axis     = axis;
            dc.center   = center;
            dc.color    = color;
            dc.segments = segments;
            dc.filled   = filled;
            dc.in3d     = in3d;
            circles.push_back (dc);
        }

        static void drawAll (void)
        {

            for (DeferredCircles::iterator i = circles.begin();
                 i < circles.end();
                 i++)
            {
                DeferredCircle& dc = *i;
                drawCircleOrDisk (dc.radius, dc.axis, dc.center, dc.color,
                                  dc.segments, dc.filled, dc.in3d);
            }

            circles.clear ();
        }

        typedef std::vector<DeferredCircle> DeferredCircles;

    private:

        float radius;
        OpenSteer::Vec3 axis;
        OpenSteer::Vec3 center;
        OpenSteer::Color color;
        int segments;
        bool filled;
        bool in3d;

        static DeferredCircles circles;
    };

DeferredCircle::DeferredCircles DeferredCircle::circles;

} 

void 
OpenSteer::deferredDrawCircleOrDisk (const float radius,
                                     const Vec3& axis,
                                     const Vec3& center,
                                     const Color& color,
                                     const int segments,
                                     const bool filled,
                                     const bool in3d)
{
    DeferredCircle::addToBuffer (radius, axis, center, color,
                                 segments, filled, in3d);
}

void 
OpenSteer::drawAllDeferredCirclesOrDisks (void)
{
    DeferredCircle::drawAll ();
}

void 
OpenSteer::draw2dTextAt3dLocation (const char& text,
                                   const Vec3& location,
                                   const Color& color, float w, float h)
{

    glColor3f (color.r(), color.g(), color.b());
    glRasterPos3f (location.x, location.y, location.z);

    GLint rasterPosition[4];
    glGetIntegerv (GL_CURRENT_RASTER_POSITION, rasterPosition);
    const GLint originalMatrixMode = begin2dDrawing (w, h);

    int lines = 0;
    for (const char* p = &text; *p; p++)
    {
        if (*p == '\n')
        {

            lines++;
            const int fontHeight = 15; 
            const int vOffset = lines * (fontHeight + 1);
            glRasterPos2i (rasterPosition[0], rasterPosition[1] - vOffset);
        }
        else
        {

            #ifndef HAVE_NO_GLUT

            #else

            #endif
        }
    }

    end2dDrawing (originalMatrixMode);
}

void 
OpenSteer::draw2dTextAt3dLocation (const std::ostringstream& text,
                                   const Vec3& location,
                                   const Color& color, float w, float h)
{
    draw2dTextAt3dLocation (*text.str().c_str(), location, color, w, h);
}

void 
OpenSteer::draw2dTextAt2dLocation (const char& text,
                                   const Vec3 location,
                                   const Color& color, float w, float h)
{
    const GLint originalMatrixMode = begin2dDrawing (w, h);

    draw2dTextAt3dLocation (text, location, color, w, h);

    end2dDrawing (originalMatrixMode);
}

void 
OpenSteer::draw2dTextAt2dLocation (const std::ostringstream& text,
                                   const Vec3 location,
                                   const Color& color, float w, float h)
{
    draw2dTextAt2dLocation (*text.str().c_str(), location, color, w, h);
}

namespace OpenSteer {

    class DrawSphereHelper
    {
    public:
        Vec3 center;
        float radius;
        float maxEdgeLength;
        bool filled;
        Color color;
        bool drawFrontFacing;
        bool drawBackFacing;
        Vec3 viewpoint;

        DrawSphereHelper ()
            : center (Vec3::zero),
              radius (1.0f),
              maxEdgeLength (1.0f),
              filled (false),
              color (gWhite),
              drawFrontFacing (true),
              drawBackFacing (true),
              viewpoint (Vec3::zero)
        {}

        DrawSphereHelper (const Vec3 _center,
                          const float _radius,
                          const float _maxEdgeLength,
                          const bool _filled,
                          const Color& _color,
                          const bool _drawFrontFacing,
                          const bool _drawBackFacing,
                          const Vec3& _viewpoint)
            : center (_center),
              radius (_radius),
              maxEdgeLength (_maxEdgeLength),
              filled (_filled),
              color (_color),
              drawFrontFacing (_drawFrontFacing),
              drawBackFacing (_drawBackFacing),
              viewpoint (_viewpoint)
        {}

        void draw (void) const
        {

            const float sqrt5 = sqrtXXX (5.0f);
            const float phi = (1.0f + sqrt5) * 0.5f; 

            const float ratio = sqrtXXX (10.0f + (2.0f * sqrt5)) / (4.0f * phi);
            const float a = (radius / ratio) * 0.5f;
            const float b = (radius / ratio) / (2.0f * phi);

            const Vec3 v1  = center + Vec3 ( 0,  b, -a);
            const Vec3 v2  = center + Vec3 ( b,  a,  0);
            const Vec3 v3  = center + Vec3 (-b,  a,  0);
            const Vec3 v4  = center + Vec3 ( 0,  b,  a);
            const Vec3 v5  = center + Vec3 ( 0, -b,  a);
            const Vec3 v6  = center + Vec3 (-a,  0,  b);
            const Vec3 v7  = center + Vec3 ( 0, -b, -a);
            const Vec3 v8  = center + Vec3 ( a,  0, -b);
            const Vec3 v9  = center + Vec3 ( a,  0,  b);
            const Vec3 v10 = center + Vec3 (-a,  0, -b);
            const Vec3 v11 = center + Vec3 ( b, -a,  0);
            const Vec3 v12 = center + Vec3 (-b, -a,  0);

            drawMeshedTriangleOnSphere (v1, v2, v3);
            drawMeshedTriangleOnSphere (v4, v3, v2);
            drawMeshedTriangleOnSphere (v4, v5, v6);
            drawMeshedTriangleOnSphere (v4, v9, v5);
            drawMeshedTriangleOnSphere (v1, v7, v8);
            drawMeshedTriangleOnSphere (v1, v10, v7);
            drawMeshedTriangleOnSphere (v5, v11, v12);
            drawMeshedTriangleOnSphere (v7, v12, v11);
            drawMeshedTriangleOnSphere (v3, v6, v10);
            drawMeshedTriangleOnSphere (v12, v10, v6);
            drawMeshedTriangleOnSphere (v2, v8, v9);
            drawMeshedTriangleOnSphere (v11, v9, v8);
            drawMeshedTriangleOnSphere (v4, v6, v3);
            drawMeshedTriangleOnSphere (v4, v2, v9);
            drawMeshedTriangleOnSphere (v1, v3, v10);
            drawMeshedTriangleOnSphere (v1, v8, v2);
            drawMeshedTriangleOnSphere (v7, v10, v12);
            drawMeshedTriangleOnSphere (v7, v11, v8);
            drawMeshedTriangleOnSphere (v5, v12, v6);
            drawMeshedTriangleOnSphere (v5, v9, v11);
        }

        inline Vec3 midpointOnSphere (const Vec3& a, const Vec3& b) const
        {
            const Vec3 midpoint = (a + b) * 0.5f;
            const Vec3 unitRadial = (midpoint - center).normalize ();
            return center + (unitRadial * radius);
        }

        void drawMeshedTriangleOnSphere (const Vec3& a, 
                                         const Vec3& b,
                                         const Vec3& c) const
        {

            if ((((a - b).length ()) < maxEdgeLength) &&
                (((b - c).length ()) < maxEdgeLength) &&
                (((c - a).length ()) < maxEdgeLength))
            {

                drawTriangleOnSphere (a, b, c);
            }
            else 
            {

                const Vec3 ab = midpointOnSphere (a, b);
                const Vec3 bc = midpointOnSphere (b, c);
                const Vec3 ca = midpointOnSphere (c, a);

                drawMeshedTriangleOnSphere ( a, ab, ca);
                drawMeshedTriangleOnSphere (ab,  b, bc);
                drawMeshedTriangleOnSphere (ca, bc,  c);
                drawMeshedTriangleOnSphere (ab, bc, ca);
            }
        }

        void drawTriangleOnSphere (const Vec3& a, 
                                   const Vec3& b,
                                   const Vec3& c) const
        {

            const Vec3 triCenter = (a + b + c) / 3.0f;
            const Vec3 triNormal = triCenter - center; 
            const Vec3 view = triCenter - viewpoint;
            const float dot = view.dot (triNormal); 
            const bool seen = ((dot>0.0f) ? drawBackFacing : drawFrontFacing);
            if (seen)
            {
                if (filled)
                {

                    if (drawFrontFacing)
                        drawTriangle (c, b, a, color);
                    else
                        drawTriangle (a, b, c, color);
                }
                else
                {

                    const float unitDot = view.dot (triNormal.normalize ());
                    const float t = 0.05f; 
                    const bool nearSilhouette = (unitDot<t) || (unitDot>-t);
                    if (nearSilhouette && !(drawBackFacing&&drawFrontFacing))
                    {
                        drawLine (a, b, color);
                        drawLine (b, c, color);
                        drawLine (c, a, color);
                    }
                    else
                    {
                        drawMeshedTriangleLine (a, b, color);
                        drawMeshedTriangleLine (b, c, color);
                        drawMeshedTriangleLine (c, a, color);
                    }
                }
            }
        }

        void drawMeshedTriangleLine (const Vec3& a, 
                                     const Vec3& b,
                                     const Color& color) const
        {
            if (a.x != b.x)
            {
                if (a.x > b.x) drawLine (a, b, color);
            }
            else
            {
                if (a.y != b.y)
                {
                    if (a.y > b.y) drawLine (a, b, color); 
                }
                else
                {
                    if (a.z > b.z) drawLine (a, b, color); 
                }
            }
        }

    };

    void drawSphere (const Vec3 center,
                     const float radius,
                     const float maxEdgeLength,
                     const bool filled,
                     const Color& color,
                     const bool drawFrontFacing,
                     const bool drawBackFacing,
                     const Vec3& viewpoint)
    {
        const DrawSphereHelper s (center, radius, maxEdgeLength, filled, color,
                                  drawFrontFacing, drawBackFacing, viewpoint);
        s.draw ();
    }

    void drawSphereObstacle (const SphereObstacle& so,
                             const float maxEdgeLength,
                             const bool filled,
                             const Color& color,
                             const Vec3& viewpoint)
    {
        bool front, back;
        switch (so.seenFrom ())
        {
        default:
        case Obstacle::both:    front = true;  back = true;  break;
        case Obstacle::inside:  front = false; back = true;  break;
        case Obstacle::outside: front = true;  back = false; break;
        }
        drawSphere (so.center, so.radius, maxEdgeLength,
                    filled, color, front, back, viewpoint);
    }

} 