#ifndef SEGAAPI_SPEAKER_LAYOUT_H
#define SEGAAPI_SPEAKER_LAYOUT_H

#include <stdint.h>

#define SEGAAPI_PHYSICAL_OUTPUT_COUNT 6

#define SEGAAPI_SPEAKER_FRONT_LEFT 0x00000001u
#define SEGAAPI_SPEAKER_FRONT_RIGHT 0x00000002u
#define SEGAAPI_SPEAKER_FRONT_CENTER 0x00000004u
#define SEGAAPI_SPEAKER_LOW_FREQUENCY 0x00000008u
#define SEGAAPI_SPEAKER_BACK_LEFT 0x00000010u
#define SEGAAPI_SPEAKER_BACK_RIGHT 0x00000020u
#define SEGAAPI_SPEAKER_BACK_CENTER 0x00000100u
#define SEGAAPI_SPEAKER_SIDE_LEFT 0x00000200u
#define SEGAAPI_SPEAKER_SIDE_RIGHT 0x00000400u

uint32_t segaapiDefaultSpeakerMask(unsigned int channelCount);
void segaapiBuildPhysicalOutputMatrix(
    unsigned int endpointChannels,
    uint32_t endpointMask,
    float *matrix);
void segaapiComposeOutputMatrix(
    unsigned int sourceChannels,
    const float *physicalRouting,
    unsigned int endpointChannels,
    uint32_t endpointMask,
    float *endpointRouting);
void segaapiCenterExclusiveFrontMonoRoute(
    const float *physicalRouting,
    unsigned int endpointChannels,
    uint32_t endpointMask,
    float *endpointRouting);

#endif
