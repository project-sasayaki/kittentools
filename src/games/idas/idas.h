#include <windows.h>
#include <stdlib.h>
#include <stdint.h>
#include "../patchlib.h"
#include "../config/config.h"

/* Initial D CRC*/
/*JPN*/
#define SBML_REVA 0x361d1cbe
#define SBML_REVB 0x606914be
#define SBML_REVG 0x6cb19701
#define SBML_REVG_SERVERBOX 0x60496EEA

#define SBQZ_REVA 0xb3183112
#define SBQZ_REVC 0xda04e5e0
#define SBQZ_REVF 0xfc3dc85d
#define SBQZ_REVA_SERVERBOX 0x5fd379cd 

/*EXP*/
#define EXP20_SBTS 0x77c6b58b
#define EXP20_SBTS_REVA 0xd4910e75
#define EXP20_SBTS_REVA_SERVERBOX 0xcf40a9c2

int hook_drawBalloon(void* this, void* a2, float a3, float a4, float a5, float a6, float a7, bool a8);
extern int (__cdecl *next_drawBalloon)(void* this, void* a2, float a3, float a4, float a5, float a6, float a7, bool a8);

int hook_cCreditdisplayTexture(int a1, int a2, uint8_t a3, int a4, int a5);
extern int (__cdecl *next_cCreditdisplayTexture)(int a1, int a2, uint8_t a3, int a4, int a5);

double hook_sFont_put(void *this, float a2, float a3, uint16_t a4);
extern double (__cdecl *next_sFont_put)(void *this, float a2, float a3, uint16_t a4);

int hook_sFix_put(int a1, int a2, int a3, char *format);
extern int (__cdecl *next_sFix_put)(int a1, int a2, int a3, char *format);

int hook_setClipRect(int a1, int *a2);
extern int (__cdecl *next_setClipRect)(int a1, int *a2);

int hook_drawtext(void *this, void *a2, float a3, float a4);
extern int (__cdecl *next_drawtext)(void *this, void *a2, float a3, float a4);

int hook_amOsinfoGetNetworkProperty(DWORD *a1);
extern int (__cdecl *next_amOsinfoGetNetworkProperty)(DWORD *a1);

extern void *(__cdecl *next_CreateFrameBuffer)(
    void *renderer, uint32_t width, uint32_t height,
    uint32_t multiSample, uint32_t multiSampleQuality,
    uint32_t colorFormat, uint32_t depthBits, uint32_t stencilBits,
    uint32_t createDepth, void *colorTexture, void *depthTexture);

void *hook_CreateFrameBuffer(
    void *renderer, uint32_t width, uint32_t height,
    uint32_t multiSample, uint32_t multiSampleQuality,
    uint32_t colorFormat, uint32_t depthBits, uint32_t stencilBits,
    uint32_t createDepth, void *colorTexture, void *depthTexture);

int amDongleUserInfoEx(int a, int b, char *_arcadeContext);
int amDipswGetData(uint8_t *dip);

#pragma pack(push, 1)
typedef struct _IntPatch {
    uintptr_t addr;
    uint32_t value;
} IntPatch;

typedef struct _FuncPatch {
    uintptr_t addr;
    void* patch;
    void* next;
} FuncPatch;

typedef struct _StrPatch {
    uintptr_t addr;
    char* newStr;
    char* newStr2;
    char* newStr3;
} StrPatch;

typedef struct fixResolution {
    bool enabled;
    uint32_t width;
    uint32_t height;
}IDAS_FixResolution;

typedef struct _IDAS_DonglePatch {
    FuncPatch amDongleInit;
    FuncPatch amDongleIsAvailable;
    FuncPatch amDongleUpdate;
    FuncPatch amDongleUserInfoEx;
    FuncPatch amDongleDecryptEx;
    StrPatch amDongleDecryptExStr;
} IDAS_DonglePatch;

typedef struct _IDAS_DipSWPatch {
    FuncPatch amDipswInit;
    FuncPatch amDipswExit;
    FuncPatch amDipswGetData;
    FuncPatch amDipswSetLed;
} IDAS_DipSWPatch;

typedef struct _IDAS_NetworkPatch {
    FuncPatch isEthLinkUp;
    StrPatch skipInitStoreNetwork;
    StrPatch skipWaitDHCP;
    FuncPatch kickBackInitialize;
    StrPatch SkipInitialization;
    StrPatch skipInitAddress;
    uintptr_t InstoreSubnet[2];
    StrPatch KEYCHECK;
    StrPatch NetworkInit;
    FuncPatch altrServer;
    FuncPatch amOsinfoExecDhcpNic;
    FuncPatch amsInit;
    FuncPatch amOsinfoGetNetworkProperty;
} IDAS_NetworkPatch;

typedef struct _IDAS_NonNVPatch {
    StrPatch strglProgramParameters4fvNV;
} IDAS_NonNVPatch;

