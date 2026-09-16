#include "libsegaapi.h"
#include "segaapi_speaker_layout.h"

#define COBJMACROS
#define INITGUID

#include <windows.h>
#include <xaudio2.h>
#include <xaudio2fx.h>

#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_HANDLES 0x80
#define MAX_CHANNELS 2
#define MAX_SENDS 7
#define MAX_SOUND_BOARD_IO 21
#define MAX_EAX_PROPERTIES 64
#define MAX_EAX_PROPERTY_DATA 256
#define MIN_SAMPLE_RATE 8000
#define MAX_SAMPLE_RATE 48000
#define BUFFER_MAGIC 0x53454741u

#define SYNTH_START_DELAY_MS 40

#define EAXPROPERTY_DEFERRED 0x80000000UL
#define EAX_FXSLOT_EFFECT 0x00010002UL
#define EAX_FXSLOT_VOLUME 0x00010003UL

#pragma pack(push, 1)
typedef struct SegaXAudio2FxReverbParameters
{
    float WetDryMix;
    UINT32 ReflectionsDelay;
    BYTE ReverbDelay;
    BYTE RearDelay;
    BYTE SideDelay;
    BYTE PositionLeft;
    BYTE PositionRight;
    BYTE PositionMatrixLeft;
    BYTE PositionMatrixRight;
    BYTE EarlyDiffusion;
    BYTE LateDiffusion;
    BYTE LowEQGain;
    BYTE LowEQCutoff;
    BYTE HighEQGain;
    BYTE HighEQCutoff;
    float RoomFilterFreq;
    float RoomFilterMain;
    float RoomFilterHF;
    float ReflectionsGain;
    float ReverbGain;
    float DecayTime;
    float Density;
    float RoomSize;
    BOOL DisableLateField;
} SegaXAudio2FxReverbParameters;
#pragma pack(pop)

#define REVERB_SEND_TRIM 1.0f
#define REVERB_DECAY_TIME 1.49f
#define REVERB_WET_DRY_MIX 100.0f

const GUID EAXPROPERTYID_EAX40_SEGA_Custom = {
    0xa7feec3f, 0x2bfd, 0x4a40, {0x89, 0x1f, 0x74, 0x23, 0xe3, 0x8b, 0xac, 0x1f}};
