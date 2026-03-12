#include "grass_render.h"

int grass_render_create(GrassRenderMesh* mesh)
{
  return palm_render_create_category(mesh, PALM_RENDER_CATEGORY_GRASS);
}

void grass_render_destroy(GrassRenderMesh* mesh)
{
  palm_render_destroy(mesh);
}

int grass_render_update(
  GrassRenderMesh* mesh,
  const CameraState* camera,
  const SceneSettings* settings,
  const RendererQualityProfile* quality)
{
  return palm_render_update_category(mesh, PALM_RENDER_CATEGORY_GRASS, camera, settings, quality);
}

void grass_render_draw(const GrassRenderMesh* mesh)
{
  palm_render_draw(mesh);
}
