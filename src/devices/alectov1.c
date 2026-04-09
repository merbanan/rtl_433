// Generated from alectov1.py
/** @file
    AlectoV1 Weather Sensor decoder (proto_compiler Rows + Variants → ``alectov1.c``).

    Documentation also at http://www.tfd.hu/tfdhu/files/wsprotocol/auriol_protocol_v20.pdf

    Also Unitec W186-F (bought from Migros).

    PPM with pulse width 500 us, long gap 4000 us, short gap 2000 us, sync gap 9000 us.

    Some sensors transmit 8 long pulses (1-bits) as first row.
    Some sensors transmit 3 lone pulses (sync bits) between packets.

    Message Format: (9 nibbles, 36 bits):
    Please note that bytes need to be reversed before processing!

    Format for Temperature Humidity:

        IIIICCII BMMP TTTT TTTT TTTT HHHHHHHH CCCC
        RC       Type Temperature___ Humidity Checksum

    - I: 8 bit Random Device ID, includes 2 bit channel (X, 1, 2, 3)
    - B: 1 bit Battery status (0 normal, 1 voltage is below ~2.6 V)
    - M: 2 bit Message type, Temp/Humidity if not '11' else wind/rain sensor
    - P: 1 bit a 0 indicates regular transmission, 1 indicates requested by pushbutton
    - T: 12 bit Temperature (two's complement)
    - H: 8 bit Humidity BCD format
    - C: 4 bit Checksum

    Format for Rain:

        IIIIIIII BMMP 1100 RRRR RRRR RRRR RRRR CCCC
        RC       Type      Rain                Checksum

    - I: 8 bit Random Device ID, includes 2 bit channel (X, 1, 2, 3)
    - B: 1 bit Battery status (0 normal, 1 voltage is below ~2.6 V)
    - M: 2 bit Message type, Temp/Humidity if not '11' else wind/rain sensor
    - P: 1 bit a 0 indicates regular transmission, 1 indicates requested by pushbutton
    - R: 16 bit Rain (bitvalue * 0.25 mm)
    - C: 4 bit Checksum

    Format for Windspeed:

        IIIIIIII BMMP 1000 0000 0000 WWWWWWWW CCCC
        RC       Type                Windspd  Checksum

    - I: 8 bit Random Device ID, includes 2 bit channel (X, 1, 2, 3)
    - B: 1 bit Battery status (0 normal, 1 voltage is below ~2.6 V)
    - M: 2 bit Message type, Temp/Humidity if not '11' else wind/rain sensor
    - P: 1 bit a 0 indicates regular transmission, 1 indicates requested by pushbutton
    - W: 8 bit Windspeed  (bitvalue * 0.2 m/s, correction for webapp = 3600/1000 * 0.2 * 100 = 72)
    - C: 4 bit Checksum

    Format for Winddirection & Windgust:

        IIIIIIII BMMP 111D DDDD DDDD GGGGGGGG CCCC
        RC       Type      Winddir   Windgust Checksum

    - I: 8 bit Random Device ID, includes 2 bit channel (X, 1, 2, 3)
    - B: 1 bit Battery status (0 normal, 1 voltage is below ~2.6 V)
    - M: 2 bit Message type, Temp/Humidity if not '11' else wind/rain sensor
    - P: 1 bit a 0 indicates regular transmission, 1 indicates requested by pushbutton
    - D: 9 bit Wind direction
    - G: 8 bit Windgust (bitvalue * 0.2 m/s, correction for webapp = 3600/1000 * 0.2 * 100 = 72)
    - C: 4 bit Checksum

    Derived from the former ``src/devices/alecto.c``; PPM timing unchanged.
*/

#include "decoder.h"
#include "alectov1.h"

