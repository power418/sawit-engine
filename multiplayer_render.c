#include "multiplayer_render.h"

#include "service_api.h"

#if defined(__APPLE__)

void multiplayer_render_draw_players(
  int width,
  int height,
  const CameraState* camera,
  const SceneSettings* settings,
  const SawitServiceClient* client
)
{
  (void)width;
  (void)height;
  (void)camera;
  (void)settings;
  (void)client;
}

#else

#include "gl_headers.h"
#include "math3d.h"
#include "terrain.h"

#include <math.h>

static void multiplayer_render_draw_wire_box(float min_x, float min_y, float min_z, float max_x, float max_y, float max_z);
static void multiplayer_render_draw_player(const SawitRemotePlayer* player, const SceneSettings* settings);

void multiplayer_render_draw_players(
  int width,
  int height,
  const CameraState* camera,
  const SceneSettings* settings,
  const SawitServiceClient* client
)
{
  const SceneSettings fallback_settings = scene_settings_default();
  const SceneSettings* active_settings = (settings != NULL) ? settings : &fallback_settings;
  Matrix projection = { 0 };
  Matrix view = { 0 };
  int player_index = 0;

  if (width <= 0 || height <= 0 || camera == NULL || client == NULL || !client->connected)
  {
    return;
  }

  projection = math_get_projection_matrix(width, height, active_settings->camera_fov_degrees);
  view = math_get_view_matrix(camera->x, camera->y, camera->z, camera->yaw, camera->pitch);

  glUseProgram(0);
  glBindVertexArray(0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadMatrixf(projection.m);
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadMatrixf(view.m);

  glLineWidth(2.5f);
  for (player_index = 0; player_index < client->remote_player_count; ++player_index)
  {
    const SawitRemotePlayer* player = &client->remote_players[player_index];
    if (player->active != 0)
    {
      multiplayer_render_draw_player(player, active_settings);
    }
  }
  glLineWidth(1.0f);

  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
  glDisable(GL_BLEND);
}

static void multiplayer_render_draw_player(const SawitRemotePlayer* player, const SceneSettings* settings)
{
  const float radius = 0.34f;
  const float body_height = 1.80f;
  const float feet_y = terrain_get_render_height(player->x, player->z, settings);
  const float head_y = feet_y + body_height;
  const float facing_length = 0.85f;
  const float forward_x = -sinf(player->yaw);
  const float forward_z = -cosf(player->yaw);

  glColor4f(0.15f, 0.88f, 1.0f, 0.92f);
  multiplayer_render_draw_wire_box(
    player->x - radius,
    feet_y,
    player->z - radius,
    player->x + radius,
    head_y,
    player->z + radius);

  glColor4f(0.95f, 1.0f, 0.24f, 0.95f);
  glBegin(GL_LINES);
  glVertex3f(player->x, head_y - 0.28f, player->z);
  glVertex3f(player->x + forward_x * facing_length, head_y - 0.28f, player->z + forward_z * facing_length);
  glEnd();
}

static void multiplayer_render_draw_wire_box(float min_x, float min_y, float min_z, float max_x, float max_y, float max_z)
{
  glBegin(GL_LINES);
  glVertex3f(min_x, min_y, min_z);
  glVertex3f(max_x, min_y, min_z);
  glVertex3f(max_x, min_y, min_z);
  glVertex3f(max_x, min_y, max_z);
  glVertex3f(max_x, min_y, max_z);
  glVertex3f(min_x, min_y, max_z);
  glVertex3f(min_x, min_y, max_z);
  glVertex3f(min_x, min_y, min_z);

  glVertex3f(min_x, max_y, min_z);
  glVertex3f(max_x, max_y, min_z);
  glVertex3f(max_x, max_y, min_z);
  glVertex3f(max_x, max_y, max_z);
  glVertex3f(max_x, max_y, max_z);
  glVertex3f(min_x, max_y, max_z);
  glVertex3f(min_x, max_y, max_z);
  glVertex3f(min_x, max_y, min_z);

  glVertex3f(min_x, min_y, min_z);
  glVertex3f(min_x, max_y, min_z);
  glVertex3f(max_x, min_y, min_z);
  glVertex3f(max_x, max_y, min_z);
  glVertex3f(max_x, min_y, max_z);
  glVertex3f(max_x, max_y, max_z);
  glVertex3f(min_x, min_y, max_z);
  glVertex3f(min_x, max_y, max_z);
  glEnd();
}

#endif
