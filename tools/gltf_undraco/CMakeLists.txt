cmake_minimum_required(VERSION 3.19)
project(gltf_undraco C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

get_filename_component(PRISMA_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(DRACO_DIR "${PRISMA_ROOT}/tmp/filament/third_party/draco" CACHE PATH "Draco source tree")
set(CGLTF_DIR "${PRISMA_ROOT}/tmp/filament/third_party/cgltf" CACHE PATH "cgltf and cgltf_write headers")
set(CT_DIR "/media/projectos/projects/cpp/containers" CACHE PATH "ct library")

add_subdirectory("${DRACO_DIR}/tnt" "${CMAKE_BINARY_DIR}/draco")

add_executable(gltf_undraco main.cpp cgltf_impl.cpp)
target_include_directories(gltf_undraco PRIVATE "${DRACO_DIR}/src" "${CGLTF_DIR}" "${CT_DIR}/include")
target_link_libraries(gltf_undraco PRIVATE dracodec)
