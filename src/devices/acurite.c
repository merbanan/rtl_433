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
 * - 609TXC "TH" temperature and humidity sensor (609A1TX)
 * - Acurite 986 Refrigerator / Freezer Thermometer
 * - Acurite 606TX temperature sesor
 * - Acurite 6045M Lightning Detector (Work in Progress)
 */


#include "rtl_433.h"
#include "util.h"
#include "pulse_demod.h"
#include "data.h"

// ** Acurite 5n1 functions **

#define ACURITE_TXR_BITLEN		56
#define ACURITE_5N1_BITLEN		64
#define ACURITE_6045_BITLEN		72

// ** Acurite known message types
#define ACURITE_MSGTYPE_WINDSPEED_WINDDIR_RAINFALL  0x31
#define ACURITE_MSGTYPE_WINDSPEED_TEMP_HUMIDITY     0x38

static char time_str[LOCAL_TIME_BUFLEN];


// Acurite 5n1 Wind direction values.
// There are seem to be conflicting decodings.
// It is possible there there are different versions
// of the 5n1 station that report differently.
//
// The original implementation used by the 5n1 device type
// here seems to have a straight linear/cicular mapping.
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
char * acurite_5n1_winddirection_str[] =
    {"NW",  // 0  315
     "WSW", // 1  247.5
     "WNW", // 2  292.5
     "W",   // 3  270
     "NNW", // 4  337.5
     "SW",  // 5  225
     "N",   // 6  0
     "SSW", // 7  202.5
     "ENE", // 8  67.5
     "SE",  // 9  135
     "E",   // 10 90
     "ESE", // 11 112.5
     "NE",  // 12 45
     "SSE", // 13 157.5
     "NNE", // 14 22.5
     "S"};  // 15 180


const float acurite_5n1_winddirections[] =
    { 315.0, // 0 - NW
      247.5, // 1 - WSW
      292.5, // 2 - WNW
      270.0, // 3 - W
      337.5, // 4 - NNW
      225.0, // 5 - SW
      0.0,   // 6 - N
      202.5, // 7 - SSW
      67.5,  // 8 - ENE
      135.0, // 9 - SE
      90.0,  // a - E
      112.5, // b - ESE
      45.0,  // c - NE
      157.5, // d - SSE
      22.5,  // e - NNE
      180.0, // f - S
    };


// 5n1 keep state for how much rain has been seen so far
static int acurite_5n1raincounter = 0;  // for 5n1 decoder
static int acurite_5n1t_raincounter = 0;  // for combined 5n1/TXR decoder


static int acurite_checksum(uint8_t row[BITBUF_COLS], int cols) {
    // sum of first n-1 bytes modulo 256 should equal nth byte
    // also disregard a row of all zeros
    int i;
    int sum = 0;
    for ( i=0; i < cols; i++)
        sum += row[i];
    if (sum != 0 && (sum % 256 == row[cols]))
        return 1;
    else
        return 0;
}

// Temperature encoding for 5-n-1 sensor and possibly others
static float acurite_getTemp (uint8_t highbyte, uint8_t lowbyte) {
    // range -40 to 158 F
    int highbits = (highbyte & 0x0F) << 7 ;
    int lowbits = lowbyte & 0x7F;
    int rawtemp = highbits | lowbits;
    float temp_F = (rawtemp - 400) / 10.0;
    return temp_F;
}

static float acurite_getWindSpeed_kph (uint8_t highbyte, uint8_t lowbyte) {
    // range: 0 to 159 kph
    // raw number is cup rotations per 4 seconds
    // http://www.wxforum.net/index.php?topic=27244.0 (found from weewx driver)
	int highbits = ( highbyte & 0x1F) << 3;
    int lowbits = ( lowbyte & 0x70 ) >> 4;
    int rawspeed = highbits | lowbits;
    float speed_kph = 0;
    if (rawspeed > 0) {
        speed_kph = rawspeed * 0.8278 + 1.0;
    }
    return speed_kph;
}

static int acurite_getHumidity (uint8_t byte) {
    // range: 1 to 99 %RH
    int humidity = byte & 0x7F;
    return humidity;
}

static int acurite_getRainfallCounter (uint8_t hibyte, uint8_t lobyte) {
    // range: 0 to 99.99 in, 0.01 in incr., rolling counter?
	int raincounter = ((hibyte & 0x7f) << 7) | (lobyte & 0x7F);
    return raincounter;
}

// The high 2 bits of byte zero are the channel (bits 7,6)
//  00 = C
//  10 = B
//  11 = A
static char chLetter[4] = {'C','E','B','A'}; // 'E' stands for error

static char acurite_getChannel(uint8_t byte){
    int channel = (byte & 0xC0) >> 6;
    return chLetter[channel];
}

// 5-n-1 sensor ID is the last 12 bits of byte 0 & 1
// byte 0     | byte 1
// CC RR IIII | IIII IIII
//
static uint16_t acurite_5n1_getSensorId(uint8_t hibyte, uint8_t lobyte){
    return ((hibyte & 0x0f) << 8) | lobyte;
}


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
static int acurite_5n1_getMessageCaught(uint8_t byte){
    return (byte & 0x30) >> 4;
}


