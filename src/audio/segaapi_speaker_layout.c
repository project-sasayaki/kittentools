#include "segaapi_speaker_layout.h"

#include <string.h>

static unsigned int countBits(uint32_t value)
{
    unsigned int count = 0;
    while (value != 0)
    {
        count += value & 1u;
        value >>= 1;
    }
    return count;
}

uint32_t segaapiDefaultSpeakerMask(unsigned int channelCount)
{
    switch (channelCount)
    {
        case 1:
            return SEGAAPI_SPEAKER_FRONT_CENTER;
        case 2:
            return SEGAAPI_SPEAKER_FRONT_LEFT | SEGAAPI_SPEAKER_FRONT_RIGHT;
        case 3:
            return SEGAAPI_SPEAKER_FRONT_LEFT | SEGAAPI_SPEAKER_FRONT_RIGHT |
                   SEGAAPI_SPEAKER_LOW_FREQUENCY;
        case 4:
            return SEGAAPI_SPEAKER_FRONT_LEFT | SEGAAPI_SPEAKER_FRONT_RIGHT |
                   SEGAAPI_SPEAKER_BACK_LEFT | SEGAAPI_SPEAKER_BACK_RIGHT;
        case 5:
            return SEGAAPI_SPEAKER_FRONT_LEFT | SEGAAPI_SPEAKER_FRONT_RIGHT |
                   SEGAAPI_SPEAKER_FRONT_CENTER | SEGAAPI_SPEAKER_BACK_LEFT |
                   SEGAAPI_SPEAKER_BACK_RIGHT;
        case 6:
            return SEGAAPI_SPEAKER_FRONT_LEFT | SEGAAPI_SPEAKER_FRONT_RIGHT |
                   SEGAAPI_SPEAKER_FRONT_CENTER | SEGAAPI_SPEAKER_LOW_FREQUENCY |
                   SEGAAPI_SPEAKER_BACK_LEFT | SEGAAPI_SPEAKER_BACK_RIGHT;
        case 7:
            return SEGAAPI_SPEAKER_FRONT_LEFT | SEGAAPI_SPEAKER_FRONT_RIGHT |
                   SEGAAPI_SPEAKER_FRONT_CENTER | SEGAAPI_SPEAKER_LOW_FREQUENCY |
                   SEGAAPI_SPEAKER_BACK_CENTER | SEGAAPI_SPEAKER_SIDE_LEFT |
                   SEGAAPI_SPEAKER_SIDE_RIGHT;
        case 8:
            return SEGAAPI_SPEAKER_FRONT_LEFT | SEGAAPI_SPEAKER_FRONT_RIGHT |
                   SEGAAPI_SPEAKER_FRONT_CENTER | SEGAAPI_SPEAKER_LOW_FREQUENCY |
                   SEGAAPI_SPEAKER_BACK_LEFT | SEGAAPI_SPEAKER_BACK_RIGHT |
                   SEGAAPI_SPEAKER_SIDE_LEFT | SEGAAPI_SPEAKER_SIDE_RIGHT;
        default:
            return 0;
    }
}

static int speakerIndex(uint32_t mask, uint32_t speaker)
{
    if ((mask & speaker) == 0)
        return -1;

    unsigned int index = 0;
    for (uint32_t bit = 1; bit < speaker; bit <<= 1)
    {
        if ((mask & bit) != 0)
            index++;
    }
    return (int)index;
}

static int addSpeaker(
    float *matrix,
    unsigned int endpointChannels,
    unsigned int sourceChannel,
    uint32_t endpointMask,
    uint32_t speaker,
    float gain)
{
    int destination = speakerIndex(endpointMask, speaker);
    if (destination < 0 || (unsigned int)destination >= endpointChannels)
        return 0;

    matrix[sourceChannel * endpointChannels + (unsigned int)destination] += gain;
    return 1;
}

