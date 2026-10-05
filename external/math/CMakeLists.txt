cmake_minimum_required(VERSION 3.15)
project(mathc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

option(MATHC_NATIVE_ARCH "Enable -march=native (x86 host builds only)" ON)
option(MATHC_BUILD_TESTS "Build the GoogleTest suite" ON)
option(MATHC_BUILD_BENCH "Build the benchmark executables" ON)

add_library(mathc STATIC mathc.cpp mathc.h)
target_include_directories(mathc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_path(GLM_INCLUDE_DIR glm/glm.hpp PATHS /usr/local/include /usr/include)
if(NOT GLM_INCLUDE_DIR)
    message(WARNING "glm/glm.hpp not found; the GoogleTest suite and benchmarks are skipped")
endif()

if(MATHC_BUILD_TESTS AND NOT EMSCRIPTEN)
    enable_testing()

    # Validates the distributable single-header build with the optional
    # higher-level types excluded.  This is independent of GoogleTest/GLM.
    add_executable(mathc_smoke_no_extra tests/smoke_no_extra.cpp)
    add_test(NAME mathc_smoke_no_extra COMMAND mathc_smoke_no_extra)

    if(GLM_INCLUDE_DIR)
        include(FetchContent)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.15.2
        )
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)

        include(GoogleTest)

        add_executable(mathc_tests tests/test_vec2.cpp tests/test_vec3.cpp tests/test_vec4.cpp tests/test_mat2.cpp tests/test_transform2d.cpp tests/test_mat3.cpp tests/test_mat4.cpp tests/test_quaternion.cpp tests/test_ray.cpp tests/test_plane.cpp tests/test_box.cpp tests/test_frustum.cpp tests/test_randomized_glm.cpp)
        target_link_libraries(mathc_tests PRIVATE mathc gtest_main)
        target_include_directories(mathc_tests PRIVATE ${GLM_INCLUDE_DIR})
        gtest_discover_tests(mathc_tests)

    endif()
endif()

if(MATHC_BUILD_BENCH AND GLM_INCLUDE_DIR)
    add_executable(bench_vec2 bench/bench_vec2.cpp)
    target_link_libraries(bench_vec2 PRIVATE mathc)
    target_include_directories(bench_vec2 PRIVATE ${GLM_INCLUDE_DIR})

    add_executable(bench_vec3 bench/bench_vec3.cpp)
    target_link_libraries(bench_vec3 PRIVATE mathc)
    target_include_directories(bench_vec3 PRIVATE ${GLM_INCLUDE_DIR})

    add_executable(bench_vec4 bench/bench_vec4.cpp)
    target_link_libraries(bench_vec4 PRIVATE mathc)
    target_include_directories(bench_vec4 PRIVATE ${GLM_INCLUDE_DIR})

    add_executable(bench_mat4 bench/bench_mat4.cpp)
    target_link_libraries(bench_mat4 PRIVATE mathc)
    target_include_directories(bench_mat4 PRIVATE ${GLM_INCLUDE_DIR})

    add_executable(bench_quaternion bench/bench_quaternion.cpp)
    target_link_libraries(bench_quaternion PRIVATE mathc)
    target_include_directories(bench_quaternion PRIVATE ${GLM_INCLUDE_DIR})

    if(MATHC_NATIVE_ARCH AND NOT EMSCRIPTEN AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
        # Compile both sides of the comparison for the local CPU.  mathc is
        # normally kept portable; this applies only when benchmark targets
        # are requested.
        target_compile_options(mathc PRIVATE -march=native)
        target_compile_options(bench_vec2 PRIVATE -march=native)
        target_compile_options(bench_vec3 PRIVATE -march=native)
        target_compile_options(bench_vec4 PRIVATE -march=native)
        target_compile_options(bench_mat4 PRIVATE -march=native)
        target_compile_options(bench_quaternion PRIVATE -march=native)
    endif()
endif()
