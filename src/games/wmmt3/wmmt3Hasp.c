/* clHasp::open is replaced: it validates data only a real dongle holds. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "wmmt3.h"
#include "wmmt3Hasp.h"
#include "../patchlib.h"
#include "../../config/config.h"
#include "../../elfLoader/symbol.h"
#include "../../log/log.h"
#include "../../hooks/file.h"

#define HASP_STATUS_OK   0
#define HASP_MEM_RANGE   1
#define HASP_INV_HND     9
#define HASP_MEMORY_SIZE 4096
#define HASP_HANDLE      0x4B54

static uint8_t g_memory[HASP_MEMORY_SIZE];
static char g_memoryPath[MAX_PATH];
static char g_countPath[MAX_PATH];
static CRITICAL_SECTION g_lock;

static void load_memory(void)
{
    FILE *f = fopen(g_memoryPath, "rb");
    if (f)
    {
        fread(g_memory, 1, sizeof(g_memory), f);
        fclose(f);
    }
}

static void save_memory(void)
{
    FILE *f = fopen(g_memoryPath, "wb");
    if (f)
    {
        fwrite(g_memory, 1, sizeof(g_memory), f);
        fclose(f);
    }
}

static int load_count(void)
{
    int count = 0;
    FILE *f = fopen(g_countPath, "r");
    if (f)
    {
        if (fscanf(f, "%d", &count) != 1)
            count = 0;
        fclose(f);
    }
    return count;
}

static void save_count(int count)
{
    FILE *f = fopen(g_countPath, "w");
    if (f)
    {
        fprintf(f, "%d\n", count);
        fclose(f);
    }
}

static int hk_login(unsigned feature, const void *vendorCode, int *handle)
{
    (void)feature;
    (void)vendorCode;
    if (handle)
        *handle = HASP_HANDLE;
    return HASP_STATUS_OK;
}

static int hk_logout(int handle)
{
    (void)handle;
    return HASP_STATUS_OK;
}

static int hk_read(int handle, unsigned fileId, unsigned offset, unsigned length, void *buffer)
{
    (void)handle;
    (void)fileId;
    if (offset > HASP_MEMORY_SIZE || length > HASP_MEMORY_SIZE - offset)
        return HASP_MEM_RANGE;
    EnterCriticalSection(&g_lock);
    memcpy(buffer, g_memory + offset, length);
    LeaveCriticalSection(&g_lock);
    return HASP_STATUS_OK;
}

static int hk_write(int handle, unsigned fileId, unsigned offset, unsigned length, const void *buffer)
{
    (void)handle;
    (void)fileId;
    if (offset > HASP_MEMORY_SIZE || length > HASP_MEMORY_SIZE - offset)
        return HASP_MEM_RANGE;
    EnterCriticalSection(&g_lock);
    memcpy(g_memory + offset, buffer, length);
    save_memory();
    LeaveCriticalSection(&g_lock);
    return HASP_STATUS_OK;
}

/* The game only decrypts what it encrypted. */
static int hk_crypt(int handle, void *buffer, unsigned length)
{
    (void)handle;
    (void)buffer;
    (void)length;
    return HASP_STATUS_OK;
}

static int hk_get_rtc(int handle, unsigned long long *t)
{
    (void)handle;
    time_t now = time(NULL);
    struct tm local;
    localtime_s(&local, &now);
    if (t)
        *t = (unsigned long long)_mkgmtime(&local);
    return HASP_STATUS_OK;
}

static int hk_set_rtc(int handle, unsigned long long t)
{
    (void)handle;
    (void)t;
    return HASP_STATUS_OK;
}

static int hk_get_size(int handle, unsigned fileId, unsigned *size)
{
    (void)handle;
    (void)fileId;
    if (size)
        *size = HASP_MEMORY_SIZE;
    return HASP_STATUS_OK;
}

