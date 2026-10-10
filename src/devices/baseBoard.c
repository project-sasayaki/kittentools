#include <unistd.h>
#include <winsock2.h>
#include "../config/config.h"
#include "jvs.h"
#include "../log/log.h"
#include <time.h>

#define SERIAL_STRING Config.sysconf.pcb_serial

#define IOCTL_GET_VERSION_STRING 0x8004BC02
#define IOCTL_SEND_COMMAND 0xC020BC06
#define IOCTL_WAIT_ACK 0xC020BC07

#define BC_CMD_INIT 0x300
#define BC_CMD_SHM 0x400
#define BC_CMD_SYS_INFO_GET 0x120
#define BC_CMD_SYS_FLASH_WRITE 0x180
#define BC_CMD_JVS_SENSE 0x210
#define BC_CMD_JVS_UPDATE 0x220
#define BC_CMD_SRAM_COPY_FROM 0x601
#define BC_CMD_SRAM_COPY_TO 0x600

typedef struct
{
    uint32_t cmd;
    uint32_t srcAddress;
    uint32_t srcSize;
    uint32_t destAddress;
} BC_REQ;

BC_REQ jvsCommand = {0};

typedef struct
{
    uint32_t cmd;
    uint32_t status;
    uint32_t destAddress;
    uint32_t destSize;
} BC_RET;

typedef struct
{
    uint32_t *data;
    uint32_t offset;
    uint32_t size;
} readData_t;

typedef struct
{
    uint32_t offset;
    uint32_t *data;
    uint32_t size;
} writeData_t;

#define IO_BUFFER_SIZE 1024

static FILE *sram = NULL;

static uint8_t ioBuffer[IO_BUFFER_SIZE];

static int jvsFileDescriptor = -1;
static int jvsPacketSize = -1;


int initBaseboard()
{
    sram = fopen(Config.path.sram_path, "rb+");
    if (sram == NULL)
    {
        sram = fopen(Config.path.sram_path, "w+b");
        if (sram == NULL)
        {
            log_error(plfBoot, "Cannot create sram file.");
            return -1;
        }
    }
    fseek(sram, 0, SEEK_SET);

    return 0;
}

ssize_t baseboardRead(int fd, void *buf, size_t count)
{
    memcpy(buf, ioBuffer, count);
    return count;
}

ssize_t baseboardWrite(int fd, const void *buf, size_t count)
{
    memcpy(ioBuffer, buf, count);
    return count;
}

static int processSendCommand(BC_REQ *req)
{
    switch (req->cmd)
    {
        case BC_CMD_JVS_UPDATE:
        {
            jvsCommand = *req;
            memcpy(io.m_jvsInData, ioBuffer, req->srcSize);

            if (Config.path.emulate_jvs)
            {
                JvsHandle(&jvsPacketSize);
            }
            else if (jvsFileDescriptor >= 0)
            {
                write(jvsFileDescriptor, io.m_jvsOutData, req->srcSize);

                for (uint32_t i = 0; i < req->srcSize; i++)
                {
                    if (io.m_jvsOutData[i] == 0xF0)
                    {
                        setSenseLine(3);
                    }
                    else if (io.m_jvsOutData[i] == 0xF1)
                    {
                        setSenseLine(1);
                    }
                }
            }
        }
        break;

        case BC_CMD_JVS_SENSE:
        case BC_CMD_SYS_INFO_GET:
            break;

        default:
            log_error(plfBoot, "Unknown BC_CMD %X\n", req->cmd);
    }

    req->cmd |= 0xF0000000;
    return 0;
}

static int processWaitAck(BC_RET *ret)
{
    switch (ret->cmd & 0xFFF)
    {
        case BC_CMD_SYS_INFO_GET:
            memcpy(&ioBuffer[96], SERIAL_STRING, strlen(SERIAL_STRING));
            break;

        case BC_CMD_JVS_SENSE:
            ret->destAddress = getSenseLine();
            break;

        case BC_CMD_JVS_UPDATE:
            memcpy(ioBuffer, getOutputBuffer(), jvsPacketSize);
            ret->destAddress = jvsCommand.destAddress;
            ret->destSize = jvsPacketSize;
            break;

        default:
            log_error(plfBoot, "Unknown BC_CMD %X\n", ret->cmd & 0xFFF);
    }

    ret->status = 1;
    ret->cmd |= 0xF0000000;
    return 0;
}

int baseboardIoctl(int fd, unsigned long request, void *data)
{
    switch (request)
    {

        case IOCTL_GET_VERSION_STRING:
            uint8_t versionData[4] = {0x00, 0x19, 0x20, 0x07};
            memcpy(data, versionData, 4);
            return 0;
            break;

        case BC_CMD_SRAM_COPY_FROM:
            fseek(sram, ((readData_t *)data)->offset, SEEK_SET);
            fread(((readData_t *)data)->data, 1, ((readData_t *)data)->size, sram);
            return 0;
            break;

        case BC_CMD_SRAM_COPY_TO:
            fseek(sram, ((writeData_t *)data)->offset, SEEK_SET);
            fwrite(((writeData_t *)data)->data, 1, ((writeData_t *)data)->size, sram);
            fflush(sram);
            return 0;
            break;

        case IOCTL_SEND_COMMAND:
            return processSendCommand((BC_REQ *)data);

        case IOCTL_WAIT_ACK:
            return processWaitAck((BC_RET *)data);

        case BC_CMD_INIT:
        case BC_CMD_SHM:
            break;

        default:
            log_error(plfBoot, "Unknown BC_CMD %lX\n", request);
    }

    return 0;
}