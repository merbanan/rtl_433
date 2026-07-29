/** @file
    Roboguard (ZA:South Africa) PIR motion detection legacy security devices max 8 zones for 1 HQ(base station).
    Also works with IQ-Blue Integrated Systems (ZA:South Africa) 433.92 RF line: reed switch, radar sensor, temp alerter etc.

    Copyright (C) 2026 Cale Torino (ZR5CA)
    https://tutorials.techrad.co.za/2026/07/30/roboguard-protocol
    https://www.roboguard.co.za
    https://.iq-blue.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    ---

    Tested devices:
    - Roboguard: TXPIR 58B, CPUPIR 58B V5.8
    - IQ-Blue: Reed switch V1.3
    - IQ-Blue: Radar detector V1.1

     ______
    |______|________||_||_||_||_||_||_||_||_||_||_||_||_||_||_||_||_||_||_||_||_||_||_||_||_______________
    PREAMBLE
     _
    | |___
    LOW 1230uS
     ___
    |   |_
    HIGH 2480uS

    Examples

    heartbeat:  BIN - 0010 1101010000101111011 - DEC - 1483131 - HEX - 0x16 A17B
    alarm:      BIN - 1100 1101010000101111011 - DEC - 6726011 - HEX - 0x66 A17B
    tamper:     BIN - 0100 1101010000101111011 - DEC - 2531707 - HEX - 0x26 A17B

    remotebtn1: 0101 0010001110010101111 - 2694319 - 0x291CAF
    remotebtn2: 0101 1100001011100101010 - 3020586 - 0x2E172A
    remotebtn3: 0101 1111001100110110111 - 3119543 - 0x2F99B7
    remotebtn4: 0101 0000010111010111110 - 2633406 - 0x282EBE

    Payload: 00101101010000101111011

    Signal Type: 0010
    Device ID:   1101010000101111011

    HEX representation of 216 bits: ffffff32bd0832bd0832bd0832bd0832bd0832bd0832bd0832bd08
    Broken up into chunks :         ffffff 32bd08 32bd08 32bd08 32bd08 32bd08 32bd08 32bd08 32bd08
    preamble = ffffff
    payload =  32bd08 x 8

*/

#include "decoder.h"

#define SIGNAL_TYPES 4

