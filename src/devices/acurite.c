/*
 * Acurite weather stations and temperature / humidity sensors
 *
 * Copyright (c) 2015, Jens Jenson, Helge Weissig, David Ray Thompson, Robert Terzi
 *
 * Devices decoded:
 * - 5-n-1 weather sensor, Model; VN1TXC, 06004RM
 * - 5-n-1 pro weather sensor, Model: 06014RM
 * - 896 Rain gauge, Model: 00896
 * - 592TXR / 06002RM Tower sensor (temperature and humidity)
 *   (Note: Some newer sensors share the 592TXR coding for compatibility.
 * - 609TXC "TH" temperature and humidity sensor (609A1TX)
 * - Acurite 986 Refrigerator / Freezer Thermometer
 * - Acurite 606TX temperature sensor
 * - Acurite 6045M Lightning Detector (Work in Progress)
 * - Acurite 00275rm and 00276rm temp. and humidity with optional probe.
 */

#include "decoder.h"

// ** Acurite 5n1 functions **

#define ACURITE_TXR_BITLEN        56
#define ACURITE_5N1_BITLEN        64
#define ACURITE_6045_BITLEN        72

// ** Acurite known message types
#define ACURITE_MSGTYPE_TOWER_SENSOR                    0x04
#define ACURITE_MSGTYPE_6045M                           0x2f
#define ACURITE_MSGTYPE_5N1_WINDSPEED_WINDDIR_RAINFALL  0x31
#define ACURITE_MSGTYPE_5N1_WINDSPEED_TEMP_HUMIDITY     0x38
#define ACURITE_MSGTYPE_WINDSPEED_TEMP_HUMIDITY_3N1     0x20


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
static char chLetter[4] = {'C','E','B','A'}; // 'E' stands for error

static char acurite_getChannel(uint8_t byte)
{
    int channel = (byte & 0xC0) >> 6;
    return chLetter[channel];
}


static int acurite_rain_gauge_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitrow_t *bb = bitbuffer->bb;
    // This needs more validation to positively identify correct sensor type, but it basically works if message is really from acurite raingauge and it doesn't have any errors
    if ((bitbuffer->bits_per_row[0] >= 24) && (bb[0][0] != 0) && (bb[0][1] != 0) && (bb[0][2]!=0) && (bb[0][3] == 0) && (bb[0][4] == 0)) {
        float total_rain = ((bb[0][1]&0xf)<<8)+ bb[0][2];
        total_rain /= 2; // Sensor reports number of bucket tips.  Each bucket tip is .5mm

        if (decoder->verbose > 1) {
            fprintf(stdout, "AcuRite Rain Gauge Total Rain is %2.1fmm\n", total_rain);
            fprintf(stdout, "Raw Message ");
            bitrow_print(bb[0], bitbuffer->bits_per_row[0]);
        }

        uint8_t id = bb[0][0];
        data_t *data;

        data = data_make(
            "model",    "",        DATA_STRING,    _X("Acurite-Rain","Acurite Rain Gauge"),
            "id",        "",        DATA_INT,    id,
            "rain",     "Total Rain",    DATA_FORMAT,    "%.1f mm", DATA_DOUBLE, total_rain,
            NULL);

        decoder_output_data(decoder, data);

        return 1;
    }
    return 0;
}

// Acurite 609 Temperature and Humidity Sensor
// 5 byte messages
// II ST TT HH CC
// II - ID byte, changes at each power up
// S - Status bitmask, normally 0x2,
//     0xa - battery low (bit 0x80)
// TTT - Temp in Celsius * 10, 12 bit with complement.
// HH - Humidity
// CC - Checksum
//
// @todo - see if the 3rd nybble is battery/status
//
static int acurite_th_callback(r_device *decoder, bitbuffer_t *bitbuf)
{
    uint8_t *bb = NULL;
    int cksum, battery_low, valid = 0;
    float tempc;
    uint8_t humidity, id, status;
    data_t *data;


    for (uint16_t brow = 0; brow < bitbuf->num_rows; ++brow) {
        if (bitbuf->bits_per_row[brow] != 40) {
        continue;
    }

    bb = bitbuf->bb[brow];

    cksum = (bb[0] + bb[1] + bb[2] + bb[3]);

    if (cksum == 0 || ((cksum & 0xff) != bb[4])) {
        continue;
    }

    // Temperature in Celsius is encoded as a 12 bit integer value
    // multiplied by 10 using the 4th - 6th nybbles (bytes 1 & 2)
    // negative values are recovered by sign extend from int16_t.
    int temp_raw = (int16_t)(((bb[1] & 0x0f) << 12) | (bb[2] << 4));
    tempc = (temp_raw >> 4) * 0.1;

    id = bb[0];
    status = (bb[1] & 0xf0) >> 4;
    battery_low = status & 0x8;
    humidity = bb[3];

    data = data_make(
             "model",        "",        DATA_STRING,    _X("Acurite-609TXC","Acurite 609TXC Sensor"),
             "id",        "",        DATA_INT,    id,
             "battery",        "",        DATA_STRING,    battery_low ? "LOW" : "OK",
             "status",        "",        DATA_INT,    status,
             "temperature_C",     "Temperature",    DATA_FORMAT,    "%.1f C", DATA_DOUBLE, tempc,
             "humidity",        "Humidity",    DATA_INT,    humidity,
             NULL);

    decoder_output_data(decoder, data);
    valid++;
    }

    if (valid)
        return 1;

    return 0;
}

