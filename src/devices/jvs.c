#include <math.h>
#include <pthread.h>

#include "jvs.h"
#include "../config/config.h"
#include "../log/log.h"

static JVSStatus JvsDecode();
static JVSStatus JvsEncode(JVSPacket *packet, int* packetSize);

/* The in and out buffer used to read and write to and from */
unsigned char outputBuffer[JVS_MAX_PACKET_SIZE];

unsigned char *getOutputBuffer()
{
    return outputBuffer;
}
/* Holds the status of the sense line */
int senseLine = 3;

pthread_mutex_t jvsMutex = PTHREAD_MUTEX_INITIALIZER;

JVSIO io = {0};

static inline uint8_t JvsRead() {
    if (io.m_nInIdx < JVS_MAX_PACKET_SIZE) return io.m_jvsInData[io.m_nInIdx++];
    return 0xff;
}

static inline void JvsWrite(uint8_t data)
{
    if (io.m_nOutIdx < JVS_MAX_PACKET_SIZE) 
        ((uint8_t *)io.m_jvsOutData)[io.m_nOutIdx++] = data;
}

static inline void JvsWrite16(uint16_t data)
{
    JvsWrite((data >> 8) & 0xff);
    JvsWrite(data & 0xff);
}

static void cmd_read_analogs() {
    uint16_t analogs[8];
    uint8_t nanalogs = (uint8_t)JvsRead();

    JvsWrite(REPORT_SUCCESS);
    memset(analogs, 0, sizeof(analogs));
    if (io.ops->read_analogs) {
        io.ops->read_analogs(analogs, nanalogs);
    }

    for (uint8_t i = 0; i < nanalogs; i++) {
        JvsWrite16(analogs[i]);
    }
}

static void cmd_read_coins() {
    uint8_t coin_count = (uint8_t)JvsRead();
    JvsWrite(REPORT_SUCCESS);
    uint16_t ncoins;
    for (uint8_t slot = 0; slot < coin_count; slot++) {
        ncoins = 0;
        if (io.ops->read_coin_counter)
            io.ops->read_coin_counter(slot, &ncoins);
        JvsWrite16(ncoins);
    }
}

static JVSStatus cmd_read_switches() {
    uint8_t players = (uint8_t)JvsRead();
    uint8_t switch_bytes = (uint8_t)JvsRead();
    struct io3_switch_state state;

    if (players > io.capabilities.players || switch_bytes != 2) {
        JvsWrite(REPORT_PARAMETER_ERROR2);
    }

    JvsWrite(REPORT_SUCCESS);

    memset(&state, 0, sizeof(state));
    if (io.ops->read_switches)
        io.ops->read_switches(&state);

    JvsWrite(state.system);

    if (players > 0)
        JvsWrite16(state.p1);
    
    if (players > 1)
        JvsWrite16(state.p2);

    return JVS_STATUS_SUCCESS;
}

int initJVS(JVSIOType type)
{
    switch (type)
    {
    case SEGA_TYPE_1:
    {
        io.capabilities.switches = 13;
        io.capabilities.coins = 2;
        io.capabilities.players = 2;
        io.capabilities.analogueInBits = 8;
        io.capabilities.rightAlignBits = 0;
        io.capabilities.analogueInChannels = 8;
        io.capabilities.generalPurposeOutputs = 6;
        io.capabilities.commandVersion = 17;
        io.capabilities.jvsVersion = 48;
        io.capabilities.commsVersion = 16;
        strcpy(io.capabilities.name, "SEGA ENTERPRISESLTD.;I/O BD JVS;837-13551;Ver1.00;98/10");
    }
    break;

    default:
    case SEGA_TYPE_3:
    {
        io.capabilities.switches = 14;
        io.capabilities.coins = 2;
        io.capabilities.players = 2;
        io.capabilities.analogueInBits = 10;
        io.capabilities.rightAlignBits = 0;
        io.capabilities.analogueInChannels = 8;
        io.capabilities.generalPurposeOutputs = 20;
        io.capabilities.commandVersion = 19;
        io.capabilities.jvsVersion = 48;
        io.capabilities.commsVersion = 16;
        strcpy(io.capabilities.name, "SEGA CORPORATION;I/O BD JVS;837-14572;Ver1.00;2005/10");
    }
    break;
    }

    for (int analogueChannels = 0; analogueChannels < io.capabilities.analogueInChannels; analogueChannels++)
        io.state.analogueChannel[analogueChannels] = 0;

    for (int rotaryChannels = 0; rotaryChannels < io.capabilities.rotaryChannels; rotaryChannels++)
        io.state.rotaryChannel[rotaryChannels] = 0;

    for (int player = 0; player < io.capabilities.coins; player++)
        io.state.coinCount[player] = 0;

    senseLine = 3;
    log_info(plfJvs, "JVS initialized");
    return 0;
}