typedef struct _IDAS_ResolutionPatch {
    IntPatch maxWidthAddr;
    IntPatch MaxHeightAddr;
    IntPatch targetWidthAddr;
    IntPatch targetHeightAddr;
} IDAS_ResolutionPatch;

typedef struct _IDAS_DisplayPatch {
    FuncPatch isExistNewerSource;
    FuncPatch gl_XGetProcAddressARB;
    FuncPatch CreateFrameBuffer;
    StrPatch cutscenes;
    StrPatch disableFullscreen;
} IDAS_DisplayPatch;

typedef struct _IDAS_CutscenePatch {
    uintptr_t windowWidthAddr;
    uintptr_t windowHeightAddr;
    uintptr_t skipPromptDrawAddr;
    uintptr_t vertex5Addr;
    uintptr_t timeAttackDrawAddr;
    uintptr_t timeAttackRectangleAddr;
} IDAS_CutscenePatch;

typedef struct _IDAS_InitialData {
    const char* gameID;
    const char* verName;
    IDAS_FixResolution fixResolution;
    uintptr_t arcadeContextAddr;
    uintptr_t amDipswContextAddr;
    IntPatch debugLevelPatch[13];
    IDAS_DonglePatch donglePatch;
    IDAS_DipSWPatch dipswPatch;
    IDAS_NetworkPatch networkPatch;
    IDAS_NonNVPatch nonNVPatch;
    IDAS_DisplayPatch displayPatch;
    IDAS_ResolutionPatch resolutionPatch;
    IDAS_CutscenePatch cutscenePatch;
    FuncPatch renderPatch[];
} IDAS_InitialData;


typedef struct {
    uint32_t crc;
    const IDAS_InitialData* data;
} IDAS_VersionMap;

static const IDAS_InitialData SBML_REVA_Data = {
    .gameID = "SBML",
    .verName = "InitialD Arcade Stage 4 Rev A",
    .arcadeContextAddr = 0x089c8f60,
    .amDipswContextAddr = 0x08da0628,
    .debugLevelPatch = {{0x08d71470, 2},
                        {0x08d71480, 2},
                        {0x08d716d8, 2},
                        {0x08d716dc, 2},
                        {0x08d716e0, 2},
                        {0x08d716e4, 2},
                        {0x08d716e8, 2},
                        {0x08d716ec, 2},
                        {0x08d716f0, 2},
                        {0x08d716f8, 2},
                        {0x08d71700, 2},
                        {0x08d716f4, 2},
                        {0x08d71704, 0x0FFFFFFF}},
    .donglePatch = {.amDongleInit =
                        {
                            .addr = 0x086e20fa,
                            .patch = stubRetZero,
                        },
                    .amDongleIsAvailable =
                        {
                            .addr = 0x086e0b45,
                            .patch = stubRetOne,
                        },
                    .amDongleUserInfoEx =
                        {
                            .addr = 0x086e1fc1,
                            .patch = amDongleUserInfoEx,
                        },
                    .amDongleUpdate = {.addr = 0x086e15a9, .patch = stubRetZero}},
    .dipswPatch = {.amDipswInit =
                       {
                           .addr = 0x086e08d8,
                           .patch = stubRetZero,
                       },
                   .amDipswExit =
                       {
                           .addr = 0x086e095c,
                           .patch = stubRetZero,
                       },
                   .amDipswGetData =
                       {
                           .addr = 0x086e09d1,
                           .patch = amDipswGetData,
                       },
                   .amDipswSetLed =
                       {
                           .addr = 0x086e0a48,
                           .patch = stubRetZero,
                       }},
    .networkPatch = {.isEthLinkUp =
                         {
                             .addr = 0x0821e52c,
                             .patch = stubRetOne,
                         },
                     .skipInitStoreNetwork = {.addr = 0x082cb222, .newStr = "c0270900"},
                     .skipWaitDHCP = {.addr = 0x082cb4e9, .newStr = "e950010000"},
                     .SkipInitialization = {.addr = 0x08799acc, .newStr = "62"},
                     .skipInitAddress = {.addr = 0x08799adc, .newStr = "df"},
                     .InstoreSubnet = {0x00,  0x00}},
    .nonNVPatch =
        {
            .strglProgramParameters4fvNV = {.addr = 0x0, .newStr = ""},
        },
    .displayPatch =
        {
            .isExistNewerSource =
                {
                    .addr = 0x0825637a,
                    .patch = stubRetOne,
                },
            .disableFullscreen = {.addr = 0x085593c9, .newStr = "e92601000090"},
        }
};

