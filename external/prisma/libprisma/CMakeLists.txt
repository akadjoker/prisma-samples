file(GLOB_RECURSE PRISMA_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/prisma/*.cpp")
if(NOT PRISMA_OPENGL)
  list(FILTER PRISMA_SOURCES EXCLUDE REGEX "/rhi/gl/")
elseif(PRISMA_GLES)
  list(FILTER PRISMA_SOURCES EXCLUDE REGEX "/rhi/gl/loader/OpenGL\\.cpp$")
else()
  list(FILTER PRISMA_SOURCES EXCLUDE REGEX "/rhi/gl/loader/OpenGLES3\\.cpp$")
endif()
if(NOT PRISMA_VULKAN)
  list(FILTER PRISMA_SOURCES EXCLUDE REGEX "/rhi/vulkan/")
endif()

add_library(prisma STATIC ${PRISMA_SOURCES})
target_include_directories(prisma
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(prisma PUBLIC ct)
if(NOT MSVC)
  target_compile_options(prisma PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()

if(PRISMA_OPENGL)
  target_compile_definitions(prisma PRIVATE PRISMA_HAS_OPENGL)
  target_link_libraries(prisma PRIVATE ${CMAKE_DL_LIBS})
  if(PRISMA_GLES)
    target_compile_definitions(prisma PUBLIC PRISMA_GLES)
    target_link_libraries(prisma PRIVATE EGL)
  endif()
  if(NOT MSVC)
    set_source_files_properties(
      src/prisma/rhi/gl/loader/OpenGL.cpp
      src/prisma/rhi/gl/loader/OpenGLES3.cpp
      PROPERTIES COMPILE_OPTIONS "-w")
  endif()
endif()

if(PRISMA_VULKAN)
  find_package(Vulkan REQUIRED)
  target_compile_definitions(prisma PRIVATE PRISMA_HAS_VULKAN)
  target_link_libraries(prisma PRIVATE Vulkan::Vulkan)
endif()
