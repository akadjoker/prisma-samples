/*
 * test_app_run.c - PHASE 2. Drives app_run on the fake backend and confirms the
 * begin_frame -> frame callback -> swap cycle, including that injected input
 * reaches the frame and that the exit key tears the loop down.
 */
#include "platform.h"
#include "backend_fake.h"

#include <stdio.h>

static int g_pass, g_fail;

#define CHECK(cond)                                                \
    do                                                             \
    {                                                              \
        if (cond)                                                  \
        {                                                          \
            g_pass++;                                              \
        }                                                          \
        else                                                       \
        {                                                          \
            g_fail++;                                              \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                          \
    } while (0)

static PlatformWindow *make_window(void)
{
    WindowConfig cfg = {.title = "test", .width = 800, .height = 600, .x = WINDOW_POS_UNDEFINED, .y = WINDOW_POS_UNDEFINED};
    return window_create(&cfg);
}

/* The loop runs a fixed number of frames, swapping once per frame, and the input
   injected before app_run is visible in the first frame. */
typedef struct
{
    int frames;
    bool saw_space_in_frame0;
} CycleState;

static void cycle_frame(PlatformWindow *w, void *user)
{
    CycleState *st = user;
    if (st->frames == 0 && key_pressed(w, KEY_SPACE))
        st->saw_space_in_frame0 = true;
    st->frames++;
    if (st->frames >= 5)
        window_set_should_close(w, true);
}

static void test_cycle(void)
{
    PlatformWindow *w = make_window();
    CycleState st = {0};
    fake_key(w, KEY_SPACE, true, false); /* queued before the loop starts */
    app_run(w, cycle_frame, &st);

    CHECK(st.frames == 5);
    CHECK(st.saw_space_in_frame0);
    CHECK(fake_swap_count(w) == 5); /* one swap per frame */
    CHECK(window_should_close(w));
    window_destroy(w);
}

/* The exit key sets should_close from inside begin_frame and ends the loop. */
static void exit_frame(PlatformWindow *w, void *user)
{
    int *frames = user;
    (*frames)++;
    if (*frames == 2)
        fake_key(w, KEY_ESCAPE, true, false); /* pumped next frame -> should_close */
    if (*frames > 50)
        window_set_should_close(w, true); /* guard against a runaway loop */
}

static void test_exit_key(void)
{
    PlatformWindow *w = make_window();
    key_set_exit(w, KEY_ESCAPE);
    int frames = 0;
    app_run(w, exit_frame, &frames);

    CHECK(window_should_close(w));
    CHECK(frames < 50); /* it stopped via the exit key, not the guard */
    window_destroy(w);
}

int main(void)
{
    if (!platform_init())
    {
        printf("platform_init failed\n");
        return 1;
    }

    test_cycle();
    test_exit_key();

    platform_shutdown();

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
