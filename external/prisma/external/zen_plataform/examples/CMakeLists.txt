# One example per target. Each needs a GL header and links the platform library.
if(EMSCRIPTEN)
    add_executable(hello_web hello_web.c)
    target_link_libraries(hello_web PRIVATE platform)
    set_target_properties(hello_web PROPERTIES
        SUFFIX ".html"
        LINK_FLAGS "-sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sFULL_ES3=1")
elseif(ANDROID)
    add_library(hello_android SHARED hello_android.c)
    target_link_libraries(hello_android PRIVATE platform)
else()
    set(MATH_LIB "")
    if(NOT WIN32)
        set(MATH_LIB m)
    endif()
    add_executable(hello_x11 hello_x11.c)
    target_link_libraries(hello_x11 PRIVATE platform)
    find_package(OpenGL REQUIRED)
    target_link_libraries(hello_x11 PRIVATE OpenGL::GL)

    add_executable(raytrace  raytrace.c)
    target_link_libraries(raytrace PRIVATE platform ${MATH_LIB})

    add_executable(uitest  uitest.c)
    target_link_libraries(uitest PRIVATE platform ${MATH_LIB})

    add_executable(mwidgets  mwidgets.c)
    target_link_libraries(mwidgets PRIVATE platform ${MATH_LIB})

    add_executable(uidemo  uidemo.c)
    target_link_libraries(uidemo PRIVATE zen_ui platform ${MATH_LIB})

    add_executable(clipboard_demo clipboard_demo.c)
    target_link_libraries(clipboard_demo PRIVATE zen_ui platform ${MATH_LIB})

    add_executable(events_demo events_demo.c)
    target_link_libraries(events_demo PRIVATE zen_ui platform ${MATH_LIB})

    add_executable(wtest  wtest.c)
    target_link_libraries(wtest PRIVATE zen_2d zen_ui platform ${MATH_LIB} OpenGL::GL)

    add_executable(glapp  glapp.c)
    target_link_libraries(glapp PRIVATE zen_2d zen_ui platform ${MATH_LIB})

    add_executable(demo_widgets_pixel demo_widgets_pixel.c)
    target_link_libraries(demo_widgets_pixel PRIVATE zen_ui platform ${MATH_LIB})

    add_executable(demo_widgets_gl demo_widgets_gl.c)
    target_link_libraries(demo_widgets_gl PRIVATE zen_2d zen_ui platform ${MATH_LIB} OpenGL::GL)

    add_executable(demo_dock_pixel demo_dock_pixel.c)
    target_link_libraries(demo_dock_pixel PRIVATE zen_ui platform ${MATH_LIB})

    add_executable(demo_dialog demo_dialog.c)
    target_link_libraries(demo_dialog PRIVATE zen_ui platform ${MATH_LIB})

    add_executable(ddemo  ddemo.c)
    target_link_libraries(ddemo PRIVATE platform ${MATH_LIB})

    add_executable(galaxy  galaxy.c)
    target_link_libraries(galaxy PRIVATE platform ${MATH_LIB})

    add_executable(shaders shaders.c)
    target_link_libraries(shaders PRIVATE platform ${MATH_LIB})
    find_package(OpenGL REQUIRED)
    target_link_libraries(shaders PRIVATE OpenGL::GL)

    add_executable(bench_sprite bench_sprite.c)
    target_link_libraries(bench_sprite PRIVATE zen_2d platform ${MATH_LIB} OpenGL::GL)

    add_executable(bench bench.c)
    target_link_libraries(bench PRIVATE zen_2d platform ${MATH_LIB} OpenGL::GL)

    add_executable(benchshot benchshot.c)
    target_link_libraries(benchshot PRIVATE zen_2d platform ${MATH_LIB} OpenGL::GL)

    add_executable(batchtest batchtest.c)
    target_link_libraries(batchtest PRIVATE zen_2d platform ${MATH_LIB} OpenGL::GL)
endif()