/*
 * Acurite 06045m Lightning Sensor decoding.
 *
 * Specs:
 * - lightning strike count
 * - estimated distance to front of storm, up to 25 miles / 40 km
 * - Temperature -40 to 158 F / -40 to 70 C
 * - Humidity 1 - 99% RH
 *
 * Status Information sent per 06047M/01021 display
 * - (RF) interference (preventing lightning detection)
 * - low battery
 *
 *
 * Message format
 * --------------
 * Somewhat similar to 592TXR and 5-n-1 weather stations
 * Same pulse characteristics. checksum, and parity checking on data bytes.
 *
 * 0   1   2   3   4   5   6   7   8
 * CI II  BB  HH  ST  TT  LL  DD? KK
 *
 * C = Channel
 * I = ID
 * B = Battery + Message type 0x2f
 * S = Status/Message type/Temperature MSB.
 * T = Temperature
 * D = Lightning distance and status bits?
 * L = Lightning strike count.
 * K = Checksum
 *
 * Byte 0 - channel/?/ID?
 * - 0xC0: channel (A: 0xC, B: 0x8, C: 00)
 * - 0x3F: most significant 6 bits of bit ID
 *    (14 bits, same as Acurite Tower sensor family)
 *
 * Byte 1 - ID all 8 bits, no parity.
 * - 0xFF = least significant 8 bits of ID
 *    Note that ID is just a number and that least/most is not
 *    externally meaningful.
 *
 * Byte 2 - Battery and Message type
 * - Bitmask PBMMMMMM
 * - 0x80 = Parity
 * - 0x40 = 1 is battery OK, 0 is battery low
 * - 0x3f = Message type is 0x2f to indicate 06045M lightning
 *
 * Byte 3 - Humidity
 * - 0x80 - even parity
 * - 0x7f - humidity
 *
 * Byte 4 - Status (2 bits) and Temperature MSB (5 bits)
 * - Bitmask PAUTTTTT  (P = Parity, A = Active,  U = unknown, T = Temperature)
 * - 0x80 - even parity
 * - 0x40 - Active Mode
 *    Transmitting every 8 seconds (lightning possibly detected)
 *    normal, off, transmits every 24 seconds
 * - 0x20 - TBD: always off?
 * - 0x1F - Temperature most significant 5 bits
 *
 * Byte 5 - Temperature LSB (7 bits, 8th is parity)
 * - 0x80 - even parity
 * - 0x7F - Temperature least significant 7 bits
 *
 * Byte 6 - Lightning Strike count (7 bits, 8th is parity)
 * - 0x80 - even parity
 * - 0x7F - strike count (wraps at 127)
 *    Stored in EEPROM (or something non-volatile)
 *    @todo Does it go from 127 to 1, or to 0?
 *
 * Byte 7 - Edge of Storm Distance Approximation
 * - Bits PSSDDDDD  (P = Parity, S = Status, D = Distance
 * - 0x80 - even parity
 * - 0x40 - USSB1 (unknown strike status bit) - (possible activity?)
 *    currently decoded into "ussb1" output field
 *    @todo needs understanding
 * - 0x20 - RFI (radio frequency interference)
 *    @todo needs cross-checking with light and/or console
 * - 0x1F - distance to edge of storm (theory)
 *    value 0x1f is possible invalid value indication (value at power up)
 *    @todo determine if miles, km, or something else
 *    Note: Distance sometimes goes to 0 right after strike counter increment.
 *          Status bits might indicate validity of distance.
 *
 * Byte 8 - checksum. 8 bits, no parity.
 *
 * Data fields:
 * - active (vs standby) whether the AS39335 is in active scanning mode
 *     will be transmitting evey 8 seconds instead of every 24.
 * - RFI detected - the AS3935 uses broad RFI for detection
 *     Somewhat correlates with the Yellow LED, but stays set longer
 *     Short periods of RFI on is normal
 *     long periods of RFI means interference, solid yellow, relocate sensor
 * - Strike count - count of detection events, 7 bits, non-volatile
 * - Distance to edge of storm - See AS3935 documentation.
 *     sensor will make a distance estimate with each strike event.
 *     Units unknown, data needed from people with Acurite consoles
 *     0x1f (31) is invalid/undefined value, consumers should check for this.
 * - USSB1 - Unknown Strike Status Bit
 *     May indicate validity of distance estimate, cleared after sensor beeps
 *     Might need to also correlate against RFI bit.
 * - exception - bits that were invariant for me have changed.
 *     save raw_msg for further examination.
 *
 * @todo - check parity on bytes 2 - 7
 *
 * Additional reverse engineering needed:
 * @todo - Get distance to front of storm to match display
 * @todo - figure out remaining status bits and how to report
 */

