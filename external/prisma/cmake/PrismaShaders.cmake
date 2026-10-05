if(NOT PRISMA_ROOT_DIR)
  set(PRISMA_ROOT_DIR ${PROJECT_SOURCE_DIR})
endif()
if(NOT PRISMA_SHADER_INCLUDE_DIR)
  set(PRISMA_SHADER_INCLUDE_DIR ${PRISMA_ROOT_DIR}/tests/shaders)
endif()

find_program(PRISMA_GLSLANG NAMES glslangValidator glslang
             HINTS ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE})
find_program(PRISMA_SPIRV_CROSS spirv-cross)

set(PRISMA_SPIRV_CROSS_COMMAND ${PRISMA_SPIRV_CROSS})
set(PRISMA_SPIRV_CROSS_DEPENDS)
if(NOT PRISMA_SPIRV_CROSS AND NOT CMAKE_CROSSCOMPILING
   AND EXISTS ${PRISMA_ROOT_DIR}/external/SPIRV-Cross/CMakeLists.txt)
  set(SPIRV_CROSS_CLI ON CACHE BOOL "" FORCE)
  set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
  set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
  set(SPIRV_CROSS_SHARED OFF CACHE BOOL "" FORCE)
  add_subdirectory(${PRISMA_ROOT_DIR}/external/SPIRV-Cross
                   ${PROJECT_BINARY_DIR}/spirv-cross EXCLUDE_FROM_ALL)
  set(PRISMA_SPIRV_CROSS_COMMAND $<TARGET_FILE:spirv-cross>)
  set(PRISMA_SPIRV_CROSS_DEPENDS spirv-cross)
endif()

if(PRISMA_GLSLANG AND PRISMA_SPIRV_CROSS_COMMAND)
  set(PRISMA_SHADER_TOOLS ON)
else()
  set(PRISMA_SHADER_TOOLS OFF)
  message(STATUS "prisma: glslang or spirv-cross not found, targets that need shaders are skipped")
endif()

file(GLOB PRISMA_SHADER_INCLUDES CONFIGURE_DEPENDS ${PRISMA_SHADER_INCLUDE_DIR}/*.glsl)

function(prisma_shaders target)
  set(directory ${CMAKE_CURRENT_BINARY_DIR}/shaders/${target})
  foreach(shader ${ARGN})
    get_filename_component(name ${shader} NAME)
    string(REPLACE "." "_" variable ${name})
    set(output ${directory}/${name}.h)
    add_custom_command(
      OUTPUT ${output}
      COMMAND ${CMAKE_COMMAND}
              -DGLSLANG=${PRISMA_GLSLANG}
              -DSPIRV_CROSS=${PRISMA_SPIRV_CROSS_COMMAND}
              -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/${shader}
              -DWORK=${directory}/${name}
              -DNAME=${variable}
              -DOUTPUT=${output}
              -P ${PRISMA_ROOT_DIR}/cmake/ShaderHeader.cmake
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${shader}
              ${PRISMA_SHADER_INCLUDES}
              ${PRISMA_ROOT_DIR}/cmake/ShaderHeader.cmake
              ${PRISMA_SPIRV_CROSS_DEPENDS}
      VERBATIM)
    target_sources(${target} PRIVATE ${output})
  endforeach()
  target_include_directories(${target} PRIVATE ${directory})
endfunction()
