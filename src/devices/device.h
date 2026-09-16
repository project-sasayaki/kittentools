#include <stdint.h>
#define MAX_DEVICE_COUNT 6

typedef enum {
    DEVICE_TYPE_NONE,
    DEVICE_TYPE_BASEBOARD,
    DEVICE_TYPE_EEPROM,
    DEVICE_TYPE_SERIAL0,
    DEVICE_TYPE_SERIAL1,
    DEVICE_TYPE_PCI_CARD_000
} DeviceTypeNew;

typedef int (*DeviceInitFunc)();
typedef int (*DeviceOpenFunc)(const char *pathname, int flags, int mode);
typedef int (*DeviceCloseFunc)(int fd);
typedef ssize_t (*DeviceReadFunc)(int fd, void *buf, size_t count);
typedef ssize_t (*DeviceWriteFunc)(int fd, const void *buf, size_t count);
typedef int (*DeviceIoctlFunc)(int fd, unsigned long request, void *data);


typedef struct {
    int fd;
    char *mountPoint;
    char *redirectPoint;
    uint8_t emulate;                      
    DeviceTypeNew type;             
    const char *name;         
    DeviceInitFunc init;   
    DeviceIoctlFunc ioctl;   
    DeviceReadFunc read;     
    DeviceWriteFunc write;   
    DeviceCloseFunc close;   
    void *privateData;           
} DeviceDescriptor;


void InitDevices();
DeviceDescriptor* getDevice(int fd);
DeviceDescriptor* getDeviceByType(DeviceTypeNew type);
int setDeviceFdByType(DeviceTypeNew type, int fd);
int attachDevice(DeviceDescriptor* device);
DeviceDescriptor* getDeviceByPath(char *path);