static int32_t roboguard_decode(r_device *decoder, bitbuffer_t *bitbuffer, int32_t startPulses, uint16_t package_type)
{
    // initialize vars here
    data_t *data;
    uint8_t b[24]; // 24 for 192 bits, 27 for 216 bits
    uint8_t const *type_str = "";
    uint8_t filter_count    = 0;
    uint8_t filter_error    = 0;

    uint8_t const signal_types[] = {
            // 4
            0x2, // HEARTBEAT 00000010 = DEC 2
            0x4, // LEARN/TAMPER 00000100 = DEC 4
            0x5, // REMOTE 00000101 = DEC 5
            0xC, // ALARM 00001100 = DEC 12
    };

    const char *const sig_types[] = {
            // 4
            "HEARTBEAT",    // Device heartbeat signal every 15min+- if 3 are missed HQ status flashes, this is true for HQ3 V1.8
            "LEARN/TAMPER", // Device Learn or Tamper signal depends on HQ mode
            "REMOTE",       // Roboguard remote keyfob signal to arm/disarm etc.
            "ALERT",        // Both PIRs are triggered signal
    };

    bitbuffer_invert(bitbuffer); // This device sends data inverted relative to
                                 // the OOK_PWM decoder output.

    if (bitbuffer->num_rows != 1) // Only one message per transmission
    {
        return DECODE_ABORT_EARLY;
    }

    // Check correct data length
    // 72/8 = 9
    // 8x27 = 216 bits
    // first 24 bits = preamble
    // 216 bits with preamble, 192 bits without
    // 216-24 = 192 bits
    if (bitbuffer->bits_per_row[0] != 192) // 216 bits = 27 bytes,192 bits = 24 bytes 8bits = 1byte
    {
        return DECODE_ABORT_LENGTH;
    }

    bitbuffer_extract_bytes(bitbuffer, 0, 0, b, sizeof(b) * 8);

    // Signal repeats a transmission 8x so we check for 8 matches excluding the preamble, reduces error.
    // If any of the 8 don't match then reject
    // If (b[3] != b[6] || b[9] != b[12] || b[15] != b[18] || b[21] != b[24])
    // If none of the repeating signals match we know its an error
    for (uint8_t i = 0; i < 22; i += 3) {
        if (b[i + filter_count] != b[i + 3 + filter_count]) // 0 3, 6 9, 12 15, 18 21
        {
            filter_error++;
            if (filter_error > 3) // 4
            {
                // decoder_log(decoder, 1, __func__, "ALL 8 repeats failed");
                return DECODE_FAIL_MIC; // false positive
            }
        }
        filter_count + 3; // 0, 3, 6, 9, 12 15, 18 21
    }

    //
    // ffffff32bd0832bd0832bd0832bd0832bd0832bd0832bd0832bd08
    // ffffff 32bd08 32bd083 2bd083 2bd083 2bd083 2bd083 2bd083 2bd08
    // 110110100100 = 1100
    // getting data from the second repeat b[3]     b[4]     b[5]
    int32_t id       = ((b[3] & 0x0F) << 16) | (b[4] << 8) | b[5]; // combine 3 bytes to get device_id
    int32_t type_dec = (b[3] >> 4);                                // signal_type encoded in 1st 4 bits
    int32_t bit24    = (b[3] << 16) | (b[4] << 8) | b[5];          // gets the 1st full signal (we only need 1)
    int32_t bit23    = bit24 / 2;                                  // Helps the people using RC-SWITCH to see the signal, you know who you are ;)

    for (uint8_t i = 0; i < SIGNAL_TYPES; i++) // 4
    {
        // 01234
        if (signal_types[i] == type_dec) {
            type_str = sig_types[i];
        }
    }

    if (type_str == "") {
        type_str = "UNK";
    }

    // char buf[255];
    // snprintf(buf, sizeof(buf), "id:%u, sig_dec:%u, 24bit:%u, 23bit:%u, zone:%s, type:%s", id, type_dec, bit24, bit23, zone, type_str);
    // printLog((const char *)buf);

    data = data_make(
            "model", "", DATA_STRING, "RoboGuard",
            "id", "ID", DATA_INT, id,
            "type_dec", "Type DEC", DATA_INT, type_dec,
            "type_str", "Name", DATA_STRING, type_str,
            "bit24", "24-bit", DATA_INT, bit24,
            "bit23", "23-bit", DATA_INT, bit23,
            NULL);

    decoder_output_data(decoder, data, bitbuffer, 0, 0, startPulses, package_type);
    return 1;
}

static uint8_t const *const roboguard_output_fields[] = {
        "model",
        "id",
        "type_dec",
        "type_str",
        "bit24",
        "bit23",
        NULL,
};

/*

n=roboguard,
m=OOK_PWM,
s=1210,
l=2450,
r=29000,
g=0,
t=0,
y=13230

Z2
Attempting demodulation... short_width: 1220, long_width: 2460, reset_limit: 29760, sync_width: 13224
Perhaps we can find cyril's phala phala couch cash in the radio wave ethos
Use a flex decoder with -X 'n=name,m=OOK_PWM,s=1220,l=2460,r=29760,g=0,t=0,y=13224'
codes : {216}ffffff32bd6832bd6832bd6832bd6832bd6832bd6832bd6832bd68
*/

r_device const roboguard = {
        .name        = "RoboGuard",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 1230,
        .long_width  = 2480,
        .reset_limit = 20000,
        .disabled  = 0,
        .decode_fn = &roboguard_decode,
        .fields    = roboguard_output_fields,
};
