
#ifndef OPENSTEER_CLOCK_H
#define OPENSTEER_CLOCK_H

#include "OpenSteer/Utilities.h"

#if defined (_XBOX)
	#include <xtl.h>
#elif defined (_WIN32)
	#include <windows.h>
#endif

namespace OpenSteer {

    class Clock
    {
    public:

        Clock ();

        void update (void);

        float realTimeSinceFirstClockUpdate (void);

        float advanceSimulationTimeOneFrame (void);
        void advanceSimulationTime (const float seconds);

        void frameRateSync (void);

    private:

        bool variableFrameRateMode;

        int fixedFrameRate;

        bool animationMode;

        bool paused;
    public:
        int getFixedFrameRate (void) {return fixedFrameRate;}
        int setFixedFrameRate (int ffr) {return fixedFrameRate = ffr;}

        bool getAnimationMode (void) {return animationMode;}
        bool setAnimationMode (bool am) {return animationMode = am;}

        bool getVariableFrameRateMode (void) {return variableFrameRateMode;}
        bool setVariableFrameRateMode (bool vfrm)
             {return variableFrameRateMode = vfrm;}

        bool togglePausedState (void) {return (paused = !paused);};
        bool getPausedState (void) {return paused;};
        bool setPausedState (bool newPS) {return paused = newPS;};

    private:
        float smoothedFPS;
        float smoothedUsage;
        void updateSmoothedRegisters (void)
        {
            const float rate = getSmoothingRate ();
            if (elapsedRealTime > 0)
                blendIntoAccumulator (rate, 1 / elapsedRealTime, smoothedFPS);
            if (! getVariableFrameRateMode ())
                blendIntoAccumulator (rate, getUsage (), smoothedUsage);
        }
    public:
        float getSmoothedFPS (void) const {return smoothedFPS;}
        float getSmoothedUsage (void) const {return smoothedUsage;}
        float getSmoothingRate (void) const
        {
            if (smoothedFPS == 0) return 1; else return elapsedRealTime * 1.5f;
        }
        float getUsage (void)
        {

            return ((100 * elapsedNonWaitRealTime) / (1.0f / fixedFrameRate));
        }

    private:

        float totalRealTime;

        float totalSimulationTime;

        float totalPausedTime;

        float totalAdvanceTime;

        float elapsedSimulationTime;

        float elapsedRealTime;

        float elapsedNonWaitRealTime;
    public:
        float getTotalRealTime (void) {return totalRealTime;}
        float getTotalSimulationTime (void) {return totalSimulationTime;}
        float getTotalPausedTime (void) {return totalPausedTime;}
        float getTotalAdvanceTime (void) {return totalAdvanceTime;}
        float getElapsedSimulationTime (void) {return elapsedSimulationTime;}
        float getElapsedRealTime (void) {return elapsedRealTime;}
        float getElapsedNonWaitRealTime (void) {return elapsedNonWaitRealTime;}

    private:

        float newAdvanceTime;

    #ifdef _WIN32

        LONGLONG basePerformanceCounter;
    #else

        int baseRealTimeSec;
        int baseRealTimeUsec;
    #endif
    };

} 

#endif 