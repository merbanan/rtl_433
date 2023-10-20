/** @file
    Decoder for Carchet TPMS, tested with RTL-SDR USB, Universal Radio Hacker, RTL_433 and Carchet LED display.

        Copyright (C) 2022 Karen Suhm

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */
 
/**
Carchet TPMS decoder.

The device uses OOK (ASK) encoding,
The device sends a transmission every 1 second when quick deflation is detected, every 13 - 23 sec when quick inflation is detected,
and every 4 min 40 s under steady state pressure.
A transmission starts with a preamble of 0x0000 and the packet is sent twice.

Data layout:
    CCCCCCCC IIIIIIII IIIIIIII IIIIIIII PPPPPPPP TTTTTTTT BFFF0000 00000000

- C: 8-bit checksum, modulo 256
- I: 24-bit little-endian id
- P: 8-bit little-endian Pressure (highest bit not included in checksum)
- T: 8-bit little-endian Temperature
- B: 1-bit low battery flag (not included in checksum)
- F: 3-bit status flags: 0x01 = quick deflation, 0x03 = quick inflation, 0x02 may be accel ?, 0x00 = static/steady state

*/

/**
Data collection parameters on URH software were as follows: 
    Sensor frequency: 433.92 MHz
    Sample rate: 2.0 MSps
    Bandwidth: 2.0 Hz
    Gain: 125

    Modulation is ASK (OOK). Packets in URH arrive in the following format:

    aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa [Pause: 897679 samples]
    aaaaaaaa5956a5a5a6555aaa65959999a5aaaaaa [Pause: 6030 samples]
    aaaaaaaa5956a5a5a6555aaa65959999a5aaaaaa [Pause: 11176528 samples]

    Decoding is Manchester I.  After decoding, the packets look like this:

    00000000000000000000000000000000000000
    0000de332fc0b7553000
    0000de332fc0b7553000


Using rtl_433 software, packets were detected using the following command line entry:
rtl_433 -X "n=Carchet,m=OOK_MC_ZEROBIT,s=50,l=50,r=1000,invert" -s 1M

Using these parameters, the data packets in rtl_433 were of the format 

    ffffa9332fc0a84f1000
    PPPPCCIIIIIIPPTTF000

The manufacturer's website, http://carchet.easyofficial.com/carchet-rv-trailer-car-solar-tpms-tire-pressure-monitoring-system-6-sensor-lcd-display-p6.html,
provides the following specs:

Temperature range: -19 °C ~ + 80 °C / -2F ~ 176 F
Pressure display range: 0-8Bar / 0-99 psi

*/

#include "decoder.h"

#define MYDEVICE_BITLEN     80

static int tpms_carchet_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b; // bits of a row
    int serial_id, temperature, calc_checksum, overflow, flags, low_batt, fast_leak, infl_flag;
    float pressure;
    char id_str[7];
    char flag_str[3];

    bitbuffer_invert(bitbuffer);
    //decoder_log_bitbuffer(decoder, 2, __func__, bitbuffer, "");

    /* Reject wrong length, with margin of error for extra bits at the end */
    // We expect 80 bits per packet
    if (bitbuffer->bits_per_row[0] < MYDEVICE_BITLEN || bitbuffer->bits_per_row[0] >= MYDEVICE_BITLEN + 16) {
        decoder_log(decoder, 2, __func__, "wrong packet length");
        return DECODE_ABORT_LENGTH;
    }

    /* Check preamble */
    // inverted preamble is 0xffff
    b = bitbuffer->bb[0];
    if (b[0] != 0xff || b[1] != 0xff) {
        decoder_log(decoder, 2, __func__, "invalid preamble");
        return DECODE_FAIL_SANITY;
    }

    // last byte should be zero
    if (b[9] != 0){
        decoder_log(decoder, 2, __func__, "invalid trailer");
        return DECODE_FAIL_SANITY;
    }

    /* Check message integrity (Checksum) */
    // The checksum is 8-bit modulo 256 with an overflow flag at bit 8.
    // Overflow flag is set if checksum is greater than 0xff.
    calc_checksum = b[3] + b[4] + b[5] + b[6] + b[7] + b[8];
    overflow = 0; // initialize overflow flag
    if(calc_checksum > 0xff){
        overflow = 0x80; // set overflow flag
    }
    calc_checksum = calc_checksum & 0xff; // modulo 256 checksum
    calc_checksum = calc_checksum | overflow; // sets bit 8 checksum overflow flag
    if (calc_checksum - b[2] != 0) {
        decoder_log(decoder, 2, __func__, "checksum error");
        return DECODE_FAIL_MIC; // bad checksum
    }

    /* Get the decoded data fields */
    /* CC II II II PP TT BF 00 */

    serial_id = (unsigned) b[3] << 16 | b[4] << 8 | b[5];
    snprintf(id_str, sizeof(id_str), "%06X", serial_id );

    pressure = b[6]*  0.363 - 0.06946;
    if(pressure < 0){pressure = 0;}; // 0 PSI should display non-negative

    temperature = b[7];

    flags = b[8]; // TODO: Send various flags to Carchet monitor to check for additional states
    snprintf(flag_str, sizeof(flag_str), "%02X", flags);

    low_batt = b[8] >> 7;// Low batt flag is last bit

    infl_flag = (b[8] & 0x30) >> 5; // pressure increasing

    fast_leak = 0; // reset fast_leak flag
    if(!infl_flag){
        fast_leak = (b[8] & 0x10) >> 4;
    }
    
    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, "Carchet",
            "type",             "",             DATA_STRING, "TPMS",
            "id",               "",             DATA_STRING, id_str,
            "pressure_PSI",     "pressure",     DATA_FORMAT, "%.0f PSI", DATA_DOUBLE, (double)pressure,
            "temperature_F",    "temp",         DATA_FORMAT, "%.0f F", DATA_DOUBLE, (double)temperature * 1.8 - 58,
            "flags",            "",             DATA_STRING, flag_str,
            "fast leak",        "",             DATA_INT,    fast_leak,
            "inflate",          "",             DATA_INT,    infl_flag,
            "low batt",         "",             DATA_INT,    low_batt,
            "mic",              "integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */
    decoder_output_data(decoder, data);

    // Return 1 if message successfully decoded
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "id",
        "pressure_PSI",
        "temperature_F",
        "flags",
        "fast leak",
        "inflate",
        "low batt",
        "mic",
        NULL,
};

r_device tpms_carchet = {
        .name        = "Carchet TPMS",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 50,
        .long_width  = 50,
        .gap_limit   = 300,  // some distance above long
        .reset_limit = 300,
        .decode_fn   = &tpms_carchet_decode,
        .disabled    = 0, // disabled and hidden, use 0 if there is a MIC, 1 otherwise
        .fields      = output_fields,
};
