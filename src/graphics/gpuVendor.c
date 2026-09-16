#include <windows.h>
#include <dxgi.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../config/config.h"

static uint8_t gpu_vendor;
static char gpuVendorString[256];

static void classifyGPUVendor(const char *name)
{
    if (name == NULL)
        gpu_vendor = UNKNOWN_GPU;
    else if (strstr(name, "NVIDIA") != NULL)
        gpu_vendor = NVIDIA_GPU;
    else if (strstr(name, "AMD") != NULL)
        gpu_vendor = AMD_GPU;
    else if (strstr(name, "ATI") != NULL)
        gpu_vendor = ATI_GPU;
    else if (strstr(name, "Intel") != NULL)
        gpu_vendor = INTEL_GPU;
    else
        gpu_vendor = UNKNOWN_GPU;
}

void detectGPUType()
{
    IDXGIFactory *factory = NULL;
    IDXGIAdapter *adapter = NULL;
    DXGI_ADAPTER_DESC desc;

    HRESULT hr = CreateDXGIFactory(&IID_IDXGIFactory, (void **)&factory);
    if (FAILED(hr) || !factory)
    {
        fprintf(stderr, "DXGI factory creation failed: 0x%08lx\n", hr);
        gpu_vendor = ERROR_GPU;
        strcpy(gpuVendorString, "Unknown");
        return;
    }

    /* Enumerate all adapters and prefer NVIDIA if present (laptops with
     * dual GPUs typically expose the integrated Intel/AMD unit as adapter 0). */
    IDXGIAdapter *nvidiaAdapter = NULL;
    for (UINT i = 0; factory->lpVtbl->EnumAdapters(factory, i, &adapter) != DXGI_ERROR_NOT_FOUND; i++)
    {
        DXGI_ADAPTER_DESC adesc;
        if (FAILED(adapter->lpVtbl->GetDesc(adapter, &adesc)))
        {
            adapter->lpVtbl->Release(adapter);
            continue;
        }
        char name[256];
        WideCharToMultiByte(CP_UTF8, 0, adesc.Description, -1,
                            name, sizeof(name), NULL, NULL);
        if (strstr(name, "NVIDIA") != NULL)
        {
            if (nvidiaAdapter)
                nvidiaAdapter->lpVtbl->Release(nvidiaAdapter);
            nvidiaAdapter = adapter;
            break;
        }
        if (i == 0 && !nvidiaAdapter)
        {
            /* Keep adapter 0 as fallback until an NVIDIA one is found. */
            nvidiaAdapter = adapter;
            continue;
        }
        adapter->lpVtbl->Release(adapter);
    }

    adapter = nvidiaAdapter;
    if (!adapter)
    {
        fprintf(stderr, "DXGI adapter enumeration failed: no adapters\n");
        factory->lpVtbl->Release(factory);
        gpu_vendor = ERROR_GPU;
        strcpy(gpuVendorString, "Unknown");
        return;
    }

    hr = adapter->lpVtbl->GetDesc(adapter, &desc);
    if (FAILED(hr))
    {
        fprintf(stderr, "DXGI GetDesc failed: 0x%08lx\n", hr);
        adapter->lpVtbl->Release(adapter);
        factory->lpVtbl->Release(factory);
        gpu_vendor = ERROR_GPU;
        strcpy(gpuVendorString, "Unknown");
        return;
    }

    // Convert wide-char GPU description to UTF-8
    WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1,
                        gpuVendorString, sizeof(gpuVendorString), NULL, NULL);

    adapter->lpVtbl->Release(adapter);
    factory->lpVtbl->Release(factory);

    // Detect vendor from the description string
    classifyGPUVendor(gpuVendorString);
}

void setGPUVendorFromOpenGL(const char *vendor, const char *renderer)
{
    const char *displayName = renderer != NULL && renderer[0] != '\0' ? renderer : vendor;

    if (displayName == NULL)
        displayName = "Unknown";

    strncpy(gpuVendorString, displayName, sizeof(gpuVendorString) - 1);
    gpuVendorString[sizeof(gpuVendorString) - 1] = '\0';
    classifyGPUVendor(vendor != NULL ? vendor : displayName);
}

uint8_t getGPUVendorID()
{
    return gpu_vendor;
}

char *getGPUVendorName()
{
    return gpuVendorString;
}