static int acurite_6045_decode(r_device *decoder, bitrow_t bb, int browlen)
{
    int valid = 0;
    float tempf;
    uint8_t humidity, message_type, l_status;
    char channel, channel_str[2];
    char raw_str[31], *rawp;
    uint16_t sensor_id;
    uint8_t strike_count, strike_distance;
    int battery_low, active, rfi_detect, ussb1;
    int exception = 0;
    data_t *data;

    channel = acurite_getChannel(bb[0]);  // same as TXR
    sprintf(channel_str, "%c", channel);  // No DATA_CHAR, need null term. str

    // Tower sensor ID is the last 14 bits of byte 0 and 1
    // CCII IIII | IIII IIII
    sensor_id = ((bb[0] & 0x3f) << 8) | bb[1]; // same as TXR
    battery_low = (bb[2] & 0x40) == 0;
    humidity = (bb[3] & 0x7f); // 1-99 %rH, same as TXR
    active = (bb[4] & 0x40) == 0x40;    // Sensor is actively listening for strikes
    message_type = bb[2] & 0x3f;

    // 12 bits of temperature after removing parity and status bits.
    // Message native format appears to be in 1/10 of a degree Fahrenheit
    // Device Specification: -40 to 158 F  / -40 to 70 C
    // Available range given encoding with 12 bits -150.0 F to +259.6 F
    int temp_raw = ((bb[4] & 0x1F) << 7) | (bb[5] & 0x7F);
    tempf = (temp_raw - 1500) * 0.1;
    strike_count = bb[6] & 0x7f;
    strike_distance = bb[7] & 0x1f;
    rfi_detect = (bb[7] & 0x20) == 0x20;
    ussb1 = (bb[7] & 0x40) == 0x40;
    l_status = (bb[7] & 0x60) >> 5;


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
    };
    *rawp = '\0';

    // Flag whether this message might need further analysis
    if ((message_type != ACURITE_MSGTYPE_6045M) || // 6045 message type is 0x2f
    ((bb[2] & 0x20) != 0x20) || // unknown status bit, always on
    ((bb[2] & 0x0f) != 0x0f) || // unknown status bits, always on
    ((bb[4] & 0x20) != 0) // unknown status bits, always off
    ) {
        exception++;
      }

    // FIXME - temporarily leaving the old output for ease of debugging
    // and backward compatibility. Remove when doing a "1.0" release.
    if (decoder->verbose) {
        printf("Acurite lightning 0x%04X Ch %c Msg Type 0x%02x: %.1f F %d %% RH Strikes %d Distance %d L_status 0x%02x -",
        sensor_id, channel, message_type, tempf, humidity, strike_count, strike_distance, l_status);
    for (int i=0; i < browlen; i++) {
        char pc;
        pc = parity8(bb[i]) == 0 ? ' ' : '*';
        fprintf(stdout, " %02x%c", bb[i], pc);
    }
    printf("\n");
    }

    data = data_make(
       "model",            "",            DATA_STRING,    _X("Acurite-Lightning","Acurite Lightning 6045M"),
       "id",            NULL,              DATA_INT,    sensor_id,
       "channel",          NULL,             DATA_STRING,     channel_str,
       "temperature_F",     "temperature",        DATA_FORMAT,    "%.1f F",     DATA_DOUBLE,     tempf,
       "humidity",        "humidity",        DATA_INT,    humidity,
       "strike_count",        "strike_count",        DATA_INT,     strike_count,
       "storm_dist",        "storm_distance",    DATA_INT,     strike_distance,
       "active",        "active_mode",        DATA_INT,    active,    // @todo convert to bool
       "rfi",            "rfi_detect",        DATA_INT,    rfi_detect,     // @todo convert to bool
       "ussb1",            "unk_status1",        DATA_INT,    ussb1,    // @todo convert to bool
       "battery",        "battery",        DATA_STRING,    battery_low ? "LOW" : "OK",    // @todo convert to bool
       "exception",        "data_exception",    DATA_INT,    exception,    // @todo convert to bool
       "raw_msg",        "raw_message",        DATA_STRING,    raw_str,
     NULL);

    decoder_output_data(decoder, data);
    valid++;

    return(valid);
}

/*
 * This callback handles several Acurite devices that use a very
 * similar RF encoding and data format:
 *:
 * - 592TXR temperature and humidity sensor
 * - 5-n-1 weather station
 * - 6045M Lightning Detector with Temperature and Humidity
 *
 * CC RR IIII | IIII IIII | pBMMMMMM | pxxWWWWW | pWWWTTTT | pTTTTTTT | pSSSSSSS
 * C:2d R:2d ID:12d 1x BATT:1b TYPE:6h 1x ?2b W:5b 1x 3b T:4b 1x 7b S: 1x 7d
 *
 * @todo - refactor, move 5n1 and txr decoding into separate functions.
 * @todo - TBD Are parity and checksum the same across these devices?
 *         (opportunity to DRY-up and simplify?)
 */
