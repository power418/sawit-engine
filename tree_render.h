#ifndef TREE_RENDER_H
#define TREE_RENDER_H

#include "palm_render.h"

typedef PalmRenderMesh TreeRenderMesh;

int tree_render_create(TreeRenderMesh* mesh);
void tree_render_destroy(TreeRenderMesh* mesh);
int tree_render_update(
  TreeRenderMesh* mesh,
  const CameraState* camera,
  const SceneSettings* settings,
  const RendererQualityProfile* quality);
void tree_render_draw(const TreeRenderMesh* mesh);

#endif
