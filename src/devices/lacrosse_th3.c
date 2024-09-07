/** @file
    LaCrosse Technology View LTV-TH3 & LTV-TH2 Thermo/Hygro Sensor.

    Copyright (C) 2020 Mike Bruski (AJ9X) <michael.bruski@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
LaCrosse Technology View LTV-TH3 & LTV-TH2 Thermo/Hygro Sensor.

LaCrosse Color Forecast Station (model S84060) utilizes the remote
Thermo/Hygro LTV-TH3 and LTV-WR1 multi sensor (wind spd/dir and rain).
LaCrosse Color Forecast Station (model C84343) utilizes the remote
Thermo/Hygro LTV-TH2.

Product pages:
https://www.lacrossetechnology.com/products/S84060
https://www.lacrossetechnology.com/products/ltv-th3
https://www.lacrossetechnology.com/products/C84343
https://www.lacrossetechnology.com/products/ltv-th2

Specifications:
- Outdoor Temperature Range: -40 C to 60 C
- Outdoor Humidity Range: 10 to 99 %RH
- Update Interval: Every 30 Seconds

No internal inspection of the sensors was performed so can only
speculate that the remote sensors utilize a HopeRF CMT2119A ISM
transmitter chip which is tuned to 915Mhz.

Again, no inspection of the S84060 or C84343 console was performed but
it probably utilizes a HopeRF CMT2219A ISM receiver chip.  An application
note is available that provides further info into the capabilities of the
CMT2119A and CMT2219A.

(http://www.cmostek.com/download/CMT2119A_v0.95.pdf)
(http://www.cmostek.com/download/CMT2219A.pdf)
(http://www.cmostek.com/download/AN138%20CMT2219A%20Configuration%20Guideline.pdf)

Protocol Specification:

Data bits are NRZ encoded.  Logical 1 and 0 bits are 104us in
length for the LTV-TH3 and 107us for the LTV-TH2.

LTV-TH3
    SYNC:32h ID:24h ?:4b SEQ:3b ?:1b TEMP:12d HUM:12d CHK:8h END:

    CHK is CRC-8 poly 0x31 init 0x00 over 7 bytes following SYN

LTV-TH2
    SYNC:32h ID:24h ?:4b SEQ:3b ?:1b TEMP:12d HUM:12d CHK:8h END:

Sequence# 2 & 6
    CHK is CRC-8 poly 0x31 init 0x00 over 7 bytes following SYN
Sequence# 0,1,3,4,5 & 7
    CHK is CRC-8 poly 0x31 init 0xac over 7 bytes following SYN


*/

#include "decoder.h"

static int lacrosse_th_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0xd2, 0xaa, 0x2d, 0xd4};

    data_t *data;
    uint8_t b[11];
    uint32_t id;
    int flags, seq, offset, chk3, chk2, model_num;
    int raw_temp, humidity;
    float temp_c;

    // bit length is specified as 104us for the TH3 (~256 bits per packet)
    // but the TH2 bit length is actually 107us leading the bitbuffer to
    // report the packet length as ~286 bits long.  We'll use this fact
    // to identify which of the two models actually sent the data.
    if (bitbuffer->bits_per_row[0] < 156) {
        decoder_logf(decoder, 1, __func__, "Packet too short: %d bits", bitbuffer->bits_per_row[0]);
        return DECODE_ABORT_LENGTH;
    } else if (bitbuffer->bits_per_row[0] > 290) {
        decoder_logf(decoder, 1, __func__, "Packet too long: %d bits", bitbuffer->bits_per_row[0]);
        return DECODE_ABORT_LENGTH;
    } else {
        decoder_logf(decoder, 1, __func__, "packet length: %d", bitbuffer->bits_per_row[0]);
        model_num = (bitbuffer->bits_per_row[0] < 280) ? 3 : 2;
    }

    offset = bitbuffer_search(bitbuffer, 0, 0,
            preamble_pattern, sizeof(preamble_pattern) * 8);

    if (offset >= bitbuffer->bits_per_row[0]) {
        decoder_log(decoder, 1, __func__, "Sync word not found");
        return DECODE_ABORT_EARLY;
    }

    offset += sizeof(preamble_pattern) * 8;
    bitbuffer_extract_bytes(bitbuffer, 0, offset, b, 8 * 8);

    // failing the CRC checks indicates the packet is corrupt <OR>
    // this is not a LTV-TH3 or LTV-TH2 sensor
    chk3 = crc8(b, 8, 0x31, 0x00);
    chk2 = crc8(b, 8, 0x31, 0xac);
    if (chk3 != 0 && chk2 != 0) {
        decoder_log(decoder, 1, __func__, "CRC failed!");
        return DECODE_FAIL_MIC;
    }

    id       = (b[0] << 16) | (b[1] << 8) | b[2];
    flags    = (b[3] & 0xf1); // masks off seq bits
    seq      = (b[3] & 0x0e) >> 1;
    raw_temp = b[4] << 4 | ((b[5] & 0xf0) >> 4);
    humidity = ((b[5] & 0x0f) << 8) | b[6];

    // base and/or scale adjustments
    temp_c = (raw_temp - 400) * 0.1f;

    if (humidity < 0 || humidity > 100 || temp_c < -50 || temp_c > 70)
        return DECODE_FAIL_SANITY;

    /* clang-format off */
    data = data_make(
         "model",            "",                 DATA_STRING, model_num == 3 ? "LaCrosse-TH3" : "LaCrosse-TH2",
         "id",               "Sensor ID",        DATA_FORMAT, "%06x", DATA_INT, id,
         "seq",              "Sequence",         DATA_INT,     seq,
         "flags",            "unknown",          DATA_INT,     flags,
         "temperature_C",    "Temperature",      DATA_FORMAT, "%.1f C",  DATA_DOUBLE, temp_c,
         "humidity",         "Humidity",         DATA_FORMAT, "%u %%", DATA_INT, humidity,
         "mic",              "Integrity",        DATA_STRING, "CRC",
         NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "seq",
        "flags",
        "temperature_C",
        "humidity",
        "mic",
        NULL,
};

// flex decoder n=TH3, m=FSK_PCM, s=104, l=104, r=9600
// flex decoder n=TH2, m=FSK_PCM, s=107, l=107, r=5900
// TH3 parameters should be good enough for both sensors
r_device const lacrosse_th3 = {
        .name        = "LaCrosse Technology View LTV-TH Thermo/Hygro Sensor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 104,
        .long_width  = 104,
        .reset_limit = 9600,
        .decode_fn   = &lacrosse_th_decode,
        .fields      = output_fields,
};