GUID EAX_NULL_GUID = {0x00000000, 0x0000, 0x0000, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
GUID EAX_FREQUENCYSHIFTER_EFFECT = {
    0xdc3e1880, 0x9212, 0x11d3, {0x93, 0x9d, 0x00, 0xc0, 0xf0, 0x2d, 0xd6, 0xf0}};
GUID EAX_ECHO_EFFECT = {0x0e9f1bc0, 0xac82, 0x11d2, {0x88, 0xdd, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1}};
GUID EAX_REVERB_EFFECT = {0x0cf95c8f, 0xa3cc, 0x4849, {0xb0, 0xb6, 0x83, 0x2e, 0xcc, 0x18, 0x22, 0xdf}};
GUID EAX_EQUALIZER_EFFECT = {
    0x65f94ce0, 0x9793, 0x11d3, {0x93, 0x9d, 0x00, 0xc0, 0xf0, 0x2d, 0xd6, 0xf0}};
GUID EAX_DISTORTION_EFFECT = {
    0x975a4ce0, 0xac7e, 0x11d2, {0x88, 0xdd, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1}};
GUID EAX_AGCCOMPRESSOR_EFFECT = {
    0xbfb7a01e, 0x7825, 0x4039, {0x92, 0x7f, 0x03, 0xaa, 0xbd, 0xa0, 0xc5, 0x60}};
GUID EAX_PITCHSHIFTER_EFFECT = {
    0xe7905100, 0xafb2, 0x11d2, {0x88, 0xdd, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1}};
GUID EAX_FLANGER_EFFECT = {
    0xa70007c0, 0x07d2, 0x11d3, {0x9b, 0x1e, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1}};
GUID EAX_VOCALMORPHER_EFFECT = {
    0xe41cf10c, 0x3383, 0x11d2, {0x88, 0xdd, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1}};
GUID EAX_AUTOWAH_EFFECT = {
    0xec3130c0, 0xac7a, 0x11d2, {0x88, 0xdd, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1}};
GUID EAX_RINGMODULATOR_EFFECT = {
    0x0b89fe60, 0xafb5, 0x11d2, {0x88, 0xdd, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1}};
GUID EAX_CHORUS_EFFECT = {
    0xde6d6fe0, 0xac79, 0x11d2, {0x88, 0xdd, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1}};
GUID EAXPROPERTYID_EAX40_FXSlot0 = {
    0xc4d79f1e, 0xf1ac, 0x436b, {0xa8, 0x1d, 0xa7, 0x38, 0xe7, 0x04, 0x54, 0x69}};
GUID EAXPROPERTYID_EAX40_FXSlot1 = {
    0x08c00e96, 0x74be, 0x4491, {0x93, 0xaa, 0xe8, 0xad, 0x35, 0xa4, 0x91, 0x17}};
GUID EAXPROPERTYID_EAX40_FXSlot2 = {
    0x1d433b88, 0xf0f6, 0x4637, {0x91, 0x9f, 0x60, 0xe7, 0xe0, 0x6b, 0x5e, 0xdd}};
GUID EAXPROPERTYID_EAX40_FXSlot3 = {
    0xefff08ea, 0xc7d8, 0x44ab, {0x93, 0xad, 0x6d, 0xbd, 0x5f, 0x91, 0x00, 0x64}};
GUID EAXPROPERTYID_EAX40_Source = {
    0x1b86b823, 0x22df, 0x4eae, {0x8b, 0x3c, 0x12, 0x78, 0xce, 0x54, 0x42, 0x27}};
GUID EAXPROPERTYID_EAX40_Context = {
    0x1d4870ad, 0x0def, 0x43c0, {0xa4, 0x0c, 0x52, 0x36, 0x32, 0x29, 0x63, 0x42}};
GUID EAX_PrimaryFXSlotID = {
    0xf317866d, 0x924c, 0x450c, {0x86, 0x1b, 0xe6, 0xda, 0xa2, 0x5e, 0x7c, 0x20}};

typedef struct Buffer
{
    uint32_t magic;
    unsigned int flags;
    int ownsSourceData;
    int isSynth;

    uint8_t *sourceData;
    unsigned int sourceSize;
    uint8_t *snapshot;
    unsigned int snapshotSize;
    unsigned int snapshotBase;

    OutputFormat format;
    WAVEFORMATEX waveFormat;
    IXAudio2SourceVoice *voice;
    unsigned int voiceSampleRate;

    PlaybackStatus playbackStatus;
    unsigned int priority;
    void *userData;
    BufferCallback callback;

    unsigned int startLoop;
    unsigned int endLoop;
    unsigned int endOffset;
    unsigned int playbackPosition;
    unsigned int playBeginAbsolute;
    unsigned int notificationFrequency;
    unsigned int notificationPoint;
    int notificationPointSet;
    int continuousLooping;
    int releaseState;
    int bufferQueued;
    int streamWasUpdated;
    int streamingActive;
    unsigned int streamWriteCursor;
    int stereoPairMember;

    uint64_t samplesPlayedBase;
    float pitchRatio;
    int synthParams[MOD_ENV_TO_FILTER_CUTOFF + 1];

    Routing routing[MAX_CHANNELS][MAX_SENDS];
    unsigned int sendLevels[MAX_CHANNELS][MAX_SENDS];
    unsigned int channelVolumes[MAX_CHANNELS];

    int startPending;
    DWORD startDueTick;
} Buffer;

typedef struct EaxProperty
{
    GUID guid;
    unsigned long property;
    unsigned long size;
    uint8_t data[MAX_EAX_PROPERTY_DATA];
    int used;
} EaxProperty;

static IXAudio2 *xAudio;
static IXAudio2MasteringVoice *masteringVoice;
static IXAudio2SubmixVoice *reverbVoice;
static IUnknown *reverbEffect;
static unsigned int masteringChannels;
static unsigned int masteringSampleRate;
static DWORD masteringChannelMask;
static int reverbAvailable;
static int comInitialized;
static int segaApiInitialized;

static pthread_mutex_t handleMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t audioMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t stereoPairCondition = PTHREAD_COND_INITIALIZER;
static Buffer *activeBuffers[MAX_HANDLES];
static unsigned int activeBufferCount;
static Buffer *pendingStereoLeft;
static pthread_t stereoPairThread;
static unsigned int stereoPairOperationSet = 1;
static int stereoPairThreadStarted;
static int stereoPairThreadStopping;

static unsigned int lastStatus;
static EaxProperty eaxProperties[MAX_EAX_PROPERTIES];
static unsigned int segaCustomProperties[2];
static int fxSlotEnabled[4];
static float fxSlotGain[4];
static unsigned int ioVolumes[MAX_SOUND_BOARD_IO + 1];
static SPDIFOutputSampleRate spdifSampleRate;
static unsigned int spdifChannelStatus;
static unsigned int spdifExtChannelStatus;
static Routing spdifRouting[2];

static void STDMETHODCALLTYPE voiceProcessingPassStart(
    IXAudio2VoiceCallback *callback,
    UINT32 bytesRequired)
{
    (void)callback;
    (void)bytesRequired;
}

static void STDMETHODCALLTYPE voiceProcessingPassEnd(IXAudio2VoiceCallback *callback)
{
    (void)callback;
}

static void STDMETHODCALLTYPE voiceStreamEnd(IXAudio2VoiceCallback *callback)
{
    (void)callback;
}

static void STDMETHODCALLTYPE voiceBufferStart(
    IXAudio2VoiceCallback *callback,
    void *bufferContext)
{
    (void)callback;
    (void)bufferContext;
}

static void STDMETHODCALLTYPE voiceBufferEnd(
    IXAudio2VoiceCallback *callback,
    void *bufferContext)
{
    (void)callback;
    free(bufferContext);
}

static void STDMETHODCALLTYPE voiceLoopEnd(
    IXAudio2VoiceCallback *callback,
    void *bufferContext)
{
    (void)callback;
    (void)bufferContext;
}

static void STDMETHODCALLTYPE voiceError(
    IXAudio2VoiceCallback *callback,
    void *bufferContext,
    HRESULT error)
{
    (void)callback;
    (void)bufferContext;
    (void)error;
}

static IXAudio2VoiceCallbackVtbl voiceCallbackVtbl = {
    voiceProcessingPassStart,
    voiceProcessingPassEnd,
    voiceStreamEnd,
    voiceBufferStart,
    voiceBufferEnd,
    voiceLoopEnd,
    voiceError,
};

static IXAudio2VoiceCallback voiceCallback = {
    &voiceCallbackVtbl,
};

static void setLastStatus(int status)
{
    lastStatus = (unsigned int)status;
}

static int resultWithStatus(int status)
{
    setLastStatus(status);
    return status;
}

static int guidEquals(const GUID *left, const GUID *right)
{
    return left != NULL && right != NULL && memcmp(left, right, sizeof(GUID)) == 0;
}

static unsigned int bitsPerSample(unsigned int sampleFormat)
{
    return (sampleFormat & SIGNED_16PCM) != 0 ? 16u : 8u;
}

static unsigned int blockAlignFor(const OutputFormat *format)
{
    return format->byNumChans * (bitsPerSample(format->dwSampleFormat) / 8u);
}

static unsigned int bytesToSamples(const Buffer *buffer, unsigned int bytes)
{
    unsigned int blockAlign = blockAlignFor(&buffer->format);
    return blockAlign == 0 ? 0 : bytes / blockAlign;
}

static unsigned int samplesToBytes(const Buffer *buffer, uint64_t samples)
{
    uint64_t bytes = samples * blockAlignFor(&buffer->format);
    return bytes > UINT32_MAX ? UINT32_MAX : (unsigned int)bytes;
}

static unsigned int alignOffset(const Buffer *buffer, unsigned int offset)
{
    unsigned int blockAlign = blockAlignFor(&buffer->format);
    if (offset > buffer->sourceSize)
        offset = buffer->sourceSize;
    if (blockAlign > 1)
        offset -= offset % blockAlign;
    return offset;
}

static int validSampleFormat(unsigned int sampleFormat)
{
    return sampleFormat == UNSIGNED_8PCM || sampleFormat == SIGNED_16PCM;
}

static int validSampleRate(unsigned int sampleRate)
{
    return sampleRate >= MIN_SAMPLE_RATE && sampleRate <= MAX_SAMPLE_RATE;
}

static int validOutputFormat(const OutputFormat *format)
{
    if (format == NULL)
        return SEGA_ERROR_BAD_POINTER;
    if (!validSampleRate(format->dwSampleRate))
        return SEGA_ERROR_BAD_SAMPLERATE;
    if (!validSampleFormat(format->dwSampleFormat) || format->byNumChans == 0 || format->byNumChans > MAX_CHANNELS)
        return SEGA_ERROR_BAD_PARAM;
    return SEGA_SUCCESS;
}

static int validRouting(Routing routing)
{
    return routing == UNUSED_PORT ||
           (routing >= FRONT_LEFT_PORT && routing <= REAR_RIGHT_PORT) ||
           (routing >= FXSLOT0_PORT && routing <= FXSLOT3_PORT);
}

static int validSpdifRouting(Routing routing)
{
    return routing >= FRONT_LEFT_PORT && routing <= REAR_RIGHT_PORT;
}

static int validSoundBoardIO(SoundBoardIO io)
{
    return (io >= OUT_FRONT_LEFT && io <= OUT_REAR_RIGHT) ||
           io == OUT_OPTICAL_LEFT || io == OUT_OPTICAL_RIGHT ||
           io == IN_LINEIN_LEFT || io == IN_LINEIN_RIGHT;
}

static Buffer *lookupBuffer(void *handle)
{
    Buffer *result = NULL;

    pthread_mutex_lock(&handleMutex);
    for (unsigned int i = 0; i < activeBufferCount; i++)
    {
        if (activeBuffers[i] == handle && activeBuffers[i]->magic == BUFFER_MAGIC)
        {
            result = activeBuffers[i];
            break;
        }
    }
    pthread_mutex_unlock(&handleMutex);

    return result;
}

#define HANDLE_CHECK_BUFFER(handle, name, failure) \
    Buffer *name = lookupBuffer(handle);           \
    if (name == NULL)                              \
    {                                               \
        setLastStatus(SEGA_ERROR_BAD_HANDLE);       \
        return failure;                             \
    }

static int registerBuffer(Buffer *buffer)
{
    int result = SEGA_SUCCESS;

    pthread_mutex_lock(&handleMutex);
    if (activeBufferCount == MAX_HANDLES)
    {
        result = SEGA_ERROR_NO_RESOURCES;
    }
    else
    {
        activeBuffers[activeBufferCount++] = buffer;
    }
    pthread_mutex_unlock(&handleMutex);

    return result;
}

static void unregisterBuffer(Buffer *buffer)
{
    pthread_mutex_lock(&handleMutex);
    for (unsigned int i = 0; i < activeBufferCount; i++)
    {
        if (activeBuffers[i] == buffer)
        {
            activeBuffers[i] = activeBuffers[--activeBufferCount];
            activeBuffers[activeBufferCount] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&handleMutex);
}

static void updateWaveFormat(Buffer *buffer)
{
    unsigned int blockAlign = blockAlignFor(&buffer->format);

    memset(&buffer->waveFormat, 0, sizeof(buffer->waveFormat));
    buffer->waveFormat.wFormatTag = WAVE_FORMAT_PCM;
    buffer->waveFormat.nChannels = (WORD)buffer->format.byNumChans;
    buffer->waveFormat.nSamplesPerSec = buffer->format.dwSampleRate;
    buffer->waveFormat.nBlockAlign = (WORD)blockAlign;
    buffer->waveFormat.nAvgBytesPerSec = buffer->format.dwSampleRate * blockAlign;
    buffer->waveFormat.wBitsPerSample = (WORD)bitsPerSample(buffer->format.dwSampleFormat);
}

static float normalizedVolume(unsigned int volume)
{
    return volume / 4294967295.0f;
}

static float attenuationGain(int attenuation)
{
    float decibels = -attenuation / 10.0f;
    if (decibels <= -100.0f)
        return 0.0f;
    if (decibels >= 0.0f)
        return 1.0f;
    return powf(10.0f, decibels / 20.0f);
}

static float pitchGain(int pitch)
{
    return powf(2.0f, (pitch / 100.0f) / 12.0f);
}

static float clampCoefficient(float value)
{
    if (value < 0.0f)
        return 0.0f;
    if (value > 1.0f)
        return 1.0f;
    return value;
}

static void addPhysicalRoute(
    float *matrix,
    unsigned int sourceChannel,
    Routing destination,
    float gain)
{
    if (destination >= FRONT_LEFT_PORT && destination <= REAR_RIGHT_PORT)
    {
        matrix[sourceChannel * SEGAAPI_PHYSICAL_OUTPUT_COUNT + (unsigned int)destination] += gain;
    }
}

static int fxSlotIndexFromRouting(Routing routing)
{
    if (routing < FXSLOT0_PORT || routing > FXSLOT3_PORT)
        return -1;
    return (int)routing - (int)FXSLOT0_PORT;
}

static int fxSlotIndexFromGuid(const GUID *guid)
{
    if (guidEquals(guid, &EAXPROPERTYID_EAX40_FXSlot0))
        return 0;
    if (guidEquals(guid, &EAXPROPERTYID_EAX40_FXSlot1))
        return 1;
    if (guidEquals(guid, &EAXPROPERTYID_EAX40_FXSlot2))
        return 2;
    if (guidEquals(guid, &EAXPROPERTYID_EAX40_FXSlot3))
        return 3;
    return -1;
}

static int applyVoiceGain(Buffer *buffer)
{
    float gain = attenuationGain(buffer->synthParams[ATTENUATION]);
    HRESULT hr = IXAudio2SourceVoice_SetVolume(buffer->voice, gain, XAUDIO2_COMMIT_NOW);
    return FAILED(hr) ? SEGA_ERROR_FAIL : SEGA_SUCCESS;
}

static int applyVoicePitch(Buffer *buffer)
{
    float sampleRateRatio = buffer->voiceSampleRate == 0
                                ? 1.0f
                                : buffer->format.dwSampleRate / (float)buffer->voiceSampleRate;
    float ratio = buffer->pitchRatio * sampleRateRatio;

    if (ratio < XAUDIO2_MIN_FREQ_RATIO)
        ratio = XAUDIO2_MIN_FREQ_RATIO;
    if (ratio > XAUDIO2_MAX_FREQ_RATIO)
        ratio = XAUDIO2_MAX_FREQ_RATIO;

    HRESULT hr = IXAudio2SourceVoice_SetFrequencyRatio(buffer->voice, ratio, XAUDIO2_COMMIT_NOW);
    return FAILED(hr) ? SEGA_ERROR_FAIL : SEGA_SUCCESS;
}

static int applyRouting(Buffer *buffer)
{
    float physicalMatrix[MAX_CHANNELS * SEGAAPI_PHYSICAL_OUTPUT_COUNT] = {0};
    float wetMatrix[MAX_CHANNELS * 2] = {0};

    for (unsigned int sourceChannel = 0; sourceChannel < buffer->format.byNumChans; sourceChannel++)
    {
        float channelGain = normalizedVolume(buffer->channelVolumes[sourceChannel]);

        for (unsigned int send = 0; send < MAX_SENDS; send++)
        {
            Routing destination = buffer->routing[sourceChannel][send];
            if (destination == UNUSED_PORT)
                continue;

            float gain = normalizedVolume(buffer->sendLevels[sourceChannel][send]) * channelGain;
            if (destination >= FRONT_LEFT_PORT && destination <= REAR_RIGHT_PORT)
            {
                gain *= normalizedVolume(ioVolumes[destination]);
                addPhysicalRoute(physicalMatrix, sourceChannel, destination, gain);
                continue;
            }

            int slot = fxSlotIndexFromRouting(destination);
            if (slot < 0 || !reverbAvailable || !fxSlotEnabled[slot])
                continue;

            gain *= fxSlotGain[slot] * REVERB_SEND_TRIM;
            if (buffer->format.byNumChans == 1)
            {
                wetMatrix[0] += gain * 0.5f;
                wetMatrix[1] += gain * 0.5f;
            }
            else
            {
                wetMatrix[sourceChannel * 2 + sourceChannel] += gain;
            }
        }
    }

    for (unsigned int i = 0; i < MAX_CHANNELS * SEGAAPI_PHYSICAL_OUTPUT_COUNT; i++)
        physicalMatrix[i] = clampCoefficient(physicalMatrix[i]);
    for (unsigned int i = 0; i < MAX_CHANNELS * 2; i++)
        wetMatrix[i] = clampCoefficient(wetMatrix[i]);

    float endpointMatrix[MAX_CHANNELS * XAUDIO2_MAX_AUDIO_CHANNELS] = {0};
    segaapiComposeOutputMatrix(
        buffer->format.byNumChans,
        physicalMatrix,
        masteringChannels,
        masteringChannelMask,
        endpointMatrix);

    /*
     * Note: no center-spreading fallback for exclusively-one-sided mono
     * voices. Real hardware plays an FL-only send on the left speaker
     * only — the ID5 tunnel echo fires such hard-panned one-shots left
     * and right, and centering them doubles their energy into both ears
     * (audible as a momentary volume boost at tunnel entry).
     */

    /*
     * The routing math above uses source-major order
     * (matrix[source * destinationCount + destination]); XAudio2's
     * SetOutputMatrix expects destination-major order
     * (pLevelMatrix[SourceChannels * destination + source]).
     */
    float xAudioEndpointMatrix[MAX_CHANNELS * XAUDIO2_MAX_AUDIO_CHANNELS];
    float xAudioWetMatrix[MAX_CHANNELS * 2];
    for (unsigned int dest = 0; dest < masteringChannels; dest++)
        for (unsigned int src = 0; src < buffer->format.byNumChans; src++)
            xAudioEndpointMatrix[dest * buffer->format.byNumChans + src] =
                endpointMatrix[src * masteringChannels + dest];
    for (unsigned int dest = 0; dest < 2; dest++)
        for (unsigned int src = 0; src < buffer->format.byNumChans; src++)
            xAudioWetMatrix[dest * buffer->format.byNumChans + src] =
                wetMatrix[src * 2 + dest];

    HRESULT hr = IXAudio2SourceVoice_SetOutputMatrix(
        buffer->voice,
        (IXAudio2Voice *)masteringVoice,
        buffer->format.byNumChans,
        masteringChannels,
        xAudioEndpointMatrix,
        XAUDIO2_COMMIT_NOW);
    if (FAILED(hr))
        return SEGA_ERROR_FAIL;

    if (reverbAvailable)
    {
        hr = IXAudio2SourceVoice_SetOutputMatrix(
            buffer->voice,
            (IXAudio2Voice *)reverbVoice,
            buffer->format.byNumChans,
            2,
            xAudioWetMatrix,
            XAUDIO2_COMMIT_NOW);
        if (FAILED(hr))
            return SEGA_ERROR_FAIL;
    }

    return SEGA_SUCCESS;
}

static int createSourceVoice(Buffer *buffer)
{
    updateWaveFormat(buffer);

    XAUDIO2_SEND_DESCRIPTOR descriptors[2];
    descriptors[0].Flags = 0;
    descriptors[0].pOutputVoice = (IXAudio2Voice *)masteringVoice;

    UINT32 sendCount = 1;
    if (reverbAvailable)
    {
        descriptors[sendCount].Flags = 0;
        descriptors[sendCount].pOutputVoice = (IXAudio2Voice *)reverbVoice;
        sendCount++;
    }

    XAUDIO2_VOICE_SENDS sends;
    sends.SendCount = sendCount;
    sends.pSends = descriptors;

    HRESULT hr = IXAudio2_CreateSourceVoice(
        xAudio,
        &buffer->voice,
        &buffer->waveFormat,
        0,
        XAUDIO2_MAX_FREQ_RATIO,
        &voiceCallback,
        &sends,
        NULL);
    if (FAILED(hr))
    {
        buffer->voice = NULL;
        return SEGA_ERROR_NO_RESOURCES;
    }

    float sourceVolumes[MAX_CHANNELS] = {1.0f, 1.0f};
    hr = IXAudio2SourceVoice_SetChannelVolumes(
        buffer->voice,
        buffer->format.byNumChans,
        sourceVolumes,
        XAUDIO2_COMMIT_NOW);
    if (FAILED(hr))
    {
        IXAudio2SourceVoice_DestroyVoice(buffer->voice);
        buffer->voice = NULL;
        return SEGA_ERROR_FAIL;
    }

    buffer->voiceSampleRate = buffer->format.dwSampleRate;
    buffer->pitchRatio = pitchGain(buffer->synthParams[PITCH]);

    int result = applyVoiceGain(buffer);
    if (result == SEGA_SUCCESS)
        result = applyVoicePitch(buffer);
    if (result == SEGA_SUCCESS)
        result = applyRouting(buffer);

    if (result != SEGA_SUCCESS)
    {
        IXAudio2SourceVoice_DestroyVoice(buffer->voice);
        buffer->voice = NULL;
    }

    return result;
}

static void freeSnapshot(Buffer *buffer)
{
    free(buffer->snapshot);
    buffer->snapshot = NULL;
    buffer->snapshotSize = 0;
    buffer->snapshotBase = 0;
    buffer->bufferQueued = 0;
    buffer->streamingActive = 0;
}

static int clearVoiceQueue(Buffer *buffer)
{
    buffer->startPending = 0;
    if (pendingStereoLeft == buffer)
        pendingStereoLeft = NULL;

    if (buffer->voice == NULL)
    {
        freeSnapshot(buffer);
        return SEGA_SUCCESS;
    }

    HRESULT hr = IXAudio2SourceVoice_Stop(buffer->voice, 0, XAUDIO2_COMMIT_NOW);
    if (FAILED(hr))
        return SEGA_ERROR_FAIL;

    hr = IXAudio2SourceVoice_FlushSourceBuffers(buffer->voice);
    if (FAILED(hr))
        return SEGA_ERROR_FAIL;

    XAUDIO2_VOICE_STATE state;
    IXAudio2SourceVoice_GetState(buffer->voice, &state, XAUDIO2_VOICE_NOSAMPLESPLAYED);

    if (state.BuffersQueued != 0)
    {
        IXAudio2SourceVoice_DestroyVoice(buffer->voice);
        buffer->voice = NULL;
        freeSnapshot(buffer);
        return createSourceVoice(buffer);
    }

    freeSnapshot(buffer);
    return SEGA_SUCCESS;
}

static int submitRingLoop(Buffer *buffer, unsigned int playPosition)
{
    if (buffer->sourceSize == 0 || buffer->sourceData == NULL)
        return SEGA_SUCCESS;

    XAUDIO2_BUFFER xAudioBuffer;
    memset(&xAudioBuffer, 0, sizeof(xAudioBuffer));
    xAudioBuffer.AudioBytes = buffer->sourceSize;
    xAudioBuffer.pAudioData = buffer->sourceData;
    xAudioBuffer.PlayBegin = bytesToSamples(buffer, alignOffset(buffer, playPosition));
    xAudioBuffer.LoopCount = XAUDIO2_LOOP_INFINITE;

    HRESULT hr = IXAudio2SourceVoice_SubmitSourceBuffer(
        buffer->voice,
        &xAudioBuffer,
        NULL);
    if (FAILED(hr))
        return SEGA_ERROR_FAIL;

    buffer->bufferQueued = 1;
    return SEGA_SUCCESS;
}

static unsigned int playbackPositionFromState(Buffer *buffer, const XAUDIO2_VOICE_STATE *state)
{
    if (!buffer->bufferQueued || buffer->sourceSize == 0)
        return buffer->playbackPosition;

    uint64_t samples = state->SamplesPlayed >= buffer->samplesPlayedBase
                           ? state->SamplesPlayed - buffer->samplesPlayedBase
                           : 0;
    uint64_t position = (uint64_t)buffer->playBeginAbsolute + samplesToBytes(buffer, samples);

    unsigned int endOffset = alignOffset(buffer, buffer->endOffset);
    unsigned int startLoop = alignOffset(buffer, buffer->startLoop);
    unsigned int endLoop = alignOffset(buffer, buffer->endLoop);
    if (endOffset == 0 || endOffset > buffer->sourceSize)
        endOffset = buffer->sourceSize;
    if (endLoop <= startLoop || endLoop > endOffset)
        endLoop = endOffset;

    if (buffer->continuousLooping && endLoop > startLoop && position >= endLoop)
        position = startLoop + (position - endLoop) % (endLoop - startLoop);
    else if (position > endOffset)
        position = endOffset;

    return (unsigned int)position;
}

static int submitBuffer(Buffer *buffer)
{
    int result = clearVoiceQueue(buffer);
    if (result != SEGA_SUCCESS)
        return result;

    if (buffer->sourceSize == 0 || buffer->sourceData == NULL)
        return SEGA_SUCCESS;

    if (buffer->voiceSampleRate != buffer->format.dwSampleRate)
    {
        HRESULT hr = IXAudio2SourceVoice_SetSourceSampleRate(buffer->voice, buffer->format.dwSampleRate);
        if (FAILED(hr))
            return SEGA_ERROR_FAIL;
        buffer->voiceSampleRate = buffer->format.dwSampleRate;
    }

    unsigned int endOffset = alignOffset(buffer, buffer->endOffset);
    unsigned int startLoop = alignOffset(buffer, buffer->startLoop);
    unsigned int endLoop = alignOffset(buffer, buffer->endLoop);
    unsigned int playPosition = alignOffset(buffer, buffer->playbackPosition);

    if (endOffset == 0 || endOffset > buffer->sourceSize)
        endOffset = buffer->sourceSize;
    if (startLoop > endOffset)
        startLoop = endOffset;
    if (endLoop <= startLoop || endLoop > endOffset)
        endLoop = endOffset;
    if (playPosition >= endOffset)
        playPosition = buffer->continuousLooping && startLoop < endLoop ? startLoop : 0;

    XAUDIO2_VOICE_STATE state;
    IXAudio2SourceVoice_GetState(buffer->voice, &state, 0);

    if (buffer->continuousLooping && buffer->streamWasUpdated)
    {
        buffer->streamingActive = 1;
        buffer->samplesPlayedBase = state.SamplesPlayed;
        buffer->playBeginAbsolute = playPosition;
        buffer->playbackPosition = playPosition;

        result = submitRingLoop(buffer, playPosition);
        if (result == SEGA_SUCCESS)
            result = applyVoiceGain(buffer);
        if (result == SEGA_SUCCESS)
            result = applyVoicePitch(buffer);
        if (result == SEGA_SUCCESS)
            result = applyRouting(buffer);
        return result;
    }

    unsigned int snapshotBase = playPosition;
    if (buffer->continuousLooping && startLoop < snapshotBase)
        snapshotBase = startLoop;

    unsigned int snapshotSize = endOffset - snapshotBase;
    if (snapshotSize == 0)
        return SEGA_SUCCESS;

    uint8_t *snapshot = NULL;
    const uint8_t *audioData = buffer->sourceData + snapshotBase;

    /* Fixed loop buffers stay mapped; one-shot data is snapshotted. */
    if (!buffer->continuousLooping)
    {
        snapshot = (uint8_t *)malloc(snapshotSize);
        if (snapshot == NULL)
            return SEGA_ERROR_OUT_OF_MEMORY;
        memcpy(snapshot, audioData, snapshotSize);
        audioData = snapshot;
    }

    XAUDIO2_BUFFER xAudioBuffer;
    memset(&xAudioBuffer, 0, sizeof(xAudioBuffer));
    xAudioBuffer.AudioBytes = snapshotSize;
    xAudioBuffer.pAudioData = audioData;
    xAudioBuffer.PlayBegin = bytesToSamples(buffer, playPosition - snapshotBase);
    xAudioBuffer.PlayLength = bytesToSamples(buffer, endOffset - playPosition);

    if (buffer->continuousLooping && endLoop > startLoop)
    {
        xAudioBuffer.LoopBegin = bytesToSamples(buffer, startLoop - snapshotBase);
        xAudioBuffer.LoopLength = bytesToSamples(buffer, endLoop - startLoop);
        xAudioBuffer.LoopCount = XAUDIO2_LOOP_INFINITE;
    }
    else
    {
        xAudioBuffer.Flags = XAUDIO2_END_OF_STREAM;
    }

    HRESULT hr = IXAudio2SourceVoice_SubmitSourceBuffer(buffer->voice, &xAudioBuffer, NULL);
    if (FAILED(hr))
    {
        free(snapshot);
        return SEGA_ERROR_FAIL;
    }

    buffer->snapshot = snapshot;
    buffer->snapshotSize = snapshotSize;
    buffer->snapshotBase = snapshotBase;
    buffer->samplesPlayedBase = state.SamplesPlayed;
    buffer->playBeginAbsolute = playPosition;
    buffer->playbackPosition = playPosition;
    buffer->bufferQueued = 1;

    result = applyVoiceGain(buffer);
    if (result == SEGA_SUCCESS)
        result = applyVoicePitch(buffer);
    if (result == SEGA_SUCCESS)
        result = applyRouting(buffer);
    return result;
}

static int startFresh(Buffer *buffer)
{
    buffer->stereoPairMember = 0;
    int result = submitBuffer(buffer);
    if (result != SEGA_SUCCESS)
        return result;

    if (!buffer->bufferQueued && !buffer->streamingActive)
    {
        buffer->playbackStatus = PLAYBACK_STATUS_STOP;
        return SEGA_SUCCESS;
    }

    HRESULT hr = IXAudio2SourceVoice_Start(buffer->voice, 0, XAUDIO2_COMMIT_NOW);
    if (FAILED(hr))
        return SEGA_ERROR_FAIL;

    buffer->playbackStatus = PLAYBACK_STATUS_ACTIVE;
    return SEGA_SUCCESS;
}

static int frontSide(const Buffer *buffer)
{
    int hasLeft = 0;
    int hasRight = 0;

    for (unsigned int send = 0; send < MAX_SENDS; send++)
    {
        if (buffer->sendLevels[0][send] == 0)
            continue;

        if (buffer->routing[0][send] == FRONT_LEFT_PORT)
            hasLeft = 1;
        else if (buffer->routing[0][send] == FRONT_RIGHT_PORT)
            hasRight = 1;
    }

    if (hasLeft == hasRight)
        return 0;
    return hasLeft ? -1 : 1;
}

/* CRI represents stereo one-shots as adjacent left and right mono regions. */
static int isStereoPairLeft(const Buffer *buffer)
{
    return buffer->isSynth &&
           buffer->format.byNumChans == 1 &&
           !buffer->continuousLooping &&
           frontSide(buffer) < 0;
}

static int isMatchingStereoRight(
    const Buffer *left,
    const Buffer *right)
{
    if (left == NULL ||
        !right->isSynth ||
        right->format.byNumChans != 1 ||
        right->continuousLooping ||
        frontSide(right) <= 0 ||
        left->sourceData != right->sourceData ||
        left->sourceSize != right->sourceSize ||
        left->format.dwSampleRate != right->format.dwSampleRate ||
        left->format.dwSampleFormat != right->format.dwSampleFormat ||
        left->endOffset <= left->playbackPosition ||
        right->endOffset <= right->playbackPosition)
    {
        return 0;
    }

    unsigned int leftLength = left->endOffset - left->playbackPosition;
    unsigned int rightLength = right->endOffset - right->playbackPosition;
    /*
     * Per-channel envelopes may differ even when CRI created both handles
     * from one stereo cue. Adjacency and complementary routing identify
     * the pair without requiring identical synth parameters.
     */
    return leftLength == rightLength &&
           left->endOffset == right->playbackPosition;
}

/* Queue a submitted voice for its deferred start; audioMutex held. */
static int queueDeferredStart(Buffer *buffer, DWORD dueTick)
{
    if (!buffer->bufferQueued && !buffer->streamingActive)
    {
        buffer->playbackStatus = PLAYBACK_STATUS_STOP;
        return SEGA_SUCCESS;
    }

    buffer->startPending = 1;
    buffer->startDueTick = dueTick;
    buffer->playbackStatus = PLAYBACK_STATUS_ACTIVE;
    pthread_cond_signal(&stereoPairCondition);
    return SEGA_SUCCESS;
}

static int startSynthWithStereoPairing(Buffer *buffer)
{
    buffer->stereoPairMember = 0;

    if (!stereoPairThreadStarted)
        return startFresh(buffer);

    if (pendingStereoLeft != NULL &&
        pendingStereoLeft->startPending &&
        isMatchingStereoRight(pendingStereoLeft, buffer))
    {
        Buffer *left = pendingStereoLeft;
        pendingStereoLeft = NULL;

        left->stereoPairMember = 1;
        buffer->stereoPairMember = 1;
        int result = applyRouting(left);
        if (result != SEGA_SUCCESS)
            return result;

        result = submitBuffer(buffer);
        if (result != SEGA_SUCCESS)
            return result;
        return queueDeferredStart(buffer, left->startDueTick);
    }

    int result = submitBuffer(buffer);
    if (result != SEGA_SUCCESS)
        return result;

    result = queueDeferredStart(buffer, GetTickCount() + SYNTH_START_DELAY_MS);
    if (result != SEGA_SUCCESS)
        return result;

    if (buffer->startPending && isStereoPairLeft(buffer))
        pendingStereoLeft = buffer;
    return SEGA_SUCCESS;
}

static void *stereoPairThreadMain(void *unused)
{
    (void)unused;

    pthread_mutex_lock(&audioMutex);
    while (!stereoPairThreadStopping)
    {
        DWORD now = GetTickCount();
        DWORD nextDue = 0;
        int havePending = 0;
        int started = 0;

        UINT32 operationSet;
        do
        {
            operationSet = stereoPairOperationSet++;
        } while (operationSet == XAUDIO2_COMMIT_NOW);

        pthread_mutex_lock(&handleMutex);
        for (unsigned int i = 0; i < activeBufferCount; i++)
        {
            Buffer *buffer = activeBuffers[i];
            if (buffer == NULL || !buffer->startPending)
                continue;

            if ((int32_t)(buffer->startDueTick - now) <= 0)
            {
                buffer->startPending = 0;
                if (pendingStereoLeft == buffer)
                    pendingStereoLeft = NULL;

                if (buffer->voice != NULL &&
                    (buffer->bufferQueued || buffer->streamingActive))
                {
                    IXAudio2SourceVoice_Start(buffer->voice, 0, operationSet);
                    started = 1;
                }
                else
                {
                    buffer->playbackStatus = PLAYBACK_STATUS_STOP;
                }
            }
            else if (!havePending || (int32_t)(buffer->startDueTick - nextDue) < 0)
            {
                nextDue = buffer->startDueTick;
                havePending = 1;
            }
        }
        pthread_mutex_unlock(&handleMutex);

        if (started)
            IXAudio2_CommitChanges(xAudio, operationSet);

        if (stereoPairThreadStopping)
            break;

        if (havePending)
        {
            struct timespec deadline;
            clock_gettime(CLOCK_REALTIME, &deadline);
            int32_t delta = (int32_t)(nextDue - GetTickCount());
            if (delta < 1)
                delta = 1;
            deadline.tv_sec += delta / 1000;
            deadline.tv_nsec += (long)(delta % 1000) * 1000000L;
            if (deadline.tv_nsec >= 1000000000L)
            {
                deadline.tv_sec++;
                deadline.tv_nsec -= 1000000000L;
            }
            pthread_cond_timedwait(&stereoPairCondition, &audioMutex, &deadline);
        }
        else
        {
            pthread_cond_wait(&stereoPairCondition, &audioMutex);
        }
    }
    pthread_mutex_unlock(&audioMutex);
    return NULL;
}

static int startStereoPairThread(void)
{
    stereoPairThreadStopping = 0;
    pendingStereoLeft = NULL;
    stereoPairOperationSet = 1;

    int result = pthread_create(
        &stereoPairThread,
        NULL,
        stereoPairThreadMain,
        NULL);
    if (result != 0)
        return SEGA_ERROR_NO_RESOURCES;

    stereoPairThreadStarted = 1;
    return SEGA_SUCCESS;
}

static void stopStereoPairThread(void)
{
    if (!stereoPairThreadStarted)
        return;

    pthread_mutex_lock(&audioMutex);
    stereoPairThreadStopping = 1;
    pendingStereoLeft = NULL;
    pthread_cond_signal(&stereoPairCondition);
    pthread_mutex_unlock(&audioMutex);

    pthread_join(stereoPairThread, NULL);
    stereoPairThreadStarted = 0;
    stereoPairThreadStopping = 0;
}

static void destroyBufferResources(Buffer *buffer)
{
    if (buffer == NULL)
        return;

    pthread_mutex_lock(&audioMutex);
    if (buffer->voice != NULL)
    {
        clearVoiceQueue(buffer);
        IXAudio2SourceVoice_DestroyVoice(buffer->voice);
        buffer->voice = NULL;
    }
    freeSnapshot(buffer);
    pthread_mutex_unlock(&audioMutex);

    if (buffer->ownsSourceData)
        free(buffer->sourceData);
    buffer->sourceData = NULL;
    buffer->magic = 0;
}

static void destroyAllBuffers(void)
{
    for (;;)
    {
        pthread_mutex_lock(&handleMutex);
        if (activeBufferCount == 0)
        {
            pthread_mutex_unlock(&handleMutex);
            break;
        }

        Buffer *buffer = activeBuffers[--activeBufferCount];
        activeBuffers[activeBufferCount] = NULL;
        pthread_mutex_unlock(&handleMutex);

        destroyBufferResources(buffer);
        free(buffer);
    }
}

static void resetState(void)
{
    memset(eaxProperties, 0, sizeof(eaxProperties));
    memset(segaCustomProperties, 0, sizeof(segaCustomProperties));
    memset(ioVolumes, 0, sizeof(ioVolumes));

    fxSlotEnabled[0] = 1;
    fxSlotEnabled[1] = 1;
    fxSlotEnabled[2] = 0;
    fxSlotEnabled[3] = 0;
    for (unsigned int i = 0; i < 4; i++)
        fxSlotGain[i] = 1.0f;

    ioVolumes[OUT_FRONT_LEFT] = VOL_MAX;
    ioVolumes[OUT_FRONT_RIGHT] = VOL_MAX;
    ioVolumes[OUT_FRONT_CENTER] = VOL_MAX;
    ioVolumes[OUT_LFE_PORT] = VOL_MAX;
    ioVolumes[OUT_REAR_LEFT] = VOL_MAX;
    ioVolumes[OUT_REAR_RIGHT] = VOL_MAX;
    ioVolumes[OUT_OPTICAL_LEFT] = VOL_MAX;
    ioVolumes[OUT_OPTICAL_RIGHT] = VOL_MAX;
    ioVolumes[IN_LINEIN_LEFT] = 0x99980000u;
    ioVolumes[IN_LINEIN_RIGHT] = 0x99980000u;

    spdifSampleRate = SPDIFOUT_48KHZ;
    spdifChannelStatus = 0;
    spdifExtChannelStatus = 0;
    spdifRouting[0] = FRONT_LEFT_PORT;
    spdifRouting[1] = FRONT_RIGHT_PORT;
}

static int createReverbGraph(void)
{
    reverbAvailable = 0;
    reverbVoice = NULL;
    reverbEffect = NULL;

    HRESULT hr = CreateAudioReverb(&reverbEffect);
    if (FAILED(hr) || reverbEffect == NULL)
        return SEGA_SUCCESS;

    XAUDIO2_EFFECT_DESCRIPTOR descriptor;
    descriptor.InitialState = TRUE;
    descriptor.OutputChannels = 2;
    descriptor.pEffect = reverbEffect;

    XAUDIO2_EFFECT_CHAIN chain;
    chain.EffectCount = 1;
    chain.pEffectDescriptors = &descriptor;

    hr = IXAudio2_CreateSubmixVoice(
        xAudio,
        &reverbVoice,
        2,
        masteringSampleRate,
        0,
        0,
        NULL,
        &chain);
    if (FAILED(hr))
    {
        IUnknown_Release(reverbEffect);
        reverbEffect = NULL;
        return SEGA_SUCCESS;
    }

    /*
     * The retail FX-slot reverb defaults (EAX GENERIC, see the comment at
     * REVERB_SEND_TRIM) mapped to XAudio2FX native parameters with the
     * standard I3DL2-to-native conversion. RoomFilterMain stays 0 dB
     * because the retail mixer applies no listener Room attenuation.
     */
    SegaXAudio2FxReverbParameters reverbParams;
    memset(&reverbParams, 0, sizeof(reverbParams));
    reverbParams.WetDryMix = REVERB_WET_DRY_MIX;
    reverbParams.ReflectionsDelay = 7;
    reverbParams.ReverbDelay = 11;
    reverbParams.RearDelay = 5;
    reverbParams.SideDelay = 5;
    reverbParams.PositionLeft = 6;
    reverbParams.PositionRight = 6;
    reverbParams.PositionMatrixLeft = 27;
    reverbParams.PositionMatrixRight = 27;
    reverbParams.EarlyDiffusion = 15;
    reverbParams.LateDiffusion = 15;
    reverbParams.LowEQGain = 8;
    reverbParams.LowEQCutoff = 4;
    reverbParams.HighEQGain = 8;
    reverbParams.HighEQCutoff = 6;
    reverbParams.RoomFilterFreq = 5000.0f;
    reverbParams.RoomFilterMain = 0.0f;
    reverbParams.RoomFilterHF = -1.0f;
    reverbParams.ReflectionsGain = -26.02f;
    reverbParams.ReverbGain = 2.0f;
    reverbParams.DecayTime = REVERB_DECAY_TIME;
    reverbParams.Density = 100.0f;
    reverbParams.RoomSize = 100.0f;
    reverbParams.DisableLateField = FALSE;

    /* Non-fatal on failure: the APO's own defaults are still a usable room. */
    IXAudio2SubmixVoice_SetEffectParameters(
        reverbVoice,
        0,
        &reverbParams,
        sizeof(reverbParams),
        XAUDIO2_COMMIT_NOW);

    float physicalMatrix[SEGAAPI_PHYSICAL_OUTPUT_COUNT * XAUDIO2_MAX_AUDIO_CHANNELS];
    float matrix[2 * XAUDIO2_MAX_AUDIO_CHANNELS] = {0};
    segaapiBuildPhysicalOutputMatrix(
        masteringChannels,
        masteringChannelMask,
        physicalMatrix);
    /* Rows 0/1 of physicalMatrix are FL/FR in source-major order; XAudio2
     * wants destination-major (pLevelMatrix[SourceChannels * dest + src]). */
    for (unsigned int dest = 0; dest < masteringChannels; dest++)
        for (unsigned int src = 0; src < 2; src++)
            matrix[dest * 2 + src] = physicalMatrix[src * masteringChannels + dest];

    hr = IXAudio2SubmixVoice_SetOutputMatrix(
        reverbVoice,
        (IXAudio2Voice *)masteringVoice,
        2,
        masteringChannels,
        matrix,
        XAUDIO2_COMMIT_NOW);
    if (FAILED(hr))
    {
        IXAudio2SubmixVoice_DestroyVoice(reverbVoice);
        reverbVoice = NULL;
        IUnknown_Release(reverbEffect);
        reverbEffect = NULL;
        return SEGA_SUCCESS;
    }

    reverbAvailable = 1;
    return SEGA_SUCCESS;
}

static void refreshAllRouting(void)
{
    pthread_mutex_lock(&handleMutex);
    for (unsigned int i = 0; i < activeBufferCount; i++)
    {
        Buffer *buffer = activeBuffers[i];
        if (buffer != NULL && buffer->voice != NULL)
            applyRouting(buffer);
    }
    pthread_mutex_unlock(&handleMutex);
}

int SEGAAPI_Play(void *hHandle)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    pthread_mutex_lock(&audioMutex);
    int result = SEGA_SUCCESS;

    XAUDIO2_VOICE_STATE state;
    IXAudio2SourceVoice_GetState(buffer->voice, &state, 0);

    if (buffer->playbackStatus == PLAYBACK_STATUS_PAUSE && state.BuffersQueued != 0)
    {
        HRESULT hr = IXAudio2SourceVoice_Start(buffer->voice, 0, XAUDIO2_COMMIT_NOW);
        if (FAILED(hr))
            result = SEGA_ERROR_FAIL;
        else
            buffer->playbackStatus = PLAYBACK_STATUS_ACTIVE;
    }
    else if (buffer->playbackStatus == PLAYBACK_STATUS_ACTIVE && state.BuffersQueued != 0)
    {
        result = SEGA_SUCCESS;
    }
    else
    {
        result = startFresh(buffer);
    }

    pthread_mutex_unlock(&audioMutex);
    return resultWithStatus(result);
}

int SEGAAPI_Pause(void *hHandle)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    pthread_mutex_lock(&audioMutex);
    buffer->startPending = 0;
    HRESULT hr = IXAudio2SourceVoice_Stop(buffer->voice, 0, XAUDIO2_COMMIT_NOW);
    if (SUCCEEDED(hr))
        buffer->playbackStatus = PLAYBACK_STATUS_PAUSE;
    pthread_mutex_unlock(&audioMutex);

    return resultWithStatus(FAILED(hr) ? SEGA_ERROR_FAIL : SEGA_SUCCESS);
}

