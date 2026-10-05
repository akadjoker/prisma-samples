/*
 * gesture.c - touch gesture recognizer.
 *
 * The detection logic follows rgestures.h from raylib, so a gesture reads the
 * same as it does there.
 *   rgestures.h  Copyright (c) 2014-2023 Ramon Santamaria (@raysan5), zlib/libpng.
 *
 * What differs: positions stay in pixels (thresholds are normalized here, by the
 * screen size), angles are measured in pixel space so they do not skew on a wide
 * window, there is no global state, and time comes in as an argument.
 */
#include "gesture_internal.h"

#include <stdint.h>
#include <string.h>

#define FORCE_TO_SWIPE 0.2f  /* swipe force, normalized screen units per second */
#define DRAG_TIMEOUT 0.3     /* seconds a hold must last before it turns into a drag */
#define MINIMUM_PINCH 0.005f /* normalized screen units */
#define TAP_TIMEOUT 0.3      /* seconds between taps of a double tap */
#define DOUBLETAP_RANGE 0.03f

#define PI_F 3.14159265358979f
#define RAD2DEG (180.0f / PI_F)

/* The core links no libm, so these two are done by hand. Newton from a bit-trick
   guess for the root; a degree-9 polynomial for the arctangent (error < 1e-5 rad). */
static float fsqrt(float x)
{
    if (x <= 0.0f)
        return 0.0f;
    uint32_t i;
    memcpy(&i, &x, sizeof i);
    i = 0x1fbd1df5u + (i >> 1);
    float y;
    memcpy(&y, &i, sizeof y);
    for (int k = 0; k < 3; k++)
        y = 0.5f * (y + x / y);
    return y;
}

static float fatan2(float y, float x)
{
    float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    if (ax == 0.0f && ay == 0.0f)
        return 0.0f;
    bool swap = ay > ax;
    float z = swap ? ax / ay : ay / ax; /* 0..1 */
    float z2 = z * z;
    float a = z * (0.9998660f + z2 * (-0.3302995f + z2 * (0.1801410f + z2 * (-0.0851330f + z2 * 0.0208351f))));
    if (swap)
        a = PI_F / 2 - a;
    if (x < 0)
        a = PI_F - a;
    return y < 0 ? -a : a;
}

/* Distance with each axis divided by the screen size, so a threshold means the
   same on every resolution. */
static float dist_n(const GestureState *g, GVec2 a, GVec2 b)
{
    float dx = (b.x - a.x) / g->width, dy = (b.y - a.y) / g->height;
    return fsqrt(dx * dx + dy * dy);
}

/* Degrees from the x axis, 0..360, measured in pixel space. */
static float angle_of(GVec2 a, GVec2 b)
{
    float deg = fatan2(b.y - a.y, b.x - a.x) * RAD2DEG;
    return deg < 0 ? deg + 360.0f : deg;
}

void gesture_state_init(GestureState *g)
{
    *g = (GestureState){0};
    g->current = GESTURE_NONE;
    g->enabled = 0x3FF; /* every gesture */
    g->width = g->height = 1.0f;
}

static void reset(GestureState *g)
{
    g->current = GESTURE_NONE;
    g->point_count = 0;
    g->tap_counter = 0;
    g->hold_reset = false;
    g->drag_vector = (GVec2){0, 0};
    g->drag_angle = 0;
    g->pinch_vector = (GVec2){0, 0};
    g->pinch_angle = 0;
}

