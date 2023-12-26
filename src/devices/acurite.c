/** @file
    Acurite weather stations and temperature / humidity sensors.

    Copyright (c) 2015, Jens Jenson, Helge Weissig, David Ray Thompson, Robert Terzi
    Enhanced Acurite-606TX by Boing <dhs.mobil@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

Acurite weather stations and temperature / humidity sensors.

Devices decoded:
- Acurite Iris (5-n-1) weather station, Model; VN1TXC, 06004RM
- Acurite 5-n-1 pro weather sensor, Model: 06014RM
- Acurite Atlas (7-n-1) weather station
- Acurite Notos (3-n-1) weather station
- Acurite 896 Rain gauge, Model: 00896
- Acurite 592TXR / 06002RM / 6044m Tower sensor (temperature and humidity)
  (Note: Some newer sensors share the 592TXR coding for compatibility.
- Acurite 609TXC "TH" temperature and humidity sensor (609A1TX)
- Acurite 986 Refrigerator / Freezer Thermometer
- Acurite 515 Refrigerator / Freezer Thermometer
- Acurite 606TX temperature sensor, optional with channels and [TX]Button
- Acurite 6045M Lightning Detector
- Acurite 00275rm and 00276rm temp. and humidity with optional probe.
- Acurite 1190/1192 leak/water detector
*/

#include "decoder.h"

#define ACURITE_515_BITLEN        50
#define ACURITE_TXR_BITLEN        56
#define ACURITE_5N1_BITLEN        64
#define ACURITE_6045_BITLEN       72
#define ACURITE_ATLAS_BITLEN      80

#define ACURITE_515_BYTELEN             6
#define ACURITE_TXR_BYTELEN             7
#define ACURITE_1190_BYTELEN            7
#define ACURITE_3N1_BYTELEN             8
#define ACURITE_5N1_BYTELEN             8
#define ACURITE_899_BYTELEN             8
#define ACURITE_ATLAS_BYTELEN           8
#define ACURITE_6045_BYTELEN            9
#define ACURITE_ATLAS_LTNG_BYTELEN      10


// ** Acurite known message types
#define ACURITE_MSGTYPE_1190_DETECTOR                   0x01

#define ACURITE_MSGTYPE_TOWER_SENSOR                    0x04

#define ACURITE_MSGTYPE_ATLAS_WNDSPD_TEMP_HUM           0x05
#define ACURITE_MSGTYPE_ATLAS_WNDSPD_RAIN               0x06
#define ACURITE_MSGTYPE_ATLAS_WNDSPD_UV_LUX             0x07

#define ACURITE_MSGTYPE_515_REFRIGERATOR                0x08
#define ACURITE_MSGTYPE_515_FREEZER                     0x09

#define ACURITE_MSGTYPE_3N1_WINDSPEED_TEMP_HUMIDITY     0x20

#define ACURITE_MSGTYPE_ATLAS_WNDSPD_TEMP_HUM_LTNG      0x25
#define ACURITE_MSGTYPE_ATLAS_WNDSPD_RAIN_LTNG          0x26
#define ACURITE_MSGTYPE_ATLAS_WNDSPD_UV_LUX_LTNG        0x27

#define ACURITE_MSGTYPE_6045M                           0x2f
#define ACURITE_MSGTYPE_899_RAINFALL                    0x30
#define ACURITE_MSGTYPE_5N1_WINDSPEED_WINDDIR_RAINFALL  0x31
#define ACURITE_MSGTYPE_5N1_WINDSPEED_TEMP_HUMIDITY     0x38



// Acurite 5n1 Wind direction values.
// There are seem to be conflicting decodings.
// It is possible there there are different versions
// of the 5n1 station that report differently.
//
// The original implementation used by the 5n1 device type
// here seems to have a straight linear/circular mapping.
//
// The newer 5n1 mapping seems to just jump around with no clear
// meaning, but does map to the values sent by Acurite's
// only Acu-Link Internet Bridge and physical console 1512.
// This is may be a modified/non-standard Gray Code.

// Mapping 5n1 raw RF wind direction values to aculink's values
//    RF, AcuLink
//     0,  6,   NW,  315.0
//     1,  8,  WSW,  247.5
//     2,  2,  WNW,  292.5
//     3,  0,    W,  270.0
//     4,  4,  NNW,  337.5
//     5,  A,   SW,  225.0
//     6,  5,    N,    0.0
//     7,  E,  SSW,  202.5
//     8,  1,  ENE,   67.5
//     9,  F,   SE,  135.0
//     A,  9,    E,   90.0
//     B,  B,  ESE,  112.5
//     C,  3,   NE,   45.0
//     D,  D,  SSE,  157.0
//     E,  7,  NNE,   22.5
//     F,  C,    S,  180.0

// From draythomp/Desert-home-rtl_433
// matches acu-link internet bridge values
// The mapping isn't circular, it jumps around.
// units are 22.5 deg
int const acurite_5n1_winddirections[] = {
    14, // 0 - NW
    11, // 1 - WSW
    13, // 2 - WNW
    12, // 3 - W
    15, // 4 - NNW
    10, // 5 - SW
    0,   // 6 - N
    9, // 7 - SSW
    3,  // 8 - ENE
    6, // 9 - SE
    4,  // a - E
    5, // b - ESE
    2,  // c - NE
    7, // d - SSE
    1,  // e - NNE
    8, // f - S
};

// The high 2 bits of byte zero are the channel (bits 7,6)
//  00 = C
//  10 = B
//  11 = A
static char const *acurite_getChannel(uint8_t byte)
{
    static char const *const channel_strs[] = {"C", "E", "B", "A"}; // 'E' stands for error

    int channel = (byte & 0xC0) >> 6;
    return channel_strs[channel];
}

// Add exception and raw message bytes to message to enable
// later analysis of unexpected/possibly undecoded data
static void data_append_exception(data_t* data, int exception, uint8_t* bb, int browlen)
{
    char raw_str[31], *rawp;

    rawp = (char *)raw_str;
    for (int i=0; i < browlen; i++) {
        sprintf(rawp,"%02x",bb[i]);
        rawp += 2;
    }
    *rawp = '\0';

    /* clang-format off */
    data = data_append(data,
            "exception",        "data_exception",   DATA_INT,    exception,
            "raw_msg",          "raw_message",      DATA_STRING, raw_str,
            NULL);
    /* clang-format on */

}


