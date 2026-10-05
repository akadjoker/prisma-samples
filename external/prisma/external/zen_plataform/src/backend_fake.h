/*
 * backend_fake.h - test-only injection API for backend_fake.c. Lets a test feed
 * events into a window as if the OS had produced them, with no display. Not part
 * of the public contract; never shipped to consumers.
 */
#ifndef BACKEND_FAKE_H
#define BACKEND_FAKE_H

#include "platform.h"

/* Queue a raw item; delivered to the core on the next window_begin_frame. */
void fake_inject_event(PlatformWindow *w, const Event *ev);
void fake_inject_char(PlatformWindow *w, uint32_t codepoint);

/* Convenience builders for the common cases. */
void fake_key(PlatformWindow *w, int key, bool down, bool repeat);
void fake_mouse_move(PlatformWindow *w, int x, int y);
void fake_mouse_button(PlatformWindow *w, int button, bool down);
void fake_wheel(PlatformWindow *w, float x, float y);
void fake_touch(PlatformWindow *w, int id, float x, float y, TouchPhase phase);
void fake_set_lock_state(int mask); /* KEYMOD_CAPS_LOCK | KEYMOD_NUM_LOCK, read back by key_mods */
HitTestResult fake_hit_test(PlatformWindow *w, int x, int y); /* what the window's hit-test function says */
bool fake_is_decorated(PlatformWindow *w);
PlatformCursor *fake_cursor_image(PlatformWindow *w); /* the cursor image set last, or NULL */
void fake_real_touch(PlatformWindow *w, int id, float x, float y, TouchPhase phase); /* as a desktop touch screen: also drives the mouse */
void fake_text_edit(PlatformWindow *w, const char *utf8, int cursor); /* an input method composing */
bool fake_text_input_on(PlatformWindow *w);
void fake_text_input_rect(PlatformWindow *w, int out[4]);
void fake_resize(PlatformWindow *w, int width, int height);    /* EVENT_WINDOW_RESIZE, screen coords */
void fake_fb_resize(PlatformWindow *w, int width, int height); /* EVENT_WINDOW_FB_RESIZE, pixels */

/* Number of swaps performed so far; used to confirm the app_run frame cycle. */
int fake_swap_count(PlatformWindow *w);

#endif /* BACKEND_FAKE_H */