static const IDAS_InitialData SBML_REVB_Data = {
    .gameID = "SBML",
    .verName = "InitialD Arcade Stage 4 Rev B",
    .arcadeContextAddr = 0x08318a45,
    .amDipswContextAddr = 0x08da0908,
    .debugLevelPatch = {{0x08d71750, 2},
                        {0x08d71760, 2},
                        {0x08d719b8, 2},
                        {0x08d719bc, 2},
                        {0x08d719c0, 2},
                        {0x08d719c4, 2},
                        {0x08d719c8, 2},
                        {0x08d719cc, 2},
                        {0x08d719d0, 2},
                        {0x08d719d8, 2},
                        {0x08d719e0, 2},
                        {0x08d719d4, 2},
                        {0x08d719e4, 0x0FFFFFFF}},
    .donglePatch = {.amDongleInit =
                        {
                            .addr = 0x086e2336,
                            .patch = stubRetZero,
                        },
                    .amDongleIsAvailable =
                        {
                            .addr = 0x086e0d81,
                            .patch = stubRetOne,
                        },
                    .amDongleUserInfoEx =
                        {
                            .addr = 0x086e21fd,
                            .patch = amDongleUserInfoEx,
                        },
                    .amDongleUpdate = {.addr = 0x086e17e5, .patch = stubRetZero}},
    .dipswPatch = {.amDipswInit =
                       {
                           .addr = 0x086e0b14,
                           .patch = stubRetZero,
                       },
                   .amDipswExit =
                       {
                           .addr = 0x086e0b98,
                           .patch = stubRetZero,
                       },
                   .amDipswGetData =
                       {
                           .addr = 0x086e0c0d,
                           .patch = amDipswGetData,
                       },
                   .amDipswSetLed =
                       {
                           .addr = 0x086e0c84,
                           .patch = stubRetZero,
                       }},
    .networkPatch = {.isEthLinkUp =
                         {
                             .addr = 0x0821e6dc,
                             .patch = stubRetOne,
                         },
                     .kickBackInitialize = {.addr = 0x0, .patch = stubRetOne},
                     .skipInitStoreNetwork = {.addr = 0x082cb412, .newStr = "c0270900"},
                     .skipWaitDHCP = {.addr = 0x082cb6d9, .newStr = "e950010000"},
                     .SkipInitialization = {.addr = 0x082cce58, .newStr = "EB"},
                     .skipInitAddress = {.addr = 0x08799d8c, .newStr = "52"},
                     .InstoreSubnet = {0x00,  0x00}},
    .nonNVPatch =
        {
            .strglProgramParameters4fvNV = {.addr = 0x0, .newStr = ""},
        },
    .displayPatch =
        {
            .isExistNewerSource =
                {
                    .addr = 0x0825653a,
                    .patch = stubRetOne,
                },
            .disableFullscreen = {.addr = 0x08559609, .newStr = "e92601000090"},
        }
};

static const IDAS_InitialData SBML_REVG_Data = {
    .gameID = "SBML",
    .verName = "InitialD Arcade Stage 4 Rev G",
    .arcadeContextAddr = 0x087da56c,
    .amDipswContextAddr = 0x08e0d7a8,
    .debugLevelPatch = {
        {0x08dde130, 2},
        {0x08dde140, 2},
        {0x08dde398, 2},
        {0x08dde39c, 2},
        {0x08dde3a0, 2},
        {0x08dde3a4, 2},
        {0x08dde3a8, 2},
        {0x08dde3ac, 2},
        {0x08dde3b0, 2},
        {0x08dde3b8, 2},
        {0x08dde3c0, 2},
        {0x08dde3b4, 2},
        {0x08dde3c4, 0x0FFFFFFF}
    },
    .donglePatch = {
        .amDongleInit = {
            .addr = 0x0872c8d6, 
            .patch = stubRetZero,
        },
        .amDongleIsAvailable = {
            .addr = 0x0872b321, 
            .patch = stubRetOne,
        },
        .amDongleUserInfoEx = {
            .addr = 0x0872c79d,
            .patch = amDongleUserInfoEx,
        },
        .amDongleUpdate = {
            .addr = 0x0872bd85, 
            .patch = stubRetZero
        }
    },
    .dipswPatch = {
        .amDipswInit = {
            .addr = 0x0872b0b4, 
            .patch = stubRetZero,
        },
        .amDipswExit = {
            .addr = 0x0872b138, 
            .patch = stubRetZero,
        },
        .amDipswGetData = {
            .addr = 0x0872b1ad, 
            .patch = amDipswGetData,
        },
        .amDipswSetLed = {
            .addr = 0x0872b224, 
            .patch = stubRetZero,
        }
    },
    .networkPatch = {
        .isEthLinkUp = {
            .addr = 0x0823813a,
            .patch = stubRetOne,
        },
        .skipInitStoreNetwork = {
            .addr = 0x082f4396, 
            .newStr = "c0270900"
        },
        .skipWaitDHCP = {
            .addr = 0x082f4efb, 
            .newStr = "e94d010000"
        },
        .kickBackInitialize = {
            .addr = 0x0822226A, 
            .patch = stubRetOne,
        },
        .SkipInitialization = {
            .addr = 0x087e9eac, 
            .newStr = "C2",
            .newStr2 = "3F36"
        },
        .skipInitAddress = {
            .addr = 0x082F6569, 
            .newStr = "eb60"
        },
        .InstoreSubnet = {0x082423C8, 0x0836E17F}
    },
    .nonNVPatch = {
        .strglProgramParameters4fvNV = {
            .addr = 0x0, 
            .newStr = ""
        },
    },
    .displayPatch = {
        .isExistNewerSource = {
            .addr = 0x08271cec, 
            .patch = stubRetOne,
        },
        .CreateFrameBuffer = {
            .addr = 0x08262CCE,
            .patch = hook_CreateFrameBuffer,
            .next = (void **) &next_CreateFrameBuffer,
        },
        .disableFullscreen = {
            .addr = 0x08599819, 
            .newStr = "e92601000090"
        },
    },
    .resolutionPatch = {
        .maxWidthAddr = {0x08393FEC},
        .MaxHeightAddr = {0x08394044},
        .targetWidthAddr = {0x0839409A},
        .targetHeightAddr = {0x0839404B},
    },
    .cutscenePatch = {
        .windowWidthAddr = 0x08DBDCE8,
        .windowHeightAddr = 0x08DBDCEC,
        .timeAttackDrawAddr = 0x080AEF3C,
        .timeAttackRectangleAddr = 0x0837F472,
    },
    .renderPatch = {
        {
            .addr = 0x081F2B7E,
            .patch = hook_drawBalloon,
            .next = (void **) &next_drawBalloon
        },
        {
            .addr = 0x083502B0,
            .patch = hook_cCreditdisplayTexture,
            .next = (void **) &next_cCreditdisplayTexture
        },
        {
            .addr = 0x08594186,
            .patch = hook_sFix_put,
            .next = (void **) &next_sFix_put
        },
        {
            .addr = 0x085A9928,
            .patch = hook_setClipRect,
            .next = (void **) &next_setClipRect
        },
        {
            .addr = 0x081F3CE4,
            .patch = hook_drawtext,
            .next = (void **) &next_drawtext
        },
        {
            .addr = 0,
            .patch = 0,
            .next = 0
        }
    }
};

