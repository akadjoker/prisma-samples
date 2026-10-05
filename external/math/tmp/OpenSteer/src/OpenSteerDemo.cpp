
#include "OpenSteer/OpenSteerDemo.h"
#include "OpenSteer/Annotation.h"
#include "OpenSteer/Color.h"
#include "OpenSteer/Vec3.h"

#include <algorithm>
#include <string>
#include <sstream>
#include <iomanip>

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

GLFWwindow* demo_window = nullptr;

OpenSteer::Clock OpenSteer::OpenSteerDemo::clock;

OpenSteer::Camera OpenSteer::OpenSteerDemo::camera;

OpenSteer::PlugIn* OpenSteer::OpenSteerDemo::selectedPlugIn = NULL;

OpenSteer::AbstractVehicle* OpenSteer::OpenSteerDemo::selectedVehicle = NULL;

int OpenSteer::OpenSteerDemo::phase = OpenSteer::OpenSteerDemo::overheadPhase;

bool OpenSteer::enableAnnotation = true;

const int OpenSteer::OpenSteerDemo::overheadPhase = 0;
const int OpenSteer::OpenSteerDemo::updatePhase = 1;
const int OpenSteer::OpenSteerDemo::drawPhase = 2;

namespace {

    void printPlugIn (OpenSteer::PlugIn& pi) {std::cout << " " << pi << std::endl;} 

} 

void 
OpenSteer::OpenSteerDemo::initialize (void)
{

    selectDefaultPlugIn ();

    {

        std::cout << std::endl << "Known plugins:" << std::endl;   
        PlugIn::applyToAll (printPlugIn);                          
        std::cout << std::endl;                                    

        if (!selectedPlugIn) errorExit ("no default PlugIn");
        std::cout << std::endl << "Default plugin:" << std::endl;  
        std::cout << " " << *selectedPlugIn << std::endl;          
        std::cout << std::endl;                                    
    }

    openSelectedPlugIn ();
}

void 
OpenSteer::OpenSteerDemo::updateSimulationAndRedraw (void)
{

    clock.update ();

    initPhaseTimers ();

    updateSelectedPlugIn (clock.getTotalSimulationTime (),
                          clock.getElapsedSimulationTime ());

    redrawSelectedPlugIn (clock.getTotalRealTime (),
                          clock.getElapsedRealTime ());
}

void 
OpenSteer::OpenSteerDemo::errorExit (const char* message)
{
    printMessage (message);
#ifdef _MSC_VER
	MessageBox(0, message, "OpenSteerDemo Unfortunate Event", MB_ICONERROR);
#endif
    exit (-1);
}

void 
OpenSteer::OpenSteerDemo::exit (int exitCode)
{
    ::exit (exitCode);
}

void 
OpenSteer::OpenSteerDemo::selectDefaultPlugIn (void)
{
    PlugIn::sortBySelectionOrder ();
    selectedPlugIn = PlugIn::findDefault ();
}

void 
OpenSteer::OpenSteerDemo::selectNextPlugIn (void)
{
    closeSelectedPlugIn ();
    selectedPlugIn = selectedPlugIn->next ();
    openSelectedPlugIn ();
}

void 
OpenSteer::OpenSteerDemo::functionKeyForPlugIn (int keyNumber)
{
    selectedPlugIn->handleFunctionKeys (keyNumber);
}

const char* 
OpenSteer::OpenSteerDemo::nameOfSelectedPlugIn (void)
{
    return (selectedPlugIn ? selectedPlugIn->name() : "no PlugIn");
}

void 
OpenSteer::OpenSteerDemo::openSelectedPlugIn (void)
{
    camera.reset ();
    selectedVehicle = NULL;
    selectedPlugIn->open ();
}

void 
OpenSteer::OpenSteerDemo::updateSelectedPlugIn (const float currentTime,
                                                const float elapsedTime)
{

    pushPhase (updatePhase);

    doDelayedResetPlugInXXX ();

    if (selectedVehicle == NULL)
    {
        const AVGroup& vehicles = allVehiclesOfSelectedPlugIn();
        if (vehicles.size() > 0) selectedVehicle = vehicles.front();
    }

    selectedPlugIn->update (currentTime, elapsedTime);

    popPhase ();
}