// So far, all that's known about the battery is that the
// third byte, high nibble has two values.xo 0xb0=low and 0x70=OK
// so this routine just returns the nibble shifted to make a byte
// for more work as time goes by
//
// Battery status appears to be the 7th bit 0x40. 1 = normal, 0 = low
// The 8th bit appears to be parity.
// @todo - determine if the 5th & 6th bits (0x30) are status bits or
//         part of the message type. So far these appear to always be 1
static int acurite_5n1_getBatteryLevel(uint8_t byte){
    return (byte & 0x40) >> 6;
}


static int acurite_rain_gauge_callback(bitbuffer_t *bitbuffer) {
 	bitrow_t *bb = bitbuffer->bb;
   // This needs more validation to positively identify correct sensor type, but it basically works if message is really from acurite raingauge and it doesn't have any errors
    if ((bb[0][0] != 0) && (bb[0][1] != 0) && (bb[0][2]!=0) && (bb[0][3] == 0) && (bb[0][4] == 0)) {
	    float total_rain = ((bb[0][1]&0xf)<<8)+ bb[0][2];
		total_rain /= 2; // Sensor reports number of bucket tips.  Each bucket tip is .5mm

		if (debug_output > 1) {
			fprintf(stdout, "AcuRite Rain Gauge Total Rain is %2.1fmm\n", total_rain);
			fprintf(stdout, "Raw Message: %02x %02x %02x %02x %02x\n",bb[0][0],bb[0][1],bb[0][2],bb[0][3],bb[0][4]);
		}

		uint8_t id = bb[0][0];
		data_t *data;
		local_time_str(0, time_str);

		data = data_make(
			"time",	"",		DATA_STRING,	time_str,
			"model",	"",		DATA_STRING,	"Acurite Rain Gague",
			"id",		"",		DATA_INT,	id,
			"rain", 	"Total Rain",	DATA_FORMAT,	"%.1f mm", DATA_DOUBLE, total_rain,
			NULL);

		data_acquired_handler(data);

        return 1;
    }
    return 0;
}