static const IDAS_InitialData SBML_REVG_ServerBox_Data = {
    .gameID = "SBML",
    .verName = "InitialD Arcade Stage 4 Server Box Rev G",
    .fixResolution = {
        .enabled = true,
        .width = 640,
        .height = 480,
    },
    .amDipswContextAddr = 0x08358648,
    .debugLevelPatch = {
        {0x08350718, 2},
        {0x08350720, 2},
        {0x08350978, 2},
        {0x0835097C, 2},
        {0x08350980, 2},
        {0x08350984, 2},
        {0x08350988, 2},
        {0x0835098C, 2},
        {0x08350990, 2},
        {0x08350998, 2},
        {0x083509A0, 2},
        {0x08350994, 2},
        {0x083509A4, 0x0FFFFFFF}
    },
    .donglePatch = {
        .amDongleInit = {
            .addr = 0x080FCA5A, 
            .patch = stubRetZero,
        },
        .amDongleIsAvailable = {
            .addr = 0x080FB4A5, 
            .patch = stubRetOne,
        },
        .amDongleUpdate = {
            .addr = 0x080FBF09, 
            .patch = stubRetZero,
        },
        .amDongleUserInfoEx = {
            .addr = 0x080FC921, 
            .patch = amDongleUserInfoEx
        },
        .amDongleDecryptEx = {
            .addr = 0x080FC40E, 
            .patch = stubRetZero,
        },
    },
    .dipswPatch = {
        .amDipswInit = {
            .addr = 0x080FB238, 
            .patch = stubRetZero
        },
        .amDipswExit = {
            .addr = 0x080FB2BC, 
            .patch = stubRetZero
        },
        .amDipswGetData = {
            .addr = 0x080FB331, 
            .patch = amDipswGetData
        },
        .amDipswSetLed = {
            .addr = 0x080FB3A8, 
            .patch = stubRetZero
        }
    },
    .networkPatch = {
        .skipWaitDHCP = {
            .addr = 0x0807FCAB, 
            .newStr = "e9e9000000"
        },
        .amOsinfoGetNetworkProperty = {
            .addr = 0x08104032, 
            .patch = hook_amOsinfoGetNetworkProperty,
            .next = (void **) &next_amOsinfoGetNetworkProperty
        }
    }   
};