void 
OpenSteer::OpenSteerDemo::redrawSelectedPlugIn (const float currentTime,
                                                const float elapsedTime)
{

    pushPhase (drawPhase);

    selectedPlugIn->redraw (currentTime, elapsedTime);

    drawAllDeferredLines ();
    drawAllDeferredCirclesOrDisks ();

    popPhase ();
}

void 
OpenSteer::OpenSteerDemo::closeSelectedPlugIn (void)
{
    selectedPlugIn->close ();
    selectedVehicle = NULL;
}

void 
OpenSteer::OpenSteerDemo::resetSelectedPlugIn (void)
{
    selectedPlugIn->reset ();
}

namespace {

    bool gDelayedResetPlugInXXX = false;

} 

void 
OpenSteer::OpenSteerDemo::queueDelayedResetPlugInXXX (void)
{
    gDelayedResetPlugInXXX = true;
}

void 
OpenSteer::OpenSteerDemo::doDelayedResetPlugInXXX (void)
{
    if (gDelayedResetPlugInXXX)
    {
        resetSelectedPlugIn ();
        gDelayedResetPlugInXXX = false;
    }
}

const OpenSteer::AVGroup& 
OpenSteer::OpenSteerDemo::allVehiclesOfSelectedPlugIn (void)
{
    return selectedPlugIn->allVehicles ();
}

void 
OpenSteer::OpenSteerDemo::selectNextVehicle (void)
{
    if (selectedVehicle != NULL)
    {

        const AVGroup& all = allVehiclesOfSelectedPlugIn ();
        const AVIterator first = all.begin();
        const AVIterator last = all.end();

        const AVIterator s = std::find (first, last, selectedVehicle);

        selectedVehicle = *(s+1);

        if (s == last-1) selectedVehicle = *first;

        if (s == last) selectedVehicle = NULL;
    }
}

void 
OpenSteer::OpenSteerDemo::selectVehicleNearestScreenPosition (int x, int y)
{
    selectedVehicle = findVehicleNearestScreenPosition (x, y);
}

OpenSteer::AbstractVehicle* 
OpenSteer::OpenSteerDemo::vehicleNearestToMouse (void)
{
    return (mouseInWindow ? 
            findVehicleNearestScreenPosition (mouseX, mouseY) :
            NULL);
}

OpenSteer::AbstractVehicle* 
OpenSteer::OpenSteerDemo::findVehicleNearestScreenPosition (int x, int y)
{

    return nullptr;
#if 0

    const Vec3 direction = directionFromCameraToScreenPosition (x, y, glutGet (GLUT_WINDOW_HEIGHT));

    float minDistance = FLT_MAX;       
    AbstractVehicle* nearest = NULL;   
    const AVGroup& vehicles = allVehiclesOfSelectedPlugIn();
    for (AVIterator i = vehicles.begin(); i != vehicles.end(); i++)
    {

        const float d = distanceFromLine ((**i).position(),
                                          camera.position(),
                                          direction);

        if (d < minDistance)
        {
            minDistance = d;
            nearest = *i;
        }
    }

    return nearest;
#endif
}

int OpenSteer::OpenSteerDemo::mouseX = 0;
int OpenSteer::OpenSteerDemo::mouseY = 0;
bool OpenSteer::OpenSteerDemo::mouseInWindow = false;

void 
OpenSteer::OpenSteerDemo::init3dCamera (AbstractVehicle& selected)
{
    init3dCamera (selected, cameraTargetDistance, camera2dElevation);
}

void 
OpenSteer::OpenSteerDemo::init3dCamera (AbstractVehicle& selected,
                                  float distance,
                                  float elevation)
{
    position3dCamera (selected, distance, elevation);
    camera.fixedDistDistance = distance;
    camera.fixedDistVOffset = elevation;
    camera.mode = Camera::cmFixedDistanceOffset;
}

void 
OpenSteer::OpenSteerDemo::init2dCamera (AbstractVehicle& selected)
{
    init2dCamera (selected, cameraTargetDistance, camera2dElevation);
}

void 
OpenSteer::OpenSteerDemo::init2dCamera (AbstractVehicle& selected,
                                  float distance,
                                  float elevation)
{
    position2dCamera (selected, distance, elevation);
    camera.fixedDistDistance = distance;
    camera.fixedDistVOffset = elevation;
    camera.mode = Camera::cmFixedDistanceOffset;
}

