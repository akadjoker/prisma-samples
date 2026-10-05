cmake_minimum_required(VERSION 3.14)
project(ct_containers CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(CT_IS_TOP_LEVEL ON)
else()
  set(CT_IS_TOP_LEVEL OFF)
endif()
option(CT_BUILD_TESTS "Build the ct test suite (fetches GoogleTest)" ${CT_IS_TOP_LEVEL})
option(CT_BUILD_BENCH "Build the ct benchmarks" ${CT_IS_TOP_LEVEL})
set(CT_PHYS_DIR "" CACHE PATH "Optional phys project root; enables the BlockAllocator comparison in ct_bench_pool")

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(ct INTERFACE)
add_library(ct::ct ALIAS ct)
target_include_directories(ct INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(Threads REQUIRED)
add_library(ct_threads INTERFACE)
add_library(ct::threads ALIAS ct_threads)
target_link_libraries(ct_threads INTERFACE ct Threads::Threads)

add_library(ct_sockets INTERFACE)
add_library(ct::sockets ALIAS ct_sockets)
target_link_libraries(ct_sockets INTERFACE ct_threads)
if(WIN32)
  target_link_libraries(ct_sockets INTERFACE ws2_32)
endif()

if(NOT CT_BUILD_TESTS AND NOT CT_BUILD_BENCH)
  return()
endif()

if(MSVC)
  set(CT_BENCH_OPTIONS /O2)
else()
  set(CT_BENCH_OPTIONS -O3 -march=native)
endif()

if(CT_BUILD_TESTS)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

include(FetchContent)
FetchContent_Declare(googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(ct_tests
  tests/test_vector.cpp
  tests/test_arena.cpp
  tests/test_pool.cpp
  tests/test_string.cpp
  tests/test_maps.cpp
  tests/test_array_sort.cpp
  tests/test_deque.cpp
  tests/test_stack.cpp
  tests/test_queue.cpp
  tests/test_json.cpp
  tests/test_span.cpp
  tests/test_slotmap.cpp
  tests/test_slotmap32.cpp
  tests/test_ptr.cpp
  tests/test_xml.cpp
  tests/test_function.cpp
  tests/test_variant.cpp
  tests/test_limits.cpp
  tests/test_rectpacker.cpp
  tests/test_regex.cpp
  tests/test_thread.cpp
  tests/test_stream.cpp
  tests/test_binary.cpp
  tests/test_text.cpp
  tests/test_http.cpp
  tests/test_socket.cpp
  tests/test_ini.cpp
  tests/test_soup.cpp)
target_link_libraries(ct_tests PRIVATE ct_sockets gtest_main)
include(GoogleTest)
gtest_discover_tests(ct_tests)

set(CT_PUBLIC_HEADERS
  ct/arena.hpp
  ct/array.hpp
  ct/binary.hpp
  ct/deque.hpp
  ct/flatmap.hpp
  ct/function.hpp
  ct/hashmap.hpp
  ct/hashset.hpp
  ct/http.hpp
  ct/http_client.hpp
  ct/http_server.hpp
  ct/ini.hpp
  ct/json.hpp
  ct/json_stream.hpp
  ct/pool.hpp
  ct/priority_queue.hpp
  ct/ptr.hpp
  ct/queue.hpp
  ct/rectpacker.hpp
  ct/regex.hpp
  ct/socket.hpp
  ct/socket_stream.hpp
  ct/soup.hpp
  ct/stream.hpp
  ct/text.hpp
  ct/slotmap.hpp
  ct/slotmap32.hpp
  ct/sort.hpp
  ct/span.hpp
  ct/stack.hpp
  ct/string.hpp
  ct/thread.hpp
  ct/threadpool.hpp
  ct/treemap.hpp
  ct/variant.hpp
  ct/vector.hpp
  ct/xml.hpp
  ct/xml_stream.hpp)

set(CT_HEADER_TARGETS)
foreach(header IN LISTS CT_PUBLIC_HEADERS)
  string(MAKE_C_IDENTIFIER "${header}" target_suffix)
  set(target "ct_header_${target_suffix}")
  add_library(${target} OBJECT tests/header_standalone.cpp)
  target_link_libraries(${target} PRIVATE ct)
  target_compile_definitions(${target} PRIVATE CT_HEADER="${header}")
  list(APPEND CT_HEADER_TARGETS ${target})
endforeach()
add_custom_target(ct_headers DEPENDS ${CT_HEADER_TARGETS})

add_executable(ct_torture
  tests/torture.cpp)
target_link_libraries(ct_torture PRIVATE ct)
endif()

if(NOT CT_BUILD_BENCH)
  return()
endif()

add_executable(ct_bench
  bench/bench_vector.cpp)
target_link_libraries(ct_bench PRIVATE ct)
target_compile_options(ct_bench PRIVATE ${CT_BENCH_OPTIONS})

add_executable(ct_bench_arena
  bench/bench_arena.cpp)
target_link_libraries(ct_bench_arena PRIVATE ct)
target_compile_options(ct_bench_arena PRIVATE ${CT_BENCH_OPTIONS})

add_executable(ct_bench_pool
  bench/bench_pool.cpp)
target_link_libraries(ct_bench_pool PRIVATE ct)
target_compile_options(ct_bench_pool PRIVATE ${CT_BENCH_OPTIONS})

add_executable(ct_bench_string
  bench/bench_string.cpp)
target_link_libraries(ct_bench_string PRIVATE ct)
target_compile_options(ct_bench_string PRIVATE ${CT_BENCH_OPTIONS})

add_executable(ct_bench_map
  bench/bench_map.cpp)
target_link_libraries(ct_bench_map PRIVATE ct)
target_compile_options(ct_bench_map PRIVATE ${CT_BENCH_OPTIONS})

add_executable(ct_bench_slotmap
  bench/bench_slotmap.cpp)
target_link_libraries(ct_bench_slotmap PRIVATE ct)
target_compile_options(ct_bench_slotmap PRIVATE ${CT_BENCH_OPTIONS})

add_executable(ct_bench_sort
  bench/bench_sort.cpp)
target_link_libraries(ct_bench_sort PRIVATE ct)
target_compile_options(ct_bench_sort PRIVATE ${CT_BENCH_OPTIONS})

add_executable(ct_bench_regex
  bench/bench_regex.cpp)
target_link_libraries(ct_bench_regex PRIVATE ct)
target_compile_options(ct_bench_regex PRIVATE ${CT_BENCH_OPTIONS})

add_executable(ct_bench_deque
  bench/bench_deque.cpp)
target_link_libraries(ct_bench_deque PRIVATE ct)
target_compile_options(ct_bench_deque PRIVATE ${CT_BENCH_OPTIONS})

add_executable(ct_bench_stack
  bench/bench_stack.cpp)
target_link_libraries(ct_bench_stack PRIVATE ct)
target_compile_options(ct_bench_stack PRIVATE ${CT_BENCH_OPTIONS})

add_executable(ct_bench_queue
  bench/bench_queue.cpp)
target_link_libraries(ct_bench_queue PRIVATE ct)
target_compile_options(ct_bench_queue PRIVATE ${CT_BENCH_OPTIONS})

if(CT_PHYS_DIR AND EXISTS "${CT_PHYS_DIR}/src/block_allocator.cpp")
  target_sources(ct_bench_pool PRIVATE "${CT_PHYS_DIR}/src/block_allocator.cpp")
  target_include_directories(ct_bench_pool PRIVATE "${CT_PHYS_DIR}/include")
  target_compile_definitions(ct_bench_pool PRIVATE CT_BENCH_HAVE_PHYS)
endif()
