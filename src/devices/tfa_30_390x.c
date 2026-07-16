/** @file
    TFA Dostmann ID-AX series temperature/humidity sensors.

    Copyright (c) 2026 Jacob Maxa <jack77@gmx.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
TFA Dostmann ID-AX series temperature/humidity sensors.

Covers 5 sensors from TFA's cloud-connected "ID+" series, all using the
same FSK-PCM protocol at 868.025 MHz and differing only in which fields
are populated:

- TFA 30.3901.02 (ID-A5): internal temperature
- TFA 30.3902.02 (ID-A3): internal + external temperature
- TFA 30.3905.02 (ID-A4): internal + external temperature + humidity
- TFA 30.3906.02 (ID-A6): internal temperature + humidity
- TFA 30.3908.02 (ID-A0): internal temperature + humidity, big display

Data layout, following a 32 bit sync word (0x4b2dd42b):

    LL IIII SS CC F1 F1 (F1) F1 F2 F2 (F2) F2 F3 F3 (F3) F3 XXXX

- LL: 8 bit payload length in bytes, counted from this byte through the CRC
- I: 32 bit device identifier, printed on the back of the sensor; its
  first byte also encodes the model variant (0xA0/0xA3/0xA4/0xA5/0xA6)
- S: 8 bit status: bit 3 is battery low, bit 1 is a manual/button transmit
- C: 16 bit little-endian up-counter, incremented every transmission
- F1/F2/F3: the current reading followed by the previous two readings, so
  a lost packet can be recovered from the next one using the counter to
  detect the gap. Each reading is, little-endian, depending on model:
    - ID-A0/A6: 16 bit temperature, 16 bit humidity, 16 bit offset (unused here)
    - ID-A3: 16 bit temperature, 16 bit external temperature, 16 bit offset
    - ID-A4: 16 bit temperature, 16 bit humidity, 16 bit external
      temperature, 16 bit offset
    - ID-A5: 16 bit temperature, 16 bit offset
  Temperature is sign-extended from the low 11 bits and scaled by 10;
  the external temperature on ID-A4 is sign-extended from 12 bits instead
  (a wider range). Humidity is unsigned and scaled by 10.
- X: 32 bit little-endian CRC-32 (poly 0x04c11db7, reflected, init/xorout
  0xffffffff) over every byte from LL to the last data byte.

Based on the protocol reverse engineering and byte layout from
https://github.com/merbanan/rtl_433/pull/3446 (Jacob Maxa), with fixes for:
- the preamble/length bounds check, which compared the found sync offset
  against a fixed bit-length constant instead of the actual row length,
  and so could wrongly reject a valid frame preceded by other data, or
  wrongly accept one with too little row left to hold a full frame
- the humidity field losing its decimal digit through a plain int cast
- reporting the model name as a fixed string per variant, as required by
  tests/symbolizer.py (the original passed a runtime-built string on the
  "model" data line)

No raw .cu8 captures were available to verify this decoder against
(searched both this repo and rtl_433_tests), but the PR's own doc-comment
included 6 worked examples (hand-transcribed hex, for ID-A0/A3/A5 and
three sequential ID-A4 readings) that all validate against the CRC-32
above -- real device data, not fabricated. Decoding them here also gives
internally consistent sliding-window temperatures across the 3 ID-A4
frames (each one's "current" reading matches the next frame's "1 reading
ago", etc.), further confirming the field layout. No genuine ID-A6
example exists in the PR text (it duplicates the ID-A0 example under an
ID-A6 heading by copy-paste, but the ID byte says ID-A0); the ID-A6 test
fixture is a synthetic frame instead.
*/

#include "decoder.h"

#define TFA_30390X_SYNC_BITLEN 32
#define TFA_30390X_MAX_LEN     36 // ID-A3/A4, the longest frame variant

static uint32_t tfa_30390x_crc32(uint8_t const *msg, unsigned num_bytes)
{
    uint32_t crc = 0xffffffff;
    for (unsigned n = 0; n < num_bytes; n++) {
        crc ^= msg[n];
        for (int i = 0; i < 8; i++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xedb88320 : crc >> 1;
        }
    }
    return crc ^ 0xffffffff;
}

