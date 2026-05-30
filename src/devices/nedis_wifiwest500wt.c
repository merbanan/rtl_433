/** @file
    Nedis WIFIWEST500WT weather station sensor.

    Copyright (C) 2026 elcodedocle

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
    Nedis WIFIWEST500WT Tuya compatible weather station sensor.

    17-byte Datagram format (~60s period):

    FH 5N IIIIII XTTT HH AADA GGDG RRRR 00 CKXK

    - FH: Fixed header byte
    - 5N: N = 3-bit rolling counter (0-7)
    - IIIIII: 24-bit device ID (unique per sensor unit)
    - XTTT Temperature (12-bit, bits 0-3 of [5] + all of [6]).
           Format is (temp_c * 10). Two's complement for < 0.
    - HH: Humidity (%RH)
    - AA: Wind speed average LSB
    - DA: D = Average Wind Direction (high nibble),
          A = Wind speed average MSB (low nibble)
    - GG: Wind gust LSB
    - DG: D = Gust Wind Direction (high nibble),
          G = Wind gust MSB (low nibble)
    - RRRR: Rain counter (16-bit little-endian), 0.35 mm per tip
    - 00: Padding/reserved
    - CK: Checksum byte
    - XK: Ones-complement of CK

    Original Model Radio Spec:
    Frequency   : 868.4 MHz FSK
    Modulation  : FSK-PWM over FSK carrier
                  Each bit is one 7-chip symbol at 122 us/chip (854 us/symbol):
                    Bit 1 = F1 long (610 us) + F2 short (244 us)
                    Bit 0 = F1 short (244 us) + F2 long (610 us)
                  Preamble: ~66 equal-duty symbols (~850 us each) before each packet.
    Bit rate    : ~1170 baud
    Packet      : 17 bytes, repeated 8x per burst.
    Burst cycle : Device sends one burst per transmission cycle. All sensor data is
                  included in every packet (no channel multiplexing).

*/

#include "decoder.h"

// Wind direction lookup: 16-point compass rose
static char const *const nedis_wind_dir_str[] = {
        "N",
        "NNE",
        "NE",
        "ENE",
        "E",
        "ESE",
        "SE",
        "SSE",
        "S",
        "SSW",
        "SW",
        "WSW",
        "W",
        "WNW",
        "NW",
        "NNW",
};

/**
 * Validate packet integrity.
 * The checksum byte and its ones-complement must sum 0xFF.
 */
static int nedis_check(uint8_t const *b)
{
    if (b[0] != 0xF0) {
        return 0;
    }
    if (((b[15] + b[16]) & 0xFF) != 0xFF) {
        return 0;
    }
    return 1;
}

static int nedis_wifiwest500wt_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int row = bitbuffer_find_repeated_row(bitbuffer, 2, 17 * 8 * 7);
    if (row < 0) {
        // Fall back: scan all rows for a decodable one.
        for (int r = 0; r < bitbuffer->num_rows; r++) {
            if (bitbuffer->bits_per_row[r] >= 952) {
                row = r;
                break;
            }
        }
    }
    if (row < 0) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t const *chips = bitbuffer->bb[row];
    int nchips           = bitbuffer->bits_per_row[row];

    // PWM-decode chip stream to bytes.
    // Scan for a run of 3+ preamble symbols then collect 17 bytes of data.
