
#ifndef OPENSTEER_OPENSTEERDEMO_H
#define OPENSTEER_OPENSTEERDEMO_H

#include "OpenSteer/Clock.h"
#include "OpenSteer/PlugIn.h"
#include "OpenSteer/Camera.h"
#include "OpenSteer/Utilities.h"

namespace OpenSteer {

    class Color;
    class Vec3;

    class OpenSteerDemo
    {
    public:

        static Clock clock;

        static Camera camera;

        static PlugIn* selectedPlugIn;

        static AbstractVehicle* selectedVehicle;

        static void initialize (void);

        static void updateSimulationAndRedraw (void);

        static void errorExit (const char* message);
        static void exit (int exitCode);

        static void selectDefaultPlugIn (void);

        static void selectNextPlugIn (void);

        static void functionKeyForPlugIn (int keyNumber);

        static const char* nameOfSelectedPlugIn (void);

        static void openSelectedPlugIn (void);

        static void updateSelectedPlugIn (const float currentTime,
                                          const float elapsedTime);

        static void redrawSelectedPlugIn (const float currentTime,
                                          const float elapsedTime);

        static void closeSelectedPlugIn (void);

        static void resetSelectedPlugIn (void);

        static const AVGroup& allVehiclesOfSelectedPlugIn(void);

        static bool phaseIsDraw     (void) {return phase == drawPhase;}
        static bool phaseIsUpdate   (void) {return phase == updatePhase;}
        static bool phaseIsOverhead (void) {return phase == overheadPhase;}

        static float phaseTimerDraw     (void) {return phaseTimers[drawPhase];}
        static float phaseTimerUpdate   (void) {return phaseTimers[updatePhase];}

        static float phaseTimerOverhead (void)
        {
            return (clock.getElapsedRealTime() -
                    (phaseTimerDraw() + phaseTimerUpdate()));
        }

        static void queueDelayedResetPlugInXXX (void);
        static void doDelayedResetPlugInXXX (void);

        static void selectNextVehicle (void);

        static void selectVehicleNearestScreenPosition (int x, int y);

        static AbstractVehicle* vehicleNearestToMouse (void);

        static AbstractVehicle* findVehicleNearestScreenPosition (int x, int y);

        static int mouseX;
        static int mouseY;
        static bool mouseInWindow;

        static void init2dCamera (AbstractVehicle& selected);
        static void init2dCamera (AbstractVehicle& selected,
                                  float distance,
                                  float elevation);
        static void init3dCamera (AbstractVehicle& selected);
        static void init3dCamera (AbstractVehicle& selected,
                                  float distance,
                                  float elevation);

        static void position3dCamera (AbstractVehicle& selected);
        static void position3dCamera (AbstractVehicle& selected,
                                      float distance,
                                      float elevation);
        static void position2dCamera (AbstractVehicle& selected);
        static void position2dCamera (AbstractVehicle& selected,
                                      float distance,
                                      float elevation);

        static void updateCamera (const float currentTime,
                                  const float elapsedTime,
                                  const AbstractVehicle* selected);

        static const float camera2dElevation;
        static const float cameraTargetDistance;
        static const Vec3 cameraTargetOffset;

        static void initializeGraphics (void);

        static void gridUtility (const Vec3& gridTarget);

        static void highlightVehicleUtility (const AbstractVehicle* vehicle);

        static void circleHighlightVehicleUtility (const AbstractVehicle* vehicle);

        static void drawBoxHighlightOnVehicle (const AbstractVehicle* v,
                                               const Color& color);

        static void drawCircleHighlightOnVehicle (const AbstractVehicle* v,
                                                  const float radiusMultiplier,
                                                  const Color& color);

        static void printMessage (const char* message);
        static void printMessage (const std::ostringstream& message);

        static void printWarning (const char* message);
        static void printWarning (const std::ostringstream& message);

        static void keyboardMiniHelp (void);

    private:
        static int phase;
        static int phaseStack[];
        static int phaseStackIndex;
        static float phaseTimers[];
        static float phaseTimerBase;
        static const int phaseStackSize;
        static void pushPhase (const int newPhase);
        static void popPhase (void);
        static void initPhaseTimers (void);
        static void updatePhaseTimers (void);

        static const int drawPhase;
        static const int updatePhase;
        static const int overheadPhase;
    };

    void initializeGraphics (int argc, char **argv);

    void runGraphics (void);

    float drawGetWindowHeight (void);
    float drawGetWindowWidth (void);

} 

#include "OpenSteer/Draw.h"

#endif 