static int acurite_txr_callback(r_device *decoder, bitbuffer_t *bitbuf)
{
    int browlen, valid = 0;
    uint8_t *bb;
    float tempc, tempf, wind_dir, wind_speed_kph, wind_speed_mph;
    uint8_t humidity, sensor_status, sequence_num, message_type;
    char channel;
    char channel_str[2];
    uint16_t sensor_id;
    int raincounter, battery_low;
    data_t *data;

    bitbuffer_invert(bitbuf);

    if (decoder->verbose > 1) {
        fprintf(stderr,"acurite_txr\n");
        bitbuffer_print(bitbuf);
    }

    for (uint16_t brow = 0; brow < bitbuf->num_rows; ++brow) {
        browlen = (bitbuf->bits_per_row[brow] + 7)/8;
        bb = bitbuf->bb[brow];

        if (decoder->verbose > 1)
            fprintf(stderr,"acurite_txr: row %d bits %d, bytes %d \n", brow, bitbuf->bits_per_row[brow], browlen);

        if ((bitbuf->bits_per_row[brow] < ACURITE_TXR_BITLEN ||
            bitbuf->bits_per_row[brow] > ACURITE_5N1_BITLEN + 1) &&
            bitbuf->bits_per_row[brow] != ACURITE_6045_BITLEN) {
            if (decoder->verbose > 1 && bitbuf->bits_per_row[brow] > 16)
                fprintf(stderr,"acurite_txr: skipping wrong len\n");
            continue;
        }

        // There will be 1 extra false zero bit added by the demod.
        // this forces an extra zero byte to be added
        if (bb[browlen - 1] == 0)
            browlen--;

        // sum of first n-1 bytes modulo 256 should equal nth byte
        // also disregard a row of all zeros
        int sum = add_bytes(bb, browlen - 1);
        if (sum == 0 || (sum & 0xff) != bb[browlen - 1]) {
            if (decoder->verbose)
                bitrow_printf(bb, browlen, "Acurite bad checksum: ");
            continue;
        }

        if (decoder->verbose) {
            fprintf(stderr, "acurite_txr Parity: ");
            for (uint8_t i = 0; i < browlen; i++) {
                fprintf(stderr, "%d", parity8(bb[i]));
            }
            fprintf(stderr,"\n");
        }

        // acurite sensors with a common format appear to have a message type
        // in the lower 6 bits of the 3rd byte.
        // Format: PBMMMMMM
        // P = Parity
        // B = Battery Normal
        // M = Message type
        message_type = bb[2] & 0x3f;

        // tower sensor messages are 7 bytes.
        // @todo - see if there is a type in the message that
        // can be used instead of length to determine type
        if (browlen == ACURITE_TXR_BITLEN / 8) {
            channel = acurite_getChannel(bb[0]);
            // Tower sensor ID is the last 14 bits of byte 0 and 1
            // CCII IIII | IIII IIII
            sensor_id = ((bb[0] & 0x3f) << 8) | bb[1];
            sensor_status = bb[2]; // @todo, uses parity? & 0x07f
            humidity = (bb[3] & 0x7f); // 1-99 %rH
            // temperature encoding used by "tower" sensors 592txr
            // 14 bits available after removing both parity bits.
            // 11 bits needed for specified range -40 C to 70 C (-40 F - 158 F)
            // range -100 C to 1538.4 C
            int temp_raw = ((bb[4] & 0x7F) << 7) | (bb[5] & 0x7F);
            tempc = temp_raw * 0.1 - 100;
            sprintf(channel_str, "%c", channel);
            // Battery status is the 7th bit 0x40. 1 = normal, 0 = low
            battery_low = (bb[2] & 0x40) == 0;

            data = data_make(
                    "model",                "",        DATA_STRING,    _X("Acurite-Tower","Acurite tower sensor"),
                    "id",            "",        DATA_INT,    sensor_id,
                    "sensor_id",            NULL,          DATA_FORMAT,    "0x%04x",   DATA_INT,       sensor_id, // @todo hex output not working, delete at 1.0 release
                    "channel",          NULL,         DATA_STRING,     &channel_str,
                    "temperature_C",         "Temperature",    DATA_FORMAT,    "%.1f C", DATA_DOUBLE, tempc,
                    "humidity",             "Humidity",    DATA_INT,    humidity,
                    _X("battery_ok","battery_low"),            "",    DATA_INT,    _X(!battery_low,battery_low),

                    NULL);

            decoder_output_data(decoder, data);
            valid++;
        }

        // The 5-n-1 weather sensor messages are 8 bytes.
        if (browlen == ACURITE_5N1_BITLEN / 8) {
            if (decoder->verbose)
                bitrow_printf(bb, 8, "Acurite 5n1 raw msg: ");
            channel = acurite_getChannel(bb[0]);
            sprintf(channel_str, "%c", channel);

            // 5-n-1 sensor ID is the last 12 bits of byte 0 & 1
            // byte 0     | byte 1
            // CC RR IIII | IIII IIII
            sensor_id    = ((bb[0] & 0x0f) << 8) | bb[1];
            // The sensor sends the same data three times, each of these have
            // an indicator of which one of the three it is. This means the
            // checksum and first byte will be different for each one.
            // The bits 5,4 of byte 0 indicate which copy of the 65 bit data string
            //  00 = first copy
            //  01 = second copy
            //  10 = third copy
            //  1100 xxxx  = channel A 1st copy
            //  1101 xxxx  = channel A 2nd copy
            //  1110 xxxx  = channel A 3rd copy
            sequence_num = (bb[0] & 0x30) >> 4;
            battery_low = (bb[2] & 0x40) >> 6;

            // Only for 5N1, range: 0 to 159 kph
            // raw number is cup rotations per 4 seconds
            // http://www.wxforum.net/index.php?topic=27244.0 (found from weewx driver)
            int speed_raw = ((bb[3] & 0x1F) << 3)| ((bb[4] & 0x70) >> 4);
            wind_speed_kph = 0;
            if (speed_raw > 0) {
                wind_speed_kph = speed_raw * 0.8278 + 1.0;
            }

            if (message_type == ACURITE_MSGTYPE_5N1_WINDSPEED_WINDDIR_RAINFALL) {
                // Wind speed, wind direction, and rain fall
                wind_dir = acurite_5n1_winddirections[bb[4] & 0x0f] * 22.5f;

                // range: 0 to 99.99 in, 0.01 in incr., rolling counter?
                raincounter = ((bb[5] & 0x7f) << 7) | (bb[6] & 0x7F);

                data = data_make(
                    "model",        "",   DATA_STRING,    _X("Acurite-5n1","Acurite 5n1 sensor"),
                    "sensor_id",    NULL, DATA_INT,       sensor_id, // @todo normaiize to "id" at 1.0 release.
                    "channel",      NULL,   DATA_STRING,    &channel_str,
                    "sequence_num",  NULL,   DATA_INT,      sequence_num,
                    "battery",      NULL,   DATA_STRING,    battery_low ? "OK" : "LOW",
                    "message_type", NULL,   DATA_INT,       message_type,
                    "wind_speed_kph",   "wind_speed",   DATA_FORMAT,    "%.1f kph", DATA_DOUBLE,     wind_speed_kph,
                    "wind_dir_deg", NULL,   DATA_FORMAT,    "%.1f", DATA_DOUBLE,    wind_dir,
                    "rain_inch", "Rainfall Accumulation",   DATA_FORMAT, "%.2f in", DATA_DOUBLE, raincounter * 0.01f,
                    NULL);

                decoder_output_data(decoder, data);

            } else if (message_type == ACURITE_MSGTYPE_5N1_WINDSPEED_TEMP_HUMIDITY) {
                // Wind speed, temperature and humidity

                // range -40 to 158 F
                int temp_raw = (bb[4] & 0x0F) << 7 | (bb[5] & 0x7F);
                tempf = (temp_raw - 400) * 0.1;

                humidity = (bb[6] & 0x7f); // 1-99 %rH

                data = data_make(
                    "model",        "",   DATA_STRING,    _X("Acurite-5n1","Acurite 5n1 sensor"),
                    "sensor_id",    NULL, DATA_INT,  sensor_id, // @todo normalize to "id" at 1.0 release.
                    "channel",      NULL,   DATA_STRING,    &channel_str,
                    "sequence_num",  NULL,   DATA_INT,      sequence_num,
                    "battery",      NULL,   DATA_STRING,    battery_low ? "OK" : "LOW",
                    "message_type", NULL,   DATA_INT,       message_type,
                    "wind_speed_kph",   "wind_speed",   DATA_FORMAT,    "%.1f kph", DATA_DOUBLE,     wind_speed_kph,
                    "temperature_F",     "temperature",    DATA_FORMAT,    "%.1f F", DATA_DOUBLE,    tempf,
                    "humidity",     NULL,    DATA_FORMAT,    "%d",   DATA_INT,   humidity,
                    NULL);
                decoder_output_data(decoder, data);

            } else if (message_type == ACURITE_MSGTYPE_WINDSPEED_TEMP_HUMIDITY_3N1) {
                // Wind speed, temperature and humidity for 3-n-1
                sensor_id = ((bb[0] & 0x3f) << 8) | bb[1]; // 3-n-1 sensor ID is the bottom 14 bits of byte 0 & 1
                humidity = (bb[3] & 0x7f); // 1-99 %rH

                // note the 3n1 seems to have one more high bit than 5n1
                int temp_raw = (bb[4] & 0x1F) << 7 | (bb[5] & 0x7F);
                tempf        = (temp_raw - 1480) * 0.1; // regression yields (rawtemp-1480)*0.1

                wind_speed_mph = bb[6] & 0x7f; // seems to be plain MPH

                data = data_make(
                    "model",        "",   DATA_STRING,    _X("Acurite-3n1","Acurite 3n1 sensor"),
                    "sensor_id",    NULL,   DATA_FORMAT,    "0x%02X",   DATA_INT,       sensor_id,
                    "channel",      NULL,   DATA_STRING,    &channel_str,
                    "sequence_num",  NULL,   DATA_INT,      sequence_num,
                    "battery",      NULL,   DATA_STRING,    battery_low ? "OK" : "LOW",
                    "message_type", NULL,   DATA_INT,       message_type,
                    "wind_speed_mph",   "wind_speed",   DATA_FORMAT,    "%.1f mph", DATA_DOUBLE,     wind_speed_mph,
                    "temperature_F",     "temperature",    DATA_FORMAT,    "%.1f F", DATA_DOUBLE,    tempf,
                    "humidity",     NULL,    DATA_FORMAT,    "%d",   DATA_INT,   humidity,
                    NULL);
                decoder_output_data(decoder, data);

            } else {
                fprintf(stderr, "Acurite 5n1 sensor 0x%04X Ch %c, Status %02X, Unknown message type 0x%02x\n",
                    sensor_id, channel, bb[3], message_type);
            }
        }

        if (browlen == ACURITE_6045_BITLEN / 8) {
            // @todo check parity and reject if invalid
            valid += acurite_6045_decode(decoder, bb, browlen);
        }

    }

    if (valid)
        return 1;

    return 0;
}


