#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include "../config/config.h"


#define JVS_RETRY_COUNT 3
#define JVS_MAX_PACKET_SIZE 255


#define DEVICE_ID 0x01

#define SYNC 0xE0
#define ESCAPE 0xD0
#define BROADCAST 0xFF
#define BUS_MASTER 0x00
#define DEVICE_ADDR_START 0x01

/* Status for the entire packet */
#define STATUS_SUCCESS 0x01
#define STATUS_UNSUPPORTED 0x02      // an unsupported command was sent
#define STATUS_CHECKSUM_FAILURE 0x03 // the checksum on the command packet did not match a computed checksum
#define STATUS_OVERFLOW 0x04         // an overflow occurred while processing the command

/* Reporting for each individual command */
#define REPORT_SUCCESS 0x01          // all went well
#define REPORT_PARAMETER_ERROR1 0x02 // TODO: work out difference between this one and the next
#define REPORT_PARAMETER_ERROR2 0x03
#define REPORT_BUSY 0x04 // some attached hardware was busy, causing the request to fail

/* All the commands */
#define CMD_RESET 0xF0            // reset bus
#define CMD_RESET_ARG 0xD9        // fixed argument to reset command
#define CMD_ASSIGN_ADDR 0xF1      // assign address to slave
#define CMD_SET_COMMS_MODE 0xF2   // switch communications mode for devices that support it, for compatibility
#define CMD_REQUEST_ID 0x10       // requests an ID string from a device
#define CMD_COMMAND_VERSION 0x11  // gets command format version as two BCD digits packed in a byte
#define CMD_JVS_VERSION 0x12      // gets JVS version as two BCD digits packed in a byte
#define CMD_COMMS_VERSION 0x13    // gets communications version as two BCD digits packed in a byte
#define CMD_CAPABILITIES 0x14     // gets a special capability structure from the device
#define CMD_CONVEY_ID 0x15        // convey ID of main board to device
#define CMD_READ_SWITCHES 0x20    // read switch inputs
#define CMD_READ_COINS 0x21       // read coin inputs
#define CMD_READ_ANALOGS 0x22     // read analog inputs
#define CMD_READ_ROTARY 0x23      // read rotary encoder inputs
#define CMD_READ_KEYPAD 0x24      // read keypad inputs
#define CMD_READ_LIGHTGUN 0x25    // read light gun inputs
#define CMD_READ_GPI 0x26         // read general-purpose inputs
#define CMD_RETRANSMIT 0x2F       // ask device to retransmit data
#define CMD_DECREASE_COINS 0x30   // decrease number of coins
#define CMD_WRITE_GPO 0x32        // write to general-purpose outputs
#define CMD_WRITE_ANALOG 0x33     // write to analog outputs
#define CMD_WRITE_DISPLAY 0x34    // write to an alphanumeric display
#define CMD_WRITE_COINS 0x35      // add to coins
#define CMD_REMAINING_PAYOUT 0x2E // read remaining payout
#define CMD_SET_PAYOUT 0x31       // write remaining payout
#define CMD_SUBTRACT_PAYOUT 0x36  // subtract from remaining payout
#define CMD_WRITE_GPO_BYTE 0x37   // write single gpo byte
#define CMD_WRITE_GPO_BIT 0x38    // write single gpo bit

/* Manufacturer specific commands */
#define CMD_MANUFACTURER_START 0x60 // start of manufacturer-specific commands
#define CMD_NAMCO_SPECIFIC 0x70
#define CMD_MANUFACTURER_END 0x7F // end of manufacturer-specific commands

/* Capabilities of the IO board */
#define CAP_END 0x00        // end of structure
#define CAP_PLAYERS 0x01    // player/switch info
#define CAP_COINS 0x02      // coin slot info
#define CAP_ANALOG_IN 0x03  // analog info
#define CAP_ROTARY 0x04     // rotary encoder info
#define CAP_KEYPAD 0x05     // keypad info
#define CAP_LIGHTGUN 0x06   // light gun info
#define CAP_GPI 0x07        // general purpose input info
#define CAP_CARD 0x10       // card system info
#define CAP_HOPPER 0x11     // token hopper info
#define CAP_GPO 0x12        // general purpose output info
#define CAP_ANALOG_OUT 0x13 // analog output info
#define CAP_DISPLAY 0x14    // character display info
#define CAP_BACKUP 0x15     // backup memory

#define ENCODINGS [ "unknown", "ascii numeric", "ascii alphanumeric", "alphanumeric/katakana", "alphanumeric/SHIFT-JIS" ]

#define JVS_MAX_STATE_SIZE 100
#define MAX_JVS_NAME_SIZE 2048


typedef struct
{
    uint16_t coinCount[JVS_MAX_STATE_SIZE];
    int analogueChannel[JVS_MAX_STATE_SIZE];
    int rotaryChannel[JVS_MAX_STATE_SIZE];
} JVSState;

typedef struct
{
    char name[MAX_JVS_NAME_SIZE];
    unsigned char commandVersion;
    unsigned char jvsVersion;
    unsigned char commsVersion;
    unsigned char players;
    unsigned char switches;
    unsigned char coins;
    unsigned char analogueInChannels;
    unsigned char analogueInBits;
    unsigned char rotaryChannels;
    unsigned char keypad;
    unsigned char card;
    unsigned char hopper;
    unsigned char generalPurposeOutputs;
    unsigned char backup;
    unsigned char rightAlignBits;
    char displayName[MAX_JVS_NAME_SIZE];
} JVSCapabilities;

struct io3_switch_state {
    uint8_t system;
    uint16_t p1;
    uint16_t p2;
};

typedef struct JVS_OPS {
    void (*reset)();
    void (*write_gpio)(uint32_t state);
    void (*read_switches)(struct io3_switch_state *out);
    void (*read_analogs)(uint16_t *analogs, uint8_t nanalogs);
    void (*read_rotarys)(uint16_t *rotaries, uint8_t nrotaries);
    void (*read_coin_counter)(uint8_t slot_no, uint16_t *out);
}JVS_OPS;

typedef struct
{
    uint8_t m_Sync;
    uint8_t m_Dst;
    uint8_t m_nBytes;
    uint8_t m_Data[JVS_MAX_PACKET_SIZE];
} JVSPacket;

typedef struct JVSIO
{
    uint8_t m_jvsInData[JVS_MAX_PACKET_SIZE];
    uint8_t m_nInIdx;
    uint8_t m_jvsOutData[JVS_MAX_PACKET_SIZE];
    uint8_t m_nOutIdx;

    JVS_OPS *ops;
    int deviceID;
    int analogueRestBits;
    JVSState state;
    JVSCapabilities capabilities;
    struct JVSIO *chainedIO;
} JVSIO;



typedef enum
{
    JVS_STATUS_SUCCESS,
    JVS_STATUS_NOT_FOR_US,
    JVS_STATUS_ERROR,
    JVS_STATUS_ERROR_TIMEOUT,
    JVS_STATUS_ERROR_CHECKSUM,
    JVS_STATUS_ERROR_WRITE_FAIL,
    JVS_STATUS_ERROR_UNSUPPORTED_COMMAND,
} JVSStatus;

int initJVS(JVSIOType type);

JVSStatus JvsHandle(int* packetSize);

/* The in and out packets used to read and write to and from*/
extern JVSIO io;

unsigned char *getOutputBuffer();

int getSenseLine();
void setSenseLine(int senseLine);

JVSIO* getJVSIO();