/**
Acurite 896 rain gauge

*/
static int acurite_rain_896_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t *b = bitbuffer->bb[0];
    int id;
    float total_rain;
    data_t *data;

    // This needs more validation to positively identify correct sensor type, but it basically works if message is really from acurite raingauge and it doesn't have any errors
    if (bitbuffer->bits_per_row[0] < 24)
        return DECODE_ABORT_LENGTH;

    // The nominal repeat count is 16, require a minimum of 12 rows
    if (bitbuffer->num_rows < 12)
        return DECODE_ABORT_EARLY; // likely Oregon V1, not AcuRite

    if ((b[0] == 0) || (b[1] == 0) || (b[2] == 0) || (b[3] != 0) || (b[4] != 0))
        return DECODE_ABORT_EARLY;

    id = b[0];
    total_rain = ((b[1] & 0xf) << 8) | b[2];
    total_rain *= 0.5; // Sensor reports number of bucket tips.  Each bucket tip is .5mm

    decoder_logf(decoder, 2, __func__, "Total Rain is %2.1fmm", total_rain);
    decoder_log_bitrow(decoder, 2, __func__, b, bitbuffer->bits_per_row[0], "Raw Message ");

    /* clang-format off */
    data = data_make(
            "model",                "",             DATA_STRING, "Acurite-Rain",
            "id",                   "",             DATA_INT,    id,
            "rain_mm",              "Total Rain",   DATA_FORMAT, "%.1f mm", DATA_DOUBLE, total_rain,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/**
Acurite 609 Temperature and Humidity Sensor.

5 byte messages:

    II ST TT HH CC
    II - ID byte, changes at each power up
    S - Status bitmask, normally 0x2,
        0xa - battery low (bit 0x80)
    TTT - Temp in Celsius * 10, 12 bit with complement.
    HH - Humidity
    CC - Checksum

@todo - see if the 3rd nybble is battery/status
*/
static int acurite_th_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t *bb = NULL;
    int cksum, battery_low, valid = 0;
    float tempc;
    uint8_t humidity, id, status;
    data_t *data;
    int result = 0;

    for (uint16_t brow = 0; brow < bitbuffer->num_rows; ++brow) {
        if (bitbuffer->bits_per_row[brow] != 40) {
            result = DECODE_ABORT_LENGTH;
            continue; // DECODE_ABORT_LENGTH
        }

        bb = bitbuffer->bb[brow];

        cksum = (bb[0] + bb[1] + bb[2] + bb[3]);

        if (cksum == 0 || ((cksum & 0xff) != bb[4])) {
            result = DECODE_FAIL_MIC;
            continue; // DECODE_FAIL_MIC
        }

        // Temperature in Celsius is encoded as a 12 bit integer value
        // multiplied by 10 using the 4th - 6th nybbles (bytes 1 & 2)
        // negative values are recovered by sign extend from int16_t.
        int temp_raw = (int16_t)(((bb[1] & 0x0f) << 12) | (bb[2] << 4));
        tempc        = (temp_raw >> 4) * 0.1f;
        id           = bb[0];
        status       = (bb[1] & 0xf0) >> 4;
        battery_low  = status & 0x8;
        humidity     = bb[3];

        if (humidity > 100) {
            decoder_logf(decoder, 1, __func__, "609txc 0x%04X: invalid humidity: %d %%rH",
                         id, humidity);
            return DECODE_FAIL_SANITY;
        }

        /* clang-format off */
        data = data_make(
                "model",            "",             DATA_STRING, "Acurite-609TXC",
                "id",               "",             DATA_INT,    id,
                "battery_ok",       "Battery",      DATA_INT,    !battery_low,
                "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, tempc,
                "humidity",         "Humidity",     DATA_FORMAT, "%u %%", DATA_INT,    humidity,
                "status",           "",             DATA_INT,    status,
                "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        valid++;
    }

    if (valid)
        return 1;

    // Only returns the latest result, but better than nothing.
    return result;
}

/**
Acurite 06045m Lightning Sensor decoding.

Specs:
- lightning strike count
- estimated distance to front of storm, 1 to 25 miles / 1.6 to 40 km
- Temperature -40 to 158 F / -40 to 70 C
- Humidity 1 - 99% RH

Status Information sent per 06047M/01021 display
- (RF) interference (preventing lightning detection)
- low battery

Message format:

Somewhat similar to 592TXR and 5-n-1 weather stations.
Same pulse characteristics. checksum, and parity checking on data bytes.


    Byte 0   Byte 1   Byte 2   Byte 3   Byte 4   Byte 5   Byte 6   Byte 7   Byte 8
    CCIIIIII IIIIIIII pB101111 pHHHHHHH pA?TTTTT pTTTTTTT pLLLLLLL pLRDDDDD KKKKKKKK

- C = Channel (2 bits)
- I = Sensor ID (14 bit Static ID)
- p = parity check bit
- B = Battery OK (cleared for low)
- H = Humidity (7 bits)
- A = Active mode lightning detection (cleared for standby mode)
- T = Temperature (12 bits)
- L = Lightning strike count (8 bits)
- D = Lightning distance (5 bits)
- K = Checksum (8 bits)

Byte 0 - channel/ID
- bitmask CCII IIII
- 0xC0: channel (A: 0xC, B: 0x8, C: 00)
- 0x3F: most significant 6 bits of Sensor ID
   (14 bits, same as Acurite Tower sensor family)

Byte 1 - ID all 8 bits, no parity.
- 0xFF = least significant 8 bits of Sensor ID

Byte 2 - Battery and Message type
- Bitmask PBMMMMMM
- 0x80 = Parity
- 0x40 = 1 is battery OK, 0 is battery low
- 0x3f = Message type 0x2f for 06045M lightning detector

Byte 3 - Humidity
- 0x80 - even parity
- 0x7f - humidity

Byte 4 - Status (2 bits) and Temperature MSB (5 bits)
- Bitmask PA?TTTTT  (P = Parity, A = Active,  T = Temperature)
- 0x80 - even parity
- 0x40 - 1 is Active lightning detection Mode, 0 is standby
- 0x20 - TBD: always off?
- 0x1F - Temperature most significant 5 bits

Byte 5 - Temperature LSB (7 bits, 8th is parity)
- 0x80 - even parity
- 0x7F - Temperature least significant 7 bits

Byte 6 - Lightning Strike count (7 of 8 bit, 8th is parity)
- 0x80 - even parity
- 0x7F - strike count (upper 7 bits) wraps at 255 -> 0


Byte 7 - Edge of Storm Distance Approximation & other bits
- Bits PLRDDDDD  (P = Parity, S = Status, D = Distance
- 0x80 - even parity
- 0x40 - LSB of 8 bit strike counter
- 0x20 - RFI (radio frequency interference)
- 0x1F - distance to edge of storm
   value 0x1f is possible invalid value indication (value at power up)
   @todo determine mapping function/table.


Byte 8 - checksum. 8 bits, no parity.

Data fields in rtl_433 messages:
- active (vs standby) lightning detection mode
    When active:
      the AS39335 is in active scanning mode
      6045M will transmit every 8 seconds instead of every 24.

- RFI - radio frequency interference detected
    The AS3935 uses broad RFI for detection
    Somewhat correlates with the yellow LED on the sensor, but stays set longer
    Short periods of RFI appears to be somewhat normal
    long periods of RFI on indicates interference, relocate sensor until
    yellow LED is no longer on solid

- strike_count - count of detection events, 8 bits
    counts up to 255, wraps around to 0
    non-volatile (doesn't reset at power up)

- storm_distance - statistically estimated distance to edge of storm
    See AS3935 documentation
    sensor will make calculate a distance estimate with each strike event
    0x1f (31) is invalid/undefined value, used at power-up to indicate invalid
    Only 5 bits available, needs to cover range of 25 miles/40 KM per spec.
    Units unknown, data needed from people with Acurite consoles

- exception - additional analysis of message maybe needed
    Suggest reporting raw_msg for further examination.
    bits that were invariant (for me) have changed.

Notes:

2020-08-29 - changed temperature decoding, was 2.0 F too low vs. Acurite Access

@todo - storm_distance conversion to miles/KM (should match Acurite consoles)

*/
static int acurite_6045_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row)
{
    float tempf;
    uint8_t humidity;
    char raw_str[31], *rawp;
    uint16_t sensor_id;
    uint8_t strike_count, strike_distance;
    int battery_low, active, rfi_detect;
    int exception = 0;
    data_t *data;

    int browlen = (bitbuffer->bits_per_row[row] + 7) / 8;
    uint8_t *bb = bitbuffer->bb[row];

    char const *channel_str = acurite_getChannel(bb[0]); // same as TXR

    // Tower sensor ID is the last 14 bits of byte 0 and 1
    // CCII IIII | IIII IIII
    sensor_id = ((bb[0] & 0x3f) << 8) | bb[1]; // same as TXR
    battery_low = (bb[2] & 0x40) == 0;

    humidity = (bb[3] & 0x7f); // 1-99 %rH, same as TXR
    if (humidity > 100) {
        decoder_logf(decoder, 1, __func__, "6045m 0x%04X Ch %s : invalid humidity: %d %%rH",
                     sensor_id, channel_str, humidity);
        return DECODE_FAIL_SANITY;
    }

    active = (bb[4] & 0x40) == 0x40;    // Sensor is actively listening for strikes
    //message_type = bb[2] & 0x3f;

    // 12 bits of temperature after removing parity and status bits.
    // Message native format appears to be in 1/10 of a degree Fahrenheit
    // Device Specification: -40 to 158 F  / -40 to 70 C
    // Available range given 12 bits with +1480 offset: -148.0 F to +261.5 F
    int temp_raw = ((bb[4] & 0x1F) << 7) | (bb[5] & 0x7F);
    tempf = (temp_raw - 1480) * 0.1f;

    if (tempf < -40.0 || tempf > 158.0) {
        decoder_logf(decoder, 1, __func__, "6045m 0x%04X Ch %s, invalid temperature: %0.1f F",
                     sensor_id, channel_str, tempf);
        return DECODE_FAIL_SANITY;
    }

    // flag if bits 13/14 of temperature are ever non-zero so
    // they can be investigated
    if (temp_raw & 0x3000)
        exception++;

    // Strike count is 8 bits, LSB in following byte
    strike_count = ((bb[6] & 0x7f) << 1) | ((bb[7] & 0x40) >> 6);
    strike_distance = bb[7] & 0x1f;
    rfi_detect = (bb[7] & 0x20) == 0x20;


    /*
     * 2018-04-21 rct - There are still a number of unknown bits in the
     * message that need to be figured out. Add the raw message hex to
     * to the structured data output to allow future analysis without
     * having to enable debug for long running rtl_433 processes.
     */
    rawp = (char *)raw_str;
    for (int i=0; i < MIN(browlen, 15); i++) {
        sprintf(rawp,"%02x",bb[i]);
        rawp += 2;
    }
    *rawp = '\0';


    // Flag whether this message might need further analysis
    if ((bb[4] & 0x20) != 0) // unknown status bits, always off
        exception++;

    /* clang-format off */
    data = data_make(
            "model",            "",                 DATA_STRING, "Acurite-6045M",
            "id",               NULL,               DATA_INT,    sensor_id,
            "channel",          NULL,               DATA_STRING, channel_str,
            "battery_ok",       "Battery",          DATA_INT,    !battery_low,
            "temperature_F",    "temperature",      DATA_FORMAT, "%.1f F",     DATA_DOUBLE,     tempf,
            "humidity",         "humidity",         DATA_FORMAT, "%u %%", DATA_INT,    humidity,
            "strike_count",     "strike_count",     DATA_INT,    strike_count,
            "storm_dist",       "storm_distance",   DATA_INT,    strike_distance,
            "active",           "active_mode",      DATA_INT,    active,
            "rfi",              "rfi_detect",       DATA_INT,    rfi_detect,
            "exception",        "data_exception",   DATA_INT,    exception,
            "raw_msg",          "raw_message",      DATA_STRING, raw_str,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);

    return 1; // If we got here 1 valid message was output
}

