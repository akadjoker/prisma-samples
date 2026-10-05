cmake_minimum_required(VERSION 3.21)

file(MAKE_DIRECTORY ${WORK})
set(spv ${WORK}/shader.spv)

execute_process(COMMAND ${GLSLANG} -V --quiet -o ${spv} ${SOURCE}
                RESULT_VARIABLE result OUTPUT_VARIABLE log ERROR_VARIABLE log)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "${SOURCE}\n${log}")
endif()

execute_process(COMMAND ${SPIRV_CROSS} ${spv} --reflect
                RESULT_VARIABLE result OUTPUT_VARIABLE reflection ERROR_VARIABLE log)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "${SOURCE}: reflection failed\n${log}")
endif()

string(JSON mode GET "${reflection}" entryPoints 0 mode)
if(mode STREQUAL "vert")
  set(stage Vertex)
elseif(mode STREQUAL "frag")
  set(stage Fragment)
elseif(mode STREQUAL "comp")
  set(stage Compute)
elseif(mode STREQUAL "geom")
  set(stage Geometry)
elseif(mode STREQUAL "tesc")
  set(stage TessControl)
elseif(mode STREQUAL "tese")
  set(stage TessEval)
else()
  message(FATAL_ERROR "${SOURCE}: unsupported shader stage '${mode}'")
endif()

set(min_es 300)
if(stage STREQUAL "Compute")
  set(min_es 310)
elseif(stage STREQUAL "Geometry" OR stage STREQUAL "TessControl" OR stage STREQUAL "TessEval")
  set(min_es 320)
endif()

set(bindings "")
set(binding_count 0)
foreach(group ubos textures ssbos images)
  string(JSON count ERROR_VARIABLE missing LENGTH "${reflection}" ${group})
  if(missing OR count EQUAL 0)
    continue()
  endif()
  if(group STREQUAL "ubos")
    set(kind UniformBlock)
  elseif(group STREQUAL "textures")
    set(kind Texture)
  elseif(group STREQUAL "ssbos")
    set(kind StorageBuffer)
  else()
    set(kind StorageTexture)
  endif()
  if(group STREQUAL "ssbos" OR group STREQUAL "images")
    if(min_es LESS 310)
      set(min_es 310)
    endif()
  endif()
  math(EXPR last "${count} - 1")
  foreach(index RANGE ${last})
    string(JSON name GET "${reflection}" ${group} ${index} name)
    string(JSON slot GET "${reflection}" ${group} ${index} binding)
    string(JSON type GET "${reflection}" ${group} ${index} type)
    if(type MATCHES "CubeArray")
      set(min_es 320)
    endif()
    string(APPEND bindings
           "    { prisma::BindingKind::${kind}, \"${name}\", ${slot} },\n")
    math(EXPR binding_count "${binding_count} + 1")
  endforeach()
endforeach()

set(fixup)
if(stage STREQUAL "Vertex" OR stage STREQUAL "TessEval")
  set(fixup --fixup-clipspace)
endif()

set(emit_fixup "{ gl_Position.z = 2.0 * gl_Position.z - gl_Position.w; EmitVertex(); }")

execute_process(COMMAND ${SPIRV_CROSS} ${spv} --no-es --version 460
                RESULT_VARIABLE result OUTPUT_VARIABLE glsl ERROR_VARIABLE log)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "${SOURCE}: GLSL 460 generation failed\n${glsl}${log}")
endif()

file(READ ${spv} hex HEX)
string(REGEX REPLACE "(..)(..)(..)(..)" "0x\\4\\3\\2\\1, " words "${hex}")
string(REGEX REPLACE "((0x[0-9a-f]+, )(0x[0-9a-f]+, )?(0x[0-9a-f]+, )?(0x[0-9a-f]+, )?(0x[0-9a-f]+, )?(0x[0-9a-f]+, )?(0x[0-9a-f]+, )?(0x[0-9a-f]+, )?)"
       "    \\1\n" words "${words}")

set(text "#pragma once\n\n#include \"prisma/rhi/ShaderBlob.h\"\n\n")
string(APPEND text "static const std::uint32_t ${NAME}_spirv[] = {\n${words}};\n\n")
string(APPEND text "static const char ${NAME}_glsl[] = R\"prisma(${glsl})prisma\";\n\n")

foreach(version 300 310 320)
  set(essl${version} "nullptr")
  if(version LESS min_es)
    continue()
  endif()
  execute_process(COMMAND ${SPIRV_CROSS} ${spv} --es --version ${version} ${fixup}
                  RESULT_VARIABLE result OUTPUT_VARIABLE essl ERROR_VARIABLE log)
  if(NOT result EQUAL 0)
    continue()
  endif()
  if(stage STREQUAL "Geometry")
    string(REPLACE "EmitVertex();" "${emit_fixup}" essl "${essl}")
  endif()
  string(APPEND text
         "static const char ${NAME}_essl${version}[] = R\"prisma(${essl})prisma\";\n\n")
  set(essl${version} "${NAME}_essl${version}")
endforeach()

set(essl320_inner "nullptr")
if(stage STREQUAL "Vertex" OR stage STREQUAL "TessEval")
  execute_process(COMMAND ${SPIRV_CROSS} ${spv} --es --version 320
                  RESULT_VARIABLE result OUTPUT_VARIABLE inner ERROR_VARIABLE log)
  if(result EQUAL 0)
    string(APPEND text
           "static const char ${NAME}_essl320_inner[] = R\"prisma(${inner})prisma\";\n\n")
    set(essl320_inner "${NAME}_essl320_inner")
  endif()
endif()

set(binding_table "nullptr")
if(binding_count GREATER 0)
  string(APPEND text
         "static const prisma::ShaderBinding ${NAME}_bindings[] = {\n${bindings}};\n\n")
  set(binding_table "${NAME}_bindings")
endif()

string(APPEND text
       "static const prisma::ShaderBlob ${NAME} = { prisma::ShaderStage::${stage}, ${NAME}_spirv,\n"
       "    sizeof(${NAME}_spirv), ${NAME}_glsl, ${essl300}, ${essl310}, ${essl320}, ${essl320_inner}, ${binding_table},\n"
       "    ${binding_count} };\n")
file(WRITE ${OUTPUT} "${text}")
