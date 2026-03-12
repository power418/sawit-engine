cmake_minimum_required(VERSION 3.24)

if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)
endif()

project(opengl_sky LANGUAGES C)

if(APPLE)
  enable_language(OBJCXX)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

find_package(OpenGL REQUIRED)

function(opengl_sky_pick_glew_target out_var)
  foreach(candidate IN ITEMS GLEW::glew_s GLEW::glew GLEW::GLEW glew_s glew)
    if(TARGET ${candidate})
      set(${out_var} "${candidate}" PARENT_SCOPE)
      return()
    endif()
  endforeach()

  set(${out_var} "" PARENT_SCOPE)
endfunction()

find_package(GLEW QUIET)
opengl_sky_pick_glew_target(OPENGL_SKY_GLEW_TARGET)

if(NOT OPENGL_SKY_GLEW_TARGET)
  FetchContent_Declare(
    glew
    URL https://github.com/nigels-com/glew/releases/download/glew-2.2.0/glew-2.2.0.tgz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_GetProperties(glew)
  if(NOT glew_POPULATED)
    FetchContent_Populate(glew)
  endif()

  add_library(opengl_sky_glew STATIC "${glew_SOURCE_DIR}/src/glew.c")
  target_include_directories(opengl_sky_glew PUBLIC "${glew_SOURCE_DIR}/include")
  target_compile_definitions(opengl_sky_glew PUBLIC GLEW_STATIC GLEW_NO_GLU)
  target_link_libraries(opengl_sky_glew PUBLIC OpenGL::GL)

  if(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(opengl_sky_glew PRIVATE -fno-builtin -fno-stack-protector)
  endif()

  set(OPENGL_SKY_GLEW_TARGET opengl_sky_glew)
endif()

if(NOT OPENGL_SKY_GLEW_TARGET)
  message(FATAL_ERROR "GLEW could not be resolved. Install a CMake package for GLEW or allow FetchContent downloads.")
endif()

set(OPENGL_SKY_SOURCES
  app.c
  block_render.c
  block_world.c
  console_overlay.c
  diagnostics.c
  grass_render.c
  graphics_backend.c
  gpu_preferences.c
  math3d.c
  main.c
  palm_render.c
  platform_support.c
  player_controller.c
  procedural_lod.c
  render_quality.c
  renderer.c
  stats_overlay.c
  system_monitor.c
  terrain.c
  tree_render.c
)

if(WIN32)
  list(APPEND OPENGL_SKY_SOURCES
    keymap.c
    platform_win32.c
  )
elseif(APPLE)
  list(APPEND OPENGL_SKY_SOURCES
    platform_cocoa.mm
  )
else()
  message(FATAL_ERROR "Unsupported platform. Only Win32 and macOS/Cocoa builds are configured.")
endif()

set(OPENGL_SKY_SHADER_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.vert.glsl"
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.frag.glsl"
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/post.vert.glsl"
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/post.frag.glsl"
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/post/fxaa.glsl"
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain.vert.glsl"
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain.frag.glsl"
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/palm.vert.glsl"
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/palm.frag.glsl"
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/palm_shadow.vert.glsl"
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/palm_shadow.frag.glsl"
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.vert.glsl"
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.frag.glsl"
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ui/overlay_blur.vert.glsl"
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ui/overlay_blur.frag.glsl"
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ui/blur_gaussian.glsl"
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ui/blur_kawase.glsl"
)

if(WIN32)
  add_executable(opengl_sky WIN32 ${OPENGL_SKY_SOURCES} ${OPENGL_SKY_SHADER_SOURCES})
else()
  add_executable(opengl_sky ${OPENGL_SKY_SOURCES} ${OPENGL_SKY_SHADER_SOURCES})
endif()

target_include_directories(opengl_sky PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

if(DEFINED glew_SOURCE_DIR AND OPENGL_SKY_GLEW_TARGET STREQUAL "opengl_sky_glew")
  target_include_directories(opengl_sky PRIVATE "${glew_SOURCE_DIR}/include")
elseif(GLEW_INCLUDE_DIRS)
  target_include_directories(opengl_sky PRIVATE ${GLEW_INCLUDE_DIRS})
endif()

target_link_libraries(
  opengl_sky
  PRIVATE
    ${OPENGL_SKY_GLEW_TARGET}
    OpenGL::GL
)

if(WIN32)
  target_link_libraries(
    opengl_sky
    PRIVATE
      dxgi
      dxguid
      gdi32
      pdh
      user32
  )
elseif(APPLE)
  find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
  target_link_libraries(opengl_sky PRIVATE ${COCOA_FRAMEWORK})
  target_compile_definitions(opengl_sky PRIVATE GL_SILENCE_DEPRECATION)
endif()

if(MSVC)
  target_compile_options(opengl_sky PRIVATE /W4)
else()
  target_compile_options(opengl_sky PRIVATE -Wall -Wextra -Wpedantic -Wno-strict-prototypes)
endif()

set_target_properties(
  opengl_sky
  PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)

foreach(config Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER "${config}" config_upper)
  set_target_properties(
    opengl_sky
    PROPERTIES
      "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${CMAKE_BINARY_DIR}"
  )
endforeach()

add_custom_command(
  TARGET opengl_sky
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    "$<TARGET_FILE_DIR:opengl_sky>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/res"
    "$<TARGET_FILE_DIR:opengl_sky>/res"
)

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT opengl_sky)