void 
OpenSteer::OpenSteerDemo::position3dCamera (AbstractVehicle& selected)
{
    position3dCamera (selected, cameraTargetDistance, camera2dElevation);
}

void 
OpenSteer::OpenSteerDemo::position3dCamera (AbstractVehicle& selected,
                                            float distance,
                                            float )
{
    selectedVehicle = &selected;
    if (&selected)
    {
        const Vec3 behind = selected.forward() * -distance;
        camera.setPosition (selected.position() + behind);
        camera.target = selected.position();
    }
}

void 
OpenSteer::OpenSteerDemo::position2dCamera (AbstractVehicle& selected)
{
    position2dCamera (selected, cameraTargetDistance, camera2dElevation);
}

void 
OpenSteer::OpenSteerDemo::position2dCamera (AbstractVehicle& selected,
                                            float distance,
                                            float elevation)
{

    position3dCamera (selected, distance, elevation);

    Vec3 position3d = camera.position();
    position3d.y += elevation;
    camera.setPosition (position3d);
}

void 
OpenSteer::OpenSteerDemo::updateCamera (const float currentTime,
                                        const float elapsedTime,
                                        const AbstractVehicle* selected)
{
    camera.vehicleToTrack = selected;
    camera.update (currentTime, elapsedTime, clock.getPausedState ());
}

const float OpenSteer::OpenSteerDemo::camera2dElevation = 8;
const float OpenSteer::OpenSteerDemo::cameraTargetDistance = 13;
const OpenSteer::Vec3 OpenSteer::OpenSteerDemo::cameraTargetOffset (0, OpenSteer::OpenSteerDemo::camera2dElevation, 
                                                                    0);

void 
OpenSteer::OpenSteerDemo::gridUtility (const Vec3& gridTarget)
{

    const Vec3 gridCenter ((round (gridTarget.x * 0.5f) * 2),
                           (round (gridTarget.y * 0.5f) * 2) - .05f,
                           (round (gridTarget.z * 0.5f) * 2));

    const Color gray1(0.27f);
    const Color gray2(0.30f);

    drawXZCheckerboardGrid (50, 50, gridCenter, gray1, gray2);

}

void 
OpenSteer::OpenSteerDemo::highlightVehicleUtility (const AbstractVehicle* vehicle)
{
    if (vehicle)
        drawXZDisk (vehicle->radius(), vehicle->position(), gGray60, 20);
}

void 
OpenSteer::OpenSteerDemo::circleHighlightVehicleUtility (const AbstractVehicle* vehicle)
{
    if (vehicle) drawXZCircle (vehicle->radius () * 1.1f,
                                        vehicle->position(),
                                        gGray60,
                                        20);
}

void 
OpenSteer::OpenSteerDemo::drawBoxHighlightOnVehicle (const AbstractVehicle* v,
                                               const Color& color)
{
    if (v)
    {
        const float diameter = v->radius() * 2;
        const Vec3 size (diameter, diameter, diameter);
        drawBoxOutline (v, size, color);
    }
}

void 
OpenSteer::OpenSteerDemo::drawCircleHighlightOnVehicle (const AbstractVehicle* v,
                                                  const float radiusMultiplier,
                                                  const Color& color)
{
    if (v)
    {
        const Vec3& cPosition = camera.position();
        draw3dCircle  (v->radius() * radiusMultiplier,  
                       v->position(),                   
                       v->position() - cPosition,       
                       color,                          
                       20);                            
    }
}

void 
OpenSteer::OpenSteerDemo::printMessage (const char* message)
{
    std::cout << "OpenSteerDemo: " <<  message << std::endl << std::flush;
}

void 
OpenSteer::OpenSteerDemo::printMessage (const std::ostringstream& message)
{
    printMessage (message.str().c_str());
}

void 
OpenSteer::OpenSteerDemo::printWarning (const char* message)
{
    std::cout << "OpenSteerDemo: Warning: " <<  message << std::endl << std::flush;
}

void 
OpenSteer::OpenSteerDemo::printWarning (const std::ostringstream& message)
{
    printWarning (message.str().c_str());
}

