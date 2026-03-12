#include "block_render.h"

#if defined(__APPLE__)

void block_render_draw_world(
  int width,
  int height,
  const CameraState* camera,
  const AtmosphereState* atmosphere,
  const SceneSettings* settings,
  const BlockWorld* world
)
{
  (void)width;
  (void)height;
  (void)camera;
  (void)atmosphere;
  (void)settings;
  (void)world;
}

#else

#include "gl_headers.h"
#include "math3d.h"

#include <math.h>

static float block_render_clamp(float value, float min_value, float max_value);
static float block_render_mix(float a, float b, float t);
static void block_render_emit_face(int face_index, float x, float y, float z);
static void block_render_draw_wire_cube(float x, float y, float z, float r, float g, float b);

void block_render_draw_world(
  int width,
  int height,
  const CameraState* camera,
  const AtmosphereState* atmosphere,
  const SceneSettings* settings,
  const BlockWorld* world
)
{
  const SceneSettings fallback_settings = scene_settings_default();
  const SceneSettings* active_settings = (settings != NULL) ? settings : &fallback_settings;
  Matrix projection = { 0 };
  Matrix view = { 0 };
  const BlockWorldCell* cells = NULL;
  const BlockRaycastTarget* target = block_world_get_target(world);
  const float sun_x = (atmosphere != NULL) ? atmosphere->sun_direction[0] : 0.0f;
  const float sun_y = (atmosphere != NULL) ? atmosphere->sun_direction[1] : 1.0f;
  const float sun_z = (atmosphere != NULL) ? atmosphere->sun_direction[2] : 0.0f;
  const float fog_r = 0.56f + block_render_clamp(sun_y, -0.2f, 0.8f) * 0.08f;
  const float fog_g = 0.64f + block_render_clamp(sun_y, -0.2f, 0.8f) * 0.08f;
  const float fog_b = 0.76f + block_render_clamp(sun_y, -0.2f, 0.8f) * 0.06f;
  const float face_normals[6][3] = {
    { 0.0f, 0.0f, -1.0f },
    { 0.0f, 0.0f, 1.0f },
    { -1.0f, 0.0f, 0.0f },
    { 1.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f },
    { 0.0f, -1.0f, 0.0f }
  };
  int cell_count = 0;
  int cell_index = 0;

  if (width <= 0 || height <= 0 || camera == NULL || world == NULL)
  {
    return;
  }

  projection = math_get_projection_matrix(width, height, active_settings->camera_fov_degrees);
  view = math_get_view_matrix(camera->x, camera->y, camera->z, camera->yaw, camera->pitch);
  cells = block_world_get_cells(world, &cell_count);

  glUseProgram(0);
  glBindVertexArray(0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);

  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadMatrixf(projection.m);
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadMatrixf(view.m);

  glBegin(GL_QUADS);
  for (cell_index = 0; cell_index < cell_count; ++cell_index)
  {
    const BlockWorldCell* cell = &cells[cell_index];
    const float center_x = (float)cell->x + 0.5f;
    const float center_y = (float)cell->y + 0.5f;
    const float center_z = (float)cell->z + 0.5f;
    const float distance_x = center_x - camera->x;
    const float distance_y = center_y - camera->y;
    const float distance_z = center_z - camera->z;
    const float distance = sqrtf(distance_x * distance_x + distance_y * distance_y + distance_z * distance_z);
    const float fog_strength = block_render_clamp(1.0f - expf(-distance * (0.0018f + active_settings->fog_density * 0.010f)), 0.0f, 0.58f);
    float base_r = 0.7f;
    float base_g = 0.7f;
    float base_b = 0.7f;
    int face_index = 0;

    block_world_get_block_color(cell->type, &base_r, &base_g, &base_b);
    for (face_index = 0; face_index < 6; ++face_index)
    {
      const float normal_x = face_normals[face_index][0];
      const float normal_y = face_normals[face_index][1];
      const float normal_z = face_normals[face_index][2];
      const float diffuse = block_render_clamp(normal_x * sun_x + normal_y * sun_y + normal_z * sun_z, 0.0f, 1.0f);
      const float sky_lift = 0.10f + block_render_clamp((normal_y * 0.5f + 0.5f), 0.0f, 1.0f) * 0.12f;
      const float bounce_lift = 0.04f + block_render_clamp(sun_y, -0.1f, 1.0f) * 0.06f;
      float light = 0.44f + diffuse * 0.26f + sky_lift + bounce_lift;
      float face_r = 0.0f;
      float face_g = 0.0f;
      float face_b = 0.0f;

      if (normal_y > 0.5f)
      {
        light += 0.08f;
      }
      else if (normal_y < -0.5f)
      {
        light *= 0.80f;
      }

      face_r = block_render_mix(base_r * light, fog_r, fog_strength);
      face_g = block_render_mix(base_g * light, fog_g, fog_strength);
      face_b = block_render_mix(base_b * light, fog_b, fog_strength);
      glColor3f(face_r, face_g, face_b);
      block_render_emit_face(face_index, (float)cell->x, (float)cell->y, (float)cell->z);
    }
  }
  glEnd();

  if (target != NULL && target->valid != 0)
  {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(2.0f);
    if (target->kind == BLOCK_RAYCAST_BLOCK)
    {
      block_render_draw_wire_cube((float)target->block_x, (float)target->block_y, (float)target->block_z, 1.0f, 0.30f, 0.24f);
      if (target->place_x != target->block_x || target->place_y != target->block_y || target->place_z != target->block_z)
      {
        block_render_draw_wire_cube((float)target->place_x, (float)target->place_y, (float)target->place_z, 0.30f, 0.90f, 0.42f);
      }
    }
    else
    {
      block_render_draw_wire_cube((float)target->place_x, (float)target->place_y, (float)target->place_z, 0.30f, 0.90f, 0.42f);
    }
    glDisable(GL_BLEND);
    glLineWidth(1.0f);
  }

  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
}