/*
 * Acurite 00986 Refrigerator / Freezer Thermometer
 *
 * Includes two sensors and a display, labeled 1 and 2,
 * by default 1 - Refrigerator, 2 - Freezer
 *
 * PPM, 5 bytes, sent twice, no gap between repeaters
 * start/sync pulses two short, with short gaps, followed by
 * 4 long pulse/gaps.
 *
 * @todo, the 2 short sync pulses get confused as data.
 *
 * Data Format - 5 bytes, sent LSB first, reversed
 *
 * TT II II SS CC
 *
 * T - Temperature in Fahrenheit, integer, MSB = sign.
 *     Encoding is "Sign and magnitude"
 * I - 16 bit sensor ID
 *     changes at each power up
 * S - status/sensor type
 *     0x01 = Sensor 2
 *     0x02 = low battery
 * C = CRC (CRC-8 poly 0x07, little-endian)
 *
 * @todo
 * - needs new PPM demod that can separate out the short
 *   start/sync pulses which confuse things and cause
 *   one data bit to be lost in the check value.
 *
 * 2018-04 A user with a dedicated receiver indicated the
 *   possibility that the transmitter actually drops the
 *   last bit instead of the demod.
 *
 * leaving some of the debugging code until the missing
 * bit issue gets resolved.
 *
 */