int SEGAAPI_Stop(void *hHandle)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    pthread_mutex_lock(&audioMutex);
    int result = clearVoiceQueue(buffer);
    if (result == SEGA_SUCCESS)
    {
        buffer->playbackStatus = PLAYBACK_STATUS_STOP;
        buffer->playbackPosition = 0;
        buffer->playBeginAbsolute = 0;
        buffer->streamWasUpdated = 0;
        buffer->streamWriteCursor = 0;
    }
    pthread_mutex_unlock(&audioMutex);

    return resultWithStatus(result);
}

int SEGAAPI_PlayWithSetup(
    void *hHandle,
    unsigned int dwNumSendRouteParams,
    SendRouteParamSet *pSendRouteParams,
    unsigned int dwNumSendLevelParams,
    SendLevelParamSet *pSendLevelParams,
    unsigned int dwNumVoiceParams,
    VoiceParamSet *pVoiceParams,
    unsigned int dwNumSynthParams,
    SynthParamSet *pSynthParams)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    if (!buffer->isSynth)
        return resultWithStatus(SEGA_ERROR_FAIL);
    if (buffer->format.byNumChans != 1)
        return resultWithStatus(SEGA_ERROR_INVALID_CHANNEL);
    if ((dwNumSendRouteParams != 0 && pSendRouteParams == NULL) ||
        (dwNumSendLevelParams != 0 && pSendLevelParams == NULL) ||
        (dwNumVoiceParams != 0 && pVoiceParams == NULL) ||
        (dwNumSynthParams != 0 && pSynthParams == NULL))
    {
        return resultWithStatus(SEGA_ERROR_BAD_PARAM);
    }

    pthread_mutex_lock(&audioMutex);
    int result = SEGA_SUCCESS;

    for (unsigned int i = 0; i < dwNumSendRouteParams && result == SEGA_SUCCESS; i++)
    {
        SendRouteParamSet *param = &pSendRouteParams[i];
        if (param->dwChannel >= buffer->format.byNumChans)
            result = SEGA_ERROR_INVALID_CHANNEL;
        else if (param->dwSend >= MAX_SENDS)
            result = SEGA_ERROR_INVALID_SEND;
        else if (!validRouting(param->dwDest))
            result = SEGA_ERROR_BAD_PARAM;
        else
            buffer->routing[param->dwChannel][param->dwSend] = param->dwDest;
    }

    for (unsigned int i = 0; i < dwNumSendLevelParams && result == SEGA_SUCCESS; i++)
    {
        SendLevelParamSet *param = &pSendLevelParams[i];
        if (param->dwChannel >= buffer->format.byNumChans)
            result = SEGA_ERROR_INVALID_CHANNEL;
        else if (param->dwSend >= MAX_SENDS)
            result = SEGA_ERROR_INVALID_SEND;
        else
            buffer->sendLevels[param->dwChannel][param->dwSend] = param->dwLevel;
    }

    int seekRequested = 0;
    for (unsigned int i = 0; i < dwNumVoiceParams && result == SEGA_SUCCESS; i++)
    {
        VoiceParamSet *param = &pVoiceParams[i];
        switch (param->VoiceIoctl)
        {
            case SET_START_LOOP_OFFSET:
                buffer->startLoop = param->dwParam1;
                break;
            case SET_END_LOOP_OFFSET:
                buffer->endLoop = param->dwParam1;
                break;
            case SET_END_OFFSET:
                buffer->endOffset = param->dwParam1;
                break;
            case SET_PLAY_POSITION:
                buffer->playbackPosition = param->dwParam1;
                seekRequested = 1;
                break;
            case SET_LOOP_STATE:
                buffer->continuousLooping = param->dwParam1 != 0;
                break;
            case SET_NOTIFICATION_POINT:
                buffer->notificationPoint = param->dwParam1;
                buffer->notificationPointSet = 1;
                break;
            case CLEAR_NOTIFICATION_POINT:
                if (!buffer->notificationPointSet || buffer->notificationPoint == param->dwParam1)
                    buffer->notificationPointSet = 0;
                break;
            case SET_NOTIFICATION_FREQUENCY:
                buffer->notificationFrequency = param->dwParam1;
                break;
            default:
                result = SEGA_ERROR_BAD_PARAM;
                break;
        }
    }

    for (unsigned int i = 0; i < dwNumSynthParams && result == SEGA_SUCCESS; i++)
    {
        SynthParamSet *param = &pSynthParams[i];
        if (param->param < ATTENUATION || param->param > MOD_ENV_TO_FILTER_CUTOFF)
        {
            result = SEGA_ERROR_BAD_PARAM;
            break;
        }

        buffer->synthParams[param->param] = param->lPARWValue;
        if (param->param == PITCH)
            buffer->pitchRatio = pitchGain(param->lPARWValue);
    }

    if (result == SEGA_SUCCESS)
    {
        result = startSynthWithStereoPairing(buffer);
    }

    pthread_mutex_unlock(&audioMutex);
    return resultWithStatus(result);
}