static const IDAS_InitialData SBQZ_REVA_Data = {
    .gameID = "SBQZ",
    .verName = "InitialD Arcade Stage 5 Rev A",
    .arcadeContextAddr = 0x084e1707,
    .amDipswContextAddr = 0x093d93a8,
    .debugLevelPatch = {
        {0x093a92d0, 2},
        {0x093a92e0, 2},
        {0x093a9538, 2},
        {0x093a953c, 2},
        {0x093a9540, 2},
        {0x093a9544, 2},
        {0x093a9548, 2},
        {0x093a954c, 2},
        {0x093a9550, 2},
        {0x093a9558, 2},
        {0x093a9560, 2},
        {0x093a9554, 2},
        {0x093a9564, 0x0FFFFFFF}
    },
    .donglePatch = {
        .amDongleInit = {
            .addr = 0x089111da, 
            .patch = stubRetZero
        },
        .amDongleIsAvailable = {
            .addr = 0x0890fc25, 
            .patch = stubRetOne
        },
        .amDongleUpdate = {
            .addr = 0x08910689, 
            .patch = stubRetZero
        },
        .amDongleUserInfoEx = {
            .addr = 0x089110a1, 
            .patch = amDongleUserInfoEx
        },
        .amDongleDecryptEx = {
            .addr = 0x08910B8E, 
            .patch = stubRetZero
        },
    },
    .dipswPatch = {
        .amDipswInit = {
            .addr = 0x0890f9b8, 
            .patch = stubRetZero
        },
        .amDipswExit = {
            .addr = 0x0890fa3c, 
            .patch = stubRetZero
        },
        .amDipswGetData = {
            .addr = 0x0890fab1, 
            .patch = amDipswGetData
        },
        .amDipswSetLed = {
            .addr = 0x0890fb28, 
            .patch = stubRetZero
        }
    },
    .networkPatch = {
        .isEthLinkUp = {
            .addr = 0x08321968, 
            .patch = stubRetOne
        },
        .skipInitStoreNetwork = {
            .addr = 0x0843f068, 
            .newStr = "c0270900"
        },
        .skipWaitDHCP = {
            .addr = 0x0843fed0, 
            .newStr = "e98d000000"
        },
        .kickBackInitialize = {
            .addr = 0x08307b62, 
            .patch = stubRetOne
        },
        .SkipInitialization = {
            .addr = 0x089e308c, 
            .newStr = "E7", // SKIP HARDWARE SELF CHECK
            .newStr2 = "37E9" // SKIP ALL
        },
        .skipInitAddress = {
            .addr = 0x08441f99, 
            .newStr = "eb60"
        },
        .InstoreSubnet = {0x083333AE,  0x08500703}
    },
    .nonNVPatch = {
        .strglProgramParameters4fvNV = {
            .addr = 0x0, 
            .newStr = ""
        },
    },
    .displayPatch = {
        .isExistNewerSource = {
            .addr = 0x08388cb4, 
            .patch = stubRetOne
        },
        .cutscenes = {
            .addr = 0x0874433e, 
            .newStr = "00"
        },
        .disableFullscreen = {
            .addr = 0x08788e59, 
            .newStr = "e92601000090"
        }
    },
    .resolutionPatch = {
        .maxWidthAddr = {0x0853CD0C},
        .MaxHeightAddr = {0x0853CD64},
        .targetWidthAddr = {0x0853CDBA},
        .targetHeightAddr = {0x0853CD6B},
    },
    .renderPatch = {
        {
            .addr = 0x082526A6,
            .patch = hook_drawBalloon,
            .next = (void **) &next_drawBalloon
        },
        {
            .addr = 0x084E1770,
            .patch = hook_cCreditdisplayTexture,
            .next = (void **) &next_cCreditdisplayTexture
        },
        {
            .addr = 0x086D198C,
            .patch = hook_sFont_put,
            .next = (void **) &next_sFont_put
        },
        {
            .addr = 0x08775402,
            .patch = hook_sFix_put,
            .next = (void **) &next_sFix_put
        },
        {
            .addr = 0x0878AA58,
            .patch = hook_setClipRect,
            .next = (void **) &next_setClipRect
        },
        {
            .addr = 0,
            .patch = 0,
            .next = 0
        }
    }
};