static int acurite_986_callback(r_device *decoder, bitbuffer_t *bitbuf)
{
    int const browlen = 5;
    uint8_t *bb, sensor_num, status, crc, crcc;
    uint8_t br[8];
    int8_t tempf; // Raw Temp is 8 bit signed Fahrenheit
    uint16_t sensor_id, valid_cnt = 0;
    char sensor_type;
    char *channel_str;
    int battery_low;
    data_t *data;


    for (uint16_t brow = 0; brow < bitbuf->num_rows; ++brow) {

        if (decoder->verbose > 1)
            fprintf(stderr,"acurite_986: row %d bits %d, bytes %d \n", brow, bitbuf->bits_per_row[brow], browlen);

        if (bitbuf->bits_per_row[brow] < 39 ||
            bitbuf->bits_per_row[brow] > 43 ) {
            if (decoder->verbose > 1 && bitbuf->bits_per_row[brow] > 16)
                fprintf(stderr,"acurite_986: skipping wrong len\n");
            continue;
        }
        bb = bitbuf->bb[brow];

        // Reduce false positives
        // may eliminate these with a better PPM (precise?) demod.
        if ((bb[0] == 0xff && bb[1] == 0xff && bb[2] == 0xff) ||
                (bb[0] == 0x00 && bb[1] == 0x00 && bb[2] == 0x00)) {
            continue;
        }

        // Reverse the bits, msg sent LSB first
        for (uint8_t i = 0; i < browlen; i++)
            br[i] = reverse8(bb[i]);

        if (decoder->verbose)
            bitrow_printf(br, browlen, "Acurite 986 reversed: ");

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
            if (decoder->verbose > 1)
                bitrow_printf(br, browlen,  "Acurite 986 sensor bad CRC: %02x -", crc8le(br, 4, 0x07, 0));
            // HACK: rct 2018-04-22
            // the message is often missing the last 1 bit either due to a
            // problem with the device or demodulator
            // Add 1 (0x80 because message is LSB) and retry CRC.
            if (crcc == (crc | 0x80)) {
                if (decoder->verbose > 1)
                    fprintf(stderr,"Acurite 986 CRC fix %02x - %02x\n", crc,crcc);
            } else {
                continue;
            }
        }

        if (tempf & 0x80) {
            tempf = (tempf & 0x7f) * -1;
        }

        if (decoder->verbose)
            printf("Acurite 986 sensor 0x%04x - %d%c: %d F\n", sensor_id, sensor_num, sensor_type, tempf);

        data = data_make(
                "model",        "",        DATA_STRING,    _X("Acurite-986","Acurite 986 Sensor"),
                "id",            NULL,        DATA_INT,    sensor_id,
                "channel",        NULL,        DATA_STRING,    channel_str,
                "temperature_F",    "temperature",    DATA_FORMAT, "%f F", DATA_DOUBLE,    (float)tempf,
                "battery",        "battery",    DATA_STRING,    battery_low ? "LOW" : "OK",    // @todo convert to bool
                "status",        "status",    DATA_INT,    status,
                NULL);

        decoder_output_data(decoder, data);

        valid_cnt++;

    }

    if (valid_cnt)
        return 1;

    return 0;
}

// Checksum code from
// https://eclecticmusingsofachaoticmind.wordpress.com/2015/01/21/home-automation-temperature-sensors/
// with modifications listed in
// http://www.osengr.org/WxShield/Downloads/Weather-Sensor-RF-Protocols.pdf
//
// This is the same algorithm as used in ambient_weather.c
// @todo - move to util.c, (and rename)
uint8_t acurite_606_checksum(int length, uint8_t *buff)
{
    uint8_t mask = 0xd3;
    uint8_t checksum = 0x00;
    uint8_t data;
    int byteCnt;

    for (byteCnt = 0; byteCnt < length; byteCnt++) {
        int bitCnt;
        data = buff[byteCnt];

        for (bitCnt = 7; bitCnt >= 0; bitCnt--) {
            uint8_t bit;

            // Rotate mask right
            bit = mask & 1;
            mask = (mask >> 1) | (mask << 7);
            if (bit) {
                mask ^= 0x18;
            }

            // XOR mask into checksum if data bit is 1
            if (data & 0x80) {
                checksum ^= mask;
            }
            data <<= 1;
        }
    }
    return checksum;
}