PlaybackStatus SEGAAPI_GetPlaybackStatus(void *hHandle)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, PLAYBACK_STATUS_INVALID);

    pthread_mutex_lock(&audioMutex);
    XAUDIO2_VOICE_STATE state;
    IXAudio2SourceVoice_GetState(buffer->voice, &state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    if (buffer->playbackStatus == PLAYBACK_STATUS_ACTIVE &&
        state.BuffersQueued == 0 &&
        !buffer->streamingActive)
    {
        buffer->playbackStatus = PLAYBACK_STATUS_STOP;
        buffer->bufferQueued = 0;
    }
    PlaybackStatus status = buffer->playbackStatus;
    pthread_mutex_unlock(&audioMutex);

    setLastStatus(SEGA_SUCCESS);
    return status;
}

int SEGAAPI_SetFormat(void *hHandle, OutputFormat *pFormat)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    int result = validOutputFormat(pFormat);
    if (result != SEGA_SUCCESS)
        return resultWithStatus(result);
    if (buffer->isSynth && pFormat->byNumChans != 1)
        return resultWithStatus(SEGA_ERROR_BAD_PARAM);
    if (SEGAAPI_GetPlaybackStatus(hHandle) != PLAYBACK_STATUS_STOP)
        return resultWithStatus(SEGA_ERROR_PLAYING);

    pthread_mutex_lock(&audioMutex);
    result = clearVoiceQueue(buffer);
    if (result == SEGA_SUCCESS)
    {
        IXAudio2SourceVoice_DestroyVoice(buffer->voice);
        buffer->voice = NULL;
        buffer->format = *pFormat;
        result = createSourceVoice(buffer);
    }
    pthread_mutex_unlock(&audioMutex);

    return resultWithStatus(result);
}