static const IDAS_InitialData SBQZ_REVC_Data = {
    .gameID = "SBQZ",
    .verName = "InitialD Arcade Stage 5 Rev C",
    .arcadeContextAddr = 0x084e21e7,
    .amDipswContextAddr = 0x093d91c8,
    .debugLevelPatch = {
        {0x093a90f0, 2},
        {0x093a9100, 2},
        {0x093a9358, 2},
        {0x093a935c, 2},
        {0x093a9360, 2},
        {0x093a9364, 2},
        {0x093a9368, 2},
        {0x093a936c, 2},
        {0x093a9370, 2},
        {0x093a9378, 2},
        {0x093a9380, 2},
        {0x093a9374, 2},
        {0x093a9384, 0x0FFFFFFF}
    },
    .donglePatch = {
        .amDongleInit = {
            .addr = 0x08911bfa, 
            .patch = stubRetZero
        },
        .amDongleIsAvailable = {
            .addr = 0x08910645, 
            .patch = stubRetOne
        },
        .amDongleUpdate = {
            .addr = 0x089110a9, 
            .patch = stubRetZero
        },
        .amDongleUserInfoEx = {
            .addr = 0x08911ac1, 
            .patch = amDongleUserInfoEx
        },
        .amDongleDecryptEx = {
            .addr = 0x00, 
            .patch = stubRetZero
        },
    },
    .dipswPatch = {
        .amDipswInit = {
            .addr = 0x089103d8, 
            .patch = stubRetZero
        },
        .amDipswExit = {
            .addr = 0x0891045c, 
            .patch = stubRetZero
        },
        .amDipswGetData = {
            .addr = 0x089104d1, 
            .patch = amDipswGetData
        },
        .amDipswSetLed = {
            .addr = 0x08910548, 
            .patch = stubRetZero
        }
    },
    .networkPatch = {
        .isEthLinkUp = {
            .addr = 0x00, 
            .patch = 0
        },
        .skipInitStoreNetwork = {
            .addr = 0x0843fac8, 
            .newStr = "C0270900"
        },
        .skipWaitDHCP = {
            .addr = 0x08440930, 
            .newStr = "e98d000000"
        },
        .kickBackInitialize = {
            .addr = 0x08308472, 
            .patch = stubRetOne
        },
        .SkipInitialization = {
            .addr = 0x089e3aac, 
            .newStr = "47f3", // SKIP HARDWARE SELF CHECK
            .newStr2 = "acf3", // SKIP ALL
            .newStr3 = "71F3"
        },
        .skipInitAddress = {
            .addr = 0x084429f9, 
            .newStr = "eb60"
        },
        .InstoreSubnet = {0x083333AE,  0x08500703}
    },
    .nonNVPatch = {
        .strglProgramParameters4fvNV = {
            .addr = 0x0, 
            .newStr = ""
        },
    },
    .displayPatch = {
        .isExistNewerSource = {
            .addr = 0x08389594, 
            .patch = stubRetOne
        },
        .cutscenes = {
            .addr = 0x08744d5e, 
            .newStr = "00"
        },
        .disableFullscreen = {
            .addr = 0x08789879, 
            .newStr = "e92601000090"
        }
    },
    .resolutionPatch = {
        .maxWidthAddr = {0x0853D72C},
        .MaxHeightAddr = {0x0853D784},
        .targetWidthAddr = {0x0853D7DA},
        .targetHeightAddr = {0x0853D78B},
    },
    .cutscenePatch = {
        .windowWidthAddr = 0x09388CC8,
        .windowHeightAddr = 0x09388CCC,
        .skipPromptDrawAddr = 0x0826A6E8,
        .vertex5Addr = 0x08528C36,
        .timeAttackDrawAddr = 0x080C39B0,
        .timeAttackRectangleAddr = 0x08528B00,
    },
    .renderPatch = {
        {
            .addr = 0x082536C8,
            .patch = hook_drawBalloon,
            .next = (void **) &next_drawBalloon
        },
        {
            .addr = 0x084E2250,
            .patch = hook_cCreditdisplayTexture,
            .next = (void **) &next_cCreditdisplayTexture
        },
        {
            .addr = 0x086D23AC,
            .patch = hook_sFont_put,
            .next = (void **) &next_sFont_put
        },
        {
            .addr = 0x0878B478,
            .patch = hook_setClipRect,
            .next = (void **) &next_setClipRect
        },
        {
            .addr = 0,
            .patch = 0,
            .next = 0
        }
    }
};

static const IDAS_InitialData SBQZ_REVA_ServerBox_Data = {
    .gameID = "SBQZ",
    .verName = "InitialD Arcade Stage 5 Server Box Rev A",
    .fixResolution = {
        .enabled = true,
        .width = 640,
        .height = 480,
    },
    .arcadeContextAddr = 0,
    .amDipswContextAddr = 0x083bc4e8,
    .debugLevelPatch = {
        {0x083b3bb8, 2},
        {0x083b3bc0, 2},
        {0x083b3e18, 2},
        {0x083b3e1c, 2},
        {0x083b3e20, 2},
        {0x083b3e24, 2},
        {0x083b3e28, 2},
        {0x083b3e2c, 2},
        {0x083b3e30, 2},
        {0x083b3e38, 2},
        {0x083b3e40, 2},
        {0x083b3e34, 2},
        {0x083b3e44, 0x0FFFFFFF}
    },
    .donglePatch = {
        .amDongleInit = {
            .addr = 0x080fd3ee, 
            .patch = stubRetZero
        },
        .amDongleIsAvailable = {
            .addr = 0x080fbe39, 
            .patch = stubRetOne
        },
        .amDongleUpdate = {
            .addr = 0x080fc89d, 
            .patch = stubRetZero
        },
        .amDongleUserInfoEx = {
            .addr = 0x080fd2b5, 
            .patch = amDongleUserInfoEx
        },
        .amDongleDecryptEx = {
            .addr = 0x080fcda2, 
            .patch = stubRetZero
        }
    },
    .dipswPatch = {
        .amDipswInit = {
            .addr = 0x080fbbcc, 
            .patch = stubRetZero
        },
        .amDipswExit = {
            .addr = 0x080fbc50, 
            .patch = stubRetZero
        },
        .amDipswGetData = {
            .addr = 0x080fbcc5, 
            .patch = amDipswGetData
        },
        .amDipswSetLed = {
            .addr = 0x080fbd3c, 
            .patch = stubRetZero
        }
    },
    .networkPatch = {
        .skipWaitDHCP = {
            .addr = 0x0807fe5a, 
            .newStr = "e9e9000000"
        },
        .altrServer = {
            .addr = 0x08114034, 
            .patch = stubRetThree
        },
        .amOsinfoGetNetworkProperty = {
            .addr = 0x081049BE, 
            .patch = hook_amOsinfoGetNetworkProperty,
            .next = (void **) &next_amOsinfoGetNetworkProperty
        }
    }
};

