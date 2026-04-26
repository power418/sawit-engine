CONFIG ?= Release
COMPILER ?= clang
RC ?= llvm-rc

ifeq ($(OS),Windows_NT)
EXE := .exe
else
$(error This Makefile currently supports Windows only)
endif

CONFIG_LOWER := $(shell powershell -NoProfile -Command "'$(CONFIG)'.ToLowerInvariant()")
BUILD_DIR ?= build/$(COMPILER)-$(CONFIG_LOWER)
OBJ_DIR := $(BUILD_DIR)/obj
TARGET := $(BUILD_DIR)/engine-core$(EXE)

GLEW_VERSION := 2.2.0
GLEW_URL := https://github.com/nigels-com/glew/releases/download/glew-$(GLEW_VERSION)/glew-$(GLEW_VERSION).tgz
GLEW_CACHE_DIR := .cache
GLEW_ARCHIVE := $(GLEW_CACHE_DIR)/glew-$(GLEW_VERSION).tgz
GLEW_LOCAL_ROOT := $(GLEW_CACHE_DIR)/glew-$(GLEW_VERSION)
GLEW_ROOT := $(GLEW_LOCAL_ROOT)

GLEW_SOURCE := $(GLEW_ROOT)/src/glew.c
GLEW_READY := $(BUILD_DIR)/glew.ready

PROJECT_SOURCES := \
  app.c \
  audio.c \
  block_render.c \
  block_world.c \
  console_overlay.c \
  diagnostics.c \
  grass_render.c \
  graphics_backend.c \
  gpu_preferences.c \
  keymap.c \
  main.c \
  math3d.c \
  palm_render.c \
  platform_support.c \
  platform_win32.c \
  player_controller.c \
  procedural_lod.c \
  render_quality.c \
  renderer.c \
  stats_overlay.c \
  system_monitor.c \
  terrain.c \
  tree_render.c

PROJECT_OBJECTS := \
  $(OBJ_DIR)/app.obj \
  $(OBJ_DIR)/audio.obj \
  $(OBJ_DIR)/block_render.obj \
  $(OBJ_DIR)/block_world.obj \
  $(OBJ_DIR)/console_overlay.obj \
  $(OBJ_DIR)/diagnostics.obj \
  $(OBJ_DIR)/grass_render.obj \
  $(OBJ_DIR)/graphics_backend.obj \
  $(OBJ_DIR)/gpu_preferences.obj \
  $(OBJ_DIR)/keymap.obj \
  $(OBJ_DIR)/main.obj \
  $(OBJ_DIR)/math3d.obj \
  $(OBJ_DIR)/palm_render.obj \
  $(OBJ_DIR)/platform_support.obj \
  $(OBJ_DIR)/platform_win32.obj \
  $(OBJ_DIR)/player_controller.obj \
  $(OBJ_DIR)/procedural_lod.obj \
  $(OBJ_DIR)/render_quality.obj \
  $(OBJ_DIR)/renderer.obj \
  $(OBJ_DIR)/stats_overlay.obj \
  $(OBJ_DIR)/system_monitor.obj \
  $(OBJ_DIR)/terrain.obj \
  $(OBJ_DIR)/tree_render.obj

GLEW_OBJECT := $(OBJ_DIR)/glew.obj
RESOURCE_FILE := $(OBJ_DIR)/shaders.res
RESOURCE_DEPENDENCIES := \
  resource.h \
  res/icon/sawit_app.ico \
  shaders/sky.vert.glsl \
  shaders/sky.frag.glsl \
  shaders/post.vert.glsl \
  shaders/post.frag.glsl

OBJECTS := $(PROJECT_OBJECTS) $(GLEW_OBJECT) $(RESOURCE_FILE)
DEPS := $(PROJECT_OBJECTS:.obj=.d) $(GLEW_OBJECT:.obj=.d)

