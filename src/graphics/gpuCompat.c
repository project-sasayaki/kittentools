#include <stdint.h>

#include "gpuCompat.h"
#include "gpuVendor.h"
#include "../config/config.h"
#include "../log/log.h"

static unsigned s_features;

void gpuCompat_setFeatures(unsigned features)
{
    s_features = features;
    log_debug(plfBoot, "Non-NVIDIA GPU workarounds for this game: 0x%02x", features);
}

bool gpuCompat_on(unsigned features)
{
    return (s_features & features) != 0 && getGPUVendorID() != NVIDIA_GPU;
}

bool gpuCompat_onAmd(unsigned features)
{
    uint8_t vendor = getGPUVendorID();
    return (s_features & features) != 0 && (vendor == AMD_GPU || vendor == ATI_GPU);
}

bool gpuCompat_onIntel(unsigned features)
{
    return (s_features & features) != 0 && getGPUVendorID() == INTEL_GPU;
}

bool gpuCompat_onKnownVendor(unsigned features)
{
    uint8_t vendor = getGPUVendorID();
    return (s_features & features) != 0 &&
           (vendor == AMD_GPU || vendor == ATI_GPU || vendor == INTEL_GPU);
}
