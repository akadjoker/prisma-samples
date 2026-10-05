#include <emscripten.h>

// Waits for the next animation frame of the browser. The demos run their own loop, which Asyncify
// suspends here once per frame so that the page can show the canvas and handle input.
EM_ASYNC_JS(void, zenapp_wait_frame_js, (), {
    await new Promise(resolve => requestAnimationFrame(resolve));
});

extern "C" void zenapp_wait_frame()
{
    zenapp_wait_frame_js();
}