CPPFLAGS := -I. -I$(GLEW_ROOT)/include -DGLEW_STATIC -DGLEW_NO_GLU
WARNINGS := -Wall -Wextra -Wpedantic -Wno-strict-prototypes
COMMON_CFLAGS := -std=c11 $(WARNINGS)
GLEW_CFLAGS := -fno-builtin -fno-stack-protector
DEPFLAGS := -MMD -MP
LDFLAGS := -mwindows
LDLIBS := -lopengl32 -ldxgi -ldxguid -lgdi32 -lmfplat -lmfreadwrite -lmfuuid -lole32 -loleaut32 -lpdh -luser32 -ladvapi32 -lwbemuuid -lwinmm

ifeq ($(CONFIG),Debug)
CONFIG_CFLAGS := -O0 -g
else ifeq ($(CONFIG),Release)
CONFIG_CFLAGS := -O2 -DNDEBUG
else
$(error Unsupported CONFIG '$(CONFIG)'. Use CONFIG=Debug or CONFIG=Release)
endif

CFLAGS := $(COMMON_CFLAGS) $(CONFIG_CFLAGS)

.PHONY: all build assets run clean distclean

all: build

build: $(TARGET) assets

$(BUILD_DIR):
	@powershell -NoProfile -Command "New-Item -ItemType Directory -Force '$(BUILD_DIR)' | Out-Null"

$(OBJ_DIR): | $(BUILD_DIR)
	@powershell -NoProfile -Command "New-Item -ItemType Directory -Force '$(OBJ_DIR)' | Out-Null"

$(GLEW_READY): | $(BUILD_DIR)
	@powershell -NoProfile -Command "New-Item -ItemType Directory -Force '$(GLEW_CACHE_DIR)' | Out-Null; if (-not (Test-Path '$(GLEW_SOURCE)')) { if (-not (Test-Path '$(GLEW_ARCHIVE)')) { & curl.exe -L '$(GLEW_URL)' -o '$(GLEW_ARCHIVE)' }; & tar -xf '$(GLEW_ARCHIVE)' -C '$(GLEW_CACHE_DIR)' }; if (-not (Test-Path '$(GLEW_SOURCE)')) { throw 'GLEW source was not found after bootstrap.' }; New-Item -ItemType File -Force '$(GLEW_READY)' | Out-Null"

$(OBJ_DIR)/%.obj: %.c $(GLEW_READY) | $(OBJ_DIR)
	$(COMPILER) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.obj=.d) -c $< -o $@

$(GLEW_OBJECT): $(GLEW_READY) | $(OBJ_DIR)
	$(COMPILER) $(CPPFLAGS) $(CFLAGS) $(GLEW_CFLAGS) $(DEPFLAGS) -MF $(@:.obj=.d) -c $(GLEW_SOURCE) -o $@

$(RESOURCE_FILE): shaders.rc $(RESOURCE_DEPENDENCIES) | $(OBJ_DIR)
	$(RC) /FO $@ $<

$(TARGET): $(OBJECTS) | $(BUILD_DIR)
	$(COMPILER) $(OBJECTS) $(LDFLAGS) -o $@ $(LDLIBS)

assets: $(TARGET)
	@powershell -NoProfile -Command "Copy-Item -Path 'shaders' -Destination '$(BUILD_DIR)' -Recurse -Force"
	@powershell -NoProfile -Command "Copy-Item -Path 'res' -Destination '$(BUILD_DIR)' -Recurse -Force"

run: build
	@powershell -NoProfile -Command "& '$(TARGET)'"

clean:
	@powershell -NoProfile -Command "if (Test-Path '$(BUILD_DIR)') { Remove-Item -Recurse -Force '$(BUILD_DIR)' }"

distclean: clean
	@powershell -NoProfile -Command "if (Test-Path '$(GLEW_LOCAL_ROOT)') { Remove-Item -Recurse -Force '$(GLEW_LOCAL_ROOT)' }; if (Test-Path '$(GLEW_ARCHIVE)') { Remove-Item -Force '$(GLEW_ARCHIVE)' }"

-include $(DEPS)