int SEGAAPI_GetFormat(void *hHandle, OutputFormat *pFormat)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);
    if (pFormat == NULL)
        return resultWithStatus(SEGA_ERROR_BAD_POINTER);

    *pFormat = buffer->format;
    return resultWithStatus(SEGA_SUCCESS);
}

int SEGAAPI_SetSampleRate(void *hHandle, unsigned int dwSampleRate)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);
    if (!validSampleRate(dwSampleRate))
        return resultWithStatus(SEGA_ERROR_BAD_SAMPLERATE);

    pthread_mutex_lock(&audioMutex);
    buffer->format.dwSampleRate = dwSampleRate;
    int result = applyVoicePitch(buffer);
    pthread_mutex_unlock(&audioMutex);

    return resultWithStatus(result);
}

unsigned int SEGAAPI_GetSampleRate(void *hHandle)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, 0);
    setLastStatus(SEGA_SUCCESS);
    return buffer->format.dwSampleRate;
}

int SEGAAPI_SetPriority(void *hHandle, unsigned int dwPriority)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);
    buffer->priority = dwPriority;
    return resultWithStatus(SEGA_SUCCESS);
}

unsigned int SEGAAPI_GetPriority(void *hHandle)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, 0);
    setLastStatus(SEGA_SUCCESS);
    return buffer->priority;
}

