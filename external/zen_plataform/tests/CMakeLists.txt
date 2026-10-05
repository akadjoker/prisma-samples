# Tests compile core.c + backend_fake.c directly, independent of the backend the
# main lib chose. The X11 lib and the headless fake tests live in the same tree.
function(add_fake_test name)
    add_executable(${name} ${name}.c
        ${CMAKE_SOURCE_DIR}/src/core.c
        ${CMAKE_SOURCE_DIR}/src/gesture.c
        ${CMAKE_SOURCE_DIR}/src/error.c
        ${CMAKE_SOURCE_DIR}/src/log.c
        ${CMAKE_SOURCE_DIR}/src/gamepad.c
        ${CMAKE_SOURCE_DIR}/src/gamepad_stub.c
        ${CMAKE_SOURCE_DIR}/src/backend_fake.c)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
    target_compile_features(${name} PRIVATE c_std_11)
    target_compile_definitions(${name} PRIVATE PLATFORM_STATIC)
    target_link_libraries(${name} PRIVATE platform_sanitize)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_fake_test(test_input_logic)
add_fake_test(test_app_run)
add_fake_test(test_gestures)
add_fake_test(test_log)
add_fake_test(test_events)

# Portable modules with no window backend: compile just their sources (in ARGN).
function(add_unit_test name)
    set(srcs "")
    foreach(s ${ARGN})
        list(APPEND srcs ${CMAKE_SOURCE_DIR}/${s})
    endforeach()
    add_executable(${name} ${name}.c ${srcs})
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_features(${name} PRIVATE c_std_11)
    target_compile_definitions(${name} PRIVATE PLATFORM_STATIC)
    target_link_libraries(${name} PRIVATE platform_sanitize)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

if(WIN32)
    set(FS_SRC src/os.c src/error.c src/log.c src/io.c src/io_win32.c src/fs_win32.c src/win32_util.c)
else()
    set(FS_SRC src/os.c src/error.c src/log.c src/io.c src/io_posix.c src/fs_posix.c)
endif()
add_unit_test(test_fs ${FS_SRC})
add_unit_test(test_path ${FS_SRC})
add_unit_test(test_io ${FS_SRC})
add_unit_test(test_fsys ${FS_SRC})
add_unit_test(test_xinput_map src/gamepad.c src/gamepad_stub.c)
add_unit_test(test_library src/library.c ${FS_SRC})
add_unit_test(test_preedit src/preedit.c)
target_link_libraries(test_library PRIVATE ${CMAKE_DL_LIBS})
add_unit_test(test_draw2d src/draw2d.c)
add_unit_test(test_image src/draw2d.c src/image.c ${FS_SRC})

if(UNIX AND NOT APPLE)
    add_unit_test(test_gamepad src/gamepad.c ${FS_SRC})
endif()

if(WIN32 AND NOT PLATFORM_USE_FAKE)
    add_executable(test_win32 test_win32.c)
    target_link_libraries(test_win32 PRIVATE platform)
    add_test(NAME test_win32 COMMAND test_win32)
endif()

if(PLATFORM_BACKEND_X11 OR (WIN32 AND NOT PLATFORM_USE_FAKE))
    add_executable(test_vulkan test_vulkan.c)
    target_link_libraries(test_vulkan PRIVATE platform)
    add_test(NAME test_vulkan COMMAND test_vulkan)
    set_tests_properties(test_vulkan PROPERTIES SKIP_RETURN_CODE 77)
endif()

if(PLATFORM_BACKEND_X11)
    add_executable(test_gl_x11 test_gl_x11.c)
    target_link_libraries(test_gl_x11 PRIVATE platform)
    add_test(NAME test_gl_x11 COMMAND test_gl_x11)
    set_tests_properties(test_gl_x11 PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(test_clipboard_x11 test_clipboard_x11.c)
    target_link_libraries(test_clipboard_x11 PRIVATE platform)
    add_test(NAME test_clipboard_x11 COMMAND test_clipboard_x11)
    set_tests_properties(test_clipboard_x11 PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)

    add_executable(test_multiwindow_x11 test_multiwindow_x11.c)
    target_link_libraries(test_multiwindow_x11 PRIVATE platform)
    add_test(NAME test_multiwindow_x11 COMMAND test_multiwindow_x11)
    set_tests_properties(test_multiwindow_x11 PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(test_dialog test_dialog.c)
    target_link_libraries(test_dialog PRIVATE platform)
    add_test(NAME test_dialog COMMAND test_dialog)

    add_executable(test_x11_window test_x11_window.c)
    target_link_libraries(test_x11_window PRIVATE platform)
    add_test(NAME test_x11_window COMMAND test_x11_window)
    set_tests_properties(test_x11_window PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(test_input_x11 test_input_x11.c)
    target_link_libraries(test_input_x11 PRIVATE platform)
    add_test(NAME test_input_x11 COMMAND test_input_x11)
    set_tests_properties(test_input_x11 PROPERTIES SKIP_RETURN_CODE 77)
endif()


# Fake test with extra source files (widgets + UI + software renderer).
function(add_fake_ext_test name)
    set(srcs "")
    foreach(s ${ARGN})
        list(APPEND srcs ${CMAKE_SOURCE_DIR}/${s})
    endforeach()
    add_executable(${name} ${name}.c
        ${CMAKE_SOURCE_DIR}/src/core.c
        ${CMAKE_SOURCE_DIR}/src/gesture.c
        ${CMAKE_SOURCE_DIR}/src/backend_fake.c
        ${srcs})
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
    target_compile_features(${name} PRIVATE c_std_11)
    target_compile_definitions(${name} PRIVATE PLATFORM_STATIC)
    target_link_libraries(${name} PRIVATE platform_sanitize)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# Full widget test in both PIXEL and GL mode
add_fake_ext_test(test_widgets
    ${FS_SRC}
    src/gamepad.c
    src/gamepad_stub.c
    src/draw2d.c
    src/ui/ui_core.c
    src/ui/ui_widgets.c
    src/ui/ui_layout.c
    src/ui/ui_state.c
    src/ui/ui_theme.c
    src/ui/ui_render_software.c
    src/ui/ui_font.c
    src/ui/stb_impl.c
    src/ui/ui_utf8.c
    src/ui/ui_dialog.c)

# Clipboard API over the fake backend's in-memory store, PNG encoder and decoder.
add_fake_ext_test(test_clipboard
    ${FS_SRC}
    src/gamepad.c
    src/gamepad_stub.c
    src/clipboard.c
    src/png.c
    src/draw2d.c)

# Threads: the fake core only supplies time_sleep and time_seconds to the test.
add_fake_ext_test(test_thread
    ${FS_SRC}
    src/gamepad.c
    src/gamepad_stub.c
    src/thread.c)
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(test_thread PRIVATE Threads::Threads)
endif()

# The test also needs libm for stb_truetype math functions (sqrt, floor, etc.)
if(NOT WIN32)
    target_link_libraries(test_widgets PRIVATE m)
endif()

# A test that hangs must not hold a runner for the default 25 minutes.
get_property(ZEN_ALL_TESTS DIRECTORY PROPERTY TESTS)
foreach(t ${ZEN_ALL_TESTS})
    get_test_property(${t} TIMEOUT existing)
    if(NOT existing)
        set_tests_properties(${t} PROPERTIES TIMEOUT 120)
    endif()
endforeach()