static int acurite_606_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b;
    int row;
    int16_t temp_raw; // temperature as read from the data packet
    float temp_c;     // temperature in C
    int battery;      // the battery status: 1 is good, 0 is low
    int sensor_id;    // the sensor ID - basically a random number that gets reset whenever the battery is removed

    row = bitbuffer_find_repeated_row(bitbuffer, 3, 32); // expected are 6 rows
    if (row < 0)
        return 0;

    if (bitbuffer->bits_per_row[row] > 33)
        return 0;

    b = bitbuffer->bb[row];

    if (b[4] != 0)
        return 0;

    // reject all blank messages
    if (b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 0)
        return 0;

    if (decoder->verbose > 1)
        bitbuffer_printf(bitbuffer, "acurite_606: ");

    // calculate the checksum and only continue if we have a matching checksum
    uint8_t chk = acurite_606_checksum(3, b);
    if (chk != b[3])
        return 0;

    // Processing the temperature:
    // Upper 4 bits are stored in nibble 1, lower 8 bits are stored in nibble 2
    // upper 4 bits of nibble 1 are reserved for other usages (e.g. battery status)
    sensor_id = b[0];
    battery   = (b[1] & 0x80) >> 7;
    temp_raw  = (int16_t)((b[1] << 12) | (b[2] << 4));
    temp_raw  = temp_raw >> 4;
    temp_c    = temp_raw * 0.1;

    data = data_make(
            "model",            "",             DATA_STRING, _X("Acurite-606TX","Acurite 606TX Sensor"),
            "id",               "",             DATA_INT, sensor_id,
            "battery",          "Battery",      DATA_STRING, battery ? "OK" : "LOW",
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    decoder_output_data(decoder, data);
    return 1;
}

static int acurite_00275rm_callback(r_device *decoder, bitbuffer_t *bitbuf)
{
    int crc, battery_low, id, model, valid = 0;
    data_t *data;
    float tempc, ptempc;
    uint8_t probe, humidity, phumidity, water;
    uint8_t signal[3][11];  //  Hold three copies of the signal
    int     nsignal = 0;

    bitbuffer_invert(bitbuf);

    if (decoder->verbose > 1) {
        fprintf(stderr,"acurite_00275rm\n");
        bitbuffer_print(bitbuf);
    }

    //  This sensor repeats signal three times.  Store each copy.
    for (uint16_t brow = 0; brow < bitbuf->num_rows; ++brow) {
        if (bitbuf->bits_per_row[brow] != 88) continue;
        if (nsignal>=3) continue;
        memcpy(signal[nsignal], bitbuf->bb[brow], 11);
        if (decoder->verbose)
            bitrow_printf(signal[nsignal], 11, "acurite_00275rm: ");
        nsignal++;
    }

    //  All three signals were found
    if (nsignal==3) {
        //  Combine signal copies so that majority bit count wins
        for (int i=0; i<11; i++) {
            signal[0][i] =
                    (signal[0][i] & signal[1][i]) |
                    (signal[1][i] & signal[2][i]) |
                    (signal[2][i] & signal[0][i]);
        }
        // CRC check fails?
        if ((crc=crc16lsb(&(signal[0][0]), 11/*len*/, 0x00b2/*poly*/, 0x00d0/*seed*/)) != 0) {
            if (decoder->verbose)
                bitrow_printf(signal[0], 11, "Acurite 00275rm sensor bad CRC: %02x -", crc);
        // CRC is OK
        } else {
            //  Decode the combined signal
            id          = (signal[0][0] << 16) | (signal[0][1] << 8) | signal[0][3];
            battery_low = (signal[0][2] & 0x40) == 0;
            model       = (signal[0][2] & 1);
            tempc       = ((signal[0][4] << 4) | (signal[0][5] >> 4)) * 0.1 - 100;
            probe       = signal[0][5] & 3;
            humidity    = ((signal[0][6] & 0x1f) << 2) | (signal[0][7] >> 6);
            //  No probe
            data = data_make(
                    "model",           "",             DATA_STRING,    model ? _X("Acurite-00275rm","00275rm") : _X("Acurite-00276rm","00276rm"),
                    "probe",           "",             DATA_INT,       probe,
                    "id",              "",             DATA_INT,       id,
                    "battery",         "",             DATA_STRING,    battery_low ? "LOW" : "OK",
                    "temperature_C",   "Celsius",      DATA_FORMAT,    "%.1f C",  DATA_DOUBLE, tempc,
                    "humidity",        "Humidity",     DATA_INT,       humidity,
                    NULL);
            //  Water probe (detects water leak)
            if (probe == 1) {
                water = (signal[0][7] & 0x0f) == 15;
                data = data_append(data,
                        "water",           "",             DATA_INT,       water,
                        "mic",             "Integrity",    DATA_STRING,    "CRC",
                        NULL);
            }
            //  Soil probe (detects temperature)
            else if (probe == 2) {
                ptempc = (((signal[0][7] & 0x0f) << 8) | signal[0][8]) * 0.1 - 100;
                data = data_append(data,
                        "ptemperature_C",  "Celsius",      DATA_FORMAT,    "%.1f C",  DATA_DOUBLE, ptempc,
                        "mic",             "Integrity",    DATA_STRING,    "CRC",
                        NULL);
            }
            //  Spot probe (detects temperature and humidity)
            else if (probe == 3) {
                ptempc    = (((signal[0][7] & 0x0f) << 8) | signal[0][8]) * 0.1 - 100;
                phumidity = signal[0][9] & 0x7f;
                data = data_append(data,
                        "ptemperature_C",  "Celsius",      DATA_FORMAT,    "%.1f C",  DATA_DOUBLE, ptempc,
                        "phumidity",       "Humidity",     DATA_INT,       phumidity,
                        "mic",             "Integrity",    DATA_STRING,    "CRC",
                        NULL);
            }
            data = data_append(data,
                    "mic",             "Integrity",    DATA_STRING,    "CRC",
                    NULL);
            decoder_output_data(decoder, data);
            valid=1;
        }
    }
    if (valid) return 1;
    return 0;
}

