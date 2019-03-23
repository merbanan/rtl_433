/** @file
    Fine Offset WH1080/WH3080 Weather Station
*/
/** \fn int fineoffset_wh1080_callback(r_device *decoder, bitbuffer_t *bitbuffer)
This module is based on Stanisław Pitucha ('viraptor' https://github.com/viraptor) code stub for the Digitech XC0348
Weather Station, which seems to be a rebranded Fine Offset WH1080 Weather Station.

Some info and code derived from Kevin Sangelee's page:
http://www.susa.net/wordpress/2012/08/raspberry-pi-reading-wh1081-weather-sensors-using-an-rfm01-and-rfm12b/ .

See also Frank 'SevenW' page ( https://www.sevenwatt.com/main/wh1080-protocol-v2-fsk/ ) for some other useful info.

For the WH1080 part I mostly have re-elaborated and merged their works. Credits (and kudos) should go to them all
(and to many others too).

Reports 1 row, 88 pulses.

Data layout:

    ff FI IT TT HH SS GG ?R RR BD CC

- F: 4 bit fixed message format
- I: 8 bit device id
- T: 12 bit temperature, offset 40 scale 10, i.e. 0.1C steps -40C
- H: 8 bit humidity percent
- S: 8 bit wind speed, 0.34m/s steps
- G: 8 bit gust speed, 0.34m/s steps
- R: 12 bit? rain, 0.3mm steps
- B: 4 bit flags, 0x1 is battery_low
- D: 8 bit wind direction: 00 is N, 02 is NE, 04 is E, etc. up to 0F is seems
- C: 8 bit checksum


## WH1080

(aka Watson W-8681)
(aka Digitech XC0348 Weather Station)
(aka PCE-FWS 20)
(aka Elecsa AstroTouch 6975)
(aka Froggit WH1080)
(aka .....)

This weather station is based on an indoor touchscreen receiver, and on a 5+1 outdoor wireless sensors group
(rain, wind speed, wind direction, temperature, humidity, plus a DCF77 time signal decoder, maybe capable to decode
some other time signal standard).
See the product page here: http://www.foshk.com/weather_professional/wh1080.htm .
It's a very popular weather station, you can easily find it on eBay or Amazon (just do a search for 'WH1080').

The module works fine, decoding all of the data as read into the original console (there is some minimal difference
sometime on the decimals due to the different architecture of the console processor, which is a little less precise).

Please note that the pressure sensor (barometer) is enclosed in the indoor console unit, NOT in the outdoor
wireless sensors group.
That's why it's NOT possible to get pressure data by wireless communication. If you need pressure data you should try
an Arduino/Raspberry solution wired with a BMP180/280 or BMP085 sensor.

Data are transmitted in a 48 seconds cycle (data packet, then wait 48 seconds, then data packet...).

This module is also capable to decode the DCF77/WWVB time signal sent by the time signal decoder
(which is enclosed on the sensor tx): around the minute 59 of the even hours the sensor's TX stops sending weather data,
probably to receive (and sync with) DCF77/WWVB signals.
After around 3-4 minutes of silence it starts to send just time data for some minute, then it starts again with
weather data as usual.

By living in Europe I can only test DCF77 time decoding, so if you live outside Europe and you find garbage instead
of correct time, you should disable/ignore time decoding
(or, better, try to implement a more complete time decoding system :) ).

To recognize message type (weather or time) you can use the 'msg_type' field on json output:
- msg_type 0 = weather data
- msg_type 1 = time data

The 'Total rainfall' field is a cumulative counter, increased by 0.3 millimeters of rain at once.

The station comes in three TX operating frequency versions: 433, 868.3 and 915 Mhz.
The module is tested with a 'Froggit WH1080' on 868.3 Mhz, using '-f 868140000' as frequency parameter and
it works fine (compiled in x86, RaspberryPi 1 (v2), Raspberry Pi2 and Pi3, and also on a BananaPi platform. Everything is OK).
I don't know if it works also with ALL of the rebranded versions/models of this weather station.
I guess it *should* do... Just give it a try! :)

## WH3080

The WH3080 Weather Station seems to be basically a WH1080 with the addition of UV/Light sensors onboard.
The weather/datetime radio protocol used for both is identical, the only difference is for the addition in the WH3080
of the UV/Light part.
UV/Light radio messages are disjointed from (and shorter than) weather/datetime radio messages and are transmitted
in a 'once-every-60-seconds' cycle.

The module is able to decode all kind of data coming from the WH3080: weather, datetime, UV and light plus some
error/status code.

To recognize message type (weather, datetime or UV/light) you can refer to the 'msg_type' field on json output:
- msg_type 0 = weather data
- msg_type 1 = datetime data
- msg_type 2 = UV/light data

While the LCD console seems to truncate/round values in order to best fit to its display, this module keeps entire values
as received from externals sensors (exception made for some rounding while converting values from lux to watts/m and fc),
so you can see -sometimes- some little difference between module's output and LCD console's values.

2016-2017 Nicola Quiriti ('ovrheat' - 'seven')
*/

