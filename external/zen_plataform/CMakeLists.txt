cmake_minimum_required(VERSION 3.16)

project(platform LANGUAGES C)

# Static CRT: executables and the library depend on no runtime DLL (vcruntime, ...).
# MSVC is only defined once the compiler is known, so this goes after project().
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(PLATFORM_BUILD_TESTS "Build unit tests" ON)
option(PLATFORM_USE_FAKE    "Force the fake backend" OFF)
option(PLATFORM_SANITIZE    "Build with ASan + UBSan (debug)" OFF)
# BUILD_SHARED_LIBS (CMake builtin): ON -> .so/.dll, OFF -> .a/.lib

set(CORE_SRC src/core.c src/gesture.c src/log.c src/library.c src/thread.c src/dialog.c src/preedit.c src/vulkan.c src/clipboard.c src/png.c src/error.c src/gamepad.c src/io.c src/os.c src/draw2d.c src/image.c src/glad.c)
if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN AND NOT PLATFORM_USE_FAKE)
    list(APPEND CORE_SRC src/gamepad_evdev.c)
elseif(WIN32 AND NOT PLATFORM_USE_FAKE)
    list(APPEND CORE_SRC src/gamepad_xinput.c)
else()
    list(APPEND CORE_SRC src/gamepad_stub.c)
endif()
if(WIN32)
    list(APPEND CORE_SRC src/io_win32.c src/fs_win32.c src/win32_util.c)
else()
    list(APPEND CORE_SRC src/io_posix.c src/fs_posix.c)
endif()

if(PLATFORM_USE_FAKE)
    set(BACKEND_SRC src/backend_fake.c)
elseif(EMSCRIPTEN)
    set(BACKEND_SRC src/backend_web.c)