// Acurite 609TXC
// Temperature in Celsius is encoded as a 12 bit integer value
// multiplied by 10 using the 4th - 6th nybbles (bytes 1 & 2)
// negative values are handled by treating it temporarily
// as a 16 bit value to put the sign bit in a usable place.
//
static float acurite_th_temperature(uint8_t *s){
    uint16_t shifted = (((s[1] & 0x0f) << 8) | s[2]) << 4; // Logical left shift
    return (((int16_t)shifted) >> 4) / 10.0; // Arithmetic right shift
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
static int acurite_th_callback(bitbuffer_t *bitbuf) {
    uint8_t *bb = NULL;
    int cksum, battery_low, valid = 0;
    float tempc;
    uint8_t humidity, id, status;
    data_t *data;

    local_time_str(0, time_str);

    for (uint16_t brow = 0; brow < bitbuf->num_rows; ++brow) {
        if (bitbuf->bits_per_row[brow] != 40) {
	    continue;
	}

	bb = bitbuf->bb[brow];

	cksum = (bb[0] + bb[1] + bb[2] + bb[3]);

	if (cksum == 0 || ((cksum & 0xff) != bb[4])) {
	    continue;
	}

	tempc = acurite_th_temperature(bb);
	id = bb[0];
	status = (bb[1] & 0xf0) >> 4;
	battery_low = status & 0x8;
	humidity = bb[3];

	data = data_make(
		     "time",		"",		DATA_STRING,	time_str,
		     "model",		"",		DATA_STRING,	"Acurite 609TXC Sensor",
		     "id",		"",		DATA_INT,	id,
		     "battery",		"",		DATA_STRING,	battery_low ? "LOW" : "OK",
		     "status",		"",		DATA_INT,	status,
		     "temperature_C", 	"Temperature",	DATA_FORMAT,	"%.1f C", DATA_DOUBLE, tempc,
		     "humidity",        "Humidity",	DATA_INT,	humidity,
		     NULL);

	data_acquired_handler(data);
	valid++;
    }

    if (valid)
        return 1;

    return 0;
}

// Tower sensor ID is the last 14 bits of byte 0 & 1
// byte 0    | byte 1
// CCII IIII | IIII IIII
//
static uint16_t acurite_txr_getSensorId(uint8_t hibyte, uint8_t lobyte){
    return ((hibyte & 0x3f) << 8) | lobyte;
}


// temperature encoding used by "tower" sensors 592txr
// 14 bits available after removing both parity bits.
// 11 bits needed for specified range -40 C to 70 C (-40 F - 158 F)
// range -100 C to 1538.4 C
static float acurite_txr_getTemp (uint8_t highbyte, uint8_t lowbyte) {
    int rawtemp = ((highbyte & 0x7F) << 7) | (lowbyte & 0x7F);
    float temp = rawtemp / 10.0 - 100;
    return temp;
}


/*
 * Acurite 06045 Lightning sensor Temperature encoding
 * 12 bits of temperature after removing parity and status bits.
 * Message native format appears to be in 1/10 of a degree Fahrenheit
 * Device Specification: -40 to 158 F  / -40 to 70 C
 * Available range given encoding with 12 bits -150.0 F to +259.6 F
 */
static float acurite_6045_getTemp (uint8_t highbyte, uint8_t lowbyte) {
    int rawtemp = ((highbyte & 0x1F) << 7) | (lowbyte & 0x7F);
    float temp = (rawtemp - 1500) / 10.0;
    return temp;
}

/*
 * Acurite 06045m Lightning Sensor decoding.
 *
 * Specs:
 * - lightning strike count
 * - extimated distance to front of storm, up to 25 miles / 40 km
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
 * CI? II  II  HH  ST  TT  LL  DD? KK
 *
 * C = Channel
 * I = ID
 * H = Humidity
 * S = Status/Message type/Temperature MSB.
 * T = Temperature
 * D = Lightning distanace and status bits?
 * L = Lightning strike count.
 * K = Checksum
 *
 * Byte 0 - channel number A/B/C
 * - Channel in 2 most significant bits - A: 0xC, B: 0x8, C: 00
 * - TBD: lower 6 bits, ID or unused?
 *
 * Bytes 1 & 2 - ID, all 8 bits, no parity.
 *
 * Byte 3 - Humidity (7 bits + parity bit)
 *
 * Byte 4 - Status (2 bits) and Temperature MSB (5 bits)
 * - Bitmask PSSTTTTT  (P = Parity, S = Status, T = Temperature)
 * - 0x40 - Transmitting every 8 seconds (lightning possibly detected)
 *          normal, off, transmits every 24 seconds
 * - 0x20 - TBD: normally off, On is possibly low battery?
 * - 0x1F - Temperature MSB (5 bits)
 *
 * Byte 5 - Temperature LSB (7 bits, 8th is parity)
 *
 * Byte 6 - Lightning Strike count (7 bits, 8th is parity)
 * - Stored in EEPROM or something non-volatile)
 * - Wraps at 127
 *
 * Byte 7 - Lightning Distance (5 bits) and status bits (2 bits)  (?)
 * - Bits PSSDDDDD  (P = Parity, S = Status, D = Distance
 * - 5 lower bits is distance in unit? (miles? km?) to edge of storm (theory)
 * - Bit 0x20: (RF) interference / strong RFI detected (to be verified)
 * - Bit 0x40: TBD, possible activity?
 * - distance = 0x1f: possible invalid value indication (value at power up)
 * - Note: Distance sometimes goes to 0 right after strike counter increment
 *         status bits might indicate validifity of distance.
 *
 * Byte 8 - checksum. 8 bits, no parity.
 *
 * @todo - Get lightning/distance to front of storm to match display
 * @todo - Low battery, figure out encoding
 * @todo - figure out remaining status bits and how to report
 * @todo - convert to data make once decoding is stable
 */
static int acurite_6045_decode (bitrow_t bb, int browlen) {
    int valid = 0;
    float tempf;
    uint8_t humidity, message_type, l_status;
    char channel, *wind_dirstr = "";
    char channel_str[2];
    uint16_t sensor_id;
    uint8_t strike_count, strike_distance;

    channel = acurite_getChannel(bb[0]);  // same as TXR
    sensor_id = (bb[1] << 8) | bb[2];     // TBD 16 bits or 20?
    humidity = acurite_getHumidity(bb[3]);  // same as TXR
    message_type = (bb[4] & 0x60) >> 5;  // status bits: 0x2 8 second xmit, 0x1 - TBD batttery?
    tempf = acurite_6045_getTemp(bb[4], bb[5]);
    strike_count = bb[6] & 0x7f;
    strike_distance = bb[7] & 0x1f;
    l_status = (bb[7] & 0x60) >> 5;

    printf("%s Acurite lightning 0x%04X Ch %c Msg Type 0x%02x: %.1f F %d %% RH Strikes %d Distance %d L_status 0x%02x -",
	   time_str, sensor_id, channel, message_type, tempf, humidity, strike_count, strike_distance, l_status);

    // FIXME Temporarily dump raw message data until the
    // decoding improves.  Includes parity indicator(*).
    for (int i=0; i < browlen; i++) {
	char pc;
	pc = byteParity(bb[i]) == 0 ? ' ' : '*';
	fprintf(stdout, " %02x%c", bb[i], pc);
    }
    printf("\n");

    valid++;
    return(valid);
}


/*
 * This callback handles several Acurite devices that use a very
 * similar RF encoding and data format:
 *:
 * - 592TXR temperature and humidity sensor
 * - 5-n-1 weather station
 * - 6045M Lightning Detectur with Temperature and Humidity
 */
static int acurite_txr_callback(bitbuffer_t *bitbuf) {
    int browlen, valid = 0;
    uint8_t *bb;
    float tempc, tempf, wind_dird, rainfall = 0.0, wind_speed, wind_speedmph;
    uint8_t humidity, sensor_status, sequence_num, message_type;
    char channel, *wind_dirstr = "";
    char channel_str[2];
    uint16_t sensor_id;
    int raincounter, temp, battery_low;
    uint8_t strike_count, strike_distance;
    data_t *data;


    local_time_str(0, time_str);

    if (debug_output > 1) {
        fprintf(stderr,"acurite_txr\n");
        bitbuffer_print(bitbuf);
    }

    for (uint16_t brow = 0; brow < bitbuf->num_rows; ++brow) {
	browlen = (bitbuf->bits_per_row[brow] + 7)/8;
	bb = bitbuf->bb[brow];

	if (debug_output > 1)
	    fprintf(stderr,"acurite_txr: row %d bits %d, bytes %d \n", brow, bitbuf->bits_per_row[brow], browlen);

	if ((bitbuf->bits_per_row[brow] < ACURITE_TXR_BITLEN ||
	     bitbuf->bits_per_row[brow] > ACURITE_5N1_BITLEN + 1) &&
	    bitbuf->bits_per_row[brow] != ACURITE_6045_BITLEN) {
	    if (debug_output > 1 && bitbuf->bits_per_row[brow] > 16)
		fprintf(stderr,"acurite_txr: skipping wrong len\n");
	    continue;
	}

	// There will be 1 extra false zero bit added by the demod.
	// this forces an extra zero byte to be added
	if (bb[browlen - 1] == 0)
	    browlen--;

	if (!acurite_checksum(bb,browlen - 1)) {
	    if (debug_output) {
		fprintf(stderr, "%s Acurite bad checksum:", time_str);
		for (uint8_t i = 0; i < browlen; i++)
		    fprintf(stderr," 0x%02x",bb[i]);
		fprintf(stderr,"\n");
	    }
	    continue;
	}

	if (debug_output) {
	    fprintf(stderr, "acurite_txr Parity: ");
	    for (uint8_t i = 0; i < browlen; i++) {
		fprintf(stderr,"%d",byteParity(bb[i]));
	    }
	    fprintf(stderr,"\n");
	}


	// tower sensor messages are 7 bytes.
	// @todo - see if there is a type in the message that
	// can be used instead of length to determine type
	if (browlen == ACURITE_TXR_BITLEN / 8) {
	    channel = acurite_getChannel(bb[0]);
	    sensor_id = acurite_txr_getSensorId(bb[0],bb[1]);
	    sensor_status = bb[2]; // @todo, uses parity? & 0x07f
	    humidity = acurite_getHumidity(bb[3]);
	    tempc = acurite_txr_getTemp(bb[4], bb[5]);
            sprintf(channel_str, "%c", channel);
            battery_low = sensor_status >>7;

            data = data_make(
                    "time",			"",		DATA_STRING,	time_str,
                    "model",	        	"",		DATA_STRING,	"Acurite tower sensor",
                    "id",			"",		DATA_INT,	sensor_id,
                    "channel",  		"",     	DATA_STRING, 	&channel_str,
                    "temperature_C", 	"Temperature",	DATA_FORMAT,	"%.1f C", DATA_DOUBLE, tempc,
                    "humidity",         "Humidity",	DATA_INT,	humidity,
                    "battery",          "Battery",    	DATA_INT, 	battery_low,
                    "status",		"",		DATA_INT,	sensor_status,
                    NULL);

            data_acquired_handler(data);
            valid++;
	}

	// The 5-n-1 weather sensor messages are 8 bytes.
	if (browlen == ACURITE_5N1_BITLEN / 8) {
        if (debug_output) {
            fprintf(stderr, "Acurite 5n1 raw msg: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                bb[0], bb[1], bb[2], bb[3], bb[4], bb[5], bb[6], bb[7]);
        }
	    channel = acurite_getChannel(bb[0]);
        sprintf(channel_str, "%c", channel);
	    sensor_id = acurite_5n1_getSensorId(bb[0],bb[1]);
	    sequence_num = acurite_5n1_getMessageCaught(bb[0]);
	    message_type = bb[2] & 0x3f;
        battery_low = (bb[2] & 0x40) >> 6;

	    if (message_type == ACURITE_MSGTYPE_WINDSPEED_WINDDIR_RAINFALL) {
            // Wind speed, wind direction, and rain fall
            wind_speed = acurite_getWindSpeed_kph(bb[3], bb[4]);
            wind_speedmph = kmph2mph(wind_speed);
            wind_dird = acurite_5n1_winddirections[bb[4] & 0x0f];
            wind_dirstr = acurite_5n1_winddirection_str[bb[4] & 0x0f];
            raincounter = acurite_getRainfallCounter(bb[5], bb[6]);
            if (acurite_5n1t_raincounter > 0) {
                // track rainfall difference after first run
                // FIXME when converting to structured output, just output
                // the reading, let consumer track state/wrap around, etc.
                rainfall = ( raincounter - acurite_5n1t_raincounter ) * 0.01;
                if (raincounter < acurite_5n1t_raincounter) {
                    fprintf(stderr, "%s Acurite 5n1 sensor 0x%04X Ch %c, rain counter reset or wrapped around (old %d, new %d)\n",
                        time_str, sensor_id, channel, acurite_5n1t_raincounter, raincounter);
                    acurite_5n1t_raincounter = raincounter;
                }
            } else {
                // capture starting counter
                acurite_5n1t_raincounter = raincounter;
                fprintf(stderr, "%s Acurite 5n1 sensor 0x%04X Ch %c, Total rain fall since last reset: %0.2f\n",
                time_str, sensor_id, channel, raincounter * 0.01);
            }

            data = data_make(
                "time",         "",   DATA_STRING,    time_str,
                "model",        "",   DATA_STRING,    "Acurite 5n1 sensor",
                "sensor_id",    NULL,   DATA_FORMAT,    "0x%02X",   DATA_INT,       sensor_id,
                "channel",      NULL,   DATA_STRING,    &channel_str,
                "sequence_num",  NULL,   DATA_INT,      sequence_num,
                "battery",      NULL,   DATA_STRING,    battery_low ? "OK" : "LOW",
                "message_type", NULL,   DATA_INT,       message_type,
                "wind_speed",   NULL,   DATA_FORMAT,    "%.1f mph", DATA_DOUBLE,     wind_speedmph,
                "wind_dir_deg", NULL,   DATA_FORMAT,    "%.1f", DATA_DOUBLE,    wind_dird,
                "wind_dir",     NULL,   DATA_STRING,    wind_dirstr,
                "rainfall_accumulation",     NULL,   DATA_FORMAT,    "%.2f in", DATA_DOUBLE,    rainfall,
                "raincounter_raw",  NULL,   DATA_INT,   raincounter,
                NULL);

            data_acquired_handler(data);

	    } else if (message_type == ACURITE_MSGTYPE_WINDSPEED_TEMP_HUMIDITY) {
            // Wind speed, temperature and humidity
            wind_speed = acurite_getWindSpeed_kph(bb[3], bb[4]);
            wind_speedmph = kmph2mph(wind_speed);
            tempf = acurite_getTemp(bb[4], bb[5]);
            tempc = fahrenheit2celsius(tempf);
            humidity = acurite_getHumidity(bb[6]);

            data = data_make(
                "time",         "",   DATA_STRING,    time_str,
                "model",        "",   DATA_STRING,    "Acurite 5n1 sensor",
                "sensor_id",    NULL,   DATA_FORMAT,    "0x%02X",   DATA_INT,       sensor_id,
                "channel",      NULL,   DATA_STRING,    &channel_str,
                "sequence_num",  NULL,   DATA_INT,      sequence_num,
                "battery",      NULL,   DATA_STRING,    battery_low ? "OK" : "LOW",
                "message_type", NULL,   DATA_INT,       message_type,
                "wind_speed",   NULL,   DATA_FORMAT,    "%.1f mph", DATA_DOUBLE,     wind_speedmph,
                "temperature_F", 	"temperature",	DATA_FORMAT,    "%.1f F", DATA_DOUBLE,    tempf,
                "humidity",     NULL,	DATA_FORMAT,    "%d",   DATA_INT,   humidity,
                NULL);
            data_acquired_handler(data);

	    } else {
            fprintf(stderr, "%s Acurite 5n1 sensor 0x%04X Ch %c, Status %02X, Unknown message type 0x%02x\n",
                time_str, sensor_id, channel, bb[3], message_type);
	    }
	}

	if (browlen == ACURITE_6045_BITLEN / 8) {
	    // @todo check parity and reject if invalid
	    valid += acurite_6045_decode(bb, browlen);
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
 * by default 1 - Refridgerator, 2 - Freezer
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
 * T - Temperature in Fahrenehit, integer, MSB = sign.
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
 * - low battery detection
 *
 */

static int acurite_986_callback(bitbuffer_t *bitbuf) {
    int browlen;
    uint8_t *bb, sensor_num, status, crc, crcc;
    uint8_t br[8];
    int8_t tempf; // Raw Temp is 8 bit signed Fahrenheit
    float tempc;
    uint16_t sensor_id, valid_cnt = 0;
    char sensor_type;

    local_time_str(0, time_str);

    if (debug_output > 1) {
        fprintf(stderr,"acurite_986\n");
        bitbuffer_print(bitbuf);
    }

    for (uint16_t brow = 0; brow < bitbuf->num_rows; ++brow) {
	browlen = (bitbuf->bits_per_row[brow] + 7)/8;
	bb = bitbuf->bb[brow];

	if (debug_output > 1)
	    fprintf(stderr,"acurite_986: row %d bits %d, bytes %d \n", brow, bitbuf->bits_per_row[brow], browlen);

	if (bitbuf->bits_per_row[brow] < 39 ||
	    bitbuf->bits_per_row[brow] > 43 ) {
	    if (debug_output > 1 && bitbuf->bits_per_row[brow] > 16)
		fprintf(stderr,"acurite_986: skipping wrong len\n");
	    continue;
	}

	// Reduce false positives
	// may eliminate these with a beter PPM (precise?) demod.
	if ((bb[0] == 0xff && bb[1] == 0xff && bb[2] == 0xff) ||
	   (bb[0] == 0x00 && bb[1] == 0x00 && bb[2] == 0x00)) {
	    continue;
	}

	// There will be 1 extra false zero bit added by the demod.
	// this forces an extra zero byte to be added
	if (browlen > 5 && bb[browlen - 1] == 0)
	    browlen--;

	// Reverse the bits
	for (uint8_t i = 0; i < browlen; i++)
	    br[i] = reverse8(bb[i]);

	if (debug_output > 0) {
	    fprintf(stderr,"Acurite 986 reversed: ");
	    for (uint8_t i = 0; i < browlen; i++)
		fprintf(stderr," %02x",br[i]);
	    fprintf(stderr,"\n");
	}

	tempf = br[0];
	sensor_id = (br[1] << 8) + br[2];
	status = br[3];
	sensor_num = (status & 0x01) + 1;
	status = status >> 1;
	// By default Sensor 1 is the Freezer, 2 Refrigerator
	sensor_type = sensor_num == 2 ? 'F' : 'R';
	crc = br[4];

	if ((crcc = crc8le(br, 5, 0x07, 0)) != 0) {
	    // XXX make debug
	    if (debug_output) {
		fprintf(stderr,"%s Acurite 986 sensor bad CRC: %02x -",
			time_str, crc8le(br, 4, 0x07, 0));
		for (uint8_t i = 0; i < browlen; i++)
		    fprintf(stderr," %02x", br[i]);
		fprintf(stderr,"\n");
	    }
	    continue;
	}

	if ((status & 1) == 1) {
	    fprintf(stderr, "%s Acurite 986 sensor 0x%04x - %d%c: low battery, status %02x\n",
		    time_str, sensor_id, sensor_num, sensor_type, status);
	}

	// catch any status bits that haven't been decoded yet
	if ((status & 0xFE) != 0) {
	    fprintf(stderr, "%s Acurite 986 sensor 0x%04x - %d%c: Unexpected status %02x\n",
		    time_str, sensor_id, sensor_num, sensor_type, status);
	}

	if (tempf & 0x80) {
	    tempf = (tempf & 0x7f) * -1;
	}
	tempc = fahrenheit2celsius(tempf);


	printf("%s Acurite 986 sensor 0x%04x - %d%c: %3.1f C %d F\n",
	       time_str, sensor_id, sensor_num, sensor_type,
	       tempc, tempf);

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
//
uint8_t Checksum(int length, uint8_t *buff) {
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


static int acurite_606_callback(bitbuffer_t *bitbuf) {
    data_t *data;
    bitrow_t *bb = bitbuf->bb;
    float temperature;	// temperature in C
    int16_t temp;	// temperature as read from the data packet
    int battery;        // the battery status: 1 is good, 0 is low
    int8_t sensor_id;	// the sensor ID - basically a random number that gets reset whenever the battery is removed


    local_time_str(0, time_str);

    if (debug_output > 1) {
        fprintf(stderr,"acurite_606\n");
        bitbuffer_print(bitbuf);
    }

    // throw out all blank messages
    if (bb[1][0] == 0 && bb[1][1] == 0 && bb[1][2] == 0 && bb[1][3] == 0)
      return 0;

    // do some basic checking to make sure we have a valid data record
    if ((bb[0][0] == 0) && (bb[1][4] == 0)) {					//This test may need some more scrutiny...
        // calculate the checksum and only continue if we have a maching checksum
        uint8_t chk = Checksum(3, &bb[1][0]);

        if (chk == bb[1][3]) {
	    // Processing the temperature:
            // Upper 4 bits are stored in nibble 1, lower 8 bits are stored in nibble 2
            // upper 4 bits of nibble 1 are reserved for other usages (e.g. battery status)
      	    temp = (int16_t)((uint16_t)(bb[1][1] << 12) | (bb[1][2] << 4));
      	    temp = temp >> 4;

      	    temperature = temp / 10.0;
	    sensor_id = bb[1][0];
	    battery = (bb[1][1] & 0x80) >> 7;

	    data = data_make("time",          "",            DATA_STRING, time_str,
                             "model",         "",            DATA_STRING, "Acurite 606TX Sensor",
                             "id",            "",            DATA_INT, sensor_id,
			     "battery",	      "Battery",     DATA_STRING, battery ? "OK" : "LOW",
                             "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temperature,
                             NULL);
 	    data_acquired_handler(data);
            return 1;
	}
    }

    return 0;
}


static int acurite_00275rm_callback(bitbuffer_t *bitbuf) {
    int crc, battery_low, id, model, valid = 0;
    uint8_t *bb;
    data_t *data;
    char *model1 = "00275rm", *model2 = "00276rm";
    float tempc, ptempc;
    uint8_t probe, humidity, phumidity, water;
    uint8_t signal[3][11];  //  Hold three copies of the signal
    int     nsignal = 0;

    local_time_str(0, time_str);

    if (debug_output > 1) {
        fprintf(stderr,"acurite_00275rm\n");
        bitbuffer_print(bitbuf);
    }

    //  This sensor repeats signal three times.  Store each copy.
    for (uint16_t brow = 0; brow < bitbuf->num_rows; ++brow) {
        if (bitbuf->bits_per_row[brow] != 88) continue;
        if (nsignal>=3) continue;
        memcpy(signal[nsignal], bitbuf->bb[brow], 11);
        if (debug_output) {
            fprintf(stderr,"acurite_00275rm: ");
            for (int i=0; i<11; i++) fprintf(stderr," %02x",signal[nsignal][i]);
            fprintf(stderr,"\n");
        }
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
        if ((crc=crc16(&(signal[0][0]), 11/*len*/, 0xb2/*poly*/, 0xd0/*seed*/)) != 0) {
            if (debug_output) {
                fprintf(stderr,"%s Acurite 00275rm sensor bad CRC: %02x -",
                    time_str, crc);
                for (uint8_t i = 0; i < 11; i++)
                    fprintf(stderr," %02x", signal[0][i]);
                fprintf(stderr,"\n");
            }
        // CRC is OK
        } else {
            //  Decode the combined signal
            id = (signal[0][0]<<16) | (signal[0][1]<<8) | signal[0][3];
            battery_low = (signal[0][2] & 0x40)==0;
            model       = (signal[0][2] & 1);
            tempc       = 0.1 * ( (signal[0][4]<<4) | (signal[0][5]>>4) ) - 100;
            probe       = signal[0][5] & 3;
            humidity    = ((signal[0][6] & 0x1f) << 2) | (signal[0][7] >> 6);
            //  No probe
            if (probe==0) {
                data = data_make(
                    "time",            "",             DATA_STRING,    time_str,
                    "model",           "",             DATA_STRING,    model ? model1 : model2,
                    "probe",           "",             DATA_INT,       probe,
                    "id",              "",             DATA_INT,       id,
                    "battery",         "",             DATA_STRING,    battery_low ? "LOW" : "OK",
                    "temperature_C",   "Celcius",      DATA_FORMAT,    "%.1f C",  DATA_DOUBLE, tempc,
                    "humidity",        "Humidity",     DATA_INT,       humidity,
                    "mic",             "Integrity",    DATA_STRING,    "CRC",

                    NULL);
            //  Water probe (detects water leak)
            } else if (probe==1) {
                water = (signal[0][7] & 0x0f) == 15;
                data = data_make(
                    "time",            "",             DATA_STRING,    time_str,
                    "model",           "",             DATA_STRING,    model ? model1 : model2,
                    "probe",           "",             DATA_INT,       probe,
                    "id",              "",             DATA_INT,       id,
                    "battery",         "",             DATA_STRING,    battery_low ? "LOW" : "OK",
                    "temperature_C",   "Celcius",      DATA_FORMAT,    "%.1f C",  DATA_DOUBLE, tempc,
                    "humidity",        "Humidity",     DATA_INT,       humidity,
                    "water",           "",             DATA_INT,       water,
                    "mic",             "Integrity",    DATA_STRING,    "CRC",
                    NULL);
            //  Soil probe (detects temperature)
            } else if (probe==2) {
                ptempc    = 0.1 * ( ((0x0f&signal[0][7])<<8) | signal[0][8] ) - 100;
                data = data_make(
                    "time",            "",             DATA_STRING,    time_str,
                    "model",           "",             DATA_STRING,    model ? model1 : model2,
                    "probe",           "",             DATA_INT,       probe,
                    "id",              "",             DATA_INT,       id,
                    "battery",         "",             DATA_STRING,    battery_low ? "LOW" : "OK",
                    "temperature_C",   "Celcius",      DATA_FORMAT,    "%.1f C",  DATA_DOUBLE, tempc,
                    "humidity",        "Humidity",     DATA_INT,       humidity,
                    "ptemperature_C",  "Celcius",      DATA_FORMAT,    "%.1f C",  DATA_DOUBLE, ptempc,
                    "mic",             "Integrity",    DATA_STRING,    "CRC",
                    NULL);
            //  Spot probe (detects temperature and humidity)
            } else if (probe==3) {
                ptempc    = 0.1 * ( ((0x0f&signal[0][7])<<8) | signal[0][8] ) - 100;
                phumidity = signal[0][9] & 0x7f;
                data = data_make(
                    "time",            "",             DATA_STRING,    time_str,
                    "model",           "",             DATA_STRING,    model ? model1 : model2,
                    "probe",           "",             DATA_INT,       probe,
                    "id",              "",             DATA_INT,       id,
                    "battery",         "",             DATA_STRING,    battery_low ? "LOW" : "OK",
                    "temperature_C",   "Celcius",      DATA_FORMAT,    "%.1f C",  DATA_DOUBLE, tempc,
                    "humidity",        "Humidity",     DATA_INT,       humidity,
                    "ptemperature_C",  "Celcius",      DATA_FORMAT,    "%.1f C",  DATA_DOUBLE, ptempc,
                    "phumidity",       "Humidity",     DATA_INT,       phumidity,
                    "mic",             "Integrity",    DATA_STRING,    "CRC",
                    NULL);
            } else { // suppress compiler warning
                return 0;
            }
            data_acquired_handler(data);
            valid=1;
        }
    }
    if (valid) return 1;
    return 0;
}


r_device acurite_rain_gauge = {
    .name           = "Acurite 896 Rain Gauge",
    .modulation     = OOK_PULSE_PPM_RAW,
    .short_limit    = 1744,
    .long_limit     = 3500,
    .reset_limit    = 5000,
    .json_callback  = &acurite_rain_gauge_callback,
// Disabled by default due to false positives on oregon scientific v1 protocol see issue #353
    .disabled       = 1,
    .demod_arg      = 0,
};


r_device acurite_th = {
    .name           = "Acurite 609TXC Temperature and Humidity Sensor",
    .modulation     = OOK_PULSE_PPM_RAW,
    .short_limit    = 1200,
    .long_limit     = 3000,
    .reset_limit    = 10000,
    .json_callback  = &acurite_th_callback,
    .disabled       = 0,
    .demod_arg      = 0,
};

/*
 * For Acurite 592 TXR Temp/Mumidity, but
 * Should match Acurite 592TX, 5-n-1, etc.
 *
 *
 * @todo, convert to use precise demodulator, after adding a flag
 *        to set "polarity" to flip short bits = 0 vs. 1.
 */

r_device acurite_txr = {
    .name           = "Acurite 592TXR Temp/Humidity, 5n1 Weather Station, 6045 Lightning",
    .modulation     = OOK_PULSE_PWM_TERNARY,
    .short_limit    = 320,
    .long_limit     = 520,
    .reset_limit    = 4000,
    .json_callback  = &acurite_txr_callback,
    .disabled       = 1,
    .demod_arg      = 2,
};

// @todo, find a set of values that will work reasonably
// with a range of signal levels
//
// PWM_Precise_Parameters pwm_precise_param_acurite_txr = {
// 	.pulse_tolerance	= 50,
// 	.pulse_sync_width	= 170,
// };

//r_device acurite_txr = {
//    .name           = "Acurite 592TXR Temp/Humidity sensor",
//    .modulation     = OOK_PULSE_PWM_PRECISE,
//    .short_limit    = 440,
//    .long_limit     = 260,
//    .reset_limit    = 4000,
//    .json_callback  = &acurite_txr_callback,
//    .disabled       = 0,
//    .demod_arg      = (unsigned long)&pwm_precise_param_acurite_txr,
//};


/*
 * Acurite 00986 Refrigerator / Freezer Thermometer
 *
 * Temperature only, Pulse Position
 *
 * 4 x 400 sample (150 uS) start/sync pulses
 * 40 (42) 50 (20 uS)  (sample data pulses)
 * short gap approx 130 samples
 * long gap approx 220 samples
 *
 */
r_device acurite_986 = {
    .name           = "Acurite 986 Refrigerator / Freezer Thermometer",
    .modulation     = OOK_PULSE_PPM_RAW,
    .short_limit    = 720,   // Threshold between short and long gap
    .long_limit     = 1280,
    .reset_limit    = 4000,
    .json_callback  = &acurite_986_callback,
    .disabled       = 1,
    .demod_arg      = 2,
};

/*
 * Acurite 00606TX Tower Sensor
 *
 * Temperature only
 *
 */
r_device acurite_606 = {
    .name           = "Acurite 606TX Temperature Sensor",
    .modulation     = OOK_PULSE_PPM_RAW,
    .short_limit    = 3500,
    .long_limit     = 7000,
    .reset_limit    = 10000,
    .json_callback  = &acurite_606_callback,
    .disabled       = 0,
    .demod_arg      = 0,
};

r_device acurite_00275rm = {
    .name           = "Acurite 00275rm,00276rm Temp/Humidity with optional probe",
    .modulation     = OOK_PULSE_PWM_TERNARY,
    .short_limit    = 320,  // = 4* 80,  80  is reported by -G option
    .long_limit     = 520,  // = 4*130, 130  "
  //  .reset_limit    = 608,  // = 4*152, 152  "
    .reset_limit    = 708,  // = 4*152, 152  "
    .json_callback  = &acurite_00275rm_callback,
    .disabled       = 0,
    .demod_arg      = 2,
};