static float block_render_clamp(float value, float min_value, float max_value)
{
  if (value < min_value)
  {
    return min_value;
  }
  if (value > max_value)
  {
    return max_value;
  }

  return value;
}

static float block_render_mix(float a, float b, float t)
{
  return a + (b - a) * t;
}

static void block_render_emit_face(int face_index, float x, float y, float z)
{
  switch (face_index)
  {
    case 0:
      glVertex3f(x + 0.0f, y + 0.0f, z + 0.0f);
      glVertex3f(x + 1.0f, y + 0.0f, z + 0.0f);
      glVertex3f(x + 1.0f, y + 1.0f, z + 0.0f);
      glVertex3f(x + 0.0f, y + 1.0f, z + 0.0f);
      break;

    case 1:
      glVertex3f(x + 1.0f, y + 0.0f, z + 1.0f);
      glVertex3f(x + 0.0f, y + 0.0f, z + 1.0f);
      glVertex3f(x + 0.0f, y + 1.0f, z + 1.0f);
      glVertex3f(x + 1.0f, y + 1.0f, z + 1.0f);
      break;

    case 2:
      glVertex3f(x + 0.0f, y + 0.0f, z + 1.0f);
      glVertex3f(x + 0.0f, y + 0.0f, z + 0.0f);
      glVertex3f(x + 0.0f, y + 1.0f, z + 0.0f);
      glVertex3f(x + 0.0f, y + 1.0f, z + 1.0f);
      break;

    case 3:
      glVertex3f(x + 1.0f, y + 0.0f, z + 0.0f);
      glVertex3f(x + 1.0f, y + 0.0f, z + 1.0f);
      glVertex3f(x + 1.0f, y + 1.0f, z + 1.0f);
      glVertex3f(x + 1.0f, y + 1.0f, z + 0.0f);
      break;

    case 4:
      glVertex3f(x + 0.0f, y + 1.0f, z + 0.0f);
      glVertex3f(x + 1.0f, y + 1.0f, z + 0.0f);
      glVertex3f(x + 1.0f, y + 1.0f, z + 1.0f);
      glVertex3f(x + 0.0f, y + 1.0f, z + 1.0f);
      break;

    case 5:
    default:
      glVertex3f(x + 0.0f, y + 0.0f, z + 1.0f);
      glVertex3f(x + 1.0f, y + 0.0f, z + 1.0f);
      glVertex3f(x + 1.0f, y + 0.0f, z + 0.0f);
      glVertex3f(x + 0.0f, y + 0.0f, z + 0.0f);
      break;
  }
}

static void block_render_draw_wire_cube(float x, float y, float z, float r, float g, float b)
{
  glColor4f(r, g, b, 0.95f);
  glBegin(GL_LINES);
  glVertex3f(x + 0.0f, y + 0.0f, z + 0.0f);
  glVertex3f(x + 1.0f, y + 0.0f, z + 0.0f);
  glVertex3f(x + 1.0f, y + 0.0f, z + 0.0f);
  glVertex3f(x + 1.0f, y + 0.0f, z + 1.0f);
  glVertex3f(x + 1.0f, y + 0.0f, z + 1.0f);
  glVertex3f(x + 0.0f, y + 0.0f, z + 1.0f);
  glVertex3f(x + 0.0f, y + 0.0f, z + 1.0f);
  glVertex3f(x + 0.0f, y + 0.0f, z + 0.0f);

  glVertex3f(x + 0.0f, y + 1.0f, z + 0.0f);
  glVertex3f(x + 1.0f, y + 1.0f, z + 0.0f);
  glVertex3f(x + 1.0f, y + 1.0f, z + 0.0f);
  glVertex3f(x + 1.0f, y + 1.0f, z + 1.0f);
  glVertex3f(x + 1.0f, y + 1.0f, z + 1.0f);
  glVertex3f(x + 0.0f, y + 1.0f, z + 1.0f);
  glVertex3f(x + 0.0f, y + 1.0f, z + 1.0f);
  glVertex3f(x + 0.0f, y + 1.0f, z + 0.0f);

  glVertex3f(x + 0.0f, y + 0.0f, z + 0.0f);
  glVertex3f(x + 0.0f, y + 1.0f, z + 0.0f);
  glVertex3f(x + 1.0f, y + 0.0f, z + 0.0f);
  glVertex3f(x + 1.0f, y + 1.0f, z + 0.0f);
  glVertex3f(x + 1.0f, y + 0.0f, z + 1.0f);
  glVertex3f(x + 1.0f, y + 1.0f, z + 1.0f);
  glVertex3f(x + 0.0f, y + 0.0f, z + 1.0f);
  glVertex3f(x + 0.0f, y + 1.0f, z + 1.0f);
  glEnd();
}

#endif
