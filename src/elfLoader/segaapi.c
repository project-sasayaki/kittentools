#include "segaapi.h"
#include "symbol.h"
#include "../audio/libsegaapi.h"
#include "../log/log.h"

void segaapiBridge_initBridges(void)
{
    log_debug(plfBridges, "Initializing Sega API Bridges...");

    symbol_register("SEGAAPI_Play", SEGAAPI_Play, NULL);
    symbol_register("SEGAAPI_Pause", SEGAAPI_Pause, NULL);
    symbol_register("SEGAAPI_Stop", SEGAAPI_Stop, NULL);
    symbol_register("SEGAAPI_PlayWithSetup", SEGAAPI_PlayWithSetup, NULL);
    symbol_register("SEGAAPI_GetPlaybackStatus", SEGAAPI_GetPlaybackStatus, NULL);
    symbol_register("SEGAAPI_SetFormat", SEGAAPI_SetFormat, NULL);
    symbol_register("SEGAAPI_GetFormat", SEGAAPI_GetFormat, NULL);
    symbol_register("SEGAAPI_SetSampleRate", SEGAAPI_SetSampleRate, NULL);
    symbol_register("SEGAAPI_GetSampleRate", SEGAAPI_GetSampleRate, NULL);
    symbol_register("SEGAAPI_SetPriority", SEGAAPI_SetPriority, NULL);
    symbol_register("SEGAAPI_GetPriority", SEGAAPI_GetPriority, NULL);
    symbol_register("SEGAAPI_SetUserData", SEGAAPI_SetUserData, NULL);
    symbol_register("SEGAAPI_GetUserData", SEGAAPI_GetUserData, NULL);
    symbol_register("SEGAAPI_SetSendRouting", SEGAAPI_SetSendRouting, NULL);
    symbol_register("SEGAAPI_GetSendRouting", SEGAAPI_GetSendRouting, NULL);
    symbol_register("SEGAAPI_SetSendLevel", SEGAAPI_SetSendLevel, NULL);
    symbol_register("SEGAAPI_GetSendLevel", SEGAAPI_GetSendLevel, NULL);
    symbol_register("SEGAAPI_SetChannelVolume", SEGAAPI_SetChannelVolume, NULL);
    symbol_register("SEGAAPI_GetChannelVolume", SEGAAPI_GetChannelVolume, NULL);   
    symbol_register("SEGAAPI_SetPlaybackPosition", SEGAAPI_SetPlaybackPosition, NULL);
    symbol_register("SEGAAPI_GetPlaybackPosition", SEGAAPI_GetPlaybackPosition, NULL);
    symbol_register("SEGAAPI_SetNotificationFrequency", SEGAAPI_SetNotificationFrequency, NULL);
    symbol_register("SEGAAPI_SetNotificationPoint", SEGAAPI_SetNotificationPoint, NULL);
    symbol_register("SEGAAPI_ClearNotificationPoint", SEGAAPI_ClearNotificationPoint, NULL);
    symbol_register("SEGAAPI_SetStartLoopOffset", SEGAAPI_SetStartLoopOffset, NULL);
    symbol_register("SEGAAPI_GetStartLoopOffset", SEGAAPI_GetStartLoopOffset, NULL);
    symbol_register("SEGAAPI_SetEndLoopOffset", SEGAAPI_SetEndLoopOffset, NULL);
    symbol_register("SEGAAPI_GetEndLoopOffset", SEGAAPI_GetEndLoopOffset, NULL);
    symbol_register("SEGAAPI_SetEndOffset", SEGAAPI_SetEndOffset, NULL);
    symbol_register("SEGAAPI_GetEndOffset", SEGAAPI_GetEndOffset, NULL);
    symbol_register("SEGAAPI_SetLoopState", SEGAAPI_SetLoopState, NULL);
    symbol_register("SEGAAPI_GetLoopState", SEGAAPI_GetLoopState, NULL);
    symbol_register("SEGAAPI_UpdateBuffer", SEGAAPI_UpdateBuffer, NULL);
    symbol_register("SEGAAPI_SetSynthParam", SEGAAPI_SetSynthParam, NULL);
    symbol_register("SEGAAPI_GetSynthParam", SEGAAPI_GetSynthParam, NULL);
    symbol_register("SEGAAPI_SetSynthParamMultiple", SEGAAPI_SetSynthParamMultiple, NULL);
    symbol_register("SEGAAPI_GetSynthParamMultiple", SEGAAPI_GetSynthParamMultiple, NULL);
    symbol_register("SEGAAPI_SetReleaseState", SEGAAPI_SetReleaseState, NULL);
    symbol_register("SEGAAPI_CreateBuffer", SEGAAPI_CreateBuffer, NULL);
    symbol_register("SEGAAPI_DestroyBuffer", SEGAAPI_DestroyBuffer, NULL);
    symbol_register("SEGAAPI_SetGlobalEAXProperty", SEGAAPI_SetGlobalEAXProperty, NULL);
    symbol_register("SEGAAPI_GetGlobalEAXProperty", SEGAAPI_GetGlobalEAXProperty, NULL);
    symbol_register("SEGAAPI_SetSPDIFOutChannelStatus", SEGAAPI_SetSPDIFOutChannelStatus, NULL);
    symbol_register("SEGAAPI_GetSPDIFOutChannelStatus", SEGAAPI_GetSPDIFOutChannelStatus, NULL);
    symbol_register("SEGAAPI_SetSPDIFOutSampleRate", SEGAAPI_SetSPDIFOutSampleRate, NULL);
    symbol_register("SEGAAPI_GetSPDIFOutSampleRate", SEGAAPI_GetSPDIFOutSampleRate, NULL);
    symbol_register("SEGAAPI_SetSPDIFOutChannelRouting", SEGAAPI_SetSPDIFOutChannelRouting, NULL);
    symbol_register("SEGAAPI_GetSPDIFOutChannelRouting", SEGAAPI_GetSPDIFOutChannelRouting, NULL);
    symbol_register("SEGAAPI_SetIOVolume", SEGAAPI_SetIOVolume, NULL);
    symbol_register("SEGAAPI_GetIOVolume", SEGAAPI_GetIOVolume, NULL);
    symbol_register("SEGAAPI_SetLastStatus", SEGAAPI_SetLastStatus, NULL);
    symbol_register("SEGAAPI_GetLastStatus", SEGAAPI_GetLastStatus, NULL);
    symbol_register("SEGAAPI_Reset", SEGAAPI_Reset, NULL);
    symbol_register("SEGAAPI_Init", SEGAAPI_Init, NULL);
    symbol_register("SEGAAPI_Exit", SEGAAPI_Exit, NULL);

    symbol_register("EAXPROPERTYID_EAX40_SEGA_Custom", (void *)&EAXPROPERTYID_EAX40_SEGA_Custom, NULL);
    symbol_register("EAX_NULL_GUID", (void *)&EAX_NULL_GUID, NULL);
    symbol_register("EAX_FREQUENCYSHIFTER_EFFECT", (void *)&EAX_FREQUENCYSHIFTER_EFFECT, NULL);
    symbol_register("EAX_ECHO_EFFECT", (void *)&EAX_ECHO_EFFECT, NULL);
    symbol_register("EAX_REVERB_EFFECT", (void *)&EAX_REVERB_EFFECT, NULL);
    symbol_register("EAX_EQUALIZER_EFFECT", (void *)&EAX_EQUALIZER_EFFECT, NULL);
    symbol_register("EAX_DISTORTION_EFFECT", (void *)&EAX_DISTORTION_EFFECT, NULL);
    symbol_register("EAX_AGCCOMPRESSOR_EFFECT", (void *)&EAX_AGCCOMPRESSOR_EFFECT, NULL);
    symbol_register("EAX_PITCHSHIFTER_EFFECT", (void *)&EAX_PITCHSHIFTER_EFFECT, NULL);
    symbol_register("EAX_FLANGER_EFFECT", (void *)&EAX_FLANGER_EFFECT, NULL);
    symbol_register("EAX_VOCALMORPHER_EFFECT", (void *)&EAX_VOCALMORPHER_EFFECT, NULL);
    symbol_register("EAX_AUTOWAH_EFFECT", (void *)&EAX_AUTOWAH_EFFECT, NULL);
    symbol_register("EAX_RINGMODULATOR_EFFECT", (void *)&EAX_RINGMODULATOR_EFFECT, NULL);
    symbol_register("EAX_CHORUS_EFFECT", (void *)&EAX_CHORUS_EFFECT, NULL);
    symbol_register("EAXPROPERTYID_EAX40_FXSlot0", (void *)&EAXPROPERTYID_EAX40_FXSlot0, NULL);
    symbol_register("EAXPROPERTYID_EAX40_FXSlot1", (void *)&EAXPROPERTYID_EAX40_FXSlot1, NULL);
    symbol_register("EAXPROPERTYID_EAX40_FXSlot2", (void *)&EAXPROPERTYID_EAX40_FXSlot2, NULL);
    symbol_register("EAXPROPERTYID_EAX40_FXSlot3", (void *)&EAXPROPERTYID_EAX40_FXSlot3, NULL);
    symbol_register("EAXPROPERTYID_EAX40_Source", (void *)&EAXPROPERTYID_EAX40_Source, NULL);
    symbol_register("EAXPROPERTYID_EAX40_Context", (void *)&EAXPROPERTYID_EAX40_Context, NULL);
    symbol_register("EAX_PrimaryFXSlotID", (void *)&EAX_PrimaryFXSlotID, NULL);
}