elseif(ANDROID)
    set(BACKEND_SRC src/backend_android.c src/io_android.c
        ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
elseif(WIN32)
    set(BACKEND_SRC src/backend_win32.c)
elseif(APPLE)
    message(FATAL_ERROR "No native macOS/iOS backend yet; configure with -DPLATFORM_USE_FAKE=ON for a headless build")
elseif(UNIX)
    set(BACKEND_SRC src/backend_x11.c)
    set(PLATFORM_BACKEND_X11 ON)
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

add_library(platform ${CORE_SRC} ${BACKEND_SRC})
target_include_directories(platform PUBLIC include PRIVATE src)
target_compile_features(platform PUBLIC c_std_11)
set_target_properties(platform PROPERTIES
    C_VISIBILITY_PRESET hidden POSITION_INDEPENDENT_CODE ON)
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(platform PUBLIC PLATFORM_STATIC)
endif()

# zen_ui: immediate-mode widget toolkit. Backend-agnostic core; links only the
# platform, never OpenGL. Ships the GL-free software renderer; the GL renderer
# lives in the separate zen_2d library below.
# The embedded default font: a vendored TTF turned into a C array at build time
# by cmake/embed_font.cmake (cmake -P, so it works under cross toolchains too).
set(ZEN_UI_FONT_TTF ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/fonts/DejaVuSans.ttf)
set(ZEN_UI_FONT_C   ${CMAKE_CURRENT_BINARY_DIR}/ui_font_default.c)
add_custom_command(
    OUTPUT ${ZEN_UI_FONT_C}
    COMMAND ${CMAKE_COMMAND} -DSRC=${ZEN_UI_FONT_TTF} -DDST=${ZEN_UI_FONT_C}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_font.cmake
    DEPENDS ${ZEN_UI_FONT_TTF} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_font.cmake
    COMMENT "Embedding default font ${ZEN_UI_FONT_TTF}"
    VERBATIM)

set(ZEN_UI_SRC
    src/ui/ui_core.c
    src/ui/ui_utf8.c
    src/ui/ui_font.c
    src/ui/ui_layout.c
    src/ui/ui_theme.c
    src/ui/ui_state.c
    src/ui/stb_impl.c
    src/ui/ui_widgets.c
    src/ui/ui_dialog.c
    src/ui/ui_render_software.c
    ${ZEN_UI_FONT_C})
add_library(zen_ui ${ZEN_UI_SRC})
target_include_directories(zen_ui PUBLIC include PRIVATE src src/ui)
target_compile_features(zen_ui PUBLIC c_std_11)
set_target_properties(zen_ui PROPERTIES
    C_VISIBILITY_PRESET hidden POSITION_INDEPENDENT_CODE ON)
target_link_libraries(zen_ui PUBLIC platform)

# zen_2d (r2d): batched 2D engine on GL, and the zen_ui GL renderer. Separate
# library so the toolkit core never depends on OpenGL. Desktop uses GL 3.3 core
# via glad (Linux and Windows); web (WebGL2) and Android (GLES3) use the GLES3 path in r2d.c.
if(NOT PLATFORM_USE_FAKE AND (EMSCRIPTEN OR ANDROID OR UNIX OR WIN32))
    add_library(zen_2d src/r2d/r2d.c src/r2d/r2d_ui.c)
    target_include_directories(zen_2d PUBLIC include PRIVATE src/r2d)
    target_compile_features(zen_2d PUBLIC c_std_11)
    set_target_properties(zen_2d PROPERTIES
        C_VISIBILITY_PRESET hidden POSITION_INDEPENDENT_CODE ON)
    if(EMSCRIPTEN)
        target_link_libraries(zen_2d PUBLIC zen_ui platform) # GL resolved at app link (FULL_ES3)
    elseif(ANDROID)
        target_link_libraries(zen_2d PUBLIC zen_ui platform GLESv3 m)
    else()
        find_package(OpenGL REQUIRED)
        target_link_libraries(zen_2d PUBLIC zen_ui platform OpenGL::GL)
        if(NOT WIN32)
            target_link_libraries(zen_2d PUBLIC m)
        endif()
    endif()
endif()

# Sanitizers wire into every target through these interface flags. ASan + UBSan
# catch the lifetime and signed-overflow bugs this kind of code attracts.
add_library(platform_sanitize INTERFACE)
if(PLATFORM_SANITIZE)
    if(MSVC)
        target_compile_options(platform_sanitize INTERFACE /fsanitize=address)
    else()
        target_compile_options(platform_sanitize INTERFACE
            -fsanitize=address,undefined -fno-omit-frame-pointer -g)
        target_link_options(platform_sanitize INTERFACE
            -fsanitize=address,undefined)
    endif()
endif()
target_link_libraries(platform PUBLIC platform_sanitize)
if(MSVC)
    target_compile_options(platform PUBLIC /utf-8)
    target_compile_definitions(platform PUBLIC _CRT_SECURE_NO_WARNINGS)
endif()

# native libs per platform
if(EMSCRIPTEN)
    # GL/EGL emulated; nothing to link
elseif(ANDROID)
    target_include_directories(platform PUBLIC
        ${ANDROID_NDK}/sources/android/native_app_glue)
    target_link_libraries(platform PUBLIC android EGL GLESv3 log)
    # Keep the activity entry point the glue exports from being stripped from the .so.
    target_link_options(platform PUBLIC "-u" "ANativeActivity_onCreate")
elseif(WIN32)
    target_link_libraries(platform PUBLIC opengl32 gdi32 user32 shell32)
    if(MINGW)
        target_link_options(platform PUBLIC -static)
    endif()
elseif(UNIX AND NOT PLATFORM_USE_FAKE)
    find_package(X11 REQUIRED)
    find_package(Threads REQUIRED)
    target_link_libraries(platform PUBLIC X11 Xrandr GL ${CMAKE_DL_LIBS} Threads::Threads)
endif()

if(PLATFORM_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT ANDROID)
    enable_testing()
    add_subdirectory(tests)
endif()

option(PLATFORM_BUILD_EXAMPLES "Build examples" ON)
if(PLATFORM_BUILD_EXAMPLES AND NOT PLATFORM_USE_FAKE AND (EMSCRIPTEN OR ANDROID OR UNIX OR WIN32))
    add_subdirectory(examples)
endif()

install(TARGETS platform ARCHIVE DESTINATION lib LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
install(FILES include/platform.h include/platform_export.h DESTINATION include)