static char *acurite_rain_gauge_output_fields[] = {
    "model",
    "id",
    "rain",
    NULL
};

r_device acurite_rain_gauge = {
    .name           = "Acurite 896 Rain Gauge",
    .modulation     = OOK_PULSE_PPM,
    .short_width    = 1000,
    .long_width     = 2000,
    .gap_limit      = 3500,
    .reset_limit    = 5000,
    .decode_fn      = &acurite_rain_gauge_callback,
// Disabled by default due to false positives on oregon scientific v1 protocol see issue #353
    .disabled       = 1,
    .fields         = acurite_rain_gauge_output_fields,
};

static char *acurite_th_output_fields[] = {
    "model",
    "id",
    "battery",
    "status",
    "temperature_C",
    "humidity",
    NULL
};

r_device acurite_th = {
    .name           = "Acurite 609TXC Temperature and Humidity Sensor",
    .modulation     = OOK_PULSE_PPM,
    .short_width    = 1000,
    .long_width     = 2000,
    .gap_limit      = 3000,
    .reset_limit    = 10000,
    .decode_fn      = &acurite_th_callback,
    .disabled       = 0,
    .fields         = acurite_th_output_fields,
};

/*
 * For Acurite 592 TXR Temp/Humidity, but
 * Should match Acurite 592TX, 5-n-1, etc.
 */
static char *acurite_txr_output_fields[] = {
    "model",
    "id",
    "sensor_id",
    "channel",
    "temperature_C",
    "humidity",
    "battery_low", // TODO: remove this
    "battery_ok",
    "sequence_num",
    "battery",
    "message_type",
    "wind_speed_mph",
    "wind_dir_deg",
    "wind_dir",
    "rain_inch",
    "temperature_F",
    NULL
};

r_device acurite_txr = {
    .name           = "Acurite 592TXR Temp/Humidity, 5n1 Weather Station, 6045 Lightning",
    .modulation     = OOK_PULSE_PWM,
    .short_width    = 220,  // short pulse is 220 us + 392 us gap
    .long_width     = 408,  // long pulse is 408 us + 204 us gap
    .sync_width     = 620,  // sync pulse is 620 us + 596 us gap
    .gap_limit      = 500,  // longest data gap is 392 us, sync gap is 596 us
    .reset_limit    = 4000, // packet gap is 2192 us
    .decode_fn      = &acurite_txr_callback,
    .disabled       = 0,
    .fields         = acurite_txr_output_fields,
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
static char *acurite_986_output_fields[] = {
    "model",
    "id",
    "channel",
    "temperature_F",
    "battery",
    "status",
    NULL
};

r_device acurite_986 = {
    .name           = "Acurite 986 Refrigerator / Freezer Thermometer",
    .modulation     = OOK_PULSE_PPM,
    .short_width    = 520,
    .long_width     = 880,
    .gap_limit      = 1280,
    .reset_limit    = 4000,
    .decode_fn      = &acurite_986_callback,
    .disabled       = 0,
    .fields         = acurite_986_output_fields,
};

/*
 * Acurite 00606TX Tower Sensor
 *
 * Temperature only
 *
 */

static char *acurite_606_output_fields[] = {
    "model",
    "id",
    "battery",
    "temperature_C",
    "mic",
    NULL
};

r_device acurite_606 = {
    .name           = "Acurite 606TX Temperature Sensor",
    // actually tests/acurite/02/gfile002.cu8, check this
    //.modulation     = OOK_PULSE_PWM,
    //.short_width    = 576,
    //.long_width     = 1076,
    //.gap_limit      = 1200,
    //.reset_limit    = 12000,
    .modulation     = OOK_PULSE_PPM,
    .short_width    = 2000,
    .long_width     = 4000,
    .gap_limit      = 7000,
    .reset_limit    = 10000,
    .decode_fn      = &acurite_606_callback,
    .disabled       = 0,
    .fields         = acurite_606_output_fields,
};

static char *acurite_00275rm_output_fields[] = {
    "model",
    "probe",
    "id",
    "battery",
    "temperature_C",
    "humidity",
    "water",
    "ptemperature_C",
    "phumidity",
    "mic",
    NULL
};

r_device acurite_00275rm = {
    .name           = "Acurite 00275rm,00276rm Temp/Humidity with optional probe",
    .modulation     = OOK_PULSE_PWM,
    .short_width    = 232,  // short pulse is 232 us
    .long_width     = 420,  // long pulse is 420 us
    .gap_limit      = 520,  // long gap is 384 us, sync gap is 592 us
    .reset_limit    = 708,  // no packet gap, sync gap is 592 us
    .sync_width     = 632,  // sync pulse is 632 us
    .decode_fn      = &acurite_00275rm_callback,
    .disabled       = 0,
    .fields         = acurite_00275rm_output_fields,
};