void 
OpenSteer::OpenSteerDemo::keyboardMiniHelp (void)
{
    printMessage ("");
    printMessage ("defined single key commands:");
    printMessage ("  r      restart current PlugIn.");
    printMessage ("  s      select next vehicle.");
    printMessage ("  c      select next camera mode.");
    printMessage ("  f      select next preset frame rate");
    printMessage ("  Tab    select next PlugIn.");
    printMessage ("  a      toggle annotation on/off.");
    printMessage ("  Space  toggle between Run and Pause.");
    printMessage ("  ->     step forward one frame.");
    printMessage ("  Esc    exit.");
    printMessage ("");

    selectedPlugIn->printMiniHelpForFunctionKeys ();
}

int OpenSteer::OpenSteerDemo::phaseStackIndex = 0;
const int OpenSteer::OpenSteerDemo::phaseStackSize = 5;
int OpenSteer::OpenSteerDemo::phaseStack [OpenSteer::OpenSteerDemo::phaseStackSize];

namespace OpenSteer {
bool updatePhaseActive = false;
bool drawPhaseActive = false;
}

void 
OpenSteer::OpenSteerDemo::pushPhase (const int newPhase)
{
    updatePhaseActive = newPhase == OpenSteer::OpenSteerDemo::updatePhase;
    drawPhaseActive = newPhase == OpenSteer::OpenSteerDemo::drawPhase;

    updatePhaseTimers ();

    phaseStack[phaseStackIndex++] = phase;

    phase = newPhase;

    if (phaseStackIndex >= phaseStackSize) errorExit ("phaseStack overflow");
}

void 
OpenSteer::OpenSteerDemo::popPhase (void)
{

    updatePhaseTimers ();

    phase = phaseStack[--phaseStackIndex];
    updatePhaseActive = phase == OpenSteer::OpenSteerDemo::updatePhase;
    drawPhaseActive = phase == OpenSteer::OpenSteerDemo::drawPhase;
}

float OpenSteer::OpenSteerDemo::phaseTimerBase = 0;
float OpenSteer::OpenSteerDemo::phaseTimers [drawPhase+1];

void 
OpenSteer::OpenSteerDemo::initPhaseTimers (void)
{
    phaseTimers[drawPhase] = 0;
    phaseTimers[updatePhase] = 0;
    phaseTimers[overheadPhase] = 0;
    phaseTimerBase = clock.getTotalRealTime ();
}

void 
OpenSteer::OpenSteerDemo::updatePhaseTimers (void)
{
    const float currentRealTime = clock.realTimeSinceFirstClockUpdate();
    phaseTimers[phase] += currentRealTime - phaseTimerBase;
    phaseTimerBase = currentRealTime;
}

namespace {

    std::string const appVersionName("OpenSteerDemo 0.8.3");

    bool gMouseAdjustingCameraAngle = false;
    bool gMouseAdjustingCameraRadius = false;
    int gMouseAdjustingCameraLastX;
    int gMouseAdjustingCameraLastY;

    void reshape( GLFWwindow* window, int width, int height )
    {
      GLfloat h = (GLfloat) height / (GLfloat) width;
      GLfloat xmax, znear, zfar;

      znear = 1.0f;
      zfar  = 400.0f;
      xmax  = znear * 0.5f;

      glViewport( 0, 0, (GLint) width, (GLint) height );
      glMatrixMode( GL_PROJECTION );
      glLoadIdentity();
      glFrustum( -xmax, xmax, -xmax*h, xmax*h, znear, zfar );
      glMatrixMode( GL_MODELVIEW );
    }