/**
Acurite 899 Rain Gauge decoder

*/
static int acurite_899_decode(r_device *decoder, bitbuffer_t *bitbuffer, uint8_t *bb)
{
    (void)bitbuffer;
    // MIC (checksum, parity) validated in calling function

    uint16_t sensor_id = ((bb[0] & 0x3f) << 8) | bb[1]; //
    int battery_low = (bb[2] & 0x40) == 0;

    /*
      @todo bug? channel output isn't consistent with the rest of he Acurite
      devices in this family, should output ('A', 'B', or 'C')
      Currently outputting 00 = A, 01 = B, 10 = C
      Leaving as is to maintain compatibility for now
    */

    int channel = bb[0] >> 6;
    // @todo replace the above with this:
    // char const* channel_str = acurite_getChannel(bb[0]);


    /*
      Rain counter - one tip is 0.01 inch, i.e. 0.254mm
      Note: Device native unit arguably Imperial
      but this is being converted to metric here, so -C native won't work
      Leaving as is to maintain compatibility
    */
    int raincounter = ((bb[5] & 0x7f) << 7) | (bb[6] & 0x7f);

    /* clang-format off */
    data_t *data;
    data = data_make(
            "model",            "",                         DATA_STRING, "Acurite-Rain899",
            "id",               "",                         DATA_INT,    sensor_id,
            "channel",          "",                         DATA_INT,    channel,
            // "channel",              NULL,           DATA_STRING, channel_str,
            "battery_ok",       "Battery",                  DATA_INT,    !battery_low,
            "rain_mm",          "Rainfall Accumulation",    DATA_FORMAT, "%.2f mm", DATA_DOUBLE, raincounter * 0.254,
            "mic",                  "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);

    return 1; // if we got here, 1 message was output

}

/**
Acurite 3n1 Weather Station decoder

*/
static int acurite_3n1_decode(r_device *decoder, bitbuffer_t *bitbuffer, uint8_t *bb)
{
    // MIC (checksum, parity) validated in calling function
    (void)bitbuffer;

    char const* channel_str = acurite_getChannel(bb[0]);

    // 3n1 sensor ID is 14 bits
    uint16_t sensor_id = ((bb[0] & 0x3f) << 8) | bb[1];
    uint8_t message_type = bb[2] & 0x3f;

    if (*channel_str == 'E') {
        decoder_logf(decoder, 1, __func__,
                     "bad channel Ch %s, msg type 0x%02x",
                     channel_str, message_type);
        return DECODE_FAIL_SANITY;
    }

    /*
      @todo bug, 3n1 data format includes sequence_num
      which was copied from 5n1, but existing code 3n1 uses
      14 bits for ID. so these bits are used twice.

      Leaving for compatibility, but probably sequence_num
      doesn't exist and should be deleted. If the 3n1 did use
      a sequence number, the ID would change on each output.
    */
    uint8_t sequence_num = (bb[0] & 0x30) >> 4;

    int battery_low = (bb[2] & 0x40) == 0;
    uint8_t humidity = (bb[3] & 0x7f); // 1-99 %rH
    if (humidity > 100) {
        decoder_logf(decoder, 1, __func__, "3n1 0x%04X Ch %s : invalid humidity: %d %%rH",
                     sensor_id, channel_str, humidity);
        return DECODE_FAIL_SANITY;
    }

    // note the 3n1 seems to have one more high bit than 5n1
    // Spec: -40 to 158 F
    int temp_raw = (bb[4] & 0x1F) << 7 | (bb[5] & 0x7F);
    float tempf        = (temp_raw - 1480) * 0.1f; // regression yields (rawtemp-1480)*0.1

    if (tempf < -40.0 || tempf > 158.0) {
        decoder_logf(decoder, 1, __func__, "3n1 0x%04X Ch %s, invalid temperature: %0.1f F",
                     sensor_id, channel_str, tempf);
        return DECODE_FAIL_SANITY;
    }


    /*
      @todo bug from original decoder
      This can't be a float, must be uint8
      leaving for compatibility
    */
    float wind_speed_mph = bb[6] & 0x7f; // seems to be plain MPH

    /* clang-format off */
    data_t *data;
    data = data_make(
            "model",        "",   DATA_STRING,    "Acurite-3n1",
            "message_type", NULL,   DATA_INT,       message_type,
            "id",    NULL,   DATA_FORMAT,    "0x%02X",   DATA_INT,       sensor_id,
            "channel",      NULL,   DATA_STRING,    channel_str,
            "sequence_num",  NULL,   DATA_INT,      sequence_num,
            "battery_ok",       "Battery",      DATA_INT,    !battery_low,
            "wind_avg_mi_h",   "wind_speed",   DATA_FORMAT,    "%.1f mi/h", DATA_DOUBLE,     wind_speed_mph,
            "temperature_F",     "temperature",    DATA_FORMAT,    "%.1f F", DATA_DOUBLE,    tempf,
            "humidity",     NULL,    DATA_FORMAT,    "%u %%",   DATA_INT,   humidity,
            "mic",                  "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);

    return 1; // If we got here 1 valid message was output
}


/**
Acurite 5n1 Weather Station decoder

XXX todo docs

*/
static int acurite_5n1_decode(r_device *decoder, bitbuffer_t *bitbuffer, uint8_t* bb)
{
    // MIC (checksum, parity) validated in calling function
    (void)bitbuffer;

    char const* channel_str = acurite_getChannel(bb[0]);
    uint16_t sensor_id = ((bb[0] & 0x0f) << 8) | bb[1];
    uint8_t sequence_num = (bb[0] & 0x30) >> 4;
    int battery_low = (bb[2] & 0x40) == 0;
    uint8_t message_type = bb[2] & 0x3f;

    // Wind raw number is cup rotations per 4 seconds
    // 8 bits gives range of 0 - 212 KPH
    // http://www.wxforum.net/index.php?topic=27244.0 (found from weewx driver)
    int wind_speed_raw = ((bb[3] & 0x1F) << 3)| ((bb[4] & 0x70) >> 4);
    float wind_speed_kph = 0;
    if (wind_speed_raw > 0) {
        wind_speed_kph = wind_speed_raw * 0.8278 + 1.0;
    }

    if (message_type == ACURITE_MSGTYPE_5N1_WINDSPEED_WINDDIR_RAINFALL) {
        // Wind speed, wind direction, and rain fall
        float wind_dir = acurite_5n1_winddirections[bb[4] & 0x0f] * 22.5f;

        // range: 0 to 99.99 in, 0.01 inch increments, accumulated
        int raincounter = ((bb[5] & 0x7f) << 7) | (bb[6] & 0x7F);

        /* clang-format off */
        data_t *data;
        data = data_make(
                "model",        "",   DATA_STRING,    "Acurite-5n1",
                "message_type", NULL,   DATA_INT,       message_type,
                "id",           NULL, DATA_INT,       sensor_id,
                "channel",      NULL,   DATA_STRING,    channel_str,
                "sequence_num",  NULL,   DATA_INT,      sequence_num,
                "battery_ok",       "Battery",      DATA_INT,    !battery_low,
                "wind_avg_km_h",   "wind_speed",   DATA_FORMAT,    "%.1f km/h", DATA_DOUBLE,     wind_speed_kph,
                "wind_dir_deg", NULL,   DATA_FORMAT,    "%.1f", DATA_DOUBLE,    wind_dir,
                "rain_in",      "Rainfall Accumulation",   DATA_FORMAT, "%.2f in", DATA_DOUBLE, raincounter * 0.01f,
                "mic",                  "Integrity",    DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
    }
    else if (message_type == ACURITE_MSGTYPE_5N1_WINDSPEED_TEMP_HUMIDITY) {
        // Wind speed, temperature and humidity

        // range -40 to 158 F
        int temp_raw = (bb[4] & 0x0F) << 7 | (bb[5] & 0x7F);
        float tempf = (temp_raw - 400) * 0.1f;

        if (tempf < -40.0 || tempf > 158.0) {
            decoder_logf(decoder, 1, __func__, "5n1 0x%04X Ch %s, invalid temperature: %0.1f F",
                         sensor_id, channel_str, tempf);
            return DECODE_FAIL_SANITY;
        }

        uint8_t humidity = (bb[6] & 0x7f); // 1-99 %rH
        if (humidity > 100) {
            decoder_logf(decoder, 1, __func__, "5n1 0x%04X Ch %s : invalid humidity: %d %%rH",
                         sensor_id, channel_str, humidity);
            return DECODE_FAIL_SANITY;
        }


        /* clang-format off */
        data_t *data;
        data = data_make(
                "model",        "",   DATA_STRING,    "Acurite-5n1",
                "message_type", NULL,   DATA_INT,       message_type,
                "id",           NULL, DATA_INT,  sensor_id,
                "channel",      NULL,   DATA_STRING,    channel_str,
                "sequence_num",  NULL,   DATA_INT,      sequence_num,
                "battery_ok",       "Battery",      DATA_INT,    !battery_low,
                "wind_avg_km_h",   "wind_speed",   DATA_FORMAT,    "%.1f km/h", DATA_DOUBLE,     wind_speed_kph,
                "temperature_F",     "temperature",    DATA_FORMAT,    "%.1f F", DATA_DOUBLE,    tempf,
                "humidity",     NULL,    DATA_FORMAT,    "%u %%",   DATA_INT,   humidity,
                "mic",                  "Integrity",    DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
    } else {
        decoder_logf(decoder, 1, __func__, "unknown message type 0x02%x", message_type);
        return DECODE_FAIL_SANITY;
    }

    return 1; // If we got here 1 valid message was output
}


/**
Acurite Atlas weather and lightning sensor.

| Reading           | Operating Range               | Reading Frequency | Accuracy |
| ---               | ---                           | ---        | ---             |
| Temperature Range | -40 to 158°F (-40 to 70°C)    | 30 seconds | ± 1°F |
| Humidity Range    | 1-100% RH                     | 30 seconds | ± 2% RH |
| Wind Speed        | 0-160 mph (0-257 km/h)        | 10 seconds | ± 1 mph ≤ 10 mph, ± 10% > 10 mph |
| Wind Direction    | 360°                          | 30 seconds | ± 3° |
| Rain              | .01 inch intervals (0.254 mm) | 30 seconds | ± 5% |
| UV Index          | 0 to 15 index                 | 30 seconds | ± 1 |
| Light Intensity   | to 120,000 Lumens             | 30 seconds | n/a |
| Lightning         | Up to 25 miles away (40 km)   | 10 seconds | n/a |

The Atlas reports direction with an AS5600 hall effect sensor, it has 12-bit resolution according to the spec sheet. https://ams.com/as5600

Acurite Atlas Message Type Format:

Message Type 0x25 (Wind Speed, Temperature, Relative Humidity, ???)

    Byte 1   Byte 2   Byte 3   Byte 4   Byte 5   Byte 6   Byte 7   Byte 8   Byte 9   Byte 10
    cc??ssdd dddddddd pb011011 pWWWWWWW pWTTTTTT pTTTTTTT pHHHHHHH pCCCCCCC pCCDDDDD kkkkkkkkk

Note: 13 bits for Temp is too much, should only be 11 bits.

Message Type 0x26 (Wind Speed, Wind Vector, Rain Counter, ???)

    Byte 1   Byte 2   Byte 3   Byte 4   Byte 5   Byte 6   Byte 7   Byte 8   Byte 9   Byte 10
    cc??ssdd dddddddd pb011100 pWWWWWWW pW?VVVVV pVVVVVRR pRRRRRRR pCCCCCCC pCCDDDDD kkkkkkkkk

    CHANNEL:2b xx ~SEQ:2d ~DEVICE:10d xx ~TYPE:6h SPEED:x~7bx~1b DIR:x~5bx~5bxx x~7b x~7b x~7b CHK:8h

Note: 10 bits for Vector is too much, should only be 9 bits.
Note: 7 bits for Rain not enough, should reasonably be 10 bits.

Message Type 0x27 (Wind Speed, UV and Lux data)

    Byte 1   Byte 2   Byte 3   Byte 4   Byte 5   Byte 6   Byte 7   Byte 8   Byte 9   Byte 10
    cc??ssdd dddddddd pb011101 pWWWWWWW pW??UUUU pLLLLLLL pLLLLLLL pCCCCCCC pCCDDDDD kkkkkkkkk

Note: 6 bits for UV is too much, should only be 4 bits.
JRH - Definitely only 4 bits, seeing the occasional value of 32 or 34. No idea what the 2 bits between
      wind speed and UV are.

    CHANNEL:2b xx ~SEQ:2d ~DEVICE:10d xx ~TYPE:6h SPEED:x~7bx~1b UV:~6d LUX:x~7bx~7b x~7b x~7b CHK:8h

Lux needs to multiplied by 10.

- b = bATTERY
- c = cHANNEL
- d = dEVICE
- k = CHECkSUM
- p = pARITY
- s = sEQUENCE
- ? = uNKNOWN

- H = relative Humidity (percent)
- R = Rain (0.01 inch bucket tip count)
- T = Temperature (Fahrenheit.  Subtract 400 then divide by 10.)
- V = wind Vector (degrees decimal)
- W = Wind speed (miles per hour)
- U = UV Index
- L = Lux
- C = lightning strike Count
- D = lightning Distance (miles)

*/
static int acurite_atlas_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row)
{
    uint8_t humidity, sequence_num, message_type;
    char raw_str[31], *rawp;
    uint16_t sensor_id;
    int raincounter, battery_low;
    int exception = 0;
    float tempf, wind_dir, wind_speed_mph;
    data_t *data;

    int browlen = (bitbuffer->bits_per_row[row] + 7) / 8;
    uint8_t *bb = bitbuffer->bb[row];

    message_type = bb[2] & 0x3f;
    sensor_id = ((bb[0] & 0x03) << 8) | bb[1];
    char const *channel_str = acurite_getChannel(bb[0]);

    // There are still a few unknown/unused bits in the message that
    // message that could possibly hold some data. Add the raw message hex to
    // to the structured data output to allow future analysis without
    // having to enable debug for long running rtl_433 processes.
    rawp = (char *)raw_str;
    for (int i=0; i < MIN(browlen, 15); i++) {
        sprintf(rawp,"%02x",bb[i]);
        rawp += 2;
    }
    *rawp = '\0';

    // The sensor sends the same data three times, each of these have
    // an indicator of which one of the three it is. This means the
    // checksum and first byte will be different for each one.
    // The bits 4,5 of byte 0 indicate which copy
    //  xxxx 00 xx = first copy
    //  xxxx 01 xx = second copy
    //  xxxx 10 xx = third copy
    sequence_num = (bb[0] & 0x0c) >> 2;
    // Battery status is the 7th bit 0x40. 1 = normal, 0 = low
    battery_low = (bb[2] & 0x40) == 0;

    // Wind speed is 8-bits raw MPH
    // Spec is 0-200 MPH
    wind_speed_mph = ((bb[3] & 0x7F) << 1) | ((bb[4] & 0x40) >> 6);

    if (wind_speed_mph > 200) {
        decoder_logf(decoder, 1, __func__, "Atlas 0x%04X Ch %s, invalid wind speed: %.1f MPH",
                     sensor_id, channel_str, wind_speed_mph);
        return DECODE_FAIL_SANITY;
    }

    /* clang-format off */
    data = data_make(
            "model",                "",             DATA_STRING, "Acurite-Atlas",
            "id",                   NULL,           DATA_INT,    sensor_id,
            "channel",              NULL,           DATA_STRING, channel_str,
            "sequence_num",         NULL,           DATA_INT,    sequence_num,
            "battery_ok",           "Battery",      DATA_INT,    !battery_low,
            "message_type",         NULL,           DATA_INT,    message_type,
            "wind_avg_mi_h",        "Wind Speed",   DATA_FORMAT, "%.1f mi/h", DATA_DOUBLE, wind_speed_mph,
            NULL);
    /* clang-format on */

    if (message_type == ACURITE_MSGTYPE_ATLAS_WNDSPD_TEMP_HUM ||
            message_type == ACURITE_MSGTYPE_ATLAS_WNDSPD_TEMP_HUM_LTNG) {
        // Wind speed, temperature and humidity

        // Spec: temperature range -40 to 158 F
        // There seem to be 13 bits for temperature but only 11 needed.
        // Decode as 11 bits, flag exception if the other two bits are ever
        // non-zero so they can be investigated.
        int temp_raw = (bb[4] & 0x0F) << 7 | (bb[5] & 0x7F);
        if ((bb[4] & 0x30) != 0)
            exception++;

        tempf = (temp_raw - 400) * 0.1;
        if (tempf < -40.0 || tempf > 158.0) {
            decoder_logf(decoder, 1, __func__, "Atlas 0x%04X Ch %s, invalid temperature: %0.1f F",
                         sensor_id, channel_str, tempf);
            return DECODE_FAIL_SANITY;
        }


        // Fail sanity check over 100% humidity
        // Allow 0 because very low battery or defective sensor will report
        // those values.
        humidity = (bb[6] & 0x7f);
        if (humidity > 100) {
            decoder_logf(decoder, 1, __func__, "0x%04X Ch %s : Impossible humidity: %d %%rH",
                         sensor_id, channel_str, humidity);
            return DECODE_FAIL_SANITY;
        }

        if (humidity == 0)
            exception++;


        /* clang-format off */
        data = data_append(data,
                "temperature_F",    "temperature",  DATA_FORMAT,    "%.1f F",       DATA_DOUBLE, tempf,
                "humidity",         NULL,           DATA_FORMAT,    "%u %%",        DATA_INT,    humidity,
                NULL);
        /* clang-format on */
    }

    if (message_type == ACURITE_MSGTYPE_ATLAS_WNDSPD_RAIN ||
            message_type == ACURITE_MSGTYPE_ATLAS_WNDSPD_RAIN_LTNG) {
        // Wind speed, wind direction, and rain fall

        // Wind direction is in degrees, 0-360, only 9 bits needed
        // but historically decoded as 10 bits.
        // There seems to be 11 bits available
        // As with temperatuve message, flag msg if those two extra bits
        // are ever non-zero so they can be investigated
        // Note: output as float, but currently can only be decoded an integer
        wind_dir = ((bb[4] & 0x1f) << 5) | ((bb[5] & 0x7c) >> 2);
        if ((bb[4] & 0x30) != 0)
            exception++;

        if (wind_dir > 360) {
            decoder_logf(decoder, 1, __func__, "Atlas 0x%04X Ch %s, invalid wind direction: %0.1fF",
                         sensor_id, channel_str, wind_dir);
            return DECODE_FAIL_SANITY;
        }

        // range: 0 to 5.11 in, 0.01 inch increments, accumulated
        // JRH: Confirmed 9 bits, counter rolls over after 5.11 inches
        raincounter = ((bb[5] & 0x03) << 7) | (bb[6] & 0x7F);

        /* clang-format off */
        data = data_append(data,
                "wind_dir_deg",     NULL,           DATA_FORMAT,    "%.1f",         DATA_DOUBLE, wind_dir,
                "rain_in",          "Rainfall Accumulation", DATA_FORMAT, "%.2f in", DATA_DOUBLE, raincounter * 0.01f,
                NULL);
        /* clang-format on */
    }

    if (message_type == ACURITE_MSGTYPE_ATLAS_WNDSPD_UV_LUX ||
            message_type == ACURITE_MSGTYPE_ATLAS_WNDSPD_UV_LUX_LTNG) {
        // Wind speed, UV Index, Light Intensity, and optionally Lightning

        // Spec UV index is 0-16 (but can only be 0-15)
        int uv  = (bb[4] & 0x0f);

        // Light intensity 0 - 120,000 lumens / 10
        // 14 bits are available (0-16,383)
        int lux = ((bb[5] & 0x7f) << 7) | (bb[6] & 0x7F);
        if (lux > 12000) {
            decoder_logf(decoder, 1, __func__, "Atlas 0x%04X Ch %s, invalid lux %d",
                         sensor_id, channel_str, lux);
            return DECODE_FAIL_SANITY;
        }

        /* clang-format off */
        data = data_append(data,
                "uv",               NULL,           DATA_INT, uv,
                "lux",              NULL,           DATA_INT, lux * 10,
                NULL);
        /* clang-format on */
    }

    if ((message_type == ACURITE_MSGTYPE_ATLAS_WNDSPD_TEMP_HUM_LTNG ||
                message_type == ACURITE_MSGTYPE_ATLAS_WNDSPD_RAIN_LTNG ||
                message_type == ACURITE_MSGTYPE_ATLAS_WNDSPD_UV_LUX_LTNG)) {

        // @todo decode strike_distance to miles or KM.
        int strike_count    = ((bb[7] & 0x7f) << 2) | ((bb[8] & 0x60) >> 5);
        int strike_distance = bb[8] & 0x1f;

        /* clang-format off */
        data = data_append(data,
                "strike_count",         NULL,           DATA_INT, strike_count,
                "strike_distance",      NULL,           DATA_INT, strike_distance,
                NULL);
        /* clang-format on */
    }

    // @todo only do this if exception != 0, but would be somewhat incompatible
    data = data_append(data,
            "exception",        "data_exception",   DATA_INT,    exception,
            "raw_msg",          "raw_message",      DATA_STRING, raw_str,
            NULL);

    decoder_output_data(decoder, data);

    return 1; // one valid message decoded
}

/**
Acurite 592TXR Temperature Humidity sensor decoder.

Also:
- Acurite 592TX (without humidity sensor)

Message Type 0x04, 7 bytes

| Byte 0    | Byte 1    | Byte 2    | Byte 3    | Byte 4    | Byte 5    | Byte 6    |
| --------- | --------- | --------- | --------- | --------- | --------- | --------- |
| CCII IIII | IIII IIII | pB00 0100 | pHHH HHHH | p??T TTTT | pTTT TTTT | KKKK KKKK |


- C: Channel 00: C, 10: B, 11: A, (01 is invalid)
- I: Device ID (14 bits)
- B: Battery, 1 is battery OK, 0 is battery low
- M: Message type (6 bits), 0x04
- T: Temperature Celsius (11 - 14 bits?), + 1000 * 10
- H: Relative Humidity (%) (7 bits)
- K: Checksum (8 bits)
- p: Parity bit

Notes:

- Temperature
  - Encoded as Celsius + 1000 * 10
  - only 11 bits needed for specified range -40 C to 70 C (-40 F - 158 F)
  - However 14 bits available for temperature, giving possible range of -100 C to 1538.4 C
  - @todo - check if high 3 bits ever used for anything else

*/
static int acurite_tower_decode(r_device *decoder, bitbuffer_t *bitbuffer, uint8_t *bb)
{
    // MIC (checksum, parity) validated in calling function

    (void)bitbuffer;
    int exception = 0;
    char const* channel_str = acurite_getChannel(bb[0]);
    int sensor_id = ((bb[0] & 0x3f) << 8) | bb[1];
    int battery_low = (bb[2] & 0x40) == 0;

    // Spec is relative humidity 1-99%
    // Allowing value of 0, very low battery or broken sensor can return 0% or 1%
    int humidity = (bb[3] & 0x7f);
    if (humidity > 100 && humidity != 127) {
        decoder_logf(decoder, 1, __func__, "0x%04X Ch %s : invalid humidity: %d %%rH",
                sensor_id, channel_str, humidity);
        return DECODE_FAIL_SANITY;
    }

    // temperature encoding used by "tower" sensors 592txr
    // 14 bits available after removing both parity bits.
    // 11 bits needed for specified range -40 C to 70 C (-40 F - 158 F)
    // Possible ranges are -100 C to 1538.4 C, but most of that range
    // is not possible on Earth.
    // pIII IIII pIII IIII
    int temp_raw = ((bb[4] & 0x7F) << 7) | (bb[5] & 0x7F);
    float tempc = (temp_raw - 1000) * 0.1f;
    if (tempc < -40 || tempc > 70) {
        decoder_logf(decoder, 1, __func__, "0x%04X Ch %s : invalid temperature: %0.2f C",
                sensor_id, channel_str, tempc);
        return DECODE_FAIL_SANITY;
    }

    // flag if bits 12-14 of temperature are ever non-zero
    // so they can be investigated for other possible information
    if ((temp_raw & 0x3800) != 0)
        exception++;

    data_t* data;
    /* clang-format off */
    data = data_make(
            "model",                "",             DATA_STRING, "Acurite-Tower",
            "id",                   "",             DATA_INT,    sensor_id,
            "channel",              NULL,           DATA_STRING, channel_str,
            "battery_ok",           "Battery",      DATA_INT,    !battery_low,
            "temperature_C",        "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, tempc,
            "humidity",             "Humidity",     DATA_COND, humidity != 127, DATA_FORMAT, "%u %%", DATA_INT,    humidity,
            "mic",                  "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    if (exception)
        data_append_exception(data, exception, bb, ACURITE_TXR_BYTELEN);

    decoder_output_data(decoder, data);

    return 1;
}

/**
Acurite 1190/1192 leak detector

Note: it seems like Acurite has deleted this product and
related information from their website so specs, manual, etc.
aren't easy to find

*/
static int acurite_1190_decode(r_device *decoder, bitbuffer_t *bitbuffer, uint8_t *bb)
{
    (void)bitbuffer;
    // Channel is the first two bits of the 0th byte
    // but only 3 of the 4 possible values are valid
    char const* channel_str = acurite_getChannel(bb[0]);

    // Tower sensor ID is the last 14 bits of byte 0 and 1
    // CCII IIII | IIII IIII
    int sensor_id = ((bb[0] & 0x3f) << 8) | bb[1];

    // Battery status is the 7th bit 0x40. 1 = normal, 0 = low
    int battery_low = (bb[2] & 0x40) == 0;

    // Leak indicator bit is the 5th bit of byte 3. 1 = wet, 0 = dry
    int is_wet = (bb[3] & 0x10) >> 4;

    data_t* data;
    /* clang-format off */
    data = data_make(
            "model",                "",             DATA_STRING, "Acurite-Leak",
            "id",                   "",             DATA_INT,    sensor_id,
            "channel",              NULL,           DATA_STRING, channel_str,
            "battery_ok",           "Battery",      DATA_INT,    !battery_low,
            "leak_detected",        "Leak",         DATA_INT,    is_wet,
            "mic",                  "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);

    return 1;
}

/**
Decode Acurite 515 Refrigerator/Freezer sensors

Byte 0    | Byte 1    | Byte 2    | Byte 3    | Byte 4    | Byte 5
CCII IIII | IIII IIII | pBMM MMMM | bTTT TTTT | bTTT TTTT | KKKK KKKK

- C: Channel 00: C, 10: B, 11: A
- I: Device ID (14 bits), volatie, resets at power up
- B: Battery, 1 is battery OK, 0 is battery low
- M: Message type (6 bits), 0x8: Refrigerator, 0x9: Freezer
- T: Temperature Fahrenheit (14 bits?), + 1480 * 10
- K: Checksum (8 bits)
- p: Parity bit

*/
static int acurite_515_decode(r_device *decoder, bitbuffer_t *bitbuffer, uint8_t *bb)
{
    // length, MIC (checksum, parity) validated in calling function

    (void)bitbuffer;
    int exception = 0;
    char channel_type_str[3];
    uint8_t message_type = bb[2] & 0x3f;

    // Channel A, B, C, common with other Acurite devices
    char const* channel_str = acurite_getChannel(bb[0]);

    channel_type_str[0] = channel_str[0];

    if (message_type == ACURITE_MSGTYPE_515_REFRIGERATOR)
        channel_type_str[1] = 'R';
    else if (message_type == ACURITE_MSGTYPE_515_FREEZER)
        channel_type_str[1] = 'F';
    else {
        decoder_logf(decoder, 1, __func__, "unknown message type 0x02%x", message_type);
        return DECODE_FAIL_SANITY;
    }

    channel_type_str[2] = 0;

    // Sensor ID is the last 14 bits of byte 0 and 1
    // CCII IIII | IIII IIII
    // The sensor ID changes on each power-up of the sensor.
    uint16_t sensor_id = ((bb[0] & 0x3f) << 8) | bb[1];

    // temperature encoding 14 bits after removing both parity bits.
    // Spec range from Manual: -40 F to 158 F  (-40 to 70 C)
    // Offset to avoid negative values is 1480
    // Possible encoding range with 14 bits (0-16383) is -148.0 F to 1490.3 F
    // Only 12 bits needed to represent -40 F to 158 F with encoding offset of 1480.
    //   encoding range at 12 bits with +1480 offset: -148.0 F to +261.5 F
    int temp_raw = ((bb[3] & 0x7F) << 7) | (bb[4] & 0x7F);
    float tempf = (temp_raw - 1480) * 0.1f;
    if (tempf < -40.0 || tempf > 158.0) {
        decoder_logf(decoder, 1, __func__, "515 0x%04X Ch %s, invalid temperature: %0.1f F",
                     sensor_id, channel_str, tempf);
        return DECODE_FAIL_SANITY;
    }

    // flag if bits 13 - 14 of temperature are ever non-zero
    // so they can be investigated
    if ((temp_raw & 0x3000) != 0)
        exception++;

    // Battery status is the 7th bit 0x40. 1 = normal, 0 = low
    int battery_low = (bb[2] & 0x40) == 0;

    data_t* data;
    /* clang-format off */
    data = data_make(
        "model",                "",             DATA_STRING, "Acurite-515",
        "id",                   "",             DATA_INT,    sensor_id,
        "channel",              NULL,           DATA_STRING, channel_type_str,
        "battery_ok",           "Battery",      DATA_INT,    !battery_low,
        "temperature_F",        "Temperature",  DATA_FORMAT, "%.1f F", DATA_DOUBLE, tempf,
        "mic",                  "Integrity",    DATA_STRING, "CHECKSUM",
        NULL);
    /* clang-format on */

    if (exception)
        data_append_exception(data, exception, bb, ACURITE_515_BYTELEN);

    decoder_output_data(decoder, data);

    return 1;
}

/**
Check Acurite TXR message integrity (length, checksum, parity)

Need to pass in expected length - correct number of bytes for
that message type.

Return 0 for valid roe or DECODE_ABORT_LENGHT, DECODE_FAIL_MIC, DECODE_FAIL_SANITY

Long rows with extra bits/bytes (from demod/bit slicing)
will be accepted as long the bytes up to the expected length
pass checksum and parity tests.
*/
static int acurite_txr_check(r_device *decoder, uint8_t const bb[], unsigned browlen, unsigned explen)
{

    // Currently shortest Acurite "TXR" message is 6 bytes
    // 5 bytes could possibly be valid, but would only have
    // a single data byte after Channel, ID, message type, and checksum
    // Really short rows (1-2) bytes, should be rejected quietly earlier
    // so real error types can be seen
    if (browlen < 6)
        return DECODE_ABORT_LENGTH;

    if (browlen < explen) {
        decoder_log_bitrow(decoder, 1, __func__, bb, browlen * 8, "wrong length for msg type");
        return DECODE_ABORT_LENGTH;
    }

    // 8 bit checksum in the last byte
    if ((add_bytes(bb, explen - 1) & 0xff) != bb[explen - 1]) {
        decoder_log_bitrow(decoder, 1, __func__, bb, browlen * 8, "bad checksum");
        return DECODE_FAIL_MIC;
    }

    // Verify parity bits
    // Bytes 2 ... n-1 should all have even parity
    // (ID bytes and checksum byte are all 8 bit, so no parity check)
    int parity = parity_bytes(&bb[2], explen - 3);

    if (parity) {
        decoder_log_bitrow(decoder, 1, __func__, bb, browlen * 8,"bad parity");
        return DECODE_FAIL_MIC;
    }

    // All of these devices have channel (A, B, C) in two bits (mask 0c0) of byte 0
    // 00: C, 10: B, 11: A, (01 aka 'E' is invalid)
    // check sanity to cut down an bad messages that pass MIC checks
    char const *channel_str = acurite_getChannel(bb[0]);
    if (*channel_str == 'E') {
        uint8_t message_type = bb[2] & 0x3f;
        decoder_logf(decoder, 1, __func__,
                     "bad channel Ch %s, msg type 0x%02x, msg len %d",
                     channel_str, message_type, browlen);
        return DECODE_FAIL_SANITY;
    }

    return 0;
}

/**
Process messages for Acurite weather stations, tower and related sensors
@sa acurite_1190_decode()
@sa acurite_515_decode()
@sa acurite_6045_decode()
@sa acurite_899_decode()
#sa acurite_3n1_decode()
@sa acurite_5n1_decode()
@sa acurite_atlas_decode()
@sa acurite_tower_decode()

This callback is used for devices that use a very similar message format:

- 592TXR / 592TX / 6002RM / 6044m Tower sensor and related temperature/humidity sensors
- Atlas (7-in-1) Weather Station
- Iris (5-in-1) weather station
- Notos (3-in-1) Weather station
- 6045M Lightning Detector with Temperature and Humidity
- 899 Rain Fall Gauge
- 515 Refrigerator/Freezer sensors
- 1190/1192 Water alarm

These devices have a message type in the 3rd byte and an 8 bit checksum
in the last byte.

*/
static int acurite_txr_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int decoded = 0;
    int error_ret = 0;
    int ret = 0;
    uint8_t *bb;
    uint8_t message_type;

    bitbuffer_invert(bitbuffer);

    for (uint16_t brow = 0; brow < bitbuffer->num_rows; ++brow) {
        int row_bit_cnt = bitbuffer->bits_per_row[brow];
        int browlen = row_bit_cnt / 8;  // assumption: safe to round down, extra bits are spurious

        bb = bitbuffer->bb[brow];

        // Known messages in this family are between 6 and 10 bytes
        if (browlen < 6) {
            continue; // quietly skip short rows
        }

        // Currently known longest message is 10 bytes (Atlas with lightning sensorr)
        if (browlen > 10) {
            decoder_logf(decoder, 2, __func__, "Skipping wrong len row %u bits %u, bytes %d",
                         brow, row_bit_cnt, browlen);
            error_ret = DECODE_ABORT_LENGTH;
            continue;
        }

        decoder_logf(decoder, 2, __func__,
                     "row %u bits %u, bytes %d, extra bits %d, msg type 0x%02x",
                     brow, row_bit_cnt, browlen, row_bit_cnt % 8, bb[2] & 0x3f);


        // quietly ignore rows of zeros (ID, msg type, checksum)
        if (bb[0] == 0 && bb[1] == 0 && bb[2] == 0 && bb[browlen - 1] == 0)
            continue;

        // acurite sensors with a common format have a message type
        // in the lower 6 bits of the 3rd byte.
        // Format: PBMMMMMM
        // P = Parity
        // B = Battery Normal
        // M = Message type
        message_type = bb[2] & 0x3f;

        // Check so that unknown message type can be flagged
        // and dispatching to decoders can be easier to maintain
        switch(message_type) {
            case ACURITE_MSGTYPE_1190_DETECTOR:
            case ACURITE_MSGTYPE_TOWER_SENSOR:
            case ACURITE_MSGTYPE_6045M:
            case ACURITE_MSGTYPE_5N1_WINDSPEED_WINDDIR_RAINFALL:
            case ACURITE_MSGTYPE_5N1_WINDSPEED_TEMP_HUMIDITY:
            case ACURITE_MSGTYPE_ATLAS_WNDSPD_TEMP_HUM:
            case ACURITE_MSGTYPE_ATLAS_WNDSPD_RAIN:
            case ACURITE_MSGTYPE_ATLAS_WNDSPD_UV_LUX:
            case ACURITE_MSGTYPE_ATLAS_WNDSPD_TEMP_HUM_LTNG:
            case ACURITE_MSGTYPE_ATLAS_WNDSPD_RAIN_LTNG:
            case ACURITE_MSGTYPE_ATLAS_WNDSPD_UV_LUX_LTNG:
            case ACURITE_MSGTYPE_515_REFRIGERATOR:
            case ACURITE_MSGTYPE_515_FREEZER:
            case ACURITE_MSGTYPE_3N1_WINDSPEED_TEMP_HUMIDITY:
            case ACURITE_MSGTYPE_899_RAINFALL:
                break;

            default:
                decoder_log_bitrow(decoder, 1, __func__, bb, row_bit_cnt,
                                   "Unknown message type");
                error_ret = DECODE_FAIL_SANITY;
                continue;
                break;
        }

        // Check message type and dispatch to appropriate decoders
        // NOTE: since we are processing each row, do not return
        // until all rows have been processed
        if (message_type == ACURITE_MSGTYPE_TOWER_SENSOR) {
            if ((ret = acurite_txr_check(decoder, bb, browlen, ACURITE_TXR_BYTELEN)) != 0) {
                error_ret = ret;
            } else {
                    if ((ret = acurite_tower_decode(decoder, bitbuffer, bb)) > 0) {
                    decoded += ret;
                } else if (ret < 0) {
                    error_ret = ret;
                }
            }
        }

        if (message_type == ACURITE_MSGTYPE_1190_DETECTOR) {
            if ((ret = acurite_txr_check(decoder, bb, browlen, ACURITE_1190_BYTELEN)) != 0) {
                error_ret = ret;
            } else {
                    if ((ret = acurite_1190_decode(decoder, bitbuffer, bb)) > 0) {
                    decoded += ret;
                } else if (ret < 0) {
                    error_ret = ret;
                }
            }
        }

        if (message_type == ACURITE_MSGTYPE_6045M) {
            if ((ret = acurite_txr_check(decoder, bb, browlen, ACURITE_6045_BYTELEN)) != 0) {
                error_ret = ret;
            } else {
                if ((ret = acurite_6045_decode(decoder, bitbuffer, brow)) > 0) {
                    decoded += ret;
                } else if (ret < 0) {
                    error_ret = ret;
                }
            }
        }

        if (message_type == ACURITE_MSGTYPE_515_REFRIGERATOR ||
            message_type == ACURITE_MSGTYPE_515_FREEZER) {
            if ((ret = acurite_txr_check(decoder, bb, browlen, ACURITE_515_BYTELEN)) != 0) {
                error_ret = ret;
            } else {
                if ((ret = acurite_515_decode(decoder, bitbuffer, bb)) > 0) {
                    decoded += ret;
                } else if (ret < 0) {
                    error_ret = ret;
                }
            }
        }

        if (message_type == ACURITE_MSGTYPE_5N1_WINDSPEED_TEMP_HUMIDITY ||
            message_type == ACURITE_MSGTYPE_5N1_WINDSPEED_WINDDIR_RAINFALL) {
            if ((ret = acurite_txr_check(decoder, bb, browlen, ACURITE_5N1_BYTELEN)) != 0) {
                error_ret = ret;
            } else {
                if ((ret = acurite_5n1_decode(decoder, bitbuffer, bb)) > 0) {
                    decoded += ret;
                } else if (ret < 0) {
                    error_ret = ret;
                }
            }
        }

        if (message_type == ACURITE_MSGTYPE_3N1_WINDSPEED_TEMP_HUMIDITY) {
            /*
              @todo - does 3n1 use parity checking?
              3n1 g001 in rtl_433_test has odd parity the 2nd to last byte in both copies
              but g002 passes parity check
            */

            if (browlen < ACURITE_3N1_BYTELEN) {
                decoder_log_bitrow(decoder, 1, __func__, bb, browlen * 8, "3n1 wrong length");
                error_ret = DECODE_ABORT_LENGTH;
                continue;
            }

            if ((add_bytes(bb, ACURITE_3N1_BYTELEN - 1) & 0xff) !=
                bb[ACURITE_3N1_BYTELEN - 1]) {
                decoder_log_bitrow(decoder, 1, __func__, bb, browlen * 8, "bad checksum");
                error_ret = DECODE_FAIL_MIC;
                continue;
            }

            if ((ret = acurite_3n1_decode(decoder, bitbuffer, bb)) > 0) {
                decoded += ret;
            } else if (ret < 0) {
                error_ret = ret;
            }
        }

        if (message_type == ACURITE_MSGTYPE_899_RAINFALL) {
            /*
              @todo - does the 899 use parity checking?
              The available sample shows a parity bit in the message byte
              but there isn't enough accumulated rain in the data bytes
              to see if parity is used
            */
            if ((ret = acurite_txr_check(decoder, bb, browlen, ACURITE_899_BYTELEN)) != 0) {
                error_ret = ret;
            } else {
                if ((ret = acurite_899_decode(decoder, bitbuffer, bb)) > 0) {
                    decoded += ret;
                } else if (ret < 0) {
                    error_ret = ret;
                }
            }
        }

        // process Atlas
        switch(message_type) {
            // Atlas messages without lightning sensor installed - 8 bytes
            case ACURITE_MSGTYPE_ATLAS_WNDSPD_TEMP_HUM:
            case ACURITE_MSGTYPE_ATLAS_WNDSPD_RAIN:
            case ACURITE_MSGTYPE_ATLAS_WNDSPD_UV_LUX:
                if ((ret = acurite_txr_check(decoder, bb, browlen, ACURITE_ATLAS_BYTELEN)) != 0) {
                    error_ret = ret;
                } else {
                    if ((ret = acurite_atlas_decode(decoder, bitbuffer, brow)) > 0) {
                        decoded += ret;
                    } else if (ret < 0) {
                        error_ret = ret;
                    }
                }
                break;

            // Atlas messages with lightning sensor installed - 10 bytes
            case ACURITE_MSGTYPE_ATLAS_WNDSPD_TEMP_HUM_LTNG:
            case ACURITE_MSGTYPE_ATLAS_WNDSPD_RAIN_LTNG:
            case ACURITE_MSGTYPE_ATLAS_WNDSPD_UV_LUX_LTNG:
                if ((ret = acurite_txr_check(decoder, bb, browlen, ACURITE_ATLAS_LTNG_BYTELEN)) != 0) {
                    error_ret = ret;
                } else {
                    if ((ret = acurite_atlas_decode(decoder, bitbuffer, brow)) > 0) {
                        decoded += ret;
                    } else if (ret < 0) {
                        error_ret = ret;
                    }
                }
                break;

        }

        decoder_logf(decoder, 2, __func__,
                     "stats: row %u, msg type 0x%02x, bytes %d, decoded %d, error %d",
                     brow, message_type, browlen, decoded, error_ret);

    }

    if (decoded > 0)
        return decoded;
    else
        return error_ret;
}


/**
Acurite 00986 Refrigerator / Freezer Thermometer.

Includes two sensors and a display, labeled 1 and 2,
by default 1 - Refrigerator, 2 - Freezer.

PPM, 5 bytes, sent twice, no gap between repeaters
start/sync pulses two short, with short gaps, followed by
4 long pulse/gaps.

@todo, the 2 short sync pulses get confused as data.

Data Format - 5 bytes, sent LSB first, reversed:

    TT II II SS CC
- T - Temperature in Fahrenheit, integer, MSB = sign.
      Encoding is "Sign and magnitude"
- I - 16 bit sensor ID
      changes at each power up
- S - status/sensor type
      0x01 = Sensor 2
      0x02 = low battery
- C = CRC (CRC-8 poly 0x07, little-endian)

@todo
- needs new PPM demod that can separate out the short
  start/sync pulses which confuse things and cause
  one data bit to be lost in the check value.

2018-04 A user with a dedicated receiver indicated the
  possibility that the transmitter actually drops the
  last bit instead of the demod.

leaving some of the debugging code until the missing
bit issue gets resolved.
*/
static int acurite_986_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int const browlen = 5;
    uint8_t *bb, sensor_num, status, crc, crcc;
    uint8_t br[8];
    int8_t tempf; // Raw Temp is 8 bit signed Fahrenheit
    uint16_t sensor_id, valid_cnt = 0;
    char sensor_type;
    char const *channel_str;
    int battery_low;
    data_t *data;

    int result = 0;

    for (uint16_t brow = 0; brow < bitbuffer->num_rows; ++brow) {

        decoder_logf(decoder, 2, __func__, "row %u bits %u, bytes %d", brow, bitbuffer->bits_per_row[brow], browlen);

        if (bitbuffer->bits_per_row[brow] < 39 || bitbuffer->bits_per_row[brow] > 43) {
            if (bitbuffer->bits_per_row[brow] > 16)
                decoder_log(decoder, 2, __func__,"skipping wrong len");
            result = DECODE_ABORT_LENGTH;
            continue; // DECODE_ABORT_LENGTH
        }
        bb = bitbuffer->bb[brow];

        // Reduce false positives
        // may eliminate these with a better PPM (precise?) demod.
        if ((bb[0] == 0xff && bb[1] == 0xff && bb[2] == 0xff) ||
                (bb[0] == 0x00 && bb[1] == 0x00 && bb[2] == 0x00)) {
            result = DECODE_ABORT_EARLY;
            continue; // DECODE_ABORT_EARLY
        }

        // Reverse the bits, msg sent LSB first
        for (int i = 0; i < browlen; i++)
            br[i] = reverse8(bb[i]);

        decoder_log_bitrow(decoder, 1, __func__, br, browlen * 8, "reversed");

        tempf = br[0];
        sensor_id = (br[1] << 8) + br[2];
        status = br[3];
        sensor_num = (status & 0x01) + 1;
        status = status >> 1;
        battery_low = ((status & 1) == 1);

        // By default Sensor 1 is the Freezer, 2 Refrigerator
        sensor_type = sensor_num == 2 ? 'F' : 'R';
        channel_str = sensor_num == 2 ? "2F" : "1R";

        crc = br[4];
        crcc = crc8le(br, 4, 0x07, 0);

        if (crcc != crc) {
            decoder_logf_bitrow(decoder, 2, __func__, br, browlen * 8, "bad CRC: %02x -", crc8le(br, 4, 0x07, 0));
            // HACK: rct 2018-04-22
            // the message is often missing the last 1 bit either due to a
            // problem with the device or demodulator
            // Add 1 (0x80 because message is LSB) and retry CRC.
            if (crcc == (crc | 0x80)) {
                decoder_logf(decoder, 2, __func__, "CRC fix %02x - %02x", crc, crcc);
            }
            else {
                continue; // DECODE_FAIL_MIC
            }
        }

        if (tempf & 0x80) {
            tempf = (tempf & 0x7f) * -1;
        }

        decoder_logf(decoder, 1, __func__, "sensor 0x%04x - %d%c: %d F", sensor_id, sensor_num, sensor_type, tempf);

        /* clang-format off */
        data = data_make(
                "model",            "",             DATA_STRING, "Acurite-986",
                "id",               NULL,           DATA_INT,    sensor_id,
                "channel",          NULL,           DATA_STRING, channel_str,
                "battery_ok",       "Battery",      DATA_INT,    !battery_low,
                "temperature_F",    "temperature",  DATA_FORMAT, "%f F", DATA_DOUBLE,    (float)tempf,
                "status",           "status",       DATA_INT,    status,
                "mic",              "Integrity",    DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);

        valid_cnt++;
    }

    if (valid_cnt)
        return 1;

    return result;
}

/**
Acurite 606 Temperature sensor

*/
static int acurite_606_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b;
    int row;
    int16_t temp_raw; // temperature as read from the data packet
    float temp_c;     // temperature in C
    int battery_ok;   // the battery status: 1 is good, 0 is low
    int channel;      // the channel
    int button;       // the reset button: 1: pressed
    int sensor_id;    // the sensor ID - basically a random number that gets reset whenever the battery is removed

    row = bitbuffer_find_repeated_row(bitbuffer, 3, 32); // expected are 6 rows
    if (row < 0)
        return DECODE_ABORT_EARLY;

    if (bitbuffer->bits_per_row[row] > 33)
        return DECODE_ABORT_LENGTH;

    b = bitbuffer->bb[row];

    if (b[4] != 0)
        return DECODE_FAIL_SANITY;

    // reject all blank messages
    if (b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 0)
        return DECODE_FAIL_SANITY;

    // calculate the checksum and only continue if we have a matching checksum
    uint8_t chk = lfsr_digest8(b, 3, 0x98, 0xf1);
    if (chk != b[3])
        return DECODE_FAIL_MIC;

    // Processing the temperature:
    // Upper 4 bits are stored in nibble 1, lower 8 bits are stored in nibble 2
    // upper 4 bits of nibble 1 are reserved for other usages (e.g. battery status)
    sensor_id  = b[0];
    battery_ok = (b[1] & 0x80) >> 7;
    channel    = ((b[1] & 0x30) >> 4)+1; // Channel A,B,C / 1,2,3
    button     = (b[1] & 0x40) >> 6; // SensorTX Button
    temp_raw   = (int16_t)((b[1] << 12) | (b[2] << 4));
    temp_raw   = temp_raw >> 4;
    temp_c     = temp_raw * 0.1f;

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, "Acurite-606TX",
            "id",               "",             DATA_INT, sensor_id,
            "channel",          "Channel",      DATA_INT,   channel,
            "battery_ok",       "Battery",      DATA_INT,    battery_ok,
            "button",           "Button" ,      DATA_INT,   button,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/**
Acurite 590TX temperature/humidity sensor

*/
static int acurite_590tx_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b;
    int row;
    int sensor_id;  // the sensor ID - basically a random number that gets reset whenever the battery is removed
    int battery_ok; // the battery status: 1 is good, 0 is low
    int channel;
    int humidity;
    int temp_raw; // temperature as read from the data packet
    float temp_c; // temperature in C

    row = bitbuffer_find_repeated_row(bitbuffer, 3, 25); // expected are min 3 rows
    if (row < 0)
        return DECODE_ABORT_EARLY;

    if (bitbuffer->bits_per_row[row] > 25)
        return DECODE_ABORT_LENGTH;

    b = bitbuffer->bb[row];

    if (b[4] != 0) // last byte should be zero
        return DECODE_FAIL_SANITY;

    // reject rows that are mostly zero
    if (b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 0)
        return DECODE_FAIL_SANITY;

    // parity check: odd parity on bits [0 .. 10]
    // i.e. 8 bytes and another 2 bits.
    uint8_t parity = b[0]; // parity as byte
    parity = (parity >> 4) ^ (parity & 0xF); // fold to nibble
    parity = (parity >> 2) ^ (parity & 0x3); // fold to 2 bits
    parity ^= b[1] >> 6; // add remaining bits
    parity = (parity >> 1) ^ (parity & 0x1); // fold to 1 bit

    if (!parity) {
        decoder_log(decoder, 1, __func__, "parity check failed");
        return DECODE_FAIL_MIC;
    }

    // Processing the temperature:
    // Upper 4 bits are stored in nibble 1, lower 8 bits are stored in nibble 2
    // upper 4 bits of nibble 1 are reserved for other usages (e.g. battery status)
    sensor_id = b[0] & 0xFE; //first 6 bits and it changes each time it resets or change the battery
    battery_ok = (b[0] & 0x01); //1=ok, 0=low battery
    //next 2 bits are checksum
    //next two bits are identify ID (maybe channel ?)
    channel = (b[1] >> 4) & 0x03;

    temp_raw = (int16_t)(((b[1] & 0x0F) << 12) | (b[2] << 4));
    temp_raw = temp_raw >> 4;
    temp_c   = (temp_raw - 500) * 0.1f; // NOTE: there seems to be a 50 degree offset?

    if (temp_raw >= 0 && temp_raw <= 100) // NOTE: no other way to differentiate humidity from temperature?
        humidity = temp_raw;
    else
        humidity = -1;

    /* clang-format off */
     data = data_make(
            "model",            "",             DATA_STRING, "Acurite-590TX",
            "id",               "",             DATA_INT,    sensor_id,
            "channel",          "Channel",      DATA_INT,    channel,
            "battery_ok",       "Battery",      DATA_INT,    battery_ok,
            "humidity",         "Humidity",     DATA_COND,   humidity != -1,    DATA_INT,    humidity,
            "temperature_C",    "Temperature",  DATA_COND,   humidity == -1,    DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "mic",              "Integrity",    DATA_STRING, "PARITY",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/**
Acurite 00275rm Room Monitor sensors

*/
static int acurite_00275rm_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int result = 0;
    bitbuffer_invert(bitbuffer);

    // This sensor repeats a signal three times. Combine as fallback.
    uint8_t *b_rows[3] = {0};
    int n_rows         = 0;
    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        if (n_rows < 3 && bitbuffer->bits_per_row[row] == 88) {
            b_rows[n_rows] = bitbuffer->bb[row];
            n_rows++;
        }
    }

    // Combine signal if exactly three repeats were found
    if (n_rows == 3) {
        bitbuffer_add_row(bitbuffer);
        uint8_t *b = bitbuffer->bb[bitbuffer->num_rows - 1];
        for (int i = 0; i < 11; ++i) {
            // The majority bit count wins
            b[i] = (b_rows[0][i] & b_rows[1][i]) |
                    (b_rows[1][i] & b_rows[2][i]) |
                    (b_rows[2][i] & b_rows[0][i]);
        }
        bitbuffer->bits_per_row[bitbuffer->num_rows - 1] = 88;
    }

    // Output the first valid row
    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        if (bitbuffer->bits_per_row[row] != 88) {
            result = DECODE_ABORT_LENGTH;
            continue; // return DECODE_ABORT_LENGTH;
        }
        uint8_t *b = bitbuffer->bb[row];

        // Check CRC
        if (crc16lsb(b, 11, 0x00b2, 0x00d0) != 0) {
            decoder_log_bitrow(decoder, 1, __func__, b, 11 * 8, "sensor bad CRC");
            result = DECODE_FAIL_MIC;
            continue; // return DECODE_FAIL_MIC;
        }

        //  Decode common fields
        int id          = (b[0] << 16) | (b[1] << 8) | b[3];
        int battery_low = (b[2] & 0x40) == 0;
        int model_flag  = (b[2] & 1);
        int temp_raw    = (b[4] << 4) | (b[5] >> 4);
        float tempc     = (temp_raw - 1000) * 0.1f;
        int probe       = b[5] & 3;
        int humidity    = ((b[6] & 0x1f) << 2) | (b[7] >> 6);

        //  Water probe (detects water leak)
        int water = (b[7] & 0x0f) == 15; // valid only if (probe == 1)
        //  Soil probe (detects temperature)
        int ptemp_raw = ((b[7] & 0x0f) << 8) | (b[8]); // valid only if (probe == 2 || probe == 3)
        float ptempc = (ptemp_raw - 1000) * 0.1f;
        //  Spot probe (detects temperature and humidity)
        int phumidity = b[9] & 0x7f; // valid only if (probe == 3)

        /* clang-format off */
        data_t *data = data_make(
                "model",            "",             DATA_STRING,    model_flag ? "Acurite-00275rm" : "Acurite-00276rm",
                "subtype",          "Probe",        DATA_INT,       probe,
                "id",               "",             DATA_INT,       id,
                "battery_ok",       "Battery",      DATA_INT,       !battery_low,
                "temperature_C",    "Celsius",      DATA_FORMAT,    "%.1f C",  DATA_DOUBLE, tempc,
                "humidity",         "Humidity",     DATA_FORMAT,    "%u %%", DATA_INT,      humidity,
                "water",            "",             DATA_COND, probe == 1, DATA_INT,        water,
                "temperature_1_C",  "Celsius",      DATA_COND, probe == 2, DATA_FORMAT, "%.1f C",   DATA_DOUBLE, ptempc,
                "temperature_1_C",  "Celsius",      DATA_COND, probe == 3, DATA_FORMAT, "%.1f C",   DATA_DOUBLE, ptempc,
                "humidity_1",       "Humidity",     DATA_COND, probe == 3, DATA_FORMAT, "%u %%",    DATA_INT,    phumidity,
                "mic",              "Integrity",    DATA_STRING,    "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);

        return 1;
    }
    // Only returns the latest result, but better than nothing.
    return result;
}

static char const *const acurite_rain_gauge_output_fields[] = {
        "model",
        "id",
        "rain_mm",
        NULL,
};

r_device const acurite_rain_896 = {
        .name        = "Acurite 896 Rain Gauge",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 1000,
        .long_width  = 2000,
        .gap_limit   = 3500,
        .reset_limit = 5000,
        .decode_fn   = &acurite_rain_896_decode,
        .priority    = 10, // Eliminate false positives by letting oregon scientific v1 protocol go earlier
        .fields      = acurite_rain_gauge_output_fields,
};

static char const *const acurite_th_output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "temperature_C",
        "humidity",
        "status",
        "mic",
        NULL,
};

r_device const acurite_th = {
        .name        = "Acurite 609TXC Temperature and Humidity Sensor",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 1000,
        .long_width  = 2000,
        .gap_limit   = 3000,
        .reset_limit = 10000,
        .decode_fn   = &acurite_th_decode,
        .fields      = acurite_th_output_fields,
};

/*
 * For Acurite 592 TXR Temp/Humidity, but
 * Should match Acurite 592TX, 5-n-1, etc.
 */
static char const *const acurite_txr_output_fields[] = {
        "model",
        "message_type", // TODO: remove this
        "id",
        "channel",
        "sequence_num",
        "battery_ok",
        "leak_detected",
        "temperature_C",
        "temperature_F",
        "humidity",
        "wind_avg_mi_h",
        "wind_avg_km_h",
        "wind_dir_deg",
        "rain_in",
        "rain_mm",
        "storm_dist",
        "strike_count",
        "strike_distance",
        "uv",
        "lux",
        "active",
        "exception",
        "raw_msg",
        "rfi",
        "mic",
        NULL,
};

r_device const acurite_txr = {
        .name        = "Acurite 592TXR Temp/Humidity, 592TX Temp, 5n1 Weather Station, 6045 Lightning, 899 Rain, 3N1, Atlas",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 220,  // short pulse is 220 us + 392 us gap
        .long_width  = 408,  // long pulse is 408 us + 204 us gap
        .sync_width  = 620,  // sync pulse is 620 us + 596 us gap
        .gap_limit   = 500,  // longest data gap is 392 us, sync gap is 596 us
        .reset_limit = 4000, // packet gap is 2192 us
        .decode_fn   = &acurite_txr_callback,
        .fields      = acurite_txr_output_fields,
};

/*
 * Acurite 00986 Refrigerator / Freezer Thermometer
 *
 * Temperature only, Pulse Position
 *
 * A preamble: 2x of 216 us pulse + 276 us gap, 4x of 1600 us pulse + 1560 us gap
 * 39 bits of data: 220 us pulses with short gap of 520 us or long gap of 880 us
 * A transmission consists of two packets that run into each other.
 * There should be 40 bits of data though. But the last bit can't be detected.
 */
static char const *const acurite_986_output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "temperature_F",
        "status",
        "mic",
        NULL,
};

