#ifndef MULTIPLAYER_RENDER_H
#define MULTIPLAYER_RENDER_H

#include "camera.h"
#include "scene_settings.h"

typedef struct SawitServiceClient SawitServiceClient;

void multiplayer_render_draw_players(
  int width,
  int height,
  const CameraState* camera,
  const SceneSettings* settings,
  const SawitServiceClient* client
);

#endif