static const IDAS_InitialData SBTS_REVA_Data = {.gameID = "SBTS",
                                                .verName = "InitialD Arcade Stage 5 EXP Rev A",
                                                .arcadeContextAddr = 0,
                                                .amDipswContextAddr = 0x09427128,
                                                .debugLevelPatch = {{0x093f6fa0, 2},
                                                                    {0x093f6fc0, 2},
                                                                    {0x093f7218, 2},
                                                                    {0x093f721c, 2},
                                                                    {0x093f7220, 2},
                                                                    {0x093f7224, 2},
                                                                    {0x093f7228, 2},
                                                                    {0x093f722c, 2},
                                                                    {0x093f7230, 2},
                                                                    {0x093f7238, 2},
                                                                    {0x093f7240, 2},
                                                                    {0x093f7234, 2},
                                                                    {0x093f7244, 0x0FFFFFFF}},
                                                .donglePatch = {.amDongleInit =
                                                                    {
                                                                        .addr = 0x0893df75,
                                                                        .patch = stubRetZero,
                                                                    },
                                                                .amDongleIsAvailable =
                                                                    {
                                                                        .addr = 0x0893c801,
                                                                        .patch = stubRetOne,
                                                                    },
                                                                .amDongleUserInfoEx =
                                                                    {
                                                                        .addr = 0x0893de3b,
                                                                        .patch = amDongleUserInfoEx,
                                                                    },
                                                                .amDongleUpdate = {.addr = 0x0893d322, .patch = stubRetZero}},
                                                .dipswPatch = {.amDipswInit =
                                                                   {
                                                                       .addr = 0x0893c594,
                                                                       .patch = stubRetZero,
                                                                   },
                                                               .amDipswExit =
                                                                   {
                                                                       .addr = 0x0893c618,
                                                                       .patch = stubRetZero,
                                                                   },
                                                               .amDipswGetData =
                                                                   {
                                                                       .addr = 0x0893c68d,
                                                                       .patch = amDipswGetData,
                                                                   },
                                                               .amDipswSetLed =
                                                                   {
                                                                       .addr = 0x0893c703,
                                                                       .patch = stubRetZero,
                                                                   }},
                                                .networkPatch = {.isEthLinkUp = {.addr = 0x0832fe46, .patch = stubRetOne},
                                                                 .skipWaitDHCP = {.addr = 0x084566d8, .newStr = "e991000000"},
                                                                 .skipInitAddress = {.addr = 0x084587cb, .newStr = "eb60"},
                                                                 // .skipInitStoreNetwork = {.addr = 0x08455914, .newStr = "c0270900"},
                                                                 .amOsinfoExecDhcpNic = {.addr = 0x08944106, .patch = stubRetZero},
                                                                 .SkipInitialization = {.addr = 0x08a0f9cc, .newStr = "a7", .newStr2 = "5451", .newStr3 = "f050"},
                                                                 .KEYCHECK = {.addr = 0x084fe0cc, .newStr = "00"},
                                                                 .NetworkInit = {.addr = 0x08513c40, .newStr = "FEFFFFFF"},
                                                                 .amsInit = {.addr = 0x08938530, .patch = stubRetZero},
                                                },
                                                .displayPatch = {
                                                    .isExistNewerSource = {.addr = 0x083973a4, .patch = stubRetOne,},
                                                    .disableFullscreen = {.addr = 0x087a67e9, .newStr = "e92601000090"},
                                                    .cutscenes = {.addr = 0x08761cce, .newStr = "00"}
                                                },
                                                .resolutionPatch = {
                                                    .maxWidthAddr = {0x0855a70C},
                                                    .MaxHeightAddr = {0x0855a764},
                                                    .targetWidthAddr = {0x0855a7bA},
                                                    .targetHeightAddr = {0x0855a76B},
                                                },
                                                .cutscenePatch = {
                                                    .windowWidthAddr = 0x093d6b68,
                                                    .windowHeightAddr = 0x093d6b6c,
                                                    .skipPromptDrawAddr = 0x08273398,
                                                    .vertex5Addr = 0x08545dde,
                                                    .timeAttackDrawAddr = 0x080c8a50,
                                                    .timeAttackRectangleAddr = 0x08545ca8,
                                                },
                                                .renderPatch = {
                                                    {
                                                        .addr = 0x0825c1b6,
                                                        .patch = hook_drawBalloon,
                                                        .next = (void**)&next_drawBalloon
                                                    },
                                                    {
                                                        .addr = 0x084fe1d0,
                                                        .patch = hook_cCreditdisplayTexture,
                                                        .next = (void**)&next_cCreditdisplayTexture
                                                    },
                                                    {
                                                        .addr = 0x086ef38c,
                                                        .patch = hook_sFont_put, //DemoBallon::DrawText()
                                                        .next = (void**)&next_sFont_put
                                                    },
                                                    {
                                                        .addr = 0x087a83e8,
                                                        .patch = hook_setClipRect,
                                                        .next = (void**)&next_setClipRect
                                                    },
                                                    {
                                                        .addr = 0,
                                                        .patch = 0,
                                                        .next = 0
                                                    }
                                                }
                                            };