#include "decoder.h"

static int wind_dir_degr[]= {0, 23, 45, 68, 90, 113, 135, 158, 180, 203, 225, 248, 270, 293, 315, 338};

// The transmission differences are 8 preamble bits (EPB) and 7 preamble bits (SPB)
#define EPB 8
#define SPB 7

static int fineoffset_wh1080_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *br;
    int msg_type;      // 0=Weather 1=Datetime 2=UV/Light
    int sens_msg = 10; // 10=Weather/Time sensor  7=UV/Light sensor
    uint8_t bbuf[11];  // max 8 / 11 bytes needed
    int preamble;         // 7 or 8 preamble bits

    if (bitbuffer->num_rows != 1) {
        return 0;
    }

    if (bitbuffer->bits_per_row[0] == 88) { // FineOffset WH1080/3080 Weather data msg
        preamble = EPB;
        sens_msg = 10;
        br = bitbuffer->bb[0];
    }
    else if (bitbuffer->bits_per_row[0] == 87) { // FineOffset WH1080/3080 Weather data msg (different version (newest?))
        preamble = SPB;
        sens_msg = 10;
        /* 7 bits of preamble, bit shift the whole buffer and fix the bytestream */
        bitbuffer_extract_bytes(bitbuffer, 0, 7, bbuf + 1, 10 * 8);
        bbuf[0] = (bitbuffer->bb[0][0] >> 1) | 0x80;
        br      = bbuf;
    }
    else if (bitbuffer->bits_per_row[0] == 64) {  // FineOffset WH3080 UV/Light data msg
        preamble = EPB;
        sens_msg = 7;
        br = bitbuffer->bb[0];

    }
    else if (bitbuffer->bits_per_row[0] == 63) { // FineOffset WH3080 UV/Light data msg (different version (newest?))
        preamble = SPB;
        sens_msg = 7;
        /* 7 bits of preamble, bit shift the whole buffer and fix the bytestream */
        bitbuffer_extract_bytes(bitbuffer, 0, 7, bbuf + 1, 7 * 8);
        bbuf[0] = (bitbuffer->bb[0][0] >> 1) | 0x80;
        br      = bbuf;
    }
    else {
        return 0;
    }

    if (decoder->verbose) {
        bitrow_printf(bbuf, sens_msg * 8, "Fine Offset WH1080 data ");
    }

    if (br[0] != 0xff) {
        return 0; // preamble missing
    }

    if (sens_msg == 10) {
        if (crc8(br, 11, 0x31, 0xff)) { // init is 0 if we skip the preamble
            return 0; // crc mismatch
        }
    }
    else {
        if (crc8(br, 8, 0x31, 0xff)) { // init is 0 if we skip the preamble
            return 0; // crc mismatch
        }
    }

    if ((br[1] >> 4) == 0x0a) {
        msg_type = 0; // WH1080/3080 Weather msg
    }
    else if ((br[1] >> 4) == 0x0b) {
        msg_type = 1; // WH1080/3080 Datetime msg
    }
    else if ((br[1] >> 4) == 0x07) {
        msg_type = 2; // WH3080 UV/Light msg
    }
    else {
        // 0x03 is WH0530, Alecto WS-1200
        // 0x05 is Alecto WS-1200 DCF77
        return 0;
    }

    // GETTING WEATHER SENSORS DATA
    int temp_raw      = ((br[2] & 0x0f) << 8) | br[3];
    float temperature = (temp_raw - 400) * 0.1;
    int humidity      = br[4];
    int direction_deg = wind_dir_degr[br[9] & 0x0f];
    float speed       = (br[5] * 0.34f) * 3.6f; // m/s -> km/h
    float gust        = (br[6] * 0.34f) * 3.6f; // m/s -> km/h
    int rain_raw      = ((br[7] & 0x0f) << 8) | br[8];
    float rain        = rain_raw * 0.3f;
    int device_id     = (br[1] << 4 & 0xf0) | (br[2] >> 4);
    int battery_low   = (br[9] >> 4) == 1;

    // GETTING UV DATA
    int uv_sensor_id = (br[1] << 4 & 0xf0) | (br[2] >> 4);
    int uv_status_ok = br[3] == 85;
    int uv_index     = br[2] & 0x0F;

    // GETTING LIGHT DATA
    float light = (br[4] << 16) | (br[5] << 8) | br[6];
    float lux   = light * 0.1;
    float wm;
    if (preamble == SPB)
        wm = (light * 0.00079);
    else //EPB
        wm = (light / 6830);

    // GETTING TIME DATA
    int signal_type       = ((br[2] & 0x0F) == 10);
    char *signal_type_str = signal_type ? "DCF77" : "WWVB/MSF";

    int hours   = ((br[3] & 0x30) >> 4) * 10 + (br[3] & 0x0F);
    int minutes = ((br[4] & 0xF0) >> 4) * 10 + (br[4] & 0x0F);
    int seconds = ((br[5] & 0xF0) >> 4) * 10 + (br[5] & 0x0F);
    int year    = ((br[6] & 0xF0) >> 4) * 10 + (br[6] & 0x0F) + 2000;
    int month   = ((br[7] & 0x10) >> 4) * 10 + (br[7] & 0x0F);
    int day     = ((br[8] & 0xF0) >> 4) * 10 + (br[8] & 0x0F);

    // PRESENTING DATA
    if (msg_type == 0) {
        data = data_make(
                "model",            "",                 DATA_STRING,    _X("Fineoffset-WHx080","Fine Offset Electronics WH1080/WH3080 Weather Station"),
                "msg_type",         "Msg type",         DATA_INT,       msg_type,
                "id",               "Station ID",       DATA_INT,       device_id,
                "temperature_C",    "Temperature",      DATA_FORMAT,    "%.01f C",  DATA_DOUBLE,    temperature,
                "humidity",         "Humidity",         DATA_FORMAT,    "%u %%",    DATA_INT,       humidity,
                "direction_deg",    "Wind degrees",     DATA_INT,       direction_deg,
                "speed",            "Wind avg speed",   DATA_FORMAT,    "%.02f",    DATA_DOUBLE,    speed,
                "gust",             "Wind gust",        DATA_FORMAT,    "%.02f",    DATA_DOUBLE,    gust,
                "rain",             "Total rainfall",   DATA_FORMAT,    "%3.1f",    DATA_DOUBLE,    rain,
                "battery",          "Battery",          DATA_STRING,    battery_low ? "LOW" : "OK",
                "mic",              "Integrity",        DATA_STRING,    "CRC",
                NULL);
    }
    else if (msg_type == 1) {
        char clock_str[20];
        sprintf(clock_str, "%04d-%02d-%02dT%02d:%02d:%02d",
                year, month, day, hours, minutes, seconds);

        data = data_make(
                "model",            "",                 DATA_STRING,    _X("Fineoffset-WHx080","Fine Offset Electronics WH1080/WH3080 Weather Station"),
                "msg_type",         "Msg type",         DATA_INT,       msg_type,
                "id",               "Station ID",       DATA_INT,       device_id,
                "signal",           "Signal Type",      DATA_STRING,    signal_type_str,
                "radio_clock",      "Radio Clock",      DATA_STRING,    clock_str,
                "mic",              "Integrity",        DATA_STRING,    "CRC",
                NULL);
    }
    else {
        data = data_make(
                "model",            "",                 DATA_STRING,    _X("Fineoffset-WHx080","Fine Offset Electronics WH3080 Weather Station"),
                "msg_type",         "Msg type",         DATA_INT,       msg_type,
                "uv_sensor_id",     "UV Sensor ID",     DATA_INT,       uv_sensor_id,
                "uv_status",        "Sensor Status",    DATA_STRING,    uv_status_ok ? "OK" : "ERROR",
                "uv_index",         "UV Index",         DATA_INT,       uv_index,
                "lux",              "Lux",              DATA_FORMAT,    "%.1f",     DATA_DOUBLE,    lux,
                "wm",               "Watts/m",          DATA_FORMAT,    "%.2f",     DATA_DOUBLE,    wm,
                "mic",              "Integrity",        DATA_STRING,    "CRC",
                NULL);
    }
    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
    "model",
    "id",
    "temperature_C",
    "humidity",
    "direction_deg",
    "speed",
    "gust",
    "rain",
    "msg_type",
    "signal",
    "radio_clock",
    "battery",
    "sensor_code",
    "uv_status",
    "uv_index",
    "lux",
    "wm",
    NULL
};

r_device fineoffset_wh1080 = {
    .name           = "Fine Offset Electronics WH1080/WH3080 Weather Station",
    .modulation     = OOK_PULSE_PWM,
    .short_width    = 544,     // Short pulse 544µs, long pulse 1524µs, fixed gap 1036µs
    .long_width     = 1524,    // Maximum pulse period (long pulse + fixed gap)
    .reset_limit    = 2800,    // We just want 1 package
    .decode_fn      = &fineoffset_wh1080_callback,
    .disabled       = 0,
    .fields         = output_fields,
};
