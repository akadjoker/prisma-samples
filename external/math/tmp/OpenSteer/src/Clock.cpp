
#include "OpenSteer/Clock.h"

#if defined (_XBOX)
	#include <xtl.h>
#elif defined (_WIN32)
	#include <windows.h>
#else
	#include <sys/time.h> 
#endif

OpenSteer::Clock::Clock (void)
{

    setFixedFrameRate (0);
    setPausedState (false);
    setAnimationMode (false);
    setVariableFrameRateMode (true);

    totalRealTime = 0;

    totalSimulationTime = 0;

    totalPausedTime = 0;

    totalAdvanceTime = 0;

    elapsedSimulationTime = 0;

    elapsedRealTime = 0;

    elapsedNonWaitRealTime = 0;

    newAdvanceTime = 0;

#ifdef _WIN32
    basePerformanceCounter = 0;  
#else
    baseRealTimeSec = 0;         
    baseRealTimeUsec = 0;
#endif

    smoothedFPS = 0;
    smoothedUsage = 0;
}

void 
OpenSteer::Clock::update (void)
{

    updateSmoothedRegisters ();

    frameRateSync ();

    const float previousRealTime = totalRealTime;

    totalRealTime = realTimeSinceFirstClockUpdate ();

    elapsedRealTime = totalRealTime - previousRealTime;

    if (paused) totalPausedTime += elapsedRealTime;

    const float previousSimulationTime = totalSimulationTime;

    if (getAnimationMode ())
    {

        const float frameDuration = 1.0f / getFixedFrameRate ();
        totalSimulationTime += paused ? newAdvanceTime : frameDuration;
        if (!paused) newAdvanceTime += frameDuration - elapsedRealTime;
    }
    else
    {

        totalSimulationTime = (totalRealTime
                               + totalAdvanceTime
                               - totalPausedTime);
    }

    totalAdvanceTime += newAdvanceTime;

    elapsedSimulationTime = (paused ?
                             newAdvanceTime :
                             (totalSimulationTime - previousSimulationTime));

    newAdvanceTime = 0;
}

void 
OpenSteer::Clock::frameRateSync (void)
{

    if ((! getAnimationMode ()) && (! getVariableFrameRateMode ()))
    {

        const float targetStepSize = 1.0f / getFixedFrameRate ();
        const float now = realTimeSinceFirstClockUpdate ();
        const int lastFrameCount = (int) (now / targetStepSize);
        const float nextFrameTime = (lastFrameCount + 1) * targetStepSize;

        elapsedNonWaitRealTime = now - totalRealTime;

        do {} while (realTimeSinceFirstClockUpdate () < nextFrameTime); 
    }
}

float 
OpenSteer::Clock::advanceSimulationTimeOneFrame (void)
{

    const float fps = (getVariableFrameRateMode () ?
                       getSmoothedFPS () :
                       getFixedFrameRate ());
    const float frameTime = 1 / fps;

    advanceSimulationTime (frameTime);

    return frameTime; 
}

void 
OpenSteer::Clock::advanceSimulationTime (const float seconds)
{
    if (seconds < 0) {

        std::cerr << "negative arg to advanceSimulationTime - results will not be valid";
    }
    else
        newAdvanceTime += seconds;
}

namespace {

    float 
    clockErrorExit (void)
    {

        std::cerr << "Problem reading system clock - results will not be valid";
        return 0.0f;
    }

} 

float 
OpenSteer::Clock::realTimeSinceFirstClockUpdate (void)
#ifdef _WIN32
{

    LONGLONG counter, frequency;
    bool clockOK = (QueryPerformanceCounter ((LARGE_INTEGER *)&counter)  &&
                    QueryPerformanceFrequency ((LARGE_INTEGER *)&frequency));
    if (!clockOK) return clockErrorExit ();

    if (basePerformanceCounter == 0) basePerformanceCounter = counter;

    const LONGLONG counterDifference = counter - basePerformanceCounter;
    return ((float) counterDifference) / ((float)frequency);
}
#else
{

    timeval t;
    if (gettimeofday (&t, 0) != 0) return clockErrorExit ();

    if (baseRealTimeSec == 0)
    {
        baseRealTimeSec = t.tv_sec;
        baseRealTimeUsec = t.tv_usec;
    }

    return (( t.tv_sec  - baseRealTimeSec) +
            ((t.tv_usec - baseRealTimeUsec) / 1000000.0f));
}
#endif