static char g_sessionInfo[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" ?><hasp_info><hasp><id>1</id></hasp></hasp_info>";

static int hk_get_sessioninfo(int handle, const char *format, char **info)
{
    (void)handle;
    (void)format;
    if (info)
        *info = g_sessionInfo;
    return HASP_STATUS_OK;
}

static void hk_free(char *info)
{
    (void)info;
}

/* Offset from the getter's last load, mov reg,[eax+disp8]. */
static int getter_offset(const char *name, int fallback)
{
    const uint8_t *code = get_native_symbol_ptr(name);
    if (!code)
        return fallback;
    for (int i = 0; i < 40; i++)
    {
        /* mov eax,[eax+d8] = 8B 40 d8;  mov edx,[eax+d8] = 8B 50 d8 */
        if (code[i] == 0x8B && (code[i + 1] == 0x40 || code[i + 1] == 0x50) &&
            !(code[i + 1] == 0x40 && code[i + 2] == 0x04))
            return code[i + 2];
        if (code[i] == 0xC3)
            break;
    }
    return fallback;
}

static void parse_serial(const char *text, const char *fallback, const char *key, int *hi, int *lo)
{
    if (text && sscanf(text, "%d-%d", hi, lo) == 2 &&
        *hi >= 0 && *hi <= 999999 && *lo >= 0 && *lo <= 999999)
        return;
    log_warn(plfWmmt3, "HASP: [wmmt3] %s = \"%s\" is not NNNNNN-NNNNNN (0-999999 each); using %s",
             key, text ? text : "", fallback);
    sscanf(fallback, "%d-%d", hi, lo);
}

typedef struct
{
    const char *name;           /* mangled class prefix, e.g. "6clHasp" */
    int serialHi, serialLo, count; /* field offsets, -1 = none */
    int valueHi, valueLo;          /* the serial this dongle reports */
    void (*nextSetCount)(void *self, int count);
} HaspClass;

static HaspClass g_classes[2] = {
    {"6clHasp", -1, -1, -1, 0, 1, NULL},
    {"7clHasp2", -1, -1, -1, 0, 2, NULL},
};

static void fill_block(HaspClass *cls, void *self)
{
    uint8_t *block = *(uint8_t **)self;
    if (!block)
        return;
    *(int *)(block + 0) = 0;           /* status OK */
    *(int *)(block + 4) = HASP_HANDLE; /* logged in */
    if (cls->serialHi >= 0)
        *(int *)(block + cls->serialHi) = cls->valueHi;
    if (cls->serialLo >= 0)
        *(int *)(block + cls->serialLo) = cls->valueLo;
    if (cls->count >= 0)
        *(int *)(block + cls->count) = load_count();
}

static int open0(void *self) { fill_block(&g_classes[0], self); return 1; }
static int open1(void *self) { fill_block(&g_classes[1], self); return 1; }

static void set_count0(void *self, int count)
{
    g_classes[0].nextSetCount(self, count);
    save_count(count);
}

static void set_count1(void *self, int count)
{
    g_classes[1].nextSetCount(self, count);
    save_count(count);
}

static void *const g_opens[2] = {(void *)open0, (void *)open1};
static void *const g_setCounts[2] = {(void *)set_count0, (void *)set_count1};

void wmmt3_install_hasp(void)
{
    InitializeCriticalSection(&g_lock);
    char cwd[MAX_PATH];
    GetCurrentDirectoryA(sizeof(cwd), cwd);
    snprintf(g_memoryPath, sizeof(g_memoryPath), "%s\\kitten\\hasp_memory.bin", cwd);
    snprintf(g_countPath, sizeof(g_countPath), "%s\\kitten\\hasp_card_count.txt", cwd);
    load_memory();
    parse_serial(Config.wmmt3.hasp_serial, "000000-000001", "hasp_serial",
                 &g_classes[0].valueHi, &g_classes[0].valueLo);
    parse_serial(Config.wmmt3.hasp2_serial, "000000-000002", "hasp2_serial",
                 &g_classes[1].valueHi, &g_classes[1].valueLo);

    /* 3DX+ counts HASP keys in the kernel's USB device list. */
    char usbPath[MAX_PATH];
    snprintf(usbPath, sizeof(usbPath), "%s\\kitten\\proc_bus_usb_devices.txt", cwd);
    FILE *usb = fopen(usbPath, "wb");
    if (usb)
    {
        for (int i = 0; i < 2; i++)
            fprintf(usb,
                    "T:  Bus=01 Lev=01 Prnt=01 Port=%02d Cnt=%02d Dev#=%3d Spd=1.5 MxCh= 0\n"
                    "D:  Ver= 1.10 Cls=ff(vend.) Sub=00 Prot=00 MxPS= 8 #Cfgs=  1\n"
                    "P:  Vendor=0529 ProdID=0001 Rev= 1.00\n"
                    "S:  Manufacturer=AKS\n"
                    "S:  Product=HASP HL 3.21\n"
                    "C:* #Ifs= 1 Cfg#= 1 Atr=80 MxPwr= 64mA\n"
                    "I:  If#= 0 Alt= 0 #EPs= 0 Cls=ff(vend.) Sub=00 Prot=00 Driver=(none)\n\n",
                    i, i + 1, i + 2);
        fclose(usb);
        file_redirect_path("/proc/bus/usb/devices", usbPath);
    }

    static const struct { const char *name; void *fn; } raw[] = {
        {"hasp_login", (void *)hk_login},
        {"hasp_logout", (void *)hk_logout},
        {"hasp_read", (void *)hk_read},
        {"hasp_write", (void *)hk_write},
        {"hasp_encrypt", (void *)hk_crypt},
        {"hasp_decrypt", (void *)hk_crypt},
        {"hasp_get_rtc", (void *)hk_get_rtc},
        {"hasp_legacy_set_rtc", (void *)hk_set_rtc},
        {"hasp_get_size", (void *)hk_get_size},
        {"hasp_get_sessioninfo", (void *)hk_get_sessioninfo},
        {"hasp_free", (void *)hk_free},
    };
    int rawHooked = 0;
    for (size_t i = 0; i < sizeof(raw) / sizeof(raw[0]); i++)
        rawHooked += wmmt3_hook(raw[i].name, raw[i].fn, NULL);

    for (int i = 0; i < 2; i++)
    {
        HaspClass *cls = &g_classes[i];
        char sym[96];

        snprintf(sym, sizeof(sym), "_ZN%s4openEv", cls->name);
        if (!get_native_symbol_ptr(sym))
            continue;

        snprintf(sym, sizeof(sym), "_ZNK%s11getSerialHiEv", cls->name);
        cls->serialHi = getter_offset(sym, -1);
        snprintf(sym, sizeof(sym), "_ZNK%s11getSerialLoEv", cls->name);
        cls->serialLo = getter_offset(sym, -1);
        snprintf(sym, sizeof(sym), "_ZNK%s14getCardIdCountEv", cls->name);
        cls->count = getter_offset(sym, -1);

        snprintf(sym, sizeof(sym), "_ZN%s4openEv", cls->name);
        wmmt3_hook(sym, g_opens[i], NULL);
        snprintf(sym, sizeof(sym), "_ZNK%s7isAvailEv", cls->name);
        wmmt3_hook(sym, (void *)stubRetOne, NULL);
        snprintf(sym, sizeof(sym), "_ZN%s14setCardIdCountEi", cls->name);
        wmmt3_hook(sym, g_setCounts[i], (void **)&cls->nextSetCount);

        log_info(plfWmmt3, "HASP: %s emulated (serial at +%d/+%d, card count at %+d), serial %06d-%06d",
                 cls->name + 1, cls->serialHi, cls->serialLo, cls->count,
                 cls->valueHi, cls->valueLo);
    }
    log_info(plfWmmt3, "HASP: %d runtime calls answered, memory in %s", rawHooked, g_memoryPath);
}