void writeFeature(char capability, char arg0, char arg1, char arg2)
{
    JvsWrite(capability);
    JvsWrite(arg0);
    JvsWrite(arg1);
    JvsWrite(arg2);
}

void writeFeatures(JVSCapabilities *capabilities)
{
    /* Input Functions */

    if (capabilities->players)
        writeFeature(CAP_PLAYERS, capabilities->players, capabilities->switches, 0x00);

    if (capabilities->coins)
        writeFeature(CAP_COINS, capabilities->coins, 0x00, 0x00);

    if (capabilities->analogueInChannels)
        writeFeature(CAP_ANALOG_IN, capabilities->analogueInChannels, capabilities->analogueInBits, 0x00);

    if (capabilities->rotaryChannels)
        writeFeature(CAP_ROTARY, capabilities->rotaryChannels, 0x00, 0x00);

    if (capabilities->keypad)
        writeFeature(CAP_KEYPAD, 0x00, 0x00, 0x00);

    /* Output Functions */
    if (capabilities->generalPurposeOutputs)
        writeFeature(CAP_GPO, capabilities->generalPurposeOutputs, 0x00, 0x00);
    
    JvsWrite(CAP_END);
}

JVSStatus JvsHandle(int *packetSize)
{
    JvsDecode();

    JVSPacket *inPkt = (JVSPacket *)&io.m_jvsInData;
    JVSPacket *outPkt = (JVSPacket *)&io.m_jvsOutData;
    outPkt->m_Dst = BUS_MASTER;

    /* Set the entire packet success line */
    JvsWrite(STATUS_SUCCESS);

    pthread_mutex_lock(&jvsMutex);

    while (io.m_nInIdx < inPkt->m_nBytes + 2)
    {
        uint8_t cmd = JvsRead();
        switch (cmd)
        {

            case CMD_RESET:
            {
                io.deviceID = -1;
                senseLine = 3;
            }
            break;
            
            case CMD_RESET_ARG:
            break;

            case CMD_ASSIGN_ADDR:
            {
                io.deviceID = JvsRead();
                JvsWrite(REPORT_SUCCESS);
                senseLine = 1;
            }
            break;

            case CMD_REQUEST_ID:
            {
                JvsWrite(REPORT_SUCCESS);
                for (size_t i = 0; i < strlen(io.capabilities.name) + 1; i++)
                    JvsWrite(io.capabilities.name[i]);
            }
            break;

            case CMD_COMMAND_VERSION:
            {
                JvsWrite(REPORT_SUCCESS);
                JvsWrite(io.capabilities.commandVersion);
            }
            break;

            case CMD_JVS_VERSION:
            {
                JvsWrite(REPORT_SUCCESS);
                JvsWrite(io.capabilities.jvsVersion);
            }
            break;

            case CMD_COMMS_VERSION:
            {
                JvsWrite(REPORT_SUCCESS);
                JvsWrite(io.capabilities.commsVersion);
            }
            break;

            case CMD_CAPABILITIES:
            {
                JvsWrite(REPORT_SUCCESS);
                writeFeatures(&io.capabilities);
            }
            break;

            case CMD_READ_SWITCHES:
            {
                cmd_read_switches();
            }
            break;

            case CMD_READ_COINS:
            {
                cmd_read_coins();
            }
            break;

            case CMD_READ_ANALOGS:
            {
                cmd_read_analogs();
            }
            break;

            case CMD_READ_ROTARY:
            {
                JvsWrite(REPORT_SUCCESS);
                uint8_t rotaryCount = JvsRead();
                for (int i = 0; i < rotaryCount; i++)
                {
                    JvsWrite16(io.state.rotaryChannel[i]);
                }
            }
            break;

            case CMD_READ_KEYPAD:
            {
                JvsWrite(REPORT_SUCCESS);
                JvsWrite(0x00);
            }
            break;

            case CMD_READ_GPI:
            {
                uint8_t gpiCount = JvsRead();
                JvsWrite(REPORT_SUCCESS);
                for (int i = 0; i < gpiCount; i++)
                {
                    JvsWrite(0x00);
                }
            }
            break;

            case CMD_REMAINING_PAYOUT:
            {
                JvsWrite(REPORT_SUCCESS);
                JvsWrite16(0);
                JvsWrite(0);
            }
            break;

            case CMD_WRITE_GPO:
            {
                uint8_t gpoCount = JvsRead();
                for (int i = 0; i < gpoCount; i++)
                {
                    JvsRead();
                }
                JvsWrite(REPORT_SUCCESS);
            }
            break;
            case CMD_SET_PAYOUT:
            case CMD_WRITE_GPO_BYTE:
            case CMD_WRITE_GPO_BIT:
            case CMD_WRITE_ANALOG:
            case CMD_SUBTRACT_PAYOUT:
            case CMD_WRITE_COINS:
            case CMD_WRITE_DISPLAY:
            {
                JvsWrite(REPORT_SUCCESS);
            }
            break;

            case CMD_DECREASE_COINS:
            {
                // - 1 because JVS is 1-indexed, but our array is 0-indexed
                int slot_index = JvsRead() - 1;
                uint8_t cAmount[2];
                cAmount[0] = JvsRead();
                cAmount[1] = JvsRead();
                int coin_decrement = ((int)(cAmount[1]) | ((int)(cAmount[0]) << 8));

                JvsWrite(REPORT_SUCCESS);

                /* Prevent underflow of coins */
                if (coin_decrement > io.state.coinCount[slot_index])
                    coin_decrement = io.state.coinCount[slot_index];
                io.state.coinCount[slot_index] -= coin_decrement;
            }
            break;

            default:
            {
                log_error(plfJvs, "JVS command not supported [0x%02hhX]\n",cmd);
            }
        }
    }

    pthread_mutex_unlock(&jvsMutex);

    JvsEncode(outPkt, packetSize);

    return JVS_STATUS_SUCCESS;
}