    void 
    mouseButtonFunc(GLFWwindow* window, int button, int action, int mods)
    {

        if (action == GLFW_RELEASE)
        {

            gMouseAdjustingCameraAngle = false;
            gMouseAdjustingCameraRadius = false;
        }

        if (action == GLFW_PRESS)
        {

            const bool modNone    = (mods == 0);
            const bool modCtrl    = (mods & GLFW_MOD_CONTROL);
            const bool modAlt     = (mods & GLFW_MOD_ALT);
            const bool modCtrlAlt = modCtrl || modAlt;
            const bool mouseL     = (button == 0);
            const bool mouseM     = (button == 1);
            const bool mouseR     = (button == 2);

    #if __APPLE__ && __MACH__
            const bool macosx = true;
    #else
            const bool macosx = false;
    #endif

            if (modNone && mouseL)
            {
                OpenSteer::OpenSteerDemo::selectVehicleNearestScreenPosition (OpenSteer::OpenSteerDemo::mouseX, OpenSteer::OpenSteerDemo::mouseY);
            }

            if ((modCtrl && mouseL) ||
               (modNone && mouseR && macosx))
            {
                gMouseAdjustingCameraLastX = OpenSteer::OpenSteerDemo::mouseX;
                gMouseAdjustingCameraLastY = OpenSteer::OpenSteerDemo::mouseY;
                gMouseAdjustingCameraAngle = true;
            }

            if ((modCtrl    && mouseM) ||
                (modCtrlAlt && mouseL) ||
                (modCtrlAlt && mouseM) ||
                (modAlt     && mouseR && macosx))
            {
                gMouseAdjustingCameraLastX = OpenSteer::OpenSteerDemo::mouseX;
                gMouseAdjustingCameraLastY = OpenSteer::OpenSteerDemo::mouseY;
                gMouseAdjustingCameraRadius = true;
            }
        }
    }

    void 
    mouseMotionFunc(GLFWwindow* window, double x, double y)
    {
        OpenSteer::OpenSteerDemo::mouseX = static_cast<int>(x);
        OpenSteer::OpenSteerDemo::mouseY = static_cast<int>(y);

        if (glfwGetMouseButton(window, 0))
        {

            if (gMouseAdjustingCameraAngle || gMouseAdjustingCameraRadius)
            {

                const float dSpeed = 0.005f;
                const float rSpeed = 0.01f;

                const float dx = static_cast<float>(x - gMouseAdjustingCameraLastX);
                const float dy = static_cast<float>(y - gMouseAdjustingCameraLastY);
                gMouseAdjustingCameraLastX = static_cast<int>(x);
                gMouseAdjustingCameraLastY = static_cast<int>(y);

                OpenSteer::Vec3 cameraAdjustment;

                if (gMouseAdjustingCameraAngle)
                {
                    cameraAdjustment.x = dx * -dSpeed;
                    cameraAdjustment.y = dy * +dSpeed;
                }

                if (gMouseAdjustingCameraRadius)
                {
                    cameraAdjustment.z = dy * rSpeed;
                }

                OpenSteer::OpenSteerDemo::camera.mouseAdjustOffset (cameraAdjustment);
            }
        }
    }

    void mouseEnterExitWindowFunc(GLFWwindow* window, int entered)
    {
        OpenSteer::OpenSteerDemo::mouseInWindow = !!entered;
    }

    void 
    drawDisplayPlugInName (void)
    {
        const float h = OpenSteer::drawGetWindowWidth();
        const OpenSteer::Vec3 screenLocation (10, h-20, 0);
        draw2dTextAt2dLocation (*OpenSteer::OpenSteerDemo::nameOfSelectedPlugIn (),
                                screenLocation,
                                OpenSteer::gWhite, OpenSteer::drawGetWindowWidth(), OpenSteer::drawGetWindowHeight());
    }

    void 
    drawDisplayCameraModeName (void)
    {
        std::ostringstream message;
        message << "Camera: " << OpenSteer::OpenSteerDemo::camera.modeName () << std::ends;
        const OpenSteer::Vec3 screenLocation (10, 10, 0);
        OpenSteer::draw2dTextAt2dLocation (message, screenLocation, OpenSteer::gWhite, OpenSteer::drawGetWindowWidth(), OpenSteer::drawGetWindowHeight());
    }

    void 
    writePhaseTimerReportToStream (float phaseTimer,
                                              std::ostringstream& stream)
    {

        stream << std::setprecision (5) << std::setiosflags (std::ios::fixed);
        stream << phaseTimer;

        stream << std::setprecision (0) << std::setiosflags (std::ios::fixed);
        stream << " (";

        if (OpenSteer::OpenSteerDemo::clock.getVariableFrameRateMode())
        {

            stream << 1 / phaseTimer;
            stream << " fps)\n";
        }
        else
        {

            const int fps = OpenSteer::OpenSteerDemo::clock.getFixedFrameRate ();
            stream << ((100 * phaseTimer) / (1.0f / fps));
            stream << "% of 1/";
            stream << fps;
            stream << "sec)\n";
        }
    }

