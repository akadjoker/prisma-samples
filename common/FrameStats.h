#pragma once

#include <time.h>

namespace zenapp
{

inline double monotonicSeconds()
{
    timespec value;
    clock_gettime(CLOCK_MONOTONIC, &value);
    return static_cast<double>(value.tv_sec) + static_cast<double>(value.tv_nsec) * 1e-9;
}

class FrameStats
{
public:
    enum
    {
        kMaxPhases = 6,
        kHistory = 160
    };

    explicit FrameStats(int phaseCount)
        : phaseCount_(phaseCount < kMaxPhases ? phaseCount : kMaxPhases)
    {
    }

    void begin()
    {
        frameStart_ = monotonicSeconds();
        last_ = frameStart_;
        if (windowStart_ == 0.0) windowStart_ = frameStart_;
    }

    void phase(int index)
    {
        const double now = monotonicSeconds();
        const double milliseconds = (now - last_) * 1000.0;
        last_ = now;
        if (index < 0 || index >= phaseCount_) return;
        lastPhase_[index] = static_cast<float>(milliseconds);
        sum_[index] += milliseconds;
        if (milliseconds > worst_[index]) worst_[index] = milliseconds;
    }

    void end()
    {
        const double now = monotonicSeconds();
        const double milliseconds = (now - frameStart_) * 1000.0;
        lastFrame_ = static_cast<float>(milliseconds);
        history_[head_] = static_cast<float>(milliseconds);
        head_ = (head_ + 1) % kHistory;
        if (historyCount_ < kHistory) ++historyCount_;

        totalSum_ += milliseconds;
        if (milliseconds > totalWorst_) totalWorst_ = milliseconds;
        ++frames_;
        if (now - windowStart_ < 0.5) return;

        fps_ = static_cast<float>(static_cast<double>(frames_) / (now - windowStart_));
        average_ = static_cast<float>(totalSum_ / frames_);
        worst_ms_ = static_cast<float>(totalWorst_);
        for (int i = 0; i < phaseCount_; ++i)
        {
            phaseAverage_[i] = static_cast<float>(sum_[i] / frames_);
            phaseWorst_[i] = static_cast<float>(worst_[i]);
        }
        if (gpuSamples_ > 0)
        {
            gpuAverage_ = static_cast<float>(gpuSum_ / gpuSamples_);
            gpuWorst_ = gpuWorstRaw_;
        }
        gpuSum_ = 0.0;
        gpuSamples_ = 0;
        gpuWorstRaw_ = 0.0f;
        windowStart_ = now;
        frames_ = 0;
        totalSum_ = 0.0;
        totalWorst_ = 0.0;
        for (int i = 0; i < kMaxPhases; ++i)
        {
            sum_[i] = 0.0;
            worst_[i] = 0.0;
        }
    }

    void gpu(float milliseconds)
    {
        gpuSum_ += milliseconds;
        ++gpuSamples_;
        if (milliseconds > gpuWorstRaw_) gpuWorstRaw_ = milliseconds;
    }

    float lastFrameMs() const { return lastFrame_; }
    float lastPhaseMs(int i) const { return lastPhase_[i]; }
    float fps() const { return fps_; }
    float gpuAverageMs() const { return gpuAverage_; }
    float gpuWorstMs() const { return gpuWorst_; }
    float averageMs() const { return average_; }
    float worstMs() const { return worst_ms_; }
    float phaseAverageMs(int i) const { return phaseAverage_[i]; }
    float phaseWorstMs(int i) const { return phaseWorst_[i]; }
    int historyCount() const { return historyCount_; }
    float historyMs(int index) const
    {
        return history_[(head_ + kHistory - historyCount_ + index) % kHistory];
    }

private:
    int phaseCount_;
    double frameStart_ = 0.0;
    double last_ = 0.0;
    double windowStart_ = 0.0;
    int frames_ = 0;
    double totalSum_ = 0.0;
    double totalWorst_ = 0.0;
    double sum_[kMaxPhases] = {};
    double worst_[kMaxPhases] = {};
    double gpuSum_ = 0.0;
    int gpuSamples_ = 0;
    float gpuWorstRaw_ = 0.0f;
    float gpuAverage_ = 0.0f;
    float gpuWorst_ = 0.0f;
    float fps_ = 0.0f;
    float average_ = 0.0f;
    float worst_ms_ = 0.0f;
    float phaseAverage_[kMaxPhases] = {};
    float phaseWorst_[kMaxPhases] = {};
    float lastFrame_ = 0.0f;
    float lastPhase_[kMaxPhases] = {};
    float history_[kHistory] = {};
    int head_ = 0;
    int historyCount_ = 0;
};

} // namespace zenapp
