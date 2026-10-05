#pragma once

#include "FrameStats.h"
#include "TextOverlay.h"

#include <stdio.h>

namespace zenapp
{

inline void drawStatsOverlay(TextOverlay* overlay, const FrameStats& stats, const char* const* names,
        int phaseCount, const char* extra)
{
    const unsigned char panel[4] = { 0, 0, 0, 170 };
    const unsigned char white[4] = { 255, 255, 255, 255 };
    const unsigned char grey[4] = { 200, 200, 200, 255 };
    const unsigned char green[4] = { 80, 220, 90, 255 };
    const unsigned char yellow[4] = { 240, 210, 60, 255 };
    const unsigned char red[4] = { 240, 70, 60, 255 };
    const unsigned char line[4] = { 255, 255, 255, 90 };

    const float x = 8.0f;
    const float y = 8.0f;
    const float graphWidth = static_cast<float>(FrameStats::kHistory) * 2.0f;
    const float graphHeight = 80.0f;
    const float panelWidth = graphWidth + 16.0f;
    const float panelHeight = 8.0f + 4.0f * 18.0f + graphHeight + 14.0f;
    overlay->rect(x - 4.0f, y - 4.0f, panelWidth, panelHeight, panel);

    char text[256];
    snprintf(text, sizeof(text), "%.0f fps   %.1f ms   worst %.1f ms", stats.fps(), stats.averageMs(),
            stats.worstMs());
    overlay->text(x, y, text, stats.fps() < 30.0f ? red : (stats.fps() < 55.0f ? yellow : green));

    int length = 0;
    text[0] = '\0';
    for (int i = 0; i < phaseCount && length < static_cast<int>(sizeof(text)) - 1; ++i)
        length += snprintf(text + length, sizeof(text) - static_cast<size_t>(length), "%s %.1f%s",
                names[i], stats.phaseAverageMs(i), i + 1 < phaseCount ? "  " : "");
    overlay->text(x, y + 18.0f, text, white, 1.5f);

    length = 0;
    text[0] = '\0';
    for (int i = 0; i < phaseCount && length < static_cast<int>(sizeof(text)) - 1; ++i)
        length += snprintf(text + length, sizeof(text) - static_cast<size_t>(length), "%s %.1f%s",
                names[i], stats.phaseWorstMs(i), i + 1 < phaseCount ? "  " : "");
    overlay->text(x, y + 34.0f, text, grey, 1.5f);
    snprintf(text, sizeof(text), "gpu %.1f  worst %.1f   %s", stats.gpuAverageMs(), stats.gpuWorstMs(),
            extra ? extra : "");
    overlay->text(x, y + 50.0f, text, grey, 1.5f);

    const float graphTop = y + 68.0f;
    const float graphBottom = graphTop + graphHeight;
    const int count = stats.historyCount();
    for (int i = 0; i < count; ++i)
    {
        const float ms = stats.historyMs(i);
        float height = ms * 2.4f;
        height = height > graphHeight ? graphHeight : (height < 1.0f ? 1.0f : height);
        overlay->rect(x + static_cast<float>(i) * 2.0f, graphBottom - height, 2.0f, height,
                ms > 33.4f ? red : (ms > 16.8f ? yellow : green));
    }
    overlay->rect(x, graphBottom - 16.7f * 2.4f, graphWidth, 1.0f, line);
    overlay->rect(x, graphBottom - 33.3f * 2.4f, graphWidth, 1.0f, line);
    overlay->text(x + graphWidth - 38.0f, graphBottom - 16.7f * 2.4f - 12.0f, "60", grey, 1.0f);
    overlay->text(x + graphWidth - 38.0f, graphBottom - 33.3f * 2.4f - 12.0f, "30", grey, 1.0f);
}

} // namespace zenapp
