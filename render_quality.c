#include "render_quality.h"

#include "gpu_preferences.h"

#include <ctype.h>
#include <string.h>

static int render_quality_contains_case_insensitive(const char* text, const char* needle);
static unsigned int render_quality_query_dedicated_memory_mb(const char* renderer_name, const char* vendor_name);
static int render_quality_is_vendor_match(const char* adapter_name, const char* vendor_name);
static int render_quality_is_ultra_low_end_intel(const char* renderer_name, const char* vendor_name);

RendererQualityPreset render_quality_pick_preset(const char* renderer_name, const char* vendor_name)
{
  const unsigned int dedicated_memory_mb = render_quality_query_dedicated_memory_mb(renderer_name, vendor_name);
  const int performance_discrete_renderer =
    render_quality_contains_case_insensitive(renderer_name, "nvidia") ||
    render_quality_contains_case_insensitive(renderer_name, "geforce") ||
    render_quality_contains_case_insensitive(renderer_name, "rtx") ||
    render_quality_contains_case_insensitive(renderer_name, "gtx") ||
    render_quality_contains_case_insensitive(renderer_name, "radeon") ||
    render_quality_contains_case_insensitive(renderer_name, "rx ") ||
    render_quality_contains_case_insensitive(renderer_name, "arc");

  if (render_quality_is_ultra_low_end_intel(renderer_name, vendor_name))
  {
    return RENDER_QUALITY_PRESET_ULTRA_LOW;
  }
  if (render_quality_contains_case_insensitive(renderer_name, "intel") ||
    render_quality_contains_case_insensitive(renderer_name, "iris") ||
    render_quality_contains_case_insensitive(renderer_name, "uhd") ||
    render_quality_contains_case_insensitive(vendor_name, "intel"))
  {
    return RENDER_QUALITY_PRESET_LOW;
  }
  if (dedicated_memory_mb > 0U && dedicated_memory_mb <= 2048U)
  {
    return RENDER_QUALITY_PRESET_LOW;
  }
  if (dedicated_memory_mb > 0U && dedicated_memory_mb <= 4096U && performance_discrete_renderer == 0)
  {
    return RENDER_QUALITY_PRESET_LOW;
  }

  return RENDER_QUALITY_PRESET_HIGH;
}

RendererQualityProfile render_quality_pick(const char* renderer_name, const char* vendor_name)
{
  return render_quality_get_profile(render_quality_pick_preset(renderer_name, vendor_name), renderer_name, vendor_name);
}

RendererQualityProfile render_quality_get_profile(RendererQualityPreset preset, const char* renderer_name, const char* vendor_name)
{
  (void)renderer_name;
  (void)vendor_name;

  switch (preset)
  {
    case RENDER_QUALITY_PRESET_ULTRA_LOW:
      return (RendererQualityProfile){
        "Ultra Low",
        RENDER_QUALITY_PRESET_ULTRA_LOW,
        0.60f,
        0.24f,
        112.0f,
        512,
        161,
        0,
        0,
        0,
        0,
        0,
        5,
        0,
        0.42f,
        0.18f
      };

    case RENDER_QUALITY_PRESET_LOW:
      return (RendererQualityProfile){
        "Low",
        RENDER_QUALITY_PRESET_LOW,
        0.84f,
        0.40f,
        176.0f,
        1024,
        257,
        0,
        0,
        0,
        0,
        0,
        2,
        0,
        0.82f,
        0.68f
      };

    case RENDER_QUALITY_PRESET_HIGH:
    default:
      return (RendererQualityProfile){
        "High",
        RENDER_QUALITY_PRESET_HIGH,
        1.00f,
        0.88f,
        220.0f,
        2048,
        385,
        0,
        1,
        1,
        1,
        1,
        1,
        1,
        1.00f,
        1.00f
      };
  }
}

const char* render_quality_preset_get_label(RendererQualityPreset preset)
{
  switch (preset)
  {
    case RENDER_QUALITY_PRESET_HIGH:
      return "High";
    case RENDER_QUALITY_PRESET_LOW:
      return "Low";
    case RENDER_QUALITY_PRESET_ULTRA_LOW:
      return "Ultra Low";
    case RENDER_QUALITY_PRESET_COUNT:
    default:
      return "High";
  }
}