static void routePhysicalChannel(
    float *matrix,
    unsigned int endpointChannels,
    unsigned int sourceChannel,
    uint32_t endpointMask)
{
    static const uint32_t directSpeakers[SEGAAPI_PHYSICAL_OUTPUT_COUNT] = {
        SEGAAPI_SPEAKER_FRONT_LEFT,
        SEGAAPI_SPEAKER_FRONT_RIGHT,
        SEGAAPI_SPEAKER_FRONT_CENTER,
        SEGAAPI_SPEAKER_LOW_FREQUENCY,
        SEGAAPI_SPEAKER_BACK_LEFT,
        SEGAAPI_SPEAKER_BACK_RIGHT,
    };

    /*
     * Ports map only to their exact speaker, like the arcade cabinet's
     * amplifier wiring: the standard single cabinet connects the front
     * left/right outputs only, so audio the game sends to the rear,
     * center or LFE ports is inaudible there. Folding those ports into
     * the front speakers made surround-heavy content (e.g. the ID5
     * tunnel ambience layers) audibly louder than real hardware.
     *
     * The one concession is a mono endpoint, where the front pair is
     * folded down so the game remains audible at all.
     */
    if (addSpeaker(
            matrix,
            endpointChannels,
            sourceChannel,
            endpointMask,
            directSpeakers[sourceChannel],
            1.0f))
    {
        return;
    }

    if ((sourceChannel == 0 || sourceChannel == 1) &&
        (endpointMask & (SEGAAPI_SPEAKER_FRONT_LEFT | SEGAAPI_SPEAKER_FRONT_RIGHT)) == 0)
    {
        if (addSpeaker(
                matrix,
                endpointChannels,
                sourceChannel,
                endpointMask,
                SEGAAPI_SPEAKER_FRONT_CENTER,
                0.70710678f))
        {
            return;
        }
        if (endpointChannels != 0)
            matrix[sourceChannel * endpointChannels] = 0.70710678f;
    }
}

void segaapiBuildPhysicalOutputMatrix(
    unsigned int endpointChannels,
    uint32_t endpointMask,
    float *matrix)
{
    if (matrix == NULL || endpointChannels == 0)
        return;

    memset(
        matrix,
        0,
        sizeof(float) * SEGAAPI_PHYSICAL_OUTPUT_COUNT * endpointChannels);

    if (countBits(endpointMask) != endpointChannels)
        endpointMask = segaapiDefaultSpeakerMask(endpointChannels);

    for (unsigned int source = 0; source < SEGAAPI_PHYSICAL_OUTPUT_COUNT; source++)
        routePhysicalChannel(matrix, endpointChannels, source, endpointMask);
}

void segaapiComposeOutputMatrix(
    unsigned int sourceChannels,
    const float *physicalRouting,
    unsigned int endpointChannels,
    uint32_t endpointMask,
    float *endpointRouting)
{
    if (sourceChannels == 0 || physicalRouting == NULL ||
        endpointChannels == 0 || endpointRouting == NULL)
    {
        return;
    }

    float physicalOutput[SEGAAPI_PHYSICAL_OUTPUT_COUNT * endpointChannels];
    segaapiBuildPhysicalOutputMatrix(
        endpointChannels,
        endpointMask,
        physicalOutput);
    memset(
        endpointRouting,
        0,
        sizeof(float) * sourceChannels * endpointChannels);

    for (unsigned int source = 0; source < sourceChannels; source++)
    {
        for (unsigned int destination = 0; destination < endpointChannels; destination++)
        {
            for (unsigned int physical = 0;
                 physical < SEGAAPI_PHYSICAL_OUTPUT_COUNT;
                 physical++)
            {
                endpointRouting[source * endpointChannels + destination] +=
                    physicalRouting[
                        source * SEGAAPI_PHYSICAL_OUTPUT_COUNT + physical] *
                    physicalOutput[physical * endpointChannels + destination];
            }
        }
    }
}

void segaapiCenterExclusiveFrontMonoRoute(
    const float *physicalRouting,
    unsigned int endpointChannels,
    uint32_t endpointMask,
    float *endpointRouting)
{
    if (physicalRouting == NULL || endpointRouting == NULL ||
        endpointChannels != 2 ||
        endpointMask !=
            (SEGAAPI_SPEAKER_FRONT_LEFT | SEGAAPI_SPEAKER_FRONT_RIGHT))
    {
        return;
    }

    float left = physicalRouting[0];
    float right = physicalRouting[1];
    for (unsigned int output = 2;
         output < SEGAAPI_PHYSICAL_OUTPUT_COUNT;
         output++)
    {
        if (physicalRouting[output] != 0.0f)
            return;
    }

    if ((left != 0.0f) == (right != 0.0f))
        return;

    float gain = left != 0.0f ? left : right;
    endpointRouting[0] = gain;
    endpointRouting[1] = gain;
}