    float gSmoothedTimerDraw = 0;
    float gSmoothedTimerUpdate = 0;
    float gSmoothedTimerOverhead = 0;

    void
    drawDisplayFPS (void)
    {

        static int skipCount = 10;
        if (skipCount > 0)
        {
            skipCount--;
        }
        else
        {

            const int lh = 16; 
            const int cw = 9; 
            OpenSteer::Vec3 screenLocation (10, 10, 0);

            const int targetFPS = OpenSteer::OpenSteerDemo::clock.getFixedFrameRate ();
            const float smoothedFPS = OpenSteer::OpenSteerDemo::clock.getSmoothedFPS ();

            screenLocation.y += lh;
            std::ostringstream clockStr;
            clockStr << "Clock: ";
            if (OpenSteer::OpenSteerDemo::clock.getAnimationMode ())
            {
                clockStr << "animation mode (";
                clockStr << targetFPS << " fps,";
                clockStr << " display "<< OpenSteer::round(smoothedFPS) << " fps, ";
                const float ratio = smoothedFPS / targetFPS;
                clockStr << (int) (100 * ratio) << "% of nominal speed)";
            }
            else
            {
                clockStr << "real-time mode, ";
                if (OpenSteer::OpenSteerDemo::clock.getVariableFrameRateMode ())
                {
                    clockStr << "variable frame rate (";
                    clockStr << OpenSteer::round(smoothedFPS) << " fps)";
                }
                else
                {
                    clockStr << "fixed frame rate (target: " << targetFPS;
                    clockStr << " actual: " << OpenSteer::round(smoothedFPS) << ", ";

                    OpenSteer::Vec3 sp;
                    sp = screenLocation;
                    sp.x += cw * (int) clockStr.tellp ();

                    std::ostringstream xxxStr;
                    xxxStr << std::setprecision (0)
                           << std::setiosflags (std::ios::fixed)
                           << "usage: " << OpenSteer::OpenSteerDemo::clock.getSmoothedUsage ()
                           << "%"
                           << std::ends;

                    const int usageLength = ((int) xxxStr.tellp ()) - 1;
                    for (int i = 0; i < usageLength; i++) clockStr << " ";
                    clockStr << ")";

                    const float usage = OpenSteer::OpenSteerDemo::clock.getUsage ();
                    const OpenSteer::Color color = (usage >= 100) ? OpenSteer::gRed : OpenSteer::gWhite;
                    draw2dTextAt2dLocation (xxxStr, sp, color, OpenSteer::drawGetWindowWidth(), OpenSteer::drawGetWindowHeight());
                }
            }
            if (OpenSteer::OpenSteerDemo::clock.getPausedState ())
                clockStr << " [paused]";
            clockStr << std::ends;
            draw2dTextAt2dLocation (clockStr, screenLocation, OpenSteer::gWhite, OpenSteer::drawGetWindowWidth(), OpenSteer::drawGetWindowHeight());

            const float ptd = OpenSteer::OpenSteerDemo::phaseTimerDraw();
            const float ptu = OpenSteer::OpenSteerDemo::phaseTimerUpdate();
            const float pto = OpenSteer::OpenSteerDemo::phaseTimerOverhead();
            const float smoothRate = OpenSteer::OpenSteerDemo::clock.getSmoothingRate ();
            OpenSteer::blendIntoAccumulator (smoothRate, ptd, gSmoothedTimerDraw);
            OpenSteer::blendIntoAccumulator (smoothRate, ptu, gSmoothedTimerUpdate);
            OpenSteer::blendIntoAccumulator (smoothRate, pto, gSmoothedTimerOverhead);

            screenLocation.y += lh * 4;
            std::ostringstream timerStr;
            timerStr << "update: ";
            writePhaseTimerReportToStream (gSmoothedTimerUpdate, timerStr);
            timerStr << "draw:   ";
            writePhaseTimerReportToStream (gSmoothedTimerDraw, timerStr);
            timerStr << "other:  ";
            writePhaseTimerReportToStream (gSmoothedTimerOverhead, timerStr);
            timerStr << std::ends;
            draw2dTextAt2dLocation (timerStr, screenLocation, OpenSteer::gGreen, OpenSteer::drawGetWindowWidth(), OpenSteer::drawGetWindowHeight());
        }
    }

