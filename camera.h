#ifndef CAMERA_H
#define CAMERA_H

#include <math.h>
#include <stddef.h>

typedef struct CameraState
{
  float x;
  float y;
  float z;
  float yaw;
  float pitch;
} CameraState;

static inline float camera_clamp_pitch(float pitch)
{
  const float max_pitch = 1.55f;

  if (pitch > max_pitch)
  {
    return max_pitch;
  }
  if (pitch < -max_pitch)
  {
    return -max_pitch;
  }

  return pitch;
}

static inline void camera_get_forward_vector(const CameraState* camera, float* out_x, float* out_y, float* out_z)
{
  const float yaw = (camera != NULL) ? camera->yaw : 0.0f;
  const float pitch = (camera != NULL) ? camera->pitch : 0.0f;
  const float forward_x = -sinf(yaw) * cosf(pitch);
  const float forward_y = sinf(pitch);
  const float forward_z = -cosf(yaw) * cosf(pitch);

  if (out_x != NULL)
  {
    *out_x = forward_x;
  }
  if (out_y != NULL)
  {
    *out_y = forward_y;
  }
  if (out_z != NULL)
  {
    *out_z = forward_z;
  }
}

static inline void camera_get_flat_forward_vector(const CameraState* camera, float* out_x, float* out_z)
{
  float forward_x = 0.0f;
  float forward_y = 0.0f;
  float forward_z = -1.0f;
  float length = 0.0f;

  camera_get_forward_vector(camera, &forward_x, &forward_y, &forward_z);
  (void)forward_y;
  length = sqrtf(forward_x * forward_x + forward_z * forward_z);

  if (length > 0.0001f)
  {
    const float inverse_length = 1.0f / length;
    forward_x *= inverse_length;
    forward_z *= inverse_length;
  }
  else
  {
    forward_x = 0.0f;
    forward_z = -1.0f;
  }

  if (out_x != NULL)
  {
    *out_x = forward_x;
  }
  if (out_z != NULL)
  {
    *out_z = forward_z;
  }
}

static inline void camera_get_right_vector(const CameraState* camera, float* out_x, float* out_z)
{
  float forward_x = 0.0f;
  float forward_z = -1.0f;

  camera_get_flat_forward_vector(camera, &forward_x, &forward_z);

  if (out_x != NULL)
  {
    *out_x = -forward_z;
  }
  if (out_z != NULL)
  {
    *out_z = forward_x;
  }
}

#endif