int SEGAAPI_SetUserData(void *hHandle, void *hUserData)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);
    buffer->userData = hUserData;
    return resultWithStatus(SEGA_SUCCESS);
}

void *SEGAAPI_GetUserData(void *hHandle)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, NULL);
    setLastStatus(SEGA_SUCCESS);
    return buffer->userData;
}

int SEGAAPI_SetSendRouting(void *hHandle, unsigned int dwChannel, unsigned int dwSend, Routing dwDest)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);
    if (dwChannel >= buffer->format.byNumChans)
        return resultWithStatus(SEGA_ERROR_INVALID_CHANNEL);
    if (dwSend >= MAX_SENDS)
        return resultWithStatus(SEGA_ERROR_INVALID_SEND);
    if (!validRouting(dwDest))
        return resultWithStatus(SEGA_ERROR_BAD_PARAM);

    pthread_mutex_lock(&audioMutex);
    buffer->routing[dwChannel][dwSend] = dwDest;
    int result = applyRouting(buffer);
    pthread_mutex_unlock(&audioMutex);
    return resultWithStatus(result);
}

Routing SEGAAPI_GetSendRouting(void *hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, UNUSED_PORT);
    if (dwChannel >= buffer->format.byNumChans)
    {
        setLastStatus(SEGA_ERROR_INVALID_CHANNEL);
        return UNUSED_PORT;
    }
    if (dwSend >= MAX_SENDS)
    {
        setLastStatus(SEGA_ERROR_INVALID_SEND);
        return UNUSED_PORT;
    }

    setLastStatus(SEGA_SUCCESS);
    return buffer->routing[dwChannel][dwSend];
}

int SEGAAPI_SetSendLevel(void *hHandle, unsigned int dwChannel, unsigned int dwSend, unsigned int dwLevel)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);
    if (dwChannel >= buffer->format.byNumChans)
        return resultWithStatus(SEGA_ERROR_INVALID_CHANNEL);
    if (dwSend >= MAX_SENDS)
        return resultWithStatus(SEGA_ERROR_INVALID_SEND);

    pthread_mutex_lock(&audioMutex);
    buffer->sendLevels[dwChannel][dwSend] = dwLevel;
    int result = applyRouting(buffer);
    pthread_mutex_unlock(&audioMutex);
    return resultWithStatus(result);
}

unsigned int SEGAAPI_GetSendLevel(void *hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, 0);
    if (dwChannel >= buffer->format.byNumChans)
    {
        setLastStatus(SEGA_ERROR_INVALID_CHANNEL);
        return 0;
    }
    if (dwSend >= MAX_SENDS)
    {
        setLastStatus(SEGA_ERROR_INVALID_SEND);
        return 0;
    }

    setLastStatus(SEGA_SUCCESS);
    return buffer->sendLevels[dwChannel][dwSend];
}

int SEGAAPI_SetChannelVolume(void *hHandle, unsigned int dwChannel, unsigned int dwVolume)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);
    if (dwChannel >= buffer->format.byNumChans)
        return resultWithStatus(SEGA_ERROR_INVALID_CHANNEL);

    pthread_mutex_lock(&audioMutex);
    buffer->channelVolumes[dwChannel] = dwVolume;
    int result = applyRouting(buffer);
    pthread_mutex_unlock(&audioMutex);
    return resultWithStatus(result);
}

unsigned int SEGAAPI_GetChannelVolume(void *hHandle, unsigned int dwChannel)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, 0);
    if (dwChannel >= buffer->format.byNumChans)
    {
        setLastStatus(SEGA_ERROR_INVALID_CHANNEL);
        return 0;
    }

    setLastStatus(SEGA_SUCCESS);
    return buffer->channelVolumes[dwChannel];
}

int SEGAAPI_SetPlaybackPosition(void *hHandle, unsigned int dwPlaybackPos)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    pthread_mutex_lock(&audioMutex);
    buffer->playbackPosition = alignOffset(buffer, dwPlaybackPos);
    int result = SEGA_SUCCESS;
    if (buffer->playbackStatus == PLAYBACK_STATUS_ACTIVE)
        result = startFresh(buffer);
    else if (buffer->playbackStatus == PLAYBACK_STATUS_PAUSE)
        result = submitBuffer(buffer);
    pthread_mutex_unlock(&audioMutex);

    return resultWithStatus(result);
}

unsigned int SEGAAPI_GetPlaybackPosition(void *hHandle)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, 0);

    pthread_mutex_lock(&audioMutex);
    XAUDIO2_VOICE_STATE state;
    IXAudio2SourceVoice_GetState(buffer->voice, &state, 0);
    unsigned int position = playbackPositionFromState(buffer, &state);
    pthread_mutex_unlock(&audioMutex);

    setLastStatus(SEGA_SUCCESS);
    return position;
}

int SEGAAPI_SetNotificationFrequency(void *hHandle, unsigned int dwFrameCount)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);
    buffer->notificationFrequency = dwFrameCount;
    return resultWithStatus(SEGA_SUCCESS);
}

int SEGAAPI_SetNotificationPoint(void *hHandle, unsigned int dwBufferOffset)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);
    buffer->notificationPoint = dwBufferOffset;
    buffer->notificationPointSet = 1;
    return resultWithStatus(SEGA_SUCCESS);
}

int SEGAAPI_ClearNotificationPoint(void *hHandle, unsigned int dwBufferOffset)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);
    if (!buffer->notificationPointSet || buffer->notificationPoint == dwBufferOffset)
        buffer->notificationPointSet = 0;
    return resultWithStatus(SEGA_SUCCESS);
}

int SEGAAPI_SetStartLoopOffset(void *hHandle, unsigned int dwOffset)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);
    buffer->startLoop = dwOffset;
    return resultWithStatus(SEGA_SUCCESS);
}

unsigned int SEGAAPI_GetStartLoopOffset(void *hHandle)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, 0);
    setLastStatus(SEGA_SUCCESS);
    return buffer->startLoop;
}

int SEGAAPI_SetEndLoopOffset(void *hHandle, unsigned int dwOffset)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);
    buffer->endLoop = dwOffset;
    return resultWithStatus(SEGA_SUCCESS);
}

unsigned int SEGAAPI_GetEndLoopOffset(void *hHandle)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, 0);
    setLastStatus(SEGA_SUCCESS);
    return buffer->endLoop;
}

int SEGAAPI_SetEndOffset(void *hHandle, unsigned int dwOffset)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);
    buffer->endOffset = dwOffset;
    return resultWithStatus(SEGA_SUCCESS);
}

unsigned int SEGAAPI_GetEndOffset(void *hHandle)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, 0);
    setLastStatus(SEGA_SUCCESS);
    return buffer->endOffset;
}

int SEGAAPI_SetLoopState(void *hHandle, int bDoContinuousLooping)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);
    buffer->continuousLooping = bDoContinuousLooping != 0;
    return resultWithStatus(SEGA_SUCCESS);
}

int SEGAAPI_GetLoopState(void *hHandle)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, 0);
    setLastStatus(SEGA_SUCCESS);
    return buffer->continuousLooping;
}

int SEGAAPI_UpdateBuffer(void *hHandle, unsigned int dwStartOffset, unsigned int dwLength)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    pthread_mutex_lock(&audioMutex);

    int result = SEGA_SUCCESS;
    unsigned int blockAlign = blockAlignFor(&buffer->format);
    unsigned int startOffset = alignOffset(buffer, dwStartOffset);
    if (startOffset == buffer->sourceSize && buffer->sourceSize != 0)
        startOffset = 0;
    if (dwLength > buffer->sourceSize)
        dwLength = buffer->sourceSize;
    if (blockAlign > 1)
        dwLength -= dwLength % blockAlign;

    if (dwLength != 0 && buffer->sourceSize != 0)
    {
        buffer->streamWasUpdated = 1;
        buffer->streamWriteCursor = (startOffset + dwLength) % buffer->sourceSize;

        if (buffer->continuousLooping && buffer->streamingActive)
        {
            /*
             * The infinite-loop voice reads the shared ring live; the
             * game's write is already visible to the mixer, so partial
             * updates only need the bookkeeping above.
             */
        }
        else if (buffer->continuousLooping &&
                 (buffer->playbackStatus == PLAYBACK_STATUS_ACTIVE ||
                  buffer->playbackStatus == PLAYBACK_STATUS_PAUSE))
        {
            PlaybackStatus previousStatus = buffer->playbackStatus;
            XAUDIO2_VOICE_STATE state;
            IXAudio2SourceVoice_GetState(buffer->voice, &state, 0);
            buffer->playbackPosition = playbackPositionFromState(buffer, &state);
            result = submitBuffer(buffer);
            if (result == SEGA_SUCCESS && previousStatus == PLAYBACK_STATUS_ACTIVE)
            {
                HRESULT hr = IXAudio2SourceVoice_Start(
                    buffer->voice,
                    0,
                    XAUDIO2_COMMIT_NOW);
                if (FAILED(hr))
                    result = SEGA_ERROR_FAIL;
            }
            buffer->playbackStatus = previousStatus;
        }
    }

    pthread_mutex_unlock(&audioMutex);

    return resultWithStatus(result);
}

