#include <string.h>

#include "device.h"
#include "../log/log.h"

#define MAX_DEVICE_COUNT 6

static int deviceCount = 0;
static DeviceDescriptor deviceTable[MAX_DEVICE_COUNT];
void InitDevices() {
    memset(deviceTable, 0, sizeof(deviceTable));
    deviceCount = 0;
}

int attachDevice(DeviceDescriptor* device)
{
    if (deviceCount >= MAX_DEVICE_COUNT)
        return -1;
    memcpy(deviceTable + deviceCount, device, sizeof(DeviceDescriptor));
    deviceCount++;
    if (device->init)
        device->init();
    return 0;
}

int setDeviceFdByType(DeviceTypeNew type, int fd)
{
    for (int i = 0; i < deviceCount; i++) {
        if (deviceTable[i].type == type) {
            deviceTable[i].fd = fd;
            return 0;
        }
    }
    return -1;    
}

DeviceDescriptor* getDeviceByPath(char *path)
{
    for (int i = 0; i < deviceCount; i++) {
        if (strcmp(deviceTable[i].mountPoint, path) == 0) {
            return &deviceTable[i];
        }
    }
    return NULL;    
}


DeviceDescriptor* getDevice(int fd) {
    for (int i = 0; i < deviceCount; i++) {
        if (deviceTable[i].fd == fd) {
            return &deviceTable[i];
        }
    }
    return NULL;
}

DeviceDescriptor* getDeviceByType(DeviceTypeNew type) {
    for (int i = 0; i < deviceCount; i++) {
        if (deviceTable[i].type == type) {
            return &deviceTable[i];
        }
    }
    return NULL;
}