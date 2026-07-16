/** @file
    Elster/Honeywell R2S/REXU family power meters.

    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Elster/Honeywell R2S/REXU family power meters.

These are frequency-hopping spread-spectrum (FHSS) meters, hopping across
25 channels 902.4-927.6 MHz, 400 kHz apart, FSK modulated with Manchester
coding. Since rtl_433 tunes a single frequency it will only ever catch a
fraction of the transmitted hops, but individual packets can still be
decoded when caught.

After the preamble and sync a Manchester-encoded frame follows, whitened
with a repeating 0x55 (i.e. every decoded byte is XORed with 0x55):

    LEN:8h FLAG:8h SRC:32h DST:32h ?:8h?:8h?:8h DATA:8h* CHK:16h

- LEN: number of bytes covered by the CRC, including this length byte
  itself but excluding the trailing CRC16
- FLAG: flags byte
- SRC: sending device address; top bit set marks a mesh-routing packet
  between meters/collectors rather than a plain meter->collector report
  (that whole sub-protocol, and the separate dst==0 flood-broadcast
  format, are not decoded here, only the common meter-report case)
- DST: destination (collector/base station) address, may be 0
- DATA: mostly unknown; a sub-command with a length byte of 0x33 (51) at
  a fixed offset carries hourly usage data (command id 0xce, "every 6
  hours" per the reference), see below
- CHK: CRC-16/X-25 (poly 0x8408, init 0xffff, byte-reflected, complemented),
  transmitted low byte first, covering LEN through the last DATA byte

The hourly-usage (cmd 0xce) sub-payload layout, per decode_pcap.py:

    ?:8h CMD:8h CTR:8h ?:8h FLAG2:8h CUR_HOUR:16h LAST_HOUR:16h N:8h READING:16h*N

- CTR: a per-message counter/sequence number
- CUR_HOUR, LAST_HOUR: rolling hour counters bounding the readings below
- N: number of 16-bit hourly readings that follow (capped at 17)
- READING: hourly usage, scaled by 0.01

Since real captures show the preamble/sync region collapsing to a variable
number of bits depending on signal quality, this decoder does not rely on
a fixed sync offset: it Manchester-decodes the whole row (already done by
rtl_433's demod) and then brute-force searches every bit position for one
where the (whitened) first byte, taken as LEN, yields a valid CRC-16/X-25 --
false positive risk is negligible given the 16-bit check.
*/

#define ELSTER_MIN_LEN 9  // at least LEN FLAG SRC(4) DST(4)
#define ELSTER_MAX_LEN 64 // sanity bound, longest known frame is well under this

static int elster_power_meter_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    int row_bits = bitbuffer->bits_per_row[0];
    if (row_bits < (ELSTER_MIN_LEN + 2) * 8) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t const *row = bitbuffer->bb[0];
    uint8_t buf[ELSTER_MAX_LEN + 2];
    int found_pos = -1;
    int len       = 0;

    for (int pos = 0; pos + (ELSTER_MIN_LEN + 2) * 8 <= row_bits; ++pos) {
        int cand_len = bitrow_get_byte(row, pos) ^ 0x55;
        if (cand_len < ELSTER_MIN_LEN || cand_len > ELSTER_MAX_LEN) {
            continue;
        }
        if (pos + (cand_len + 2) * 8 > row_bits) {
            continue;
        }

        for (int i = 0; i < cand_len + 2; ++i) {
            buf[i] = bitrow_get_byte(row, pos + i * 8) ^ 0x55;
        }

        uint16_t chk      = crc16lsb(buf, cand_len, 0x8408, 0xffff) ^ 0xffff;
        uint16_t chk_recv = buf[cand_len] | (buf[cand_len + 1] << 8);
        if (chk == chk_recv) {
            found_pos = pos;
            len       = cand_len;
            break;
        }
    }

    if (found_pos < 0) {
        decoder_log(decoder, 2, __func__, "CRC fail");
        return DECODE_FAIL_MIC;
    }

    int flags   = buf[1];
    uint32_t src = ((uint32_t)buf[2] << 24) | (buf[3] << 16) | (buf[4] << 8) | buf[5];
    uint32_t dst = ((uint32_t)buf[6] << 24) | (buf[7] << 16) | (buf[8] << 8) | buf[9];

    int has_reading = 0;
    float meter_kwh = 0.0f;
    int has_hourly  = 0;
    int ctr         = 0;
    int cur_hour    = 0;
    int last_hour   = 0;
    char hourly_str[160];
    hourly_str[0] = '\0';

    // optional hourly-usage sub-command, s.a. reference decode_pcap.py;
    // only sent by plain meter reports, not mesh-routing packets
    if (!(src & 0x80000000) && len - 1 > 15) {
        int cmd_start = 15;
        int cmd_len   = buf[1 + cmd_start];
        if (cmd_len == 0x33 && len - 1 >= cmd_start + 1 + cmd_len) {
            uint8_t const *cmd = &buf[1 + cmd_start + 1];
            int cmd_id         = cmd[1];
            if (cmd_id == 0xce && cmd_len >= 10) {
                ctr         = cmd[2];
                cur_hour    = (cmd[5] << 8) | cmd[6];
                last_hour   = (cmd[7] << 8) | cmd[8];
                int n_hours = cmd[9];
                if (n_hours > 17) {
                    n_hours = 17;
                }
                has_hourly = 1;

                char *p = hourly_str;
                for (int h = 0; h < n_hours && cmd_len >= 10 + 2 * (h + 1); ++h) {
                    int raw = (cmd[10 + 2 * h] << 8) | cmd[10 + 2 * h + 1];
                    p += sprintf(p, "%s%.2f", h ? "," : "", raw * 0.01f);
                }
            }
            // unconfirmed even by decode_pcap.py, the more careful reference dissector
            if (cmd_id == 0xce && cmd_len >= 47) {
                uint32_t reading_raw = ((uint32_t)cmd[44] << 16) | (cmd[45] << 8) | cmd[46];
                meter_kwh            = (float)reading_raw;
                has_reading          = 1;
            }
        }
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",             DATA_STRING, "Elster-PowerMeter",
            "id",           "Meter ID",     DATA_FORMAT, "%08x", DATA_INT, (int)src,
            "dst",          "Collector ID", DATA_FORMAT, "%08x", DATA_INT, (int)dst,
            "flags",        "Flags",        DATA_FORMAT, "%02x", DATA_INT, flags,
            "ctr",          "Counter",      DATA_COND, has_hourly,  DATA_INT, ctr,
            "cur_hour",     "Current Hour", DATA_COND, has_hourly,  DATA_INT, cur_hour,
            "last_hour",    "Last Hour",    DATA_COND, has_hourly,  DATA_INT, last_hour,
            "hourly_kWh",   "Hourly",       DATA_COND, has_hourly,  DATA_STRING, hourly_str,
            "reading_kWh",  "Reading",      DATA_COND, has_reading, DATA_FORMAT, "%.0f kWh", DATA_DOUBLE, (double)meter_kwh,
            "mic",          "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "dst",
        "flags",
        "ctr",
        "cur_hour",
        "last_hour",
        "hourly_kWh",
        "reading_kWh",
        "mic",
        NULL,
};

r_device const elster_power_meter = {
        .name        = "Elster/Honeywell R2S/REXU power meter",
        .modulation  = FSK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 28,
        .long_width  = 28,
        .reset_limit = 3000,
        .decode_fn   = &elster_power_meter_decode,
        .fields      = output_fields,
        .disabled    = 1, // needs field-testing against real captures, protocol only partially reverse-engineered
};