r_device const acurite_986 = {
        .name        = "Acurite 986 Refrigerator / Freezer Thermometer",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 520,
        .long_width  = 880,
        .gap_limit   = 1280,
        .reset_limit = 4000,
        .decode_fn   = &acurite_986_decode,
        .fields      = acurite_986_output_fields,
};

/*
 * Acurite 00606TX Tower Sensor
 *
 * Temperature only
 *
 */

static char const *const acurite_606_output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "temperature_C",
        "mic",
        NULL,
};

static char const *const acurite_590_output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "temperature_C",
        "humidity",
        "mic",
        NULL,
};

// actually tests/acurite/02/gfile002.cu8, check this
//.modulation     = OOK_PULSE_PWM,
//.short_width    = 576,
//.long_width     = 1076,
//.gap_limit      = 1200,
//.reset_limit    = 12000,
r_device const acurite_606 = {
        .name        = "Acurite 606TX Temperature Sensor",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 2000,
        .long_width  = 4000,
        .gap_limit   = 7000,
        .reset_limit = 10000,
        .decode_fn   = &acurite_606_decode,
        .fields      = acurite_606_output_fields,
};

static char const *const acurite_00275rm_output_fields[] = {
        "model",
        "subtype",
        "id",
        "battery_ok",
        "temperature_C",
        "humidity",
        "water",
        "temperature_1_C",
        "humidity_1",
        "mic",
        NULL,
};

r_device const acurite_00275rm = {
        .name        = "Acurite 00275rm,00276rm Temp/Humidity with optional probe",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 232, // short pulse is 232 us
        .long_width  = 420, // long pulse is 420 us
        .gap_limit   = 520, // long gap is 384 us, sync gap is 592 us
        .reset_limit = 708, // no packet gap, sync gap is 592 us
        .sync_width  = 632, // sync pulse is 632 us
        .decode_fn   = &acurite_00275rm_decode,
        .fields      = acurite_00275rm_output_fields,
};

r_device const acurite_590tx = {
        .name        = "Acurite 590TX Temperature with optional Humidity",
        .modulation  = OOK_PULSE_PPM, // OOK_PULSE_PWM,
        .short_width = 500,           // short pulse is 232 us
        .long_width  = 1500,          // long pulse is 420 us
        .gap_limit   = 1484,          // long gap is 384 us, sync gap is 592 us
        .reset_limit = 3000,          // no packet gap, sync gap is 592 us
        .sync_width  = 500,           // sync pulse is 632 us
        .decode_fn   = &acurite_590tx_decode,
        .fields      = acurite_590_output_fields,
};
