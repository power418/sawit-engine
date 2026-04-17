cmake_minimum_required(VERSION 3.24)

if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)
endif()

set(
  OPENGL_SKY_AUTO_DETECT_VCPKG
  ON
  CACHE BOOL
  "Automatically detect a local vcpkg toolchain when CMAKE_TOOLCHAIN_FILE is not set."
)
set(
  OPENGL_SKY_VCPKG_ROOT
  ""
  CACHE PATH
  "Path to the vcpkg root directory. Leave empty to let CMake auto-detect it."
)

function(opengl_sky_resolve_vcpkg_root out_var)
  set(candidates "")
  set(resolved_root "")

  if(NOT "${OPENGL_SKY_VCPKG_ROOT}" STREQUAL "")
    list(APPEND candidates "${OPENGL_SKY_VCPKG_ROOT}")
  endif()

  if(DEFINED ENV{VCPKG_ROOT} AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
    list(APPEND candidates "$ENV{VCPKG_ROOT}")
  endif()

  if(DEFINED ENV{SystemDrive} AND NOT "$ENV{SystemDrive}" STREQUAL "")
    list(APPEND candidates
      "$ENV{SystemDrive}/vcpkg"
      "$ENV{SystemDrive}/src/vcpkg"
      "$ENV{SystemDrive}/tools/vcpkg"
    )
  else()
    list(APPEND candidates
      "C:/vcpkg"
      "C:/src/vcpkg"
      "C:/tools/vcpkg"
    )
  endif()

  if(DEFINED ENV{USERPROFILE} AND NOT "$ENV{USERPROFILE}" STREQUAL "")
    list(APPEND candidates "$ENV{USERPROFILE}/vcpkg")
  endif()

  if(DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
    list(APPEND candidates
      "$ENV{HOME}/vcpkg"
      "/opt/vcpkg"
      "/usr/local/vcpkg"
    )
  endif()

  list(APPEND candidates
    "${CMAKE_SOURCE_DIR}/vcpkg"
    "${CMAKE_SOURCE_DIR}/../vcpkg"
    "${CMAKE_SOURCE_DIR}/../external/vcpkg"
  )

  find_program(
    OPENGL_SKY_VCPKG_EXECUTABLE
    NAMES vcpkg vcpkg.exe
    HINTS "${OPENGL_SKY_VCPKG_ROOT}" "$ENV{VCPKG_ROOT}"
  )
  if(OPENGL_SKY_VCPKG_EXECUTABLE)
    get_filename_component(OPENGL_SKY_VCPKG_EXECUTABLE_DIR "${OPENGL_SKY_VCPKG_EXECUTABLE}" DIRECTORY)
    list(APPEND candidates "${OPENGL_SKY_VCPKG_EXECUTABLE_DIR}")
  endif()

  if(DEFINED ENV{PATH} AND NOT "$ENV{PATH}" STREQUAL "")
    cmake_path(CONVERT "$ENV{PATH}" TO_CMAKE_PATH_LIST OPENGL_SKY_PATH_ENTRIES NORMALIZE)
    list(APPEND candidates ${OPENGL_SKY_PATH_ENTRIES})
  endif()

  list(REMOVE_DUPLICATES candidates)

  foreach(candidate IN LISTS candidates)
    set(probe_dirs "")

    if("${candidate}" STREQUAL "")
      continue()
    endif()

    if(IS_DIRECTORY "${candidate}")
      get_filename_component(candidate_parent "${candidate}" DIRECTORY)
      get_filename_component(candidate_grandparent "${candidate_parent}" DIRECTORY)
      list(APPEND probe_dirs "${candidate}" "${candidate_parent}" "${candidate_grandparent}")
    endif()

    foreach(probe_dir IN LISTS probe_dirs)
      if(IS_DIRECTORY "${probe_dir}" AND EXISTS "${probe_dir}/scripts/buildsystems/vcpkg.cmake")
        set(resolved_root "${probe_dir}")
        break()
      endif()
    endforeach()

    if(NOT "${resolved_root}" STREQUAL "")
      break()
    endif()
  endforeach()

  set(${out_var} "${resolved_root}" PARENT_SCOPE)
endfunction()

function(opengl_sky_detect_vcpkg_triplet out_var)
  set(arch "")
  set(system_suffix "")

  if(DEFINED VCPKG_TARGET_TRIPLET AND NOT "${VCPKG_TARGET_TRIPLET}" STREQUAL "")
    set(${out_var} "${VCPKG_TARGET_TRIPLET}" PARENT_SCOPE)
    return()
  endif()

  if(DEFINED ENV{VCPKG_DEFAULT_TRIPLET} AND NOT "$ENV{VCPKG_DEFAULT_TRIPLET}" STREQUAL "")
    set(${out_var} "$ENV{VCPKG_DEFAULT_TRIPLET}" PARENT_SCOPE)
    return()
  endif()

  if(CMAKE_GENERATOR_PLATFORM AND NOT "${CMAKE_GENERATOR_PLATFORM}" STREQUAL "")
    set(raw_arch "${CMAKE_GENERATOR_PLATFORM}")
  elseif(CMAKE_SYSTEM_PROCESSOR AND NOT "${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "")
    set(raw_arch "${CMAKE_SYSTEM_PROCESSOR}")
  elseif(CMAKE_HOST_SYSTEM_PROCESSOR AND NOT "${CMAKE_HOST_SYSTEM_PROCESSOR}" STREQUAL "")
    set(raw_arch "${CMAKE_HOST_SYSTEM_PROCESSOR}")
  else()
    set(raw_arch "")
  endif()

  string(TOLOWER "${raw_arch}" raw_arch_lower)
  if(raw_arch_lower MATCHES "^(amd64|x86_64|x64)$")
    set(arch "x64")
  elseif(raw_arch_lower MATCHES "^(arm64|aarch64)$")
    set(arch "arm64")
  elseif(raw_arch_lower MATCHES "^(x86|win32|i[3-6]86)$")
    set(arch "x86")
  elseif(raw_arch_lower MATCHES "^arm")
    set(arch "arm")
  else()
    set(arch "x64")
  endif()

  if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(system_suffix "windows")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(system_suffix "linux")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(system_suffix "osx")
  else()
    set(system_suffix "")
  endif()

  if(NOT "${system_suffix}" STREQUAL "")
    set(${out_var} "${arch}-${system_suffix}" PARENT_SCOPE)
  else()
    set(${out_var} "" PARENT_SCOPE)
  endif()
endfunction()

# vcpkg is only used on Windows; skip auto-detection on Linux / macOS / Emscripten.
if((NOT DEFINED CMAKE_TOOLCHAIN_FILE OR "${CMAKE_TOOLCHAIN_FILE}" STREQUAL "") AND
  OPENGL_SKY_AUTO_DETECT_VCPKG AND
  NOT EMSCRIPTEN AND
  CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
  opengl_sky_resolve_vcpkg_root(OPENGL_SKY_RESOLVED_VCPKG_ROOT)
  if(NOT "${OPENGL_SKY_RESOLVED_VCPKG_ROOT}" STREQUAL "")
    set(OPENGL_SKY_VCPKG_ROOT "${OPENGL_SKY_RESOLVED_VCPKG_ROOT}" CACHE PATH "Path to the vcpkg root directory. Leave empty to let CMake auto-detect it." FORCE)
    set(ENV{VCPKG_ROOT} "${OPENGL_SKY_VCPKG_ROOT}")
    set(
      CMAKE_TOOLCHAIN_FILE
      "${OPENGL_SKY_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
      CACHE FILEPATH
      "vcpkg toolchain file"
      FORCE
    )
    message(STATUS "Auto-detected vcpkg toolchain: ${CMAKE_TOOLCHAIN_FILE}")
  endif()
endif()

project(opengl_sky LANGUAGES NONE)

set(OPENGL_SKY_PLATFORM_OS "${CMAKE_SYSTEM_NAME}")
opengl_sky_detect_vcpkg_triplet(OPENGL_SKY_VCPKG_TRIPLET)

set(OPENGL_SKY_VCPKG_INSTALLED_DIR "")
set(OPENGL_SKY_VCPKG_INCLUDE_DIR "")
if(NOT "${OPENGL_SKY_VCPKG_ROOT}" STREQUAL "" AND NOT "${OPENGL_SKY_VCPKG_TRIPLET}" STREQUAL "")
  set(OPENGL_SKY_VCPKG_INSTALLED_DIR "${OPENGL_SKY_VCPKG_ROOT}/installed/${OPENGL_SKY_VCPKG_TRIPLET}")
  set(OPENGL_SKY_VCPKG_INCLUDE_DIR "${OPENGL_SKY_VCPKG_INSTALLED_DIR}/include")
  if(IS_DIRECTORY "${OPENGL_SKY_VCPKG_INCLUDE_DIR}")
    message(STATUS "Using vcpkg include dir: ${OPENGL_SKY_VCPKG_INCLUDE_DIR}")
  endif()
endif()

if("${OPENGL_SKY_PLATFORM_OS}" STREQUAL "Windows" AND CMAKE_GENERATOR STREQUAL "Unix Makefiles")
  set(OPENGL_SKY_MSVC_LIKE_COMPILER FALSE)

  if(CMAKE_C_COMPILER MATCHES [[(^|[/\\])(cl|clang-cl)(\.exe)?$]])
    set(OPENGL_SKY_MSVC_LIKE_COMPILER TRUE)
  elseif(DEFINED ENV{CC} AND "$ENV{CC}" MATCHES [[(^|[/\\])(cl|clang-cl)(\.exe)?$]])
    set(OPENGL_SKY_MSVC_LIKE_COMPILER TRUE)
  elseif(DEFINED ENV{VCINSTALLDIR})
    set(OPENGL_SKY_MSVC_LIKE_COMPILER TRUE)
  endif()

  if(OPENGL_SKY_MSVC_LIKE_COMPILER)
    message(FATAL_ERROR
      "MSVC-style compilers cannot be used with the \"Unix Makefiles\" generator in this environment. "
      "The Git/MSYS shell rewrites linker flags such as /nologo into filesystem paths, which breaks the compiler check. "
      "Configure with a native MSVC generator instead, for example:\n"
      "  cmake --preset windows-msvc-debug\n"
      "or\n"
      "  cmake -S . -B build/msvc-debug -G \"Visual Studio 18 2026\" -A x64"
    )
  endif()
endif()

enable_language(C)

if("${OPENGL_SKY_PLATFORM_OS}" STREQUAL "Windows")
  enable_language(RC)
endif()

if("${OPENGL_SKY_PLATFORM_OS}" STREQUAL "Darwin")
  enable_language(OBJCXX)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(EMSCRIPTEN)
  add_executable(
    engine_core_wasm
    block_world.c
    terrain.c
    web_scene_service.c
    web_bridge.c
  )

  target_include_directories(engine_core_wasm PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

  if(MSVC)
    target_compile_options(engine_core_wasm PRIVATE /W4)
  else()
    target_compile_options(engine_core_wasm PRIVATE -Wall -Wextra -Wpedantic -Wno-strict-prototypes)
  endif()

  target_link_options(
    engine_core_wasm
    PRIVATE
      "-sSTANDALONE_WASM=1"
      "-sALLOW_MEMORY_GROWTH=1"
      "-sASSERTIONS=0"
      "-sFILESYSTEM=0"
      "-Wl,--no-entry"
      "-Wl,--export-memory"
      "-Wl,--export=malloc"
      "-Wl,--export=free"
      "-Wl,--export=engine_web_reset_state"
      "-Wl,--export=engine_web_set_terrain_profile"
      "-Wl,--export=engine_web_set_foliage_profile"
      "-Wl,--export=engine_web_fill_height_buffer"
      "-Wl,--export=engine_web_get_height_at"
      "-Wl,--export=engine_web_get_terrain_vertex_count"
      "-Wl,--export=engine_web_get_terrain_index_count"
      "-Wl,--export=engine_web_copy_terrain_vertices"
      "-Wl,--export=engine_web_get_block_count"
      "-Wl,--export=engine_web_copy_blocks"
      "-Wl,--export=engine_web_copy_block_instances"
      "-Wl,--export=engine_web_get_foliage_capacity"
      "-Wl,--export=engine_web_copy_foliage_instances"
      "-Wl,--export=engine_web_place_block_column"
      "-Wl,--export=engine_web_remove_block_column"
      "-Wl,--export=engine_web_describe_state"
  )

  set_target_properties(
    engine_core_wasm
    PROPERTIES
      OUTPUT_NAME "engine-core"
      SUFFIX ".wasm"
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )

  add_custom_command(
    TARGET engine_core_wasm
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/public/wasm"
    COMMAND ${CMAKE_COMMAND} -E copy
      "$<TARGET_FILE:engine_core_wasm>"
      "${CMAKE_CURRENT_SOURCE_DIR}/public/wasm/engine-core.wasm"
  )
else()
  include(FetchContent)

  find_package(OpenGL REQUIRED)
  if("${OPENGL_SKY_PLATFORM_OS}" STREQUAL "Linux")
    find_package(X11 REQUIRED)
  endif()

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

    if("${OPENGL_SKY_PLATFORM_OS}" STREQUAL "Windows" AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
      target_compile_options(opengl_sky_glew PRIVATE -fno-builtin -fno-stack-protector)
    endif()

    set(OPENGL_SKY_GLEW_TARGET opengl_sky_glew)
  endif()

  if(NOT OPENGL_SKY_GLEW_TARGET)
    message(FATAL_ERROR "GLEW could not be resolved. Install a CMake package for GLEW or allow FetchContent downloads.")
  endif()

  set(OPENGL_SKY_SOURCES
    app.c
    audio.c
    block_render.c
    block_world.c
    console_overlay.c
    diagnostics.c
    grass_render.c
    graphics_backend.c
    gpu_preferences.c
    math3d.c
    main.c
    mountain_render.c
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
    view_frustum.c
  )

  if("${OPENGL_SKY_PLATFORM_OS}" STREQUAL "Windows")
    list(APPEND OPENGL_SKY_SOURCES
      keymap.c
      platform_win32.c
      shaders.rc
    )
  elseif("${OPENGL_SKY_PLATFORM_OS}" STREQUAL "Darwin")
    list(APPEND OPENGL_SKY_SOURCES
      audio_macos.mm
      platform_cocoa.mm
    )
  elseif("${OPENGL_SKY_PLATFORM_OS}" STREQUAL "Linux")
    list(APPEND OPENGL_SKY_SOURCES
      platform_x11.c
    )
  else()
    message(FATAL_ERROR "Unsupported platform. Supported CMAKE_SYSTEM_NAME values are Windows, Darwin, and Linux.")
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

  if("${OPENGL_SKY_PLATFORM_OS}" STREQUAL "Windows")
    add_executable(opengl_sky WIN32 ${OPENGL_SKY_SOURCES} ${OPENGL_SKY_SHADER_SOURCES})
  else()
    add_executable(opengl_sky ${OPENGL_SKY_SOURCES} ${OPENGL_SKY_SHADER_SOURCES})
  endif()

  target_include_directories(opengl_sky PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  if(IS_DIRECTORY "${OPENGL_SKY_VCPKG_INCLUDE_DIR}")
    target_include_directories(opengl_sky SYSTEM PRIVATE "${OPENGL_SKY_VCPKG_INCLUDE_DIR}")
  endif()

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

  if("${OPENGL_SKY_PLATFORM_OS}" STREQUAL "Windows")
    target_link_libraries(
      opengl_sky
      PRIVATE
        dxgi
        dxguid
        gdi32
        ole32
        oleaut32
        pdh
        user32
        wbemuuid
        winmm
    )
  elseif("${OPENGL_SKY_PLATFORM_OS}" STREQUAL "Darwin")
    find_library(AVFOUNDATION_FRAMEWORK AVFoundation REQUIRED)
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    target_link_libraries(opengl_sky PRIVATE ${AVFOUNDATION_FRAMEWORK} ${COCOA_FRAMEWORK})
    target_compile_definitions(opengl_sky PRIVATE GL_SILENCE_DEPRECATION)
  elseif("${OPENGL_SKY_PLATFORM_OS}" STREQUAL "Linux")
    target_compile_definitions(opengl_sky PRIVATE _POSIX_C_SOURCE=200809L)
    target_link_libraries(opengl_sky PRIVATE m)
    if(TARGET X11::X11)
      target_link_libraries(opengl_sky PRIVATE X11::X11)
    else()
      target_include_directories(opengl_sky PRIVATE ${X11_INCLUDE_DIR})
      target_link_libraries(opengl_sky PRIVATE ${X11_LIBRARIES})
    endif()
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
endif()