static int tfa_30_390x_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const sync[] = {0x4b, 0x2d, 0xd4, 0x2b};

    unsigned bitpos = bitbuffer_search(bitbuffer, 0, 0, sync, TFA_30390X_SYNC_BITLEN);
    if (bitpos + TFA_30390X_SYNC_BITLEN + 8 > bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_LENGTH; // no sync found, or not even enough bits for the length byte
    }

    uint8_t len;
    bitbuffer_extract_bytes(bitbuffer, 0, bitpos + TFA_30390X_SYNC_BITLEN, &len, 8);
    if (len != 24 && len != 30 && len != 36) {
        return DECODE_ABORT_LENGTH; // not one of the known frame lengths
    }
    if (bitpos + TFA_30390X_SYNC_BITLEN + (unsigned)len * 8 > bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_LENGTH; // not enough bits left for the full frame
    }

    uint8_t b[TFA_30390X_MAX_LEN + TFA_30390X_SYNC_BITLEN / 8];
    bitbuffer_extract_bytes(bitbuffer, 0, bitpos, b, TFA_30390X_SYNC_BITLEN + (unsigned)len * 8);

    uint32_t crc_calc  = tfa_30390x_crc32(&b[4], len - 4);
    uint32_t crc_frame = ((uint32_t)b[len + 3] << 24) | ((uint32_t)b[len + 2] << 16)
            | ((uint32_t)b[len + 1] << 8) | b[len];
    if (crc_calc != crc_frame) {
        decoder_log(decoder, 1, __func__, "CRC fail");
        return DECODE_FAIL_MIC;
    }

    if (!b[5] && !b[6] && !b[7] && !b[8]) {
        return DECODE_FAIL_SANITY; // all-zero id
    }

    char id_str[9];
    snprintf(id_str, sizeof(id_str), "%02X%02X%02X%02X", b[5], b[6], b[7], b[8]);

    int battery_ok       = !(b[9] & 0x08);
    int manual_transmit  = (b[9] & 0x02) >> 1;
    int seq_number       = b[10] | (b[11] << 8);

    double temp_c[3];
    double ext_c[3];
    double humidity[3];

    /* clang-format off */
    switch (b[5]) {
    // ID-A0 and ID-A6 share an identical frame layout and only differ in model name.
    case 0xa0:
    case 0xa6: {
        if (len != 30) {
            return DECODE_FAIL_SANITY; // length doesn't match this model's frame
        }
        for (int k = 0; k < 3; k++) {
            temp_c[k]   = (((int16_t)((b[12 + k * 6] | (b[12 + k * 6 + 1] << 8)) << 5)) >> 5) * 0.1;
            humidity[k] = (b[14 + k * 6] | (b[14 + k * 6 + 1] << 8)) * 0.1;
        }
        data_t *data = data_make(
                "model",            "", DATA_COND, b[5] == 0xa0, DATA_STRING, "TFA-303908",
                "model",            "", DATA_COND, b[5] == 0xa6, DATA_STRING, "TFA-303906",
                "id",               "",                 DATA_STRING, id_str,
                "battery_ok",       "Battery OK",       DATA_INT,    battery_ok,
                "manual_transmit",  "Manual Transmit",  DATA_INT,    manual_transmit,
                "seq_number",       "Sequence Number",  DATA_INT,    seq_number,
                "temperature_C",    "Temperature",      DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c[0],
                "temperature_C_last", "Temp. last",     DATA_ARRAY,  data_array(3, DATA_DOUBLE, temp_c),
                "humidity",         "Humidity",         DATA_FORMAT, "%.1f %%", DATA_DOUBLE, humidity[0],
                "humidity_last",    "Humidity last",    DATA_ARRAY,  data_array(3, DATA_DOUBLE, humidity),
                "mic",              "Integrity",        DATA_STRING, "CRC",
                NULL);
        decoder_output_data(decoder, data);
        return 1;
    }
    case 0xa3: {
        if (len != 30) {
            return DECODE_FAIL_SANITY; // length doesn't match this model's frame
        }
        for (int k = 0; k < 3; k++) {
            temp_c[k] = (((int16_t)((b[12 + k * 6] | (b[12 + k * 6 + 1] << 8)) << 5)) >> 5) * 0.1;
            ext_c[k]  = (((int16_t)((b[14 + k * 6] | (b[14 + k * 6 + 1] << 8)) << 5)) >> 5) * 0.1;
        }
        data_t *data = data_make(
                "model",            "",                 DATA_STRING, "TFA-303902",
                "id",               "",                 DATA_STRING, id_str,
                "battery_ok",       "Battery OK",       DATA_INT,    battery_ok,
                "manual_transmit",  "Manual Transmit",  DATA_INT,    manual_transmit,
                "seq_number",       "Sequence Number",  DATA_INT,    seq_number,
                "temperature_C",    "Temperature",      DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c[0],
                "temperature_C_last", "Temp. last",     DATA_ARRAY,  data_array(3, DATA_DOUBLE, temp_c),
                "temperature_C_ext", "Temperature ext.", DATA_FORMAT, "%.1f C", DATA_DOUBLE, ext_c[0],
                "temperature_C_ext_last", "Temp. ext. last", DATA_ARRAY, data_array(3, DATA_DOUBLE, ext_c),
                "mic",              "Integrity",        DATA_STRING, "CRC",
                NULL);
        decoder_output_data(decoder, data);
        return 1;
    }
    case 0xa4: {
        if (len != 36) {
            return DECODE_FAIL_SANITY; // length doesn't match this model's frame
        }
        for (int k = 0; k < 3; k++) {
            temp_c[k]   = (((int16_t)((b[12 + k * 8] | (b[12 + k * 8 + 1] << 8)) << 4)) >> 4) * 0.1;
            humidity[k] = (b[14 + k * 8] | (b[14 + k * 8 + 1] << 8)) * 0.1;
            ext_c[k]    = (((int16_t)((b[16 + k * 8] | (b[16 + k * 8 + 1] << 8)) << 4)) >> 4) * 0.1;
        }
        data_t *data = data_make(
                "model",            "",                 DATA_STRING, "TFA-303905",
                "id",               "",                 DATA_STRING, id_str,
                "battery_ok",       "Battery OK",       DATA_INT,    battery_ok,
                "manual_transmit",  "Manual Transmit",  DATA_INT,    manual_transmit,
                "seq_number",       "Sequence Number",  DATA_INT,    seq_number,
                "temperature_C",    "Temperature",      DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c[0],
                "temperature_C_last", "Temp. last",     DATA_ARRAY,  data_array(3, DATA_DOUBLE, temp_c),
                "humidity",         "Humidity",         DATA_FORMAT, "%.1f %%", DATA_DOUBLE, humidity[0],
                "humidity_last",    "Humidity last",    DATA_ARRAY,  data_array(3, DATA_DOUBLE, humidity),
                "temperature_C_ext", "Temperature ext.", DATA_FORMAT, "%.1f C", DATA_DOUBLE, ext_c[0],
                "temperature_C_ext_last", "Temp. ext. last", DATA_ARRAY, data_array(3, DATA_DOUBLE, ext_c),
                "mic",              "Integrity",        DATA_STRING, "CRC",
                NULL);
        decoder_output_data(decoder, data);
        return 1;
    }
    case 0xa5: {
        if (len != 24) {
            return DECODE_FAIL_SANITY; // length doesn't match this model's frame
        }
        for (int k = 0; k < 3; k++) {
            temp_c[k] = (((int16_t)((b[12 + k * 4] | (b[12 + k * 4 + 1] << 8)) << 5)) >> 5) * 0.1;
        }
        data_t *data = data_make(
                "model",            "",                 DATA_STRING, "TFA-303901",
                "id",               "",                 DATA_STRING, id_str,
                "battery_ok",       "Battery OK",       DATA_INT,    battery_ok,
                "manual_transmit",  "Manual Transmit",  DATA_INT,    manual_transmit,
                "seq_number",       "Sequence Number",  DATA_INT,    seq_number,
                "temperature_C",    "Temperature",      DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c[0],
                "temperature_C_last", "Temp. last",     DATA_ARRAY,  data_array(3, DATA_DOUBLE, temp_c),
                "mic",              "Integrity",        DATA_STRING, "CRC",
                NULL);
        decoder_output_data(decoder, data);
        return 1;
    }
    default:
        return DECODE_FAIL_SANITY; // unknown model variant byte
    }
    /* clang-format on */
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "manual_transmit",
        "seq_number",
        "temperature_C",
        "temperature_C_last",
        "temperature_C_ext",
        "temperature_C_ext_last",
        "humidity",
        "humidity_last",
        "mic",
        NULL,
};

r_device const tfa_30_390x = {
        .name        = "TFA Dostmann 30.390X T/H sensors series",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 61,
        .long_width  = 61,
        .tolerance   = 5,
        .reset_limit = 3500,
        .decode_fn   = &tfa_30_390x_decode,
        .fields      = output_fields,
};