#define CHIP_BIT(pos) ((chips[(pos) >> 3] >> (7 - ((pos) & 7))) & 1)

    uint8_t decoded[17];
    int found  = 0;
    int retval = DECODE_ABORT_LENGTH;

    // Scan for preamble end / data start
    for (int chip = 0; chip + 952 < nchips; chip++) {
        // Count preamble symbols (7 chips same polarity) before this point
        // A data symbol starts after at least 3 preamble symbols.
        // Try to decode 17 bytes starting from 'chip'.
        int fail = 0;
        memset(decoded, 0, sizeof(decoded));

        for (int sym = 0; sym < 17 * 8; sym++) {
            int base = chip + sym * 7;
            if (base + 7 > nchips) {
                fail = 1;
                break;
            }

            // Count F1-high chips in this 7-chip window
            int ones = 0;
            for (int k = 0; k < 7; k++) {
                ones += CHIP_BIT(base + k);
            }

            int bit;
            if (ones >= 4) { // ~5 ones = long F1 = bit 1
                bit = 1;
            }
            else if (ones <= 3) { // ~2 ones = short F1 = bit 0
                bit = 0;
            }
            else {
                fail = 1;
                break;
            }

            decoded[sym >> 3] |= (bit << (7 - (sym & 7)));
        }
        if (fail) {
            continue;
        }
        if (!nedis_check(decoded)) {
            continue;
        }

        found = 1;

        uint8_t counter    = decoded[1] & 0x0F;
        uint32_t device_id = (decoded[2] << 16) | (decoded[3] << 8) | decoded[4];

        int temp_raw = ((decoded[5] & 0x0F) << 8) | decoded[6];
        if (temp_raw & 0x0800) {
            temp_raw -= 0x1000;
        }
        float temp_c = temp_raw * 0.1f;

        int humidity = decoded[7];

        int wind_raw       = decoded[8] | ((decoded[9] & 0x0F) << 8);
        float wind_avg_ms  = wind_raw * 2.0f;
        float wind_avg_kmh = wind_avg_ms * 3.6f;

        int dir_avg_idx        = (decoded[9] >> 4) & 0x0F;
        float wind_avg_dir_deg = dir_avg_idx * 22.5f;

        int gust_raw = decoded[10] | ((decoded[11] & 0x0F) << 8);
        // Raw value is in 0.1 m/s.
        float gust_ms  = gust_raw / 10.0f;
        float gust_kmh = gust_ms * 3.6f;

        int dir_gust_idx   = (decoded[11] >> 4) & 0x0F;
        float wind_dir_deg = dir_gust_idx * 22.5f;

        int rain_raw  = decoded[12] | (decoded[13] << 8);
        float rain_mm = rain_raw * 0.35f;

        data_t *data = data_make(
                "model", "", DATA_STRING, "Nedis-WIFIWEST500WT",
                "id", "Device ID", DATA_FORMAT, "%06X", DATA_INT, device_id,
                "counter", "Counter", DATA_INT, counter,
                "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, (double)temp_c,
                "humidity", "Humidity", DATA_FORMAT, "%u %%", DATA_INT, humidity,
                "wind_dir_deg", "Wind direction", DATA_FORMAT, "%.1f deg", DATA_DOUBLE, (double)wind_dir_deg,
                "wind_dir_str", "Wind direction", DATA_STRING, nedis_wind_dir_str[dir_gust_idx],
                "wind_avg_dir_deg", "Wind avg dir", DATA_FORMAT, "%.1f deg", DATA_DOUBLE, (double)wind_avg_dir_deg,
                "wind_avg_m_s", "Wind speed (avg)", DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, (double)wind_avg_ms,
                "wind_avg_km_h", "Wind speed (avg)", DATA_FORMAT, "%.1f km/h", DATA_DOUBLE, (double)wind_avg_kmh,
                "gust_m_s", "Wind gust", DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, (double)gust_ms,
                "gust_km_h", "Wind gust", DATA_FORMAT, "%.1f km/h", DATA_DOUBLE, (double)gust_kmh,
                "rain_mm", "Rain total", DATA_FORMAT, "%.1f mm", DATA_DOUBLE, (double)rain_mm,
                "mic", "Integrity", DATA_STRING, "CHECKSUM",
                NULL);

        if (data) {
            decoder_output_data(decoder, data);
            retval = 1;
        }

        // Skip and continue (multiple packets in bitbuffer)
        chip += 17 * 8 * 7;
    }

#undef CHIP_BIT

    return found ? retval : DECODE_FAIL_SANITY;
}

static char const *nedis_wifiwest500wt_output_fields[] = {
        "model",
        "id",
        "counter",
        "temperature_C",
        "humidity",
        "wind_dir_deg",
        "wind_dir_str",
        "wind_avg_dir_deg",
        "wind_avg_m_s",
        "wind_avg_km_h",
        "gust_m_s",
        "gust_km_h",
        "rain_mm",
        "mic",
        NULL,
};

r_device const nedis_wifiwest500wt = {
        .name        = "Nedis WIFIWEST500WT",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 122,  // chip width in us
        .long_width  = 122,  // same (NRZ chip stream; PWM decoded in software above)
        .reset_limit = 8000, // inter-packet gap > 8 ms
        .decode_fn   = &nedis_wifiwest500wt_decode,
        .disabled    = 0,
        .fields      = nedis_wifiwest500wt_output_fields,
};
