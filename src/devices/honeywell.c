/*
* *** Honeywell (Ademco) Door/Window Sensors (345.0Mhz) ***
*
* Tested with the Honeywell 5811 Wireless Door/Window transmitters
*
* 64 bit packets, repeated multiple times per open/close event
*
* Protocol whitepaper: "DEFCON 22: Home Insecurity" by Logan Lamb
*
* PP PP C IIIII EE SS SS
* P: 16bit Preamble and sync bit (always ff fe)
* C: 4bit Channel
* I: 20bit Device serial number / or counter value
* E: 8bit Event, where 0x80 = Open/Close, 0x04 = Heartbeat / or id
* S: 16bit CRC
*
* additional help from https://github.com/jhaines0/HoneywellSecurity/blob/18a4ac82e79cca532b1eadb33327d49a73ce7df1/rpi/digitalDecoder.cpp
*/

#include "rtl_433.h"
#include "pulse_demod.h"
#include "util.h"

#define DEVICE_NAME "Honeywell/Ademco Portal/Motion Sensor"

static uint8_t motion_detector_count = 0;
static uint32_t motion_detectors[0xFF];

static int is_motion_detector(uint32_t id) {
    for(uint8_t i = 0; i < motion_detector_count; i++) {
        if(motion_detectors[i] == id) return 1;
    }
    return 0;
}
static void add_motion_detector(int id) {
    if(!is_motion_detector(id)) motion_detectors[motion_detector_count++] = id;
}

static int honeywell_callback(bitbuffer_t *bitbuffer) {
    char time_str[LOCAL_TIME_BUFLEN];
    const uint8_t *bb;
    uint8_t channel;
    uint32_t device_id;
    uint8_t event;
    uint8_t state;
    uint8_t heartbeat;
    uint8_t tamper;
    uint8_t low_batt;
    uint16_t crc_calculated;
    uint16_t crc;

    if(bitbuffer->num_rows != 1 || bitbuffer->bits_per_row[0] != 64)
        return 0; // Unrecognized data

    for(uint16_t i=0; i < 8; i++)
        bitbuffer->bb[0][i] = ~bitbuffer->bb[0][i];

    bb = bitbuffer->bb[0];

    crc_calculated = crc16_ccitt(bb, 6, 0x8005, 0xfffe);
    crc = (((uint16_t) bb[6]) << 8) + ((uint16_t) bb[7]);
    if(crc != crc_calculated)
        return 0; // Not a valid packet

    channel = bb[2] >> 4;
    device_id = ((bb[2] & 0xf) << 16) | (bb[3] << 8)| bb[4];

    event = bb[5];

    if(event == 0x20) add_motion_detector(device_id);

    state = event & (is_motion_detector(device_id) ?  0x80 : 0x20);
    heartbeat = (event & 0x04);
    low_batt = (event & 0x02);
    tamper = (event & 0x40);

    local_time_str(0, time_str);

    data_t *data = data_make(
          "time",     "", DATA_STRING, time_str,
          "model", "", DATA_STRING, DEVICE_NAME,
          "id", "ID", DATA_FORMAT, "%05d", DATA_INT, device_id,
          "channel", "Channel", DATA_INT, channel,
          "event", "Event", DATA_FORMAT, "%02x", DATA_INT, event,
          "triggered", "Triggered", DATA_STRING, state ? "true" : "false",
          "heartbeat" , "Hearbeat", DATA_STRING, heartbeat ? "true" : "false",
          "battery","Low Battery", DATA_STRING, low_batt ? "true" : "false",
          "tamper","Tampering Detected", DATA_STRING, tamper ? "true" : "false",
          NULL);

    data_acquired_handler(data);
    return 1;
}

static char *output_fields[] = {
    "time",
    "model",
    "id",
    "channel",
    "event",
    "triggered",
    "heartbeat",
    "battery",
    "tamper",
    NULL
};

r_device honeywell = {
    .name                   = DEVICE_NAME,
    .modulation             = OOK_PULSE_MANCHESTER_ZEROBIT,
    .short_limit    =   39 * 4,
    .long_limit             = 0,
    .reset_limit    = 73 * 4,
    .json_callback  = &honeywell_callback,
    .disabled               = 0,
    .demod_arg              = 0,
    .fields                 = output_fields,
};