    void 
    selectNextPresetFrameRate (void)
    {

        static int frameRatePresetIndex = 0;
        switch (++frameRatePresetIndex)
        {
        case 3: 

            OpenSteer::OpenSteerDemo::clock.setFixedFrameRate (60);
            OpenSteer::OpenSteerDemo::clock.setAnimationMode (true);
            OpenSteer::OpenSteerDemo::clock.setVariableFrameRateMode (false);
            break;
        case 2: 

            OpenSteer::OpenSteerDemo::clock.setFixedFrameRate (60);
            OpenSteer::OpenSteerDemo::clock.setAnimationMode (false);
            OpenSteer::OpenSteerDemo::clock.setVariableFrameRateMode (false);
            break;
        case 1: 

            OpenSteer::OpenSteerDemo::clock.setFixedFrameRate (24);
            OpenSteer::OpenSteerDemo::clock.setAnimationMode (false);
            OpenSteer::OpenSteerDemo::clock.setVariableFrameRateMode (false);
            break;
        case 0:
        default:

            frameRatePresetIndex = 0;
            OpenSteer::OpenSteerDemo::clock.setFixedFrameRate (0);
            OpenSteer::OpenSteerDemo::clock.setAnimationMode (false);
            OpenSteer::OpenSteerDemo::clock.setVariableFrameRateMode (true);
            break;
        }
    }

    void key( GLFWwindow* window, int key, int s, int action, int mods )
    {
        std::ostringstream message;

        switch (key)
        {

        case 'R':
            OpenSteer::OpenSteerDemo::resetSelectedPlugIn ();
            message << "reset PlugIn "
                    << '"' << OpenSteer::OpenSteerDemo::nameOfSelectedPlugIn () << '"'
                    << std::ends;
            OpenSteer::OpenSteerDemo::printMessage (message);
            break;

        case 'S':
            OpenSteer::OpenSteerDemo::printMessage ("select next vehicle/agent");
            OpenSteer::OpenSteerDemo::selectNextVehicle ();
            break;

        case 'C':
            OpenSteer::OpenSteerDemo::camera.selectNextMode ();
            message << "select camera mode "
                    << '"' << OpenSteer::OpenSteerDemo::camera.modeName () << '"' << std::ends;
            OpenSteer::OpenSteerDemo::printMessage (message);
            break;

        case GLFW_KEY_TAB:
            OpenSteer::OpenSteerDemo::selectNextPlugIn ();
            message << "select next PlugIn: "
                    << '"' << OpenSteer::OpenSteerDemo::nameOfSelectedPlugIn () << '"'
                    << std::ends;
            OpenSteer::OpenSteerDemo::printMessage (message);
            break;

        case 'A':
            OpenSteer::OpenSteerDemo::printMessage (OpenSteer::toggleAnnotationState () ?
                                                    "annotation ON" : "annotation OFF");
            break;

        case GLFW_KEY_SPACE:
            OpenSteer::OpenSteerDemo::printMessage (OpenSteer::OpenSteerDemo::clock.togglePausedState () ?
                                                    "pause" : "run");
            break;

        case 'F':
            selectNextPresetFrameRate ();
            message << "set clock to ";
            if (OpenSteer::OpenSteerDemo::clock.getAnimationMode ())
                message << "animation mode, fixed frame rate ("
                        << OpenSteer::OpenSteerDemo::clock.getFixedFrameRate () << " fps)";
            else
            {
                message << "real-time mode, ";
                if (OpenSteer::OpenSteerDemo::clock.getVariableFrameRateMode ())
                    message << "variable frame rate";
                else
                    message << "fixed frame rate ("
                            << OpenSteer::OpenSteerDemo::clock.getFixedFrameRate () << " fps)";
            }
            message << std::ends;
            OpenSteer::OpenSteerDemo::printMessage (message);
            break;

        case '?':
            OpenSteer::OpenSteerDemo::keyboardMiniHelp ();
            break;

        case GLFW_KEY_ESCAPE:
            OpenSteer::OpenSteerDemo::printMessage ("exit.");
            OpenSteer::OpenSteerDemo::exit (0);

        case GLFW_KEY_F1:  OpenSteer::OpenSteerDemo::functionKeyForPlugIn (1);  break;
        case GLFW_KEY_F2:  OpenSteer::OpenSteerDemo::functionKeyForPlugIn (2);  break;
        case GLFW_KEY_F3:  OpenSteer::OpenSteerDemo::functionKeyForPlugIn (3);  break;
        case GLFW_KEY_F4:  OpenSteer::OpenSteerDemo::functionKeyForPlugIn (4);  break;
        case GLFW_KEY_F5:  OpenSteer::OpenSteerDemo::functionKeyForPlugIn (5);  break;
        case GLFW_KEY_F6:  OpenSteer::OpenSteerDemo::functionKeyForPlugIn (6);  break;
        case GLFW_KEY_F7:  OpenSteer::OpenSteerDemo::functionKeyForPlugIn (7);  break;
        case GLFW_KEY_F8:  OpenSteer::OpenSteerDemo::functionKeyForPlugIn (8);  break;
        case GLFW_KEY_F9:  OpenSteer::OpenSteerDemo::functionKeyForPlugIn (9);  break;
        case GLFW_KEY_F10: OpenSteer::OpenSteerDemo::functionKeyForPlugIn (10); break;
        case GLFW_KEY_F11: OpenSteer::OpenSteerDemo::functionKeyForPlugIn (11); break;
        case GLFW_KEY_F12: OpenSteer::OpenSteerDemo::functionKeyForPlugIn (12); break;

        case GLFW_KEY_RIGHT:
            OpenSteer::OpenSteerDemo::clock.setPausedState (true);
            message << "single step forward (frame time: "
                    << OpenSteer::OpenSteerDemo::clock.advanceSimulationTimeOneFrame ()
                    << ")"
                    << std::endl;
            OpenSteer::OpenSteerDemo::printMessage (message);
            break;

        default:
            message << "unrecognized single key command: " << key;
            message << " (" << (int)key << ")";
            message << std::ends;
            OpenSteer::OpenSteerDemo::printMessage ("");
            OpenSteer::OpenSteerDemo::printMessage (message);
            OpenSteer::OpenSteerDemo::keyboardMiniHelp ();
        }
    }