/** @fn static int alectov1_decode(r_device *decoder, bitbuffer_t *bitbuffer)
    AlectoV1 Weather Sensor decoder (proto_compiler Rows + Variants → ``alectov1.c``).

    Documentation also at http://www.tfd.hu/tfdhu/files/wsprotocol/auriol_protocol_v20.pdf

    Also Unitec W186-F (bought from Migros).

    PPM with pulse width 500 us, long gap 4000 us, short gap 2000 us, sync gap 9000 us.

    Some sensors transmit 8 long pulses (1-bits) as first row.
    Some sensors transmit 3 lone pulses (sync bits) between packets.

    Message Format: (9 nibbles, 36 bits):
    Please note that bytes need to be reversed before processing!

    Format for Temperature Humidity:

        IIIICCII BMMP TTTT TTTT TTTT HHHHHHHH CCCC
        RC       Type Temperature___ Humidity Checksum

    - I: 8 bit Random Device ID, includes 2 bit channel (X, 1, 2, 3)
    - B: 1 bit Battery status (0 normal, 1 voltage is below ~2.6 V)
    - M: 2 bit Message type, Temp/Humidity if not '11' else wind/rain sensor
    - P: 1 bit a 0 indicates regular transmission, 1 indicates requested by pushbutton
    - T: 12 bit Temperature (two's complement)
    - H: 8 bit Humidity BCD format
    - C: 4 bit Checksum

    Format for Rain:

        IIIIIIII BMMP 1100 RRRR RRRR RRRR RRRR CCCC
        RC       Type      Rain                Checksum

    - I: 8 bit Random Device ID, includes 2 bit channel (X, 1, 2, 3)
    - B: 1 bit Battery status (0 normal, 1 voltage is below ~2.6 V)
    - M: 2 bit Message type, Temp/Humidity if not '11' else wind/rain sensor
    - P: 1 bit a 0 indicates regular transmission, 1 indicates requested by pushbutton
    - R: 16 bit Rain (bitvalue * 0.25 mm)
    - C: 4 bit Checksum

    Format for Windspeed:

        IIIIIIII BMMP 1000 0000 0000 WWWWWWWW CCCC
        RC       Type                Windspd  Checksum

    - I: 8 bit Random Device ID, includes 2 bit channel (X, 1, 2, 3)
    - B: 1 bit Battery status (0 normal, 1 voltage is below ~2.6 V)
    - M: 2 bit Message type, Temp/Humidity if not '11' else wind/rain sensor
    - P: 1 bit a 0 indicates regular transmission, 1 indicates requested by pushbutton
    - W: 8 bit Windspeed  (bitvalue * 0.2 m/s, correction for webapp = 3600/1000 * 0.2 * 100 = 72)
    - C: 4 bit Checksum

    Format for Winddirection & Windgust:

        IIIIIIII BMMP 111D DDDD DDDD GGGGGGGG CCCC
        RC       Type      Winddir   Windgust Checksum

    - I: 8 bit Random Device ID, includes 2 bit channel (X, 1, 2, 3)
    - B: 1 bit Battery status (0 normal, 1 voltage is below ~2.6 V)
    - M: 2 bit Message type, Temp/Humidity if not '11' else wind/rain sensor
    - P: 1 bit a 0 indicates regular transmission, 1 indicates requested by pushbutton
    - D: 9 bit Wind direction
    - G: 8 bit Windgust (bitvalue * 0.2 m/s, correction for webapp = 3600/1000 * 0.2 * 100 = 72)
    - C: 4 bit Checksum

    Derived from the former ``src/devices/alecto.c``; PPM timing unchanged.
*/
static int alectov1_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->num_rows <= 6)
        return DECODE_ABORT_LENGTH;
    if (bitbuffer->bits_per_row[1] != 36)
        return DECODE_ABORT_LENGTH;

    int vret_validate_packet = alectov1_validate_packet(decoder, bitbuffer);
    if (vret_validate_packet != 0)
        return vret_validate_packet;

    int cells_b0[BITBUF_ROWS];
    int cells_b1[BITBUF_ROWS];
    int cells_b2[BITBUF_ROWS];
    int cells_b3[BITBUF_ROWS];
    int cells_b4[BITBUF_ROWS];
    static uint16_t const _rows_cells[] = {1, 2, 3, 4, 5, 6};
    for (size_t _kir_cells = 0; _kir_cells < 6; ++_kir_cells) {
        unsigned row_cells = _rows_cells[_kir_cells];
        uint8_t *rowb_cells = bitbuffer->bb[row_cells];
        unsigned _roff_cells = 0;
        cells_b0[row_cells] = bitrow_get_bits(rowb_cells, _roff_cells, 8);
        _roff_cells += 8;
        cells_b1[row_cells] = bitrow_get_bits(rowb_cells, _roff_cells, 8);
        _roff_cells += 8;
        cells_b2[row_cells] = bitrow_get_bits(rowb_cells, _roff_cells, 8);
        _roff_cells += 8;
        cells_b3[row_cells] = bitrow_get_bits(rowb_cells, _roff_cells, 8);
        _roff_cells += 8;
        cells_b4[row_cells] = bitrow_get_bits(rowb_cells, _roff_cells, 4);
        _roff_cells += 4;
    }


    if (((((((cells_b1[1]) & 0x60) >> 5) == 3) & (((cells_b1[1]) & 0xf) != 0xc)) & (((cells_b1[1]) & 0xe) == 8)) & ((cells_b2[1]) == 0)) {
        int sensor_id = (reverse8((cells_b0[1])));
        int channel = (((cells_b0[1]) & 0xc) >> 2);
        int battery_ok = ((((cells_b1[1]) & 0x80) >> 7) == 0);
        float wind_avg_m_s = ((reverse8((cells_b3[1]))) * 0.2);
        float wind_max_m_s = ((reverse8((cells_b3[5]))) * 0.2);
        int wind_dir_deg = (((reverse8((cells_b2[5]))) << 1) | ((cells_b1[5]) & 1));

        /* clang-format off */
        data_t *data = data_make(
            "model", "", DATA_STRING, "AlectoV1-Wind",
            "id", "House Code", DATA_INT, sensor_id,
            "channel", "Channel", DATA_INT, channel,
            "battery_ok", "Battery", DATA_INT, battery_ok,
            "wind_avg_m_s", "Wind speed", DATA_FORMAT, "%.2f m/s", DATA_DOUBLE, wind_avg_m_s,
            "wind_max_m_s", "Wind gust", DATA_FORMAT, "%.2f m/s", DATA_DOUBLE, wind_max_m_s,
            "wind_dir_deg", "Wind Direction", DATA_INT, wind_dir_deg,
            "mic", "Integrity", DATA_STRING, "CHECKSUM",
            NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    } else if ((((((cells_b1[1]) & 0x60) >> 5) == 3) & (((cells_b1[1]) & 0xf) != 0xc)) & (((cells_b1[1]) & 0xe) == 0xe)) {
        int sensor_id = (reverse8((cells_b0[1])));
        int channel = (((cells_b0[1]) & 0xc) >> 2);
        int battery_ok = ((((cells_b1[1]) & 0x80) >> 7) == 0);
        float wind_avg_m_s = ((reverse8((cells_b3[5]))) * 0.2);
        float wind_max_m_s = alectov1_Wind4_wind_max_m_s(bitbuffer);
        int wind_dir_deg = alectov1_Wind4_wind_dir_deg(bitbuffer);

        /* clang-format off */
        data_t *data = data_make(
            "model", "", DATA_STRING, "AlectoV1-Wind",
            "id", "House Code", DATA_INT, sensor_id,
            "channel", "Channel", DATA_INT, channel,
            "battery_ok", "Battery", DATA_INT, battery_ok,
            "wind_avg_m_s", "Wind speed", DATA_FORMAT, "%.2f m/s", DATA_DOUBLE, wind_avg_m_s,
            "wind_max_m_s", "Wind gust", DATA_FORMAT, "%.2f m/s", DATA_DOUBLE, wind_max_m_s,
            "wind_dir_deg", "Wind Direction", DATA_INT, wind_dir_deg,
            "mic", "Integrity", DATA_STRING, "CHECKSUM",
            NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    } else if (((((cells_b1[1]) & 0x60) >> 5) == 3) & (((cells_b1[1]) & 0xf) == 0xc)) {
        int sensor_id = (reverse8((cells_b0[1])));
        int channel = (((cells_b0[1]) & 0xc) >> 2);
        int battery_ok = ((((cells_b1[1]) & 0x80) >> 7) == 0);
        float rain_mm = ((((reverse8((cells_b3[1]))) << 8) | (reverse8((cells_b2[1])))) * 0.25);

        /* clang-format off */
        data_t *data = data_make(
            "model", "", DATA_STRING, "AlectoV1-Rain",
            "id", "House Code", DATA_INT, sensor_id,
            "channel", "Channel", DATA_INT, channel,
            "battery_ok", "Battery", DATA_INT, battery_ok,
            "rain_mm", "Total Rain", DATA_FORMAT, "%.2f mm", DATA_DOUBLE, rain_mm,
            "mic", "Integrity", DATA_STRING, "CHECKSUM",
            NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    } else if ((((((((cells_b1[1]) & 0x60) >> 5) != 3) & ((cells_b0[2]) == (cells_b0[3]))) & ((cells_b0[3]) == (cells_b0[4]))) & ((cells_b0[4]) == (cells_b0[5]))) & ((cells_b0[5]) == (cells_b0[6]))) {
        int sensor_id = (reverse8((cells_b0[1])));
        int channel = (((cells_b0[1]) & 0xc) >> 2);
        int battery_ok = ((((cells_b1[1]) & 0x80) >> 7) == 0);
        float temperature_C = (((((reverse8((cells_b1[1]))) & 0xf0) | ((reverse8((cells_b2[1]))) << 8)) >> 4) * 0.1);
        int humidity = ((((reverse8((cells_b3[1]))) >> 4) * 0xa) + ((reverse8((cells_b3[1]))) & 0xf));
        if (!((((((reverse8((cells_b3[1]))) >> 4) * 0xa) + ((reverse8((cells_b3[1]))) & 0xf)) <= 0x64)))
            return DECODE_FAIL_SANITY;


        /* clang-format off */
        data_t *data = data_make(
            "model", "", DATA_STRING, "AlectoV1-Temperature",
            "id", "House Code", DATA_INT, sensor_id,
            "channel", "Channel", DATA_INT, channel,
            "battery_ok", "Battery", DATA_INT, battery_ok,
            "temperature_C", "Temperature", DATA_FORMAT, "%.2f C", DATA_DOUBLE, temperature_C,
            "humidity", "Humidity", DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "mic", "Integrity", DATA_STRING, "CHECKSUM",
            NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    }

    return DECODE_FAIL_SANITY;

}

static char const *const output_fields[] = {
    "model",
    "id",
    "channel",
    "battery_ok",
    "wind_avg_m_s",
    "wind_max_m_s",
    "wind_dir_deg",
    "mic",
    "rain_mm",
    "temperature_C",
    "humidity",
    NULL,
};

r_device const alectov1 = {
    .name        = "AlectoV1 Weather Sensor (Alecto WS3500 WS4500 Ventus W155/W044 Oregon)",
    .modulation  = OOK_PULSE_PPM,
    .short_width = 2000.0,
    .long_width  = 4000.0,
    .reset_limit = 10000.0,
    .gap_limit   = 7000.0,
    .decode_fn   = &alectov1_decode,
    .fields      = output_fields,
};
