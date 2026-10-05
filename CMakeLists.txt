cmake_minimum_required(VERSION 3.21)
project(prisma_samples LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(SAMPLES_BUILD_TESTS "Build the tests of the shared demo code" ON)
option(SAMPLES_WINDOW_TESTS "Also run the GPU tests in a real window" OFF)
option(SAMPLES_HEADLESS "Build the demos without a window system, for screenshots" OFF)
set(PRISMA_MEDIA_DIR "" CACHE PATH "Folder with the media the demos load")
set(PRISMA_MODELS_DIR "" CACHE PATH "glTF models used by the viewer demos and the tests")

set(PRISMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/prisma)

set(PRISMA_ROOT_DIR ${PROJECT_SOURCE_DIR}/external/prisma)
set(PRISMA_SHADER_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/common/shaders)
include(${PRISMA_ROOT_DIR}/cmake/PrismaShaders.cmake)

if(NOT PRISMA_OPENGL)
  message(FATAL_ERROR "prisma samples: the demos need the OpenGL backend")
endif()
find_package(Vulkan QUIET)
if(SAMPLES_HEADLESS)
  find_library(PRISMA_EGL EGL REQUIRED)
  set(PLATFORM_USE_FAKE ON CACHE BOOL "" FORCE)
endif()

set(PLATFORM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PLATFORM_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(external/zen_plataform EXCLUDE_FROM_ALL)

add_library(mathc STATIC external/math/mathc.cpp)
target_include_directories(mathc PUBLIC external/math)

add_library(demo_common STATIC common/stb_impl.cpp)
if(EMSCRIPTEN)
  target_sources(demo_common PRIVATE common/WebFrame.cpp)
endif()
target_include_directories(demo_common PUBLIC common)
target_compile_options(demo_common PRIVATE -w)

if(PRISMA_SHADER_TOOLS)
  add_library(demo_ibl STATIC common/Prefilter.cpp common/Froxelizer.cpp common/TextOverlay.cpp
          common/GltfModel.cpp common/ObjModel.cpp common/GltfGpu.cpp common/cgltf_impl.cpp
          common/PostProcess.cpp common/Ssao.cpp common/SunShadow.cpp
          common/ColorPyramid.cpp
          common/third_party/mikktspace.c)
  set_source_files_properties(common/cgltf_impl.cpp common/third_party/mikktspace.c
                              PROPERTIES COMPILE_OPTIONS -w)
  target_include_directories(demo_ibl PUBLIC common)
  target_link_libraries(demo_ibl PUBLIC prisma ct demo_common)
  prisma_shaders(demo_ibl common/shaders/prefilter.vert common/shaders/prefilter.frag
                 common/shaders/text.vert common/shaders/text.frag
                 common/shaders/fullscreen.vert common/shaders/bloom_down2x.frag
                 common/shaders/bloom_down13.frag common/shaders/bloom_down9.frag
                 common/shaders/bloom_up.frag common/shaders/final.frag
                 common/shaders/fxaa.frag common/shaders/depth_only.vert
                 common/shaders/depth_only.frag common/shaders/mipmap_depth.frag
                 common/shaders/sao.frag common/shaders/ssao_blur.frag
                 common/shaders/gaussian_blur.frag)
endif()

function(prisma_demo folder name)
  cmake_parse_arguments(DEMO "" "" "SHADERS" ${ARGN})
  if(DEMO_SHADERS AND NOT PRISMA_SHADER_TOOLS)
    return()
  endif()
  add_executable(${name} ${folder}/main.cpp)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${name} PRIVATE prisma platform mathc demo_common)
  if(TARGET demo_ibl)
    target_link_libraries(${name} PRIVATE demo_ibl)
  endif()
  if(EMSCRIPTEN)
    # in the browser the media is a virtual folder packed with the page: the files a demo lists in
    # its web_media.txt are copied from PRISMA_MEDIA_DIR into a staging folder and preloaded
    set(demo_media "/media")
    set(stage ${CMAKE_BINARY_DIR}/web-media/${name})
    file(REMOVE_RECURSE ${stage})
    file(MAKE_DIRECTORY ${stage})
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${folder}/web_media.txt)
      file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/${folder}/web_media.txt entries)
      foreach(entry ${entries})
        if(entry STREQUAL "" OR entry MATCHES "^#")
          continue()
        endif()
        get_filename_component(entry_dir ${entry} DIRECTORY)
        if(IS_DIRECTORY "${PRISMA_MEDIA_DIR}/${entry}")
          file(COPY "${PRISMA_MEDIA_DIR}/${entry}" DESTINATION "${stage}/${entry_dir}")
        elseif(EXISTS "${PRISMA_MEDIA_DIR}/${entry}")
          file(COPY "${PRISMA_MEDIA_DIR}/${entry}" DESTINATION "${stage}/${entry_dir}")
        else()
          message(WARNING "web media of ${name}: ${PRISMA_MEDIA_DIR}/${entry} not found")
        endif()
      endforeach()
    endif()
    target_link_options(${name} PRIVATE
        -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sFULL_ES3=1 -sASYNCIFY
        -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=134217728 -sSTACK_SIZE=1048576)
    file(GLOB_RECURSE staged ${stage}/*)
    if(staged)
      target_link_options(${name} PRIVATE --preload-file ${stage}@/media)
    endif()
    set_target_properties(${name} PROPERTIES SUFFIX ".html")
    # the page around the canvas, with the title of the demo and the link to its source
    set(DEMO_FOLDER ${folder})
    set(DEMO_TITLE "${folder}")
    string(REGEX REPLACE "^([0-9]+)_(.*)$" "\\1 \\2" DEMO_TITLE "${folder}")
    string(REPLACE "_" " " DEMO_TITLE "${DEMO_TITLE}")
    configure_file(${PROJECT_SOURCE_DIR}/web/shell.html.in ${CMAKE_BINARY_DIR}/shells/${name}.html @ONLY)
    target_link_options(${name} PRIVATE --shell-file ${CMAKE_BINARY_DIR}/shells/${name}.html)
    set_property(TARGET ${name} APPEND PROPERTY LINK_DEPENDS ${CMAKE_BINARY_DIR}/shells/${name}.html)
  else()
    set(demo_media "${PRISMA_MEDIA_DIR}")
  endif()
  target_compile_definitions(${name} PRIVATE PRISMA_MEDIA_DIR="${demo_media}"
                             PRISMA_MODELS_DIR="${PRISMA_MODELS_DIR}")
  if(SAMPLES_HEADLESS)
    target_compile_definitions(${name} PRIVATE PRISMA_HEADLESS)
    target_link_libraries(${name} PRIVATE ${PRISMA_EGL})
    if(PRISMA_VULKAN)
      target_link_libraries(${name} PRIVATE Vulkan::Vulkan)
    endif()
  endif()
  if(DEMO_SHADERS)
    prisma_shaders(${name} ${DEMO_SHADERS})
  endif()
endfunction()

file(GLOB demo_folders RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/[0-9][0-9]_*)
list(SORT demo_folders)
foreach(folder ${demo_folders})
  if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${folder}/main.cpp)
    continue()
  endif()
  string(REGEX REPLACE "^[0-9]+_" "" name ${folder})
  file(GLOB shaders RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS
       ${CMAKE_CURRENT_SOURCE_DIR}/${folder}/shaders/*.vert
       ${CMAKE_CURRENT_SOURCE_DIR}/${folder}/shaders/*.frag
       ${CMAKE_CURRENT_SOURCE_DIR}/${folder}/shaders/*.comp
       ${CMAKE_CURRENT_SOURCE_DIR}/${folder}/shaders/*.geom
       ${CMAKE_CURRENT_SOURCE_DIR}/${folder}/shaders/*.tesc
       ${CMAKE_CURRENT_SOURCE_DIR}/${folder}/shaders/*.tese)
  if(shaders)
    prisma_demo(${folder} ${name} SHADERS ${shaders})
  else()
    prisma_demo(${folder} ${name})
  endif()
endforeach()

if(SAMPLES_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
