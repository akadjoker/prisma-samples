#pragma once

#include "FrameStats.h"

#include <ct/vector.hpp>

#include <stdio.h>

namespace zenapp
{

struct FrameSample
{
    float total;
    float phases[4];
    float gpu;
    float step;
    unsigned keys;
    unsigned triangles;
};

class FrameLog
{
public:
    void reserve(size_t count) { samples_.reserve(count); }

    void add(const FrameStats& stats, float gpu, float step, unsigned keys, unsigned triangles)
    {
        if (samples_.size() >= 60000) return;
        FrameSample sample;
        sample.total = stats.lastFrameMs();
        for (int i = 0; i < 4; ++i) sample.phases[i] = stats.lastPhaseMs(i);
        sample.gpu = gpu;
        sample.step = step;
        sample.keys = keys;
        sample.triangles = triangles;
        samples_.push_back(sample);
    }

    bool write(const char* path, const char* header) const
    {
        FILE* file = fopen(path, "w");
        if (!file) return false;
        fprintf(file, "%s\n", header);
        fprintf(file, "frame total_ms cull_ms uniforms_ms record_ms present_ms gpu_ms step keys triangles\n");
        for (size_t i = 0; i < samples_.size(); ++i)
        {
            const FrameSample& s = samples_[i];
            fprintf(file, "%zu %.2f %.2f %.2f %.2f %.2f %.2f %.4f %u %u\n", i, s.total, s.phases[0],
                    s.phases[1], s.phases[2], s.phases[3], s.gpu, s.step, s.keys, s.triangles);
        }
        fclose(file);
        return true;
    }

    size_t size() const { return samples_.size(); }

private:
    ct::Vector<FrameSample> samples_;
};

} // namespace zenapp