int SEGAAPI_SetSynthParam(void *hHandle, SynthParams param, int lPARWValue)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);
    if (!buffer->isSynth)
        return resultWithStatus(SEGA_ERROR_FAIL);
    if (buffer->format.byNumChans != 1)
        return resultWithStatus(SEGA_ERROR_INVALID_CHANNEL);
    if (param < ATTENUATION || param > MOD_ENV_TO_FILTER_CUTOFF)
        return resultWithStatus(SEGA_ERROR_BAD_PARAM);

    pthread_mutex_lock(&audioMutex);
    buffer->synthParams[param] = lPARWValue;
    int result = SEGA_SUCCESS;
    if (param == ATTENUATION)
        result = applyVoiceGain(buffer);
    else if (param == PITCH)
    {
        buffer->pitchRatio = pitchGain(lPARWValue);
        result = applyVoicePitch(buffer);
    }
    pthread_mutex_unlock(&audioMutex);

    return resultWithStatus(result);
}

int SEGAAPI_GetSynthParam(void *hHandle, SynthParams param)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, -1);
    if (!buffer->isSynth)
    {
        setLastStatus(SEGA_ERROR_FAIL);
        return -1;
    }
    if (buffer->format.byNumChans != 1)
    {
        setLastStatus(SEGA_ERROR_INVALID_CHANNEL);
        return -1;
    }
    if (param < ATTENUATION || param > MOD_ENV_TO_FILTER_CUTOFF)
    {
        setLastStatus(SEGA_ERROR_BAD_PARAM);
        return -1;
    }

    setLastStatus(SEGA_SUCCESS);
    return buffer->synthParams[param];
}

int SEGAAPI_SetSynthParamMultiple(void *hHandle, unsigned int dwNumParams, SynthParamSet *pSynthParams)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);
    if (!buffer->isSynth)
        return resultWithStatus(SEGA_ERROR_FAIL);
    if (buffer->format.byNumChans != 1)
        return resultWithStatus(SEGA_ERROR_INVALID_CHANNEL);
    if (dwNumParams != 0 && pSynthParams == NULL)
        return resultWithStatus(SEGA_ERROR_BAD_PARAM);

    pthread_mutex_lock(&audioMutex);
    int result = SEGA_SUCCESS;
    int updateGain = 0;
    int updatePitch = 0;

    for (unsigned int i = 0; i < dwNumParams; i++)
    {
        SynthParams param = pSynthParams[i].param;
        if (param < ATTENUATION || param > MOD_ENV_TO_FILTER_CUTOFF)
        {
            result = SEGA_ERROR_BAD_PARAM;
            break;
        }
        buffer->synthParams[param] = pSynthParams[i].lPARWValue;
        updateGain |= param == ATTENUATION;
        updatePitch |= param == PITCH;
    }

    if (result == SEGA_SUCCESS && updateGain)
        result = applyVoiceGain(buffer);
    if (result == SEGA_SUCCESS && updatePitch)
    {
        buffer->pitchRatio = pitchGain(buffer->synthParams[PITCH]);
        result = applyVoicePitch(buffer);
    }
    pthread_mutex_unlock(&audioMutex);

    return resultWithStatus(result);
}

int SEGAAPI_GetSynthParamMultiple(void *hHandle, unsigned int dwNumParams, SynthParamSet *pSynthParams)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);
    if (!buffer->isSynth)
        return resultWithStatus(SEGA_ERROR_FAIL);
    if (buffer->format.byNumChans != 1)
        return resultWithStatus(SEGA_ERROR_INVALID_CHANNEL);
    if (dwNumParams != 0 && pSynthParams == NULL)
        return resultWithStatus(SEGA_ERROR_BAD_PARAM);

    for (unsigned int i = 0; i < dwNumParams; i++)
    {
        SynthParams param = pSynthParams[i].param;
        if (param < ATTENUATION || param > MOD_ENV_TO_FILTER_CUTOFF)
            return resultWithStatus(SEGA_ERROR_BAD_PARAM);
        pSynthParams[i].lPARWValue = buffer->synthParams[param];
    }

    return resultWithStatus(SEGA_SUCCESS);
}

int SEGAAPI_SetReleaseState(void *hHandle, int bSet)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);
    if (!buffer->isSynth)
        return resultWithStatus(SEGA_ERROR_FAIL);
    if (buffer->format.byNumChans != 1)
        return resultWithStatus(SEGA_ERROR_INVALID_CHANNEL);

    buffer->releaseState = bSet != 0;

    /*
     * Real hardware enters the volume-envelope release phase here. The
     * games configure RELEASE_VOL_ENV as -12000 timecents (about 1 ms),
     * i.e. release means "stop now" — most importantly for looping SEs
     * (e.g. the ID5 tunnel ambience loops), which otherwise keep sounding
     * until their handle is reused and stack up audibly.
     */
    if (bSet)
    {
        pthread_mutex_lock(&audioMutex);
        /*
         * Lightweight stop: no voice recreation here (that costs an
         * engine-lock destroy/create per release and hurts frame time).
         * Residual queued buffers are handled by clearVoiceQueue on the
         * next trigger of this handle.
         */
        buffer->startPending = 0;
        if (pendingStereoLeft == buffer)
            pendingStereoLeft = NULL;
        if (buffer->voice != NULL)
        {
            IXAudio2SourceVoice_Stop(buffer->voice, 0, XAUDIO2_COMMIT_NOW);
            IXAudio2SourceVoice_FlushSourceBuffers(buffer->voice);
        }
        freeSnapshot(buffer);
        buffer->playbackStatus = PLAYBACK_STATUS_STOP;
        buffer->playbackPosition = 0;
        buffer->playBeginAbsolute = 0;
        buffer->streamWasUpdated = 0;
        buffer->streamWriteCursor = 0;
        pthread_mutex_unlock(&audioMutex);
        return resultWithStatus(SEGA_SUCCESS);
    }

    return resultWithStatus(SEGA_SUCCESS);
}

int SEGAAPI_CreateBuffer(BufferConfig *pConfig, BufferCallback pCallback, unsigned int dwFlags, void **phHandle)
{
    if (pConfig == NULL || phHandle == NULL)
        return resultWithStatus(SEGA_ERROR_BAD_POINTER);
    *phHandle = NULL;

    if (!segaApiInitialized || xAudio == NULL)
        return resultWithStatus(SEGA_ERROR_INIT_FAILED);
    if (pConfig->mapData.dwSize < pConfig->mapData.dwOffset)
        return resultWithStatus(SEGA_ERROR_BAD_CONFIG);
    if ((dwFlags & ALLOC_USER_MEM) != 0 && (dwFlags & USE_MAPPED_MEM) != 0)
        return resultWithStatus(SEGA_ERROR_BAD_PARAM);

    OutputFormat format;
    format.dwSampleRate = pConfig->dwSampleRate;
    format.dwSampleFormat = pConfig->dwSampleFormat;
    format.byNumChans = pConfig->byNumChans;

    int result = validOutputFormat(&format);
    if (result != SEGA_SUCCESS)
        return resultWithStatus(result);
    if ((dwFlags & SYNTH_BUFFER) != 0 && format.byNumChans != 1)
        return resultWithStatus(SEGA_ERROR_BAD_PARAM);

    Buffer *buffer = (Buffer *)calloc(1, sizeof(Buffer));
    if (buffer == NULL)
        return resultWithStatus(SEGA_ERROR_OUT_OF_MEMORY);

    buffer->magic = BUFFER_MAGIC;
    buffer->flags = dwFlags;
    buffer->isSynth = (dwFlags & SYNTH_BUFFER) != 0;
    buffer->format = format;
    buffer->priority = pConfig->dwPriority;
    buffer->userData = pConfig->hUserData;
    buffer->callback = pCallback;
    buffer->sourceSize = pConfig->mapData.dwSize;
    buffer->playbackStatus = PLAYBACK_STATUS_STOP;
    buffer->pitchRatio = 1.0f;

    if ((dwFlags & (ALLOC_USER_MEM | USE_MAPPED_MEM)) != 0)
    {
        buffer->sourceData = (uint8_t *)pConfig->mapData.hBufferHdr;
    }
    else if (buffer->sourceSize != 0)
    {
        buffer->sourceData = (uint8_t *)calloc(1, buffer->sourceSize);
        buffer->ownsSourceData = 1;
    }

    if (buffer->sourceSize != 0 && buffer->sourceData == NULL)
    {
        destroyBufferResources(buffer);
        free(buffer);
        return resultWithStatus(SEGA_ERROR_BAD_CONFIG);
    }

    pConfig->mapData.dwOffset = 0;
    pConfig->mapData.hBufferHdr = buffer->sourceData;

    buffer->startLoop = 0;
    buffer->endLoop = buffer->sourceSize;
    buffer->endOffset = buffer->sourceSize;
    buffer->playbackPosition = 0;

    for (unsigned int channel = 0; channel < MAX_CHANNELS; channel++)
    {
        buffer->channelVolumes[channel] = VOL_MAX;
        for (unsigned int send = 0; send < MAX_SENDS; send++)
        {
            buffer->routing[channel][send] = UNUSED_PORT;
            buffer->sendLevels[channel][send] = 0;
        }
    }

    if (format.byNumChans == 1)
    {
        buffer->routing[0][0] = FRONT_LEFT_PORT;
        buffer->routing[0][1] = FRONT_RIGHT_PORT;
    }
    else
    {
        buffer->routing[0][0] = FRONT_LEFT_PORT;
        buffer->routing[1][1] = FRONT_RIGHT_PORT;
    }

    pthread_mutex_lock(&audioMutex);
    result = createSourceVoice(buffer);
    pthread_mutex_unlock(&audioMutex);
    if (result != SEGA_SUCCESS)
    {
        destroyBufferResources(buffer);
        free(buffer);
        return resultWithStatus(result);
    }

    result = registerBuffer(buffer);
    if (result != SEGA_SUCCESS)
    {
        destroyBufferResources(buffer);
        free(buffer);
        return resultWithStatus(result);
    }

    *phHandle = buffer;
    return resultWithStatus(SEGA_SUCCESS);
}

int SEGAAPI_DestroyBuffer(void *hHandle)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);
    unregisterBuffer(buffer);
    destroyBufferResources(buffer);
    free(buffer);
    return resultWithStatus(SEGA_SUCCESS);
}

static EaxProperty *findEaxProperty(const GUID *guid, unsigned long property)
{
    for (unsigned int i = 0; i < MAX_EAX_PROPERTIES; i++)
    {
        if (eaxProperties[i].used &&
            eaxProperties[i].property == property &&
            guidEquals(&eaxProperties[i].guid, guid))
        {
            return &eaxProperties[i];
        }
    }
    return NULL;
}

static int storeEaxProperty(const GUID *guid, unsigned long property, const void *data, unsigned long size)
{
    if (size > MAX_EAX_PROPERTY_DATA)
        return SEGA_ERROR_BAD_PARAM;

    EaxProperty *entry = findEaxProperty(guid, property);
    if (entry == NULL)
    {
        for (unsigned int i = 0; i < MAX_EAX_PROPERTIES; i++)
        {
            if (!eaxProperties[i].used)
            {
                entry = &eaxProperties[i];
                break;
            }
        }
    }
    if (entry == NULL)
        return SEGA_ERROR_NO_RESOURCES;

    memset(entry, 0, sizeof(*entry));
    entry->used = 1;
    entry->guid = *guid;
    entry->property = property;
    entry->size = size;
    if (size != 0)
        memcpy(entry->data, data, size);
    return SEGA_SUCCESS;
}

