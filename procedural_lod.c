#include "procedural_lod.h"

#include <math.h>
#include <stddef.h>

static float procedural_lod_clamp(float value, float min_value, float max_value);
static float procedural_lod_mix(float a, float b, float t);
static int procedural_lod_round_to_int(float value);
static int procedural_lod_clamp_int(int value, int min_value, int max_value);

ProceduralLodState procedural_lod_resolve(const RendererQualityProfile* quality, const ProceduralLodConfig* config)
{
  static const float k_procedural_lod_pi = 3.14159265f;
  const float render_scale = (quality != NULL) ? quality->render_scale : 1.0f;
  ProceduralLodState state = { 0 };
  float requested_radius = 0.0f;
  float source_vertex_count = 0.0f;
  float vertex_budget = 0.0f;

  if (config == NULL)
  {
    return state;
  }

  state.quality_factor = procedural_lod_clamp((render_scale - 0.68f) / 0.32f, 0.0f, 1.0f);
  requested_radius = procedural_lod_clamp(
    config->requested_radius,
    config->requested_radius_min,
    config->requested_radius_max);
  state.effective_radius = procedural_lod_clamp(
    requested_radius * procedural_lod_mix(config->radius_scale_low, config->radius_scale_high, state.quality_factor),
    config->effective_radius_min,
    config->effective_radius_max);

  state.requested_instance_count = procedural_lod_round_to_int(procedural_lod_clamp(
    config->requested_instance_count,
    config->requested_instance_count_min,
    config->requested_instance_count_max));

  source_vertex_count = (config->source_vertex_count > 0.5f)
    ? config->source_vertex_count
    : procedural_lod_clamp(config->fallback_vertex_count, 1.0f, 10000000.0f);
  vertex_budget = procedural_lod_mix(config->vertex_budget_low, config->vertex_budget_high, state.quality_factor);

  state.instance_budget = procedural_lod_clamp_int(
    (int)(vertex_budget / source_vertex_count),
    config->instance_budget_min,
    config->instance_budget_max);

  state.effective_instance_count = (state.requested_instance_count > 0)
    ? ((state.requested_instance_count < state.instance_budget) ? state.requested_instance_count : state.instance_budget)
    : 0;

  state.cell_size = (state.effective_instance_count > 0)
    ? procedural_lod_clamp(
      sqrtf((k_procedural_lod_pi * state.effective_radius * state.effective_radius) / (float)state.effective_instance_count),
      config->cell_size_min,
      config->cell_size_max)
    : 0.0f;

  return state;
}

static float procedural_lod_clamp(float value, float min_value, float max_value)
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

static float procedural_lod_mix(float a, float b, float t)
{
  return a + (b - a) * t;
}

static int procedural_lod_round_to_int(float value)
{
  return (value >= 0.0f) ? (int)(value + 0.5f) : (int)(value - 0.5f);
}

static int procedural_lod_clamp_int(int value, int min_value, int max_value)
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