const char* render_quality_preset_get_description(RendererQualityPreset preset)
{
  switch (preset)
  {
    case RENDER_QUALITY_PRESET_HIGH:
      return "Maksimum visual: full resolution, bayangan besar, dan efek terberat.";
    case RENDER_QUALITY_PRESET_LOW:
      return "Balanced: cocok untuk Intel Iris Xe class iGPU, objek tetap jelas.";
    case RENDER_QUALITY_PRESET_ULTRA_LOW:
      return "Paling ringan: untuk iGPU lawas seperti Intel UHD 617.";
    case RENDER_QUALITY_PRESET_COUNT:
    default:
      return "Maksimum visual: full resolution, bayangan besar, dan efek terberat.";
  }
}

static int render_quality_is_ultra_low_end_intel(const char* renderer_name, const char* vendor_name)
{
  const int is_intel =
    render_quality_contains_case_insensitive(renderer_name, "intel") ||
    render_quality_contains_case_insensitive(renderer_name, "iris") ||
    render_quality_contains_case_insensitive(renderer_name, "uhd") ||
    render_quality_contains_case_insensitive(vendor_name, "intel");

  if (!is_intel || renderer_name == NULL)
  {
    return 0;
  }

  return render_quality_contains_case_insensitive(renderer_name, "uhd graphics 617") ||
    render_quality_contains_case_insensitive(renderer_name, "uhd 617") ||
    render_quality_contains_case_insensitive(renderer_name, "hd graphics 615") ||
    render_quality_contains_case_insensitive(renderer_name, "hd graphics 617");
}

static int render_quality_contains_case_insensitive(const char* text, const char* needle)
{
  size_t text_length = 0U;
  size_t needle_length = 0U;
  size_t i = 0U;
  size_t j = 0U;

  if (text == NULL || needle == NULL)
  {
    return 0;
  }

  text_length = strlen(text);
  needle_length = strlen(needle);
  if (needle_length == 0U || needle_length > text_length)
  {
    return 0;
  }

  for (i = 0U; i + needle_length <= text_length; ++i)
  {
    for (j = 0U; j < needle_length; ++j)
    {
      if (tolower((unsigned char)text[i + j]) != tolower((unsigned char)needle[j]))
      {
        break;
      }
    }

    if (j == needle_length)
    {
      return 1;
    }
  }

  return 0;
}

static int render_quality_is_vendor_match(const char* adapter_name, const char* vendor_name)
{
  if (adapter_name == NULL || vendor_name == NULL)
  {
    return 0;
  }

  return render_quality_contains_case_insensitive(adapter_name, vendor_name) ||
    (render_quality_contains_case_insensitive(vendor_name, "nvidia") && render_quality_contains_case_insensitive(adapter_name, "nvidia")) ||
    (render_quality_contains_case_insensitive(vendor_name, "amd") && render_quality_contains_case_insensitive(adapter_name, "radeon")) ||
    (render_quality_contains_case_insensitive(vendor_name, "ati") && render_quality_contains_case_insensitive(adapter_name, "radeon")) ||
    (render_quality_contains_case_insensitive(vendor_name, "intel") && render_quality_contains_case_insensitive(adapter_name, "intel"));
}

static unsigned int render_quality_query_dedicated_memory_mb(const char* renderer_name, const char* vendor_name)
{
  GpuPreferenceInfo gpu_info = { 0 };
  int index = 0;
  int best_score = -1;
  unsigned int best_memory_mb = 0U;

  if (!gpu_preferences_query(&gpu_info))
  {
    return 0U;
  }

  for (index = 0; index < gpu_info.adapter_count; ++index)
  {
    const GpuAdapterInfo* adapter = &gpu_info.adapters[index];
    int score = 0;

    if (adapter->name[0] == '\0')
    {
      continue;
    }

    if (renderer_name != NULL &&
      (render_quality_contains_case_insensitive(renderer_name, adapter->name) ||
        render_quality_contains_case_insensitive(adapter->name, renderer_name)))
    {
      score += 8;
    }
    if (render_quality_is_vendor_match(adapter->name, vendor_name))
    {
      score += 2;
    }
    if (adapter->is_high_performance_candidate != 0 &&
      renderer_name != NULL &&
      (render_quality_contains_case_insensitive(renderer_name, "nvidia") ||
        render_quality_contains_case_insensitive(renderer_name, "radeon") ||
        render_quality_contains_case_insensitive(renderer_name, "geforce")))
    {
      score += 1;
    }

    if (score > best_score || (score == best_score && adapter->dedicated_video_memory_mb > best_memory_mb))
    {
      best_score = score;
      best_memory_mb = adapter->dedicated_video_memory_mb;
    }
  }

  if (best_score <= 0 && gpu_info.adapter_count == 1)
  {
    return gpu_info.adapters[0].dedicated_video_memory_mb;
  }

  return best_memory_mb;
}