JVSStatus JvsDecode()
{
    uint8_t checksum = 0x00;
    JVSPacket *packet = (JVSPacket *)&io.m_jvsInData;
    bool escape = false;

    if (packet->m_Sync != SYNC)
    {
        log_warn(plfJvs, "JVS Recv Packet: missing SYNC byte");
    }

    if ( !packet->m_nBytes )
    {
        log_error(plfJvs, "JVS Packet: invalid packet length %d", packet->m_nBytes);
        return JVS_STATUS_ERROR;
    }

    if (packet->m_Dst != BROADCAST && packet->m_Dst != io.deviceID)
        return JVS_STATUS_NOT_FOR_US;

    for (int i = 1; i < packet->m_nBytes + 2; i++)
    {
        checksum += io.m_jvsInData[i];
        if (io.m_jvsInData[i] == ESCAPE)
        {
            if (escape)
            {
                log_error(plfJvs, "JVS Packet: ESCAPE fault");
                return JVS_STATUS_ERROR;
            }
            escape = true;
        }
        else
        {
            if (escape)
            {
                escape = false;
                io.m_jvsInData[i]++;
            }
        }
    }

    if (checksum != io.m_jvsInData[packet->m_nBytes + 2])
    {
        log_error(plfJvs, "JVS Packet: invalid checksum %02x, expected %02x", checksum, io.m_jvsInData[packet->m_nBytes + 2]);
        return JVS_STATUS_ERROR_CHECKSUM;
    }

    io.m_nOutIdx = 3;
    io.m_nInIdx = 3;

    return JVS_STATUS_SUCCESS;
}

JVSStatus JvsEncode(JVSPacket *packet, int *packetSize)
{
    /* Add SYNC and reset buffer */
    int checksum = 0;
    int outputIndex = 1;
    outputBuffer[0] = SYNC;
    
    packet->m_nBytes = io.m_nOutIdx - 2;

    /* Write out entire packet */
    for (int i = 1; i < io.m_nOutIdx; i++)
    {
        if (io.m_jvsOutData[i] == SYNC || io.m_jvsOutData[i] == ESCAPE)
        {
            outputBuffer[outputIndex++] = ESCAPE;
            outputBuffer[outputIndex++] = (io.m_jvsOutData[i] - 1);
        }
        else
        {
            outputBuffer[outputIndex++] = (io.m_jvsOutData[i]);
        }
        checksum = (checksum + io.m_jvsOutData[i]) & 0xFF;
    }

    /* Write out escaped checksum */
    if (checksum == SYNC || checksum == ESCAPE)
    {
        outputBuffer[outputIndex++] = ESCAPE;
        outputBuffer[outputIndex++] = (checksum - 1);
    }
    else
    {
        outputBuffer[outputIndex++] = checksum;
    }

    // Communicate the output size based on the now escaped bytes
    *packetSize = outputIndex;

    return JVS_STATUS_SUCCESS;
}

int getSenseLine()
{
    return senseLine;
}

void setSenseLine(int _senseLine)
{
    senseLine = _senseLine;
}

JVSIO *getJVSIO()
{
    return &io;
}