    void 
    displayFunc (void)
    {

        glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        OpenSteer::OpenSteerDemo::updateSimulationAndRedraw ();

        drawDisplayFPS ();

        drawDisplayPlugInName ();

        drawDisplayCameraModeName ();

        OpenSteer::checkForDrawError ("OpenSteerDemo::updateSimulationAndRedraw");

        glFlush ();
    }

} 

void 
OpenSteer::initializeGraphics (int argc, char **argv)
{
    if (!glfwInit())
    {
        fprintf( stderr, "Failed to initialize GLFW\n" );
        exit( EXIT_FAILURE );
    }

    demo_window = glfwCreateWindow( 1200, 900, "OpenSteer", NULL, NULL );
    if (!demo_window)
    {
        fprintf( stderr, "Failed to open GLFW window\n" );
        glfwTerminate();
        exit( EXIT_FAILURE );
    }

    glfwSetFramebufferSizeCallback(demo_window, reshape);
    glfwSetKeyCallback(demo_window, key);
    glfwSetCursorEnterCallback(demo_window, mouseEnterExitWindowFunc);
    glfwSetCursorPosCallback(demo_window, mouseMotionFunc);
    glfwSetMouseButtonCallback(demo_window, mouseButtonFunc);

    glfwMakeContextCurrent(demo_window);
    gladLoadGLLoader((GLADloadproc) glfwGetProcAddress);
    glfwSwapInterval( 1 );

    int width, height;
    glfwGetFramebufferSize(demo_window, &width, &height);
    reshape(demo_window, width, height);
}

void 
OpenSteer::runGraphics (void)
{

    while( !glfwWindowShouldClose(demo_window) )
    {
        displayFunc();

        glfwSwapBuffers(demo_window);
        glfwPollEvents();
    }
    glfwTerminate();
}

float 
OpenSteer::drawGetWindowHeight (void) 
{
    int width, height;
    glfwGetWindowSize(demo_window, &width, &height);
    return static_cast<float>(height);
}

float 
OpenSteer::drawGetWindowWidth  (void) 
{
    int width, height;
    glfwGetWindowSize(demo_window, &width, &height);
    return static_cast<float>(width);
}