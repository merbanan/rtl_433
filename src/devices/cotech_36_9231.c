/** @file
    Cotech 36-9231 weather station.

    The FT020TL outdoor sensor uses LoRa SF7 at 500 kHz bandwidth on
    868.35 MHz. Its application payload closely follows the Cotech 36-7959
    layout, but is byte aligned and carries a leading 0xd4 marker.
*/

#include "decoder.h"

static int cotech_36_9231_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->num_rows != 1 || bitbuffer->bits_per_row[0] != 120) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t const *b = bitbuffer->bb[0];
    if (b[0] != 0xd4) {
        return DECODE_ABORT_EARLY;
    }
    if (crc8(b, 15, 0x31, 0x00)) {
        decoder_log(decoder, 2, __func__, "CRC8 fail");
        return DECODE_FAIL_MIC;
    }

    int const id = b[1];
    int const battery_low = b[2] >> 3 & 1;
    int const wind = ((b[2] & 1) << 8) | b[3];
    int const gust = ((b[2] >> 1 & 1) << 8) | b[4];
    int const wind_dir = ((b[2] >> 2 & 1) << 8) | b[5];
    int const rain = ((b[6] & 0x0f) << 8) | b[7];
    int const temp_raw = ((b[8] & 0x0f) << 8) | b[9];
    int const humidity = b[10];
    float const temp_f = (temp_raw - 400) * 0.1f;
    float const rain_mm = rain * 0.1f;
    float const wind_avg_m_s = wind * 0.1f;
    float const wind_max_m_s = gust * 0.1f;

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",                 DATA_STRING, "Cotech-369231",
            "id",               "ID",               DATA_INT,    id,
            "battery_ok",       "Battery",          DATA_INT,    !battery_low,
            "temperature_F",    "Temperature",      DATA_FORMAT, "%.1f F", DATA_DOUBLE, (double)temp_f,
            "humidity",         "Humidity",         DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "rain_mm",          "Rain",             DATA_FORMAT, "%.1f mm", DATA_DOUBLE, (double)rain_mm,
            "wind_dir_deg",     "Wind direction",   DATA_INT,    wind_dir,
            "wind_avg_m_s",     "Wind",             DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, (double)wind_avg_m_s,
            "wind_max_m_s",     "Gust",             DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, (double)wind_max_m_s,
            "mic",              "Integrity",        DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const cotech_36_9231_output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "temperature_F",
        "humidity",
        "rain_mm",
        "wind_dir_deg",
        "wind_avg_m_s",
        "wind_max_m_s",
        "mic",
        NULL,
};

r_device const cotech_36_9231 = {
        .name = "Cotech 36-9231 FT020TL LoRa weather station",
        .modulation = LORA,
        .decode_fn = &cotech_36_9231_decode,
        .fields = cotech_36_9231_output_fields,
};
