/*
 * gesture_internal.h - touch gesture recognizer. Pure logic: no OS, no clock.
 * The caller passes positions in pixels and timestamps in seconds, so the
 * tests can drive it deterministically.
 */
#ifndef GESTURE_INTERNAL_H
#define GESTURE_INTERNAL_H

#include "platform.h"

typedef struct
{
    float x, y;
} GVec2;

typedef enum
{
    GESTURE_ACTION_UP,
    GESTURE_ACTION_DOWN,
    GESTURE_ACTION_MOVE,
    GESTURE_ACTION_CANCEL
} GestureAction;

typedef struct
{
    unsigned current;       /* gesture detected right now */
    unsigned enabled;       /* Gesture flags the caller wants reported */
    int point_count;
    double event_time;
    GVec2 up_pos, down_a, down_b, down_drag, move_a, move_b, prev_a, prev_b;
    int tap_counter;
    bool hold_reset;
    double hold_start;
    GVec2 drag_vector;
    float drag_angle;
    double swipe_start;
    GVec2 pinch_vector;
    float pinch_angle;
    float width, height; /* screen size the thresholds are normalized by */
} GestureState;

void gesture_state_init(GestureState *g);

/* One touch event. pos[0..count-1] are the points as the OS reports them: for
   an UP, count includes the point being lifted. */
void gesture_feed(GestureState *g, GestureAction action, int count, const GVec2 *pos,
                  float width, float height, double now);

/* Once per frame, after the events were fed. */
void gesture_update(GestureState *g, double now);

unsigned gesture_state_detected(const GestureState *g);
float gesture_state_hold_duration(const GestureState *g, double now);

#endif /* GESTURE_INTERNAL_H */