void gesture_feed(GestureState *g, GestureAction action, int count, const GVec2 *pos,
                  float width, float height, double now)
{
    if (width > 0 && height > 0)
    {
        g->width = width;
        g->height = height;
    }
    if (action == GESTURE_ACTION_CANCEL)
    {
        reset(g);
        return;
    }

    g->point_count = count;

    if (count == 1)
    {
        if (action == GESTURE_ACTION_DOWN)
        {
            g->tap_counter++;

            if (g->current == GESTURE_NONE && g->tap_counter >= 2 &&
                (now - g->event_time) < TAP_TIMEOUT &&
                dist_n(g, g->down_a, pos[0]) < DOUBLETAP_RANGE)
            {
                g->current = GESTURE_DOUBLETAP;
                g->tap_counter = 0;
            }
            else
            {
                g->tap_counter = 1;
                g->current = GESTURE_TAP;
            }

            g->down_a = pos[0];
            g->down_drag = pos[0];
            g->up_pos = g->down_a;
            g->event_time = now;
            g->swipe_start = now;
            g->drag_vector = (GVec2){0, 0};
        }
        else if (action == GESTURE_ACTION_UP)
        {
            /* Always the lift-off point: raylib only takes it from a drag or a hold, so
               a flick that ends before the first frame update would read as no move. */
            g->up_pos = pos[0];

            float distance = dist_n(g, g->down_a, g->up_pos);
            double dt = now - g->swipe_start;
            float intensity = dt > 0 ? distance / (float)dt : 0.0f;

            if (intensity > FORCE_TO_SWIPE && g->current != GESTURE_DRAG)
            {
                /* Y grows downwards, so mirror the angle to read it as on paper. */
                g->drag_angle = 360.0f - angle_of(g->down_a, g->up_pos);
                float a = g->drag_angle;
                if (a < 30 || a > 330)
                    g->current = GESTURE_SWIPE_RIGHT;
                else if (a >= 30 && a <= 150)
                    g->current = GESTURE_SWIPE_UP;
                else if (a > 150 && a < 210)
                    g->current = GESTURE_SWIPE_LEFT;
                else
                    g->current = GESTURE_SWIPE_DOWN;
            }
            else
            {
                g->drag_angle = 0;
                g->current = GESTURE_NONE;
            }

            g->down_drag = (GVec2){0, 0};
            g->point_count = 0;
        }
        else if (action == GESTURE_ACTION_MOVE)
        {
            g->move_a = pos[0];

            if (g->current == GESTURE_HOLD)
            {
                if (g->hold_reset)
                    g->down_a = pos[0];
                g->hold_reset = false;

                if ((now - g->event_time) > DRAG_TIMEOUT)
                {
                    g->event_time = now;
                    g->current = GESTURE_DRAG;
                }
            }

            g->drag_vector.x = g->move_a.x - g->down_drag.x;
            g->drag_vector.y = g->move_a.y - g->down_drag.y;
        }
    }
    else if (count == 2)
    {
        if (action == GESTURE_ACTION_DOWN)
        {
            g->down_a = pos[0];
            g->down_b = pos[1];
            g->prev_a = g->down_a;
            g->prev_b = g->down_b;
            /* No move seen yet: the first MOVE compares against these. */
            g->move_a = g->down_a;
            g->move_b = g->down_b;

            g->pinch_vector.x = g->down_b.x - g->down_a.x;
            g->pinch_vector.y = g->down_b.y - g->down_a.y;

            g->current = GESTURE_HOLD;
            g->hold_start = now;
        }
        else if (action == GESTURE_ACTION_MOVE)
        {
            g->move_a = pos[0];
            g->move_b = pos[1];

            g->pinch_vector.x = g->move_b.x - g->move_a.x;
            g->pinch_vector.y = g->move_b.y - g->move_a.y;

            if (dist_n(g, g->prev_a, g->move_a) >= MINIMUM_PINCH ||
                dist_n(g, g->prev_b, g->move_b) >= MINIMUM_PINCH)
            {
                g->current = dist_n(g, g->prev_a, g->prev_b) > dist_n(g, g->move_a, g->move_b)
                                 ? GESTURE_PINCH_IN
                                 : GESTURE_PINCH_OUT;
            }
            else
            {
                g->current = GESTURE_HOLD;
                g->hold_start = now;
            }

            g->pinch_angle = 360.0f - angle_of(g->move_a, g->move_b);
        }
        else if (action == GESTURE_ACTION_UP)
        {
            g->pinch_angle = 0;
            g->pinch_vector = (GVec2){0, 0};
            g->point_count = 0;
            g->current = GESTURE_NONE;
        }
    }
    /* More than two points: not recognized, as in raylib. */
}

void gesture_update(GestureState *g, double now)
{
    /* A tap that is still held becomes a hold. */
    if ((g->current == GESTURE_TAP || g->current == GESTURE_DOUBLETAP) && g->point_count < 2)
    {
        g->current = GESTURE_HOLD;
        g->hold_start = now;
    }

    /* A swipe is reported for one frame only. */
    if (g->current == GESTURE_SWIPE_RIGHT || g->current == GESTURE_SWIPE_UP ||
        g->current == GESTURE_SWIPE_LEFT || g->current == GESTURE_SWIPE_DOWN)
        g->current = GESTURE_NONE;
}

unsigned gesture_state_detected(const GestureState *g)
{
    return g->enabled & g->current;
}

float gesture_state_hold_duration(const GestureState *g, double now)
{
    return g->current == GESTURE_HOLD ? (float)(now - g->hold_start) : 0.0f;
}
