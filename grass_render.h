#ifndef GRASS_RENDER_H
#define GRASS_RENDER_H

#include "palm_render.h"

typedef PalmRenderMesh GrassRenderMesh;

int grass_render_create(GrassRenderMesh* mesh);
void grass_render_destroy(GrassRenderMesh* mesh);
int grass_render_update(
  GrassRenderMesh* mesh,
  const CameraState* camera,
  const SceneSettings* settings,
  const RendererQualityProfile* quality);
void grass_render_draw(const GrassRenderMesh* mesh);

#endif
