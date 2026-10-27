#include <windows.h>
#include <stdlib.h>
#include <stdint.h>
#include "../patchlib.h"
#include "../config/config.h"

/* Initial D CRC*/
/*JPN*/
#define SBML_REVA 0x361d1cbe
#define SBML_REVB 0x606914be
#define SBML_REVD 0xba380f8a
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
    FuncPatch gl_XGetProcAddressARB;
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
    IDAS_NetworkPatch networkPatch;
    IDAS_NonNVPatch nonNVPatch;
    IDAS_DisplayPatch displayPatch;
    IDAS_ResolutionPatch resolutionPatch;
    IDAS_CutscenePatch cutscenePatch;
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
            .disableFullscreen = {.addr = 0x085593c9, .newStr = "e92601000090"},
        }
};

static const IDAS_InitialData SBML_REVB_Data = {
    .gameID = "SBML",
    .verName = "InitialD Arcade Stage 4 Rev B",
    .arcadeContextAddr = 0x08318a45,
    .amDipswContextAddr = 0x08da0908,
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
            .disableFullscreen = {.addr = 0x08559609, .newStr = "e92601000090"},
        }
};

static const IDAS_InitialData SBML_REVD_Data = {
    .gameID = "SBML",
    .verName = "InitialD Arcade Stage 4 Rev D",
    .arcadeContextAddr = 0x087929D8,
    .amDipswContextAddr = 0x08DA8A68,
    .networkPatch = {
        .isEthLinkUp = {
            .addr = 0x0821F5CC,
            .patch = stubRetOne,
        },
        .skipInitStoreNetwork = {
            .addr = 0x082cd3b2, 
            .newStr = "c0270900"
        },
        .skipWaitDHCP = {
            .addr = 0x082cd679, 
            .newStr = "e950010000"
        },
        .kickBackInitialize = {
            .addr = 0x0820A1AA, 
            .patch = stubRetOne,
        },
        .SkipInitialization = {
            .addr = 0x087A024C, 
            .newStr = "1FBF",
            .newStr2 = "6FBF",
            .newStr3 = "49BF"
        },
        .InstoreSubnet = {0x08229283, 0x08339E77}
    },
    .nonNVPatch = {
        .strglProgramParameters4fvNV = {
            .addr = 0x0, 
            .newStr = ""
        },
    },
    .displayPatch = {
        .disableFullscreen = {
            .addr = 0x0855F519, 
            .newStr = "e92601000090"
        },
    }
};

static const IDAS_InitialData SBML_REVG_Data = {
    .gameID = "SBML",
    .verName = "InitialD Arcade Stage 4 Rev G",
    .arcadeContextAddr = 0x087da56c,
    .amDipswContextAddr = 0x08e0d7a8,
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
            .newStr = "EF35",
            .newStr2 = "3F36",
            .newStr3 = "1936"
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
    .networkPatch = {
        .skipWaitDHCP = {
            .addr = 0x0807FCAB, 
            .newStr = "e9e9000000"
        }
    }   
};


static const IDAS_InitialData SBQZ_REVA_Data = {
    .gameID = "SBQZ",
    .verName = "InitialD Arcade Stage 5 Rev A",
    .arcadeContextAddr = 0x084e1707,
    .amDipswContextAddr = 0x093d93a8,
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
            .newStr = "E7E8", // SKIP HARDWARE SELF CHECK
            .newStr2 = "37E9", // SKIP ALL
            .newStr3 = "11E9"
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
    }
};

static const IDAS_InitialData SBQZ_REVC_Data = {
    .gameID = "SBQZ",
    .verName = "InitialD Arcade Stage 5 Rev C",
    .arcadeContextAddr = 0x084e21e7,
    .amDipswContextAddr = 0x093d91c8,
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
            .newStr2 = "97f3", // SKIP ALL
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
    .networkPatch = {
        .skipWaitDHCP = {
            .addr = 0x0807fe5a, 
            .newStr = "e9e9000000"
        },
        .altrServer = {
            .addr = 0x08114034, 
            .patch = stubRetThree
        }
    }
};

static const IDAS_InitialData SBTS_REVA_Data = {.gameID = "SBTS",
                                                .verName = "InitialD Arcade Stage 5 EXP Rev A",
                                                .arcadeContextAddr = 0,
                                                .amDipswContextAddr = 0x09427128,
                                                .networkPatch = {.isEthLinkUp = {.addr = 0x0832fe46, .patch = stubRetOne},
                                                                 .skipWaitDHCP = {.addr = 0x084566d8, .newStr = "e991000000"},
                                                                 .skipInitAddress = {.addr = 0x084587cb, .newStr = "eb60"},
                                                                 // .skipInitStoreNetwork = {.addr = 0x08455914, .newStr = "c0270900"},
                                                                 .SkipInitialization = {.addr = 0x08a0f9cc, .newStr = "a7", .newStr2 = "5451", .newStr3 = "f050"},
                                                                 .KEYCHECK = {.addr = 0x084fe0cc, .newStr = "00"},
                                                                 .NetworkInit = {.addr = 0x08513c40, .newStr = "FEFFFFFF"}
                                                },
                                                .displayPatch = {
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
        }
    }
};

static const IDAS_VersionMap g_IDASVersions[] = {
    /*D4*/
    {SBML_REVA, &SBML_REVA_Data},
    {SBML_REVB, &SBML_REVB_Data},
    {SBML_REVD, &SBML_REVD_Data},
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