int SEGAAPI_SetGlobalEAXProperty(GUID *guid, unsigned long ulProperty, void *pData, unsigned long ulDataSize)
{
    if (!segaApiInitialized)
    {
        setLastStatus(SEGA_ERROR_INIT_FAILED);
        return 0;
    }
    if (guid == NULL || (pData == NULL && ulDataSize != 0))
    {
        setLastStatus(SEGA_ERROR_BAD_PARAM);
        return 0;
    }

    if (guidEquals(guid, &EAXPROPERTYID_EAX40_SEGA_Custom))
    {
        if (ulProperty > 1 || pData == NULL || ulDataSize < sizeof(unsigned int))
        {
            setLastStatus(SEGA_ERROR_BAD_PARAM);
            return 0;
        }
        segaCustomProperties[ulProperty] = *(unsigned int *)pData;
    }

    int result = storeEaxProperty(guid, ulProperty, pData, ulDataSize);
    if (result != SEGA_SUCCESS)
    {
        setLastStatus(result);
        return 0;
    }

    int slot = fxSlotIndexFromGuid(guid);
    if (slot >= 0)
    {
        unsigned long property = ulProperty & ~EAXPROPERTY_DEFERRED;
        if (property == EAX_FXSLOT_EFFECT && pData != NULL && ulDataSize >= sizeof(GUID))
        {
            fxSlotEnabled[slot] = !guidEquals((GUID *)pData, &EAX_NULL_GUID);
        }
        else if (property == EAX_FXSLOT_VOLUME && pData != NULL && ulDataSize >= sizeof(int32_t))
        {
            int32_t millibels = *(int32_t *)pData;
            if (millibels >= 0)
                fxSlotGain[slot] = 1.0f;
            else if (millibels <= -10000)
                fxSlotGain[slot] = 0.0f;
            else
                fxSlotGain[slot] = powf(10.0f, millibels / 2000.0f);
        }
        refreshAllRouting();
    }

    setLastStatus(SEGA_SUCCESS);
    return 1;
}

int SEGAAPI_GetGlobalEAXProperty(GUID *guid, unsigned long ulProperty, void *pData, unsigned long ulDataSize)
{
    if (!segaApiInitialized)
    {
        setLastStatus(SEGA_ERROR_INIT_FAILED);
        return 0;
    }
    if (guid == NULL || (pData == NULL && ulDataSize != 0))
    {
        setLastStatus(SEGA_ERROR_BAD_PARAM);
        return 0;
    }

    if (guidEquals(guid, &EAXPROPERTYID_EAX40_SEGA_Custom))
    {
        if (ulProperty > 1 || pData == NULL || ulDataSize < sizeof(unsigned int))
        {
            setLastStatus(SEGA_ERROR_BAD_PARAM);
            return 0;
        }
        memset(pData, 0, ulDataSize);
        *(unsigned int *)pData = segaCustomProperties[ulProperty];
    }
    else
    {
        EaxProperty *entry = findEaxProperty(guid, ulProperty);
        if (pData != NULL && ulDataSize != 0)
        {
            memset(pData, 0, ulDataSize);
            if (entry != NULL)
            {
                unsigned long copySize = entry->size < ulDataSize ? entry->size : ulDataSize;
                memcpy(pData, entry->data, copySize);
            }
        }
    }

    setLastStatus(SEGA_SUCCESS);
    return 1;
}

int SEGAAPI_SetSPDIFOutChannelStatus(unsigned int dwChannelStatus, unsigned int dwExtChannelStatus)
{
    if (!segaApiInitialized)
        return resultWithStatus(SEGA_ERROR_INIT_FAILED);
    spdifChannelStatus = dwChannelStatus;
    spdifExtChannelStatus = dwExtChannelStatus;
    return resultWithStatus(SEGA_SUCCESS);
}

int SEGAAPI_GetSPDIFOutChannelStatus(unsigned int *pdwChannelStatus, unsigned int *pdwExtChannelStatus)
{
    if (!segaApiInitialized)
        return resultWithStatus(SEGA_ERROR_INIT_FAILED);
    if (pdwChannelStatus == NULL || pdwExtChannelStatus == NULL)
        return resultWithStatus(SEGA_ERROR_BAD_PARAM);

    *pdwChannelStatus = spdifChannelStatus;
    *pdwExtChannelStatus = spdifExtChannelStatus;
    return resultWithStatus(SEGA_SUCCESS);
}

int SEGAAPI_SetSPDIFOutSampleRate(SPDIFOutputSampleRate dwSamplingRate)
{
    if (!segaApiInitialized)
        return resultWithStatus(SEGA_ERROR_INIT_FAILED);
    if (dwSamplingRate < SPDIFOUT_44_1KHZ || dwSamplingRate > SPDIFOUT_96KHZ)
        return resultWithStatus(SEGA_ERROR_BAD_PARAM);

    spdifSampleRate = dwSamplingRate;
    return resultWithStatus(SEGA_SUCCESS);
}

SPDIFOutputSampleRate SEGAAPI_GetSPDIFOutSampleRate(void)
{
    if (!segaApiInitialized)
    {
        setLastStatus(SEGA_ERROR_INIT_FAILED);
        return SPDIFOUT_48KHZ;
    }
    setLastStatus(SEGA_SUCCESS);
    return spdifSampleRate;
}

int SEGAAPI_SetSPDIFOutChannelRouting(unsigned int dwChannel, Routing dwSource)
{
    if (!segaApiInitialized)
        return resultWithStatus(SEGA_ERROR_INIT_FAILED);
    if (dwChannel > 1 || !validSpdifRouting(dwSource))
        return resultWithStatus(SEGA_ERROR_BAD_PARAM);

    spdifRouting[dwChannel] = dwSource;
    return resultWithStatus(SEGA_SUCCESS);
}

Routing SEGAAPI_GetSPDIFOutChannelRouting(unsigned int dwChannel)
{
    if (!segaApiInitialized)
    {
        setLastStatus(SEGA_ERROR_INIT_FAILED);
        return UNUSED_PORT;
    }
    if (dwChannel > 1)
    {
        setLastStatus(SEGA_ERROR_BAD_PARAM);
        return UNUSED_PORT;
    }

    setLastStatus(SEGA_SUCCESS);
    return spdifRouting[dwChannel];
}

int SEGAAPI_SetIOVolume(SoundBoardIO dwPhysIO, unsigned int dwVolume)
{
    if (!segaApiInitialized)
        return resultWithStatus(SEGA_ERROR_INIT_FAILED);
    if (!validSoundBoardIO(dwPhysIO))
        return resultWithStatus(SEGA_ERROR_BAD_PARAM);

    ioVolumes[dwPhysIO] = dwVolume;
    if (dwPhysIO >= OUT_FRONT_LEFT && dwPhysIO <= OUT_REAR_RIGHT)
        refreshAllRouting();
    return resultWithStatus(SEGA_SUCCESS);
}

unsigned int SEGAAPI_GetIOVolume(SoundBoardIO dwPhysIO)
{
    if (!segaApiInitialized)
    {
        setLastStatus(SEGA_ERROR_INIT_FAILED);
        return VOL_MAX;
    }
    if (!validSoundBoardIO(dwPhysIO))
    {
        setLastStatus(SEGA_ERROR_BAD_PARAM);
        return VOL_MAX;
    }

    setLastStatus(SEGA_SUCCESS);
    return ioVolumes[dwPhysIO];
}

void SEGAAPI_SetLastStatus(int LastStatus)
{
    setLastStatus(LastStatus);
}

int SEGAAPI_GetLastStatus(void)
{
    return (int)lastStatus;
}

int SEGAAPI_Reset(void)
{
    if (!segaApiInitialized)
        return resultWithStatus(SEGA_ERROR_INIT_FAILED);

    destroyAllBuffers();
    resetState();
    return resultWithStatus(SEGA_SUCCESS);
}

int SEGAAPI_Init(void)
{
    if (segaApiInitialized)
        return resultWithStatus(SEGA_SUCCESS);

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr))
        comInitialized = 1;

    hr = XAudio2Create(&xAudio, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr))
    {
        xAudio = NULL;
        if (comInitialized)
        {
            CoUninitialize();
            comInitialized = 0;
        }
        return resultWithStatus(SEGA_ERROR_INIT_FAILED);
    }

    hr = IXAudio2_CreateMasteringVoice(
        xAudio,
        &masteringVoice,
        XAUDIO2_DEFAULT_CHANNELS,
        XAUDIO2_DEFAULT_SAMPLERATE,
        0,
        NULL,
        NULL,
        AudioCategory_GameEffects);
    if (FAILED(hr))
    {
        IXAudio2_Release(xAudio);
        xAudio = NULL;
        if (comInitialized)
        {
            CoUninitialize();
            comInitialized = 0;
        }
        return resultWithStatus(SEGA_ERROR_INIT_FAILED);
    }

    XAUDIO2_VOICE_DETAILS details;
    IXAudio2MasteringVoice_GetVoiceDetails(masteringVoice, &details);
    masteringChannels = details.InputChannels;
    masteringSampleRate = details.InputSampleRate;
    if (masteringChannels == 0 || masteringChannels > XAUDIO2_MAX_AUDIO_CHANNELS)
    {
        IXAudio2MasteringVoice_DestroyVoice(masteringVoice);
        masteringVoice = NULL;
        IXAudio2_Release(xAudio);
        xAudio = NULL;
        if (comInitialized)
        {
            CoUninitialize();
            comInitialized = 0;
        }
        return resultWithStatus(SEGA_ERROR_INIT_FAILED);
    }

    masteringChannelMask = 0;
    hr = IXAudio2MasteringVoice_GetChannelMask(masteringVoice, &masteringChannelMask);
    if (FAILED(hr))
        masteringChannelMask = segaapiDefaultSpeakerMask(masteringChannels);

    createReverbGraph();
    resetState();
    startStereoPairThread();
    segaApiInitialized = 1;
    return resultWithStatus(SEGA_SUCCESS);
}

int SEGAAPI_Exit(void)
{
    if (!segaApiInitialized)
        return resultWithStatus(SEGA_SUCCESS);

    stopStereoPairThread();
    destroyAllBuffers();

    pthread_mutex_lock(&audioMutex);
    if (reverbVoice != NULL)
    {
        IXAudio2SubmixVoice_DestroyVoice(reverbVoice);
        reverbVoice = NULL;
    }
    if (reverbEffect != NULL)
    {
        IUnknown_Release(reverbEffect);
        reverbEffect = NULL;
    }
    reverbAvailable = 0;

    if (masteringVoice != NULL)
    {
        IXAudio2MasteringVoice_DestroyVoice(masteringVoice);
        masteringVoice = NULL;
    }
    if (xAudio != NULL)
    {
        IXAudio2_Release(xAudio);
        xAudio = NULL;
    }
    pthread_mutex_unlock(&audioMutex);

    masteringChannels = 0;
    masteringSampleRate = 0;
    masteringChannelMask = 0;
    segaApiInitialized = 0;

    if (comInitialized)
    {
        CoUninitialize();
        comInitialized = 0;
    }

    return resultWithStatus(SEGA_SUCCESS);
}