static const IDAS_InitialData SBTS_REVA_ServerBox_Data = {
    .gameID = "SBTS",
    .verName = "InitialD Arcade Stage 5 EXP Server Box Rev A",
    .fixResolution = {
        .enabled = true,
        .width = 640,
        .height = 480,
    },
    .arcadeContextAddr = 0,
    .amDipswContextAddr = 0x083c18e8,
    .debugLevelPatch = {
        {0x083b8ee4, 2},
        {0x083b8f00, 2},
        {0x083b9158, 2},
        {0x083b915c, 2},
        {0x083b9160, 2},
        {0x083b9164, 2},
        {0x083b9168, 2},
        {0x083b916c, 2},
        {0x083b9170, 2},
        {0x083b9178, 2},
        {0x083b9180, 2},
        {0x083b9174, 2},
        {0x083b9184, 0x0FFFFFFF}
    },
    .donglePatch = {
        .amDongleInit = {
            .addr = 0x08100f65,
            .patch = stubRetZero
        },
        .amDongleIsAvailable = {
            .addr = 0x080ff7f1,
            .patch = stubRetOne
        },
        .amDongleUpdate = {
            .addr = 0x08100312,
            .patch = stubRetZero
        },
        .amDongleUserInfoEx = {
            .addr = 0x08100e2b,
            .patch = amDongleUserInfoEx
        },
        .amDongleDecryptEx = {
            .addr = 0x08100816,
            .patch = stubRetZero
        }
    },
    .dipswPatch = {
        .amDipswInit = {
            .addr = 0x080ff584,
            .patch = stubRetZero
        },
        .amDipswExit = {
            .addr = 0x080ff608,
            .patch = stubRetZero
        },
        .amDipswGetData = {
            .addr = 0x080ff67d,
            .patch = amDipswGetData
        },
        .amDipswSetLed = {
            .addr = 0x080ff6f3,
            .patch = stubRetZero
        }
    },
    .networkPatch = {
        .skipWaitDHCP = {
            .addr = 0x080805e2,
            .newStr = "e937010000"
        },
        .skipInitAddress = {
            .addr = 0x0807fb25, 
            .newStr = "00" // m_isDongleError = 0;
        },
        .altrServer = {
            .addr = 0x081186e4,
            .patch = stubRetThree
        },
        .amOsinfoGetNetworkProperty = {
            .addr = 0x08108f06,
            .patch = hook_amOsinfoGetNetworkProperty,
            .next = (void**)&next_amOsinfoGetNetworkProperty
        },
        .amsInit = {
            .addr = 0x080fb520,
            .patch = stubRetZero
        }
    }
};

static const IDAS_VersionMap g_IDASVersions[] = {
    /*D4*/
    {SBML_REVA, &SBML_REVA_Data},
    {SBML_REVB, &SBML_REVB_Data},
    {SBML_REVG, &SBML_REVG_Data},
    {SBML_REVG_SERVERBOX, &SBML_REVG_ServerBox_Data},
    /*D5*/
    {SBQZ_REVA, &SBQZ_REVA_Data},
    {SBQZ_REVC, &SBQZ_REVC_Data},
    {SBQZ_REVA_SERVERBOX, &SBQZ_REVA_ServerBox_Data},

    {EXP20_SBTS_REVA, &SBTS_REVA_Data},
    {EXP20_SBTS_REVA_SERVERBOX, &SBTS_REVA_ServerBox_Data}
};

#pragma pack(pop)
void init_idas_prepatch(uint32_t crc);
