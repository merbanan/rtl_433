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
 */


#include "rtl_433.h"
#include "util.h"
#include "pulse_demod.h"
#include "data.h"

// ** Acurite 5n1 functions **

#define ACURITE_TXR_BITLEN		56
#define ACURITE_5N1_BITLEN		64

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



// original 5-n-1 wind direction values
// from Jens/Helge
const float acurite_winddirections[] =
    { 337.5, 315.0, 292.5, 270.0, 247.5, 225.0, 202.5, 180,
      157.5, 135.0, 112.5, 90.0, 67.5, 45.0, 22.5, 0.0 };

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
      112.5, // b - 112.5
      45.0,  // c - NE
      157.5, // d - SSE
      22.5,  // e - NNE
      180.0, // f - S
    };



static int acurite_raincounter = 0;

// FIXME< this is a checksum, not a CRC
static int acurite_crc(uint8_t row[BITBUF_COLS], int cols) {
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

static int acurite_detect(uint8_t *pRow) {
    int i;
    if ( pRow[0] != 0x00 ) {
        // invert bits due to wierd issue
        for (i = 0; i < 8; i++)
            pRow[i] = ~pRow[i] & 0xFF;
        pRow[0] |= pRow[8];  // fix first byte that has mashed leading bit

        if (acurite_crc(pRow, 7))
            return 1;  // passes crc check
    }
    return 0;
}

// Temperature encoding for 5-n-1 sensor and possibly others
static float acurite_getTemp (uint8_t highbyte, uint8_t lowbyte) {
    // range -40 to 158 F
    int highbits = (highbyte & 0x0F) << 7 ;
    int lowbits = lowbyte & 0x7F;
    int rawtemp = highbits | lowbits;
    float temp = (rawtemp - 400) / 10.0;
    return temp;
}

static int acurite_getWindSpeed (uint8_t highbyte, uint8_t lowbyte) {
    // range: 0 to 159 kph
	// TODO: sensor does not seem to be in kph, e.g.,
	// a value of 49 here was registered as 41 kph on base unit
	// value could be rpm, etc which may need (polynomial) scaling factor??
	int highbits = ( highbyte & 0x1F) << 3;
    int lowbits = ( lowbyte & 0x70 ) >> 4;
    int speed = highbits | lowbits;
    return speed;
}

// For the 5n1 based on a linear/circular encoding.
static float acurite_getWindDirection (uint8_t byte) {
    // 16 compass points, ccw from (NNW) to 15 (N)
    int direction = byte & 0x0F;
    return acurite_winddirections[direction];
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


int acurite5n1_callback(bitbuffer_t *bitbuffer) {
    // acurite 5n1 weather sensor decoding for rtl_433
    // Jens Jensen 2014
    bitrow_t *bb = bitbuffer->bb;
    int i;
    uint8_t *buf = NULL;
    // run through rows til we find one with good crc (brute force)
    for (i=0; i < BITBUF_ROWS; i++) {
        if (acurite_detect(bb[i])) {
            buf = bb[i];
            break; // done
        }
    }

    if (buf) {
        // decode packet here
        if (debug_output) {
	    fprintf(stdout, "Detected Acurite 5n1 sensor, %d bits\n",bitbuffer->bits_per_row[1]);
            for (i=0; i < 8; i++)
                fprintf(stdout, "%02X ", buf[i]);
            fprintf(stdout, "CRC OK\n");
        }

        if ((buf[2] & 0x0F) == 1) {
            // wind speed, wind direction, rainfall

            float rainfall = 0.00;
            int raincounter = acurite_getRainfallCounter(buf[5], buf[6]);
            if (acurite_raincounter > 0) {
                // track rainfall difference after first run
                rainfall = ( raincounter - acurite_raincounter ) * 0.01;
            } else {
                // capture starting counter
                acurite_raincounter = raincounter;
            }

            fprintf(stdout, "wind speed: %d kph, ",
                acurite_getWindSpeed(buf[3], buf[4]));
            fprintf(stdout, "wind direction: %0.1f°, ",
                acurite_getWindDirection(buf[4]));
            fprintf(stdout, "rain gauge: %0.2f in.\n", rainfall);

        } else if ((buf[2] & 0x0F) == 8) {
            // wind speed, temp, RH
            fprintf(stdout, "wind speed: %d kph, ",
                acurite_getWindSpeed(buf[3], buf[4]));
            fprintf(stdout, "temp: %2.1f° F, ",
                acurite_getTemp(buf[4], buf[5]));
            fprintf(stdout, "humidity: %d%% RH\n",
                acurite_getHumidity(buf[6]));
        }
    } else {
    	return 0;
    }

    return 1;
}

static int acurite_rain_gauge_callback(bitbuffer_t *bitbuffer) {
 	bitrow_t *bb = bitbuffer->bb;
   // This needs more validation to positively identify correct sensor type, but it basically works if message is really from acurite raingauge and it doesn't have any errors
    if ((bb[0][0] != 0) && (bb[0][1] != 0) && (bb[0][2]!=0) && (bb[0][3] == 0) && (bb[0][4] == 0)) {
	    float total_rain = ((bb[0][1]&0xf)<<8)+ bb[0][2];
		total_rain /= 2; // Sensor reports number of bucket tips.  Each bucket tip is .5mm
        fprintf(stdout, "AcuRite Rain Gauge Total Rain is %2.1fmm\n", total_rain);
		fprintf(stdout, "Raw Message: %02x %02x %02x %02x %02x\n",bb[0][0],bb[0][1],bb[0][2],bb[0][3],bb[0][4]);
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
// II XT TT HH CC
// II - ID byte, changes at each power up
// X - Unknown, usually 0x2, possible battery status
// TTT - Temp in Celsius * 10, 12 bit with complement.
// HH - Humidity
// CC - Checksum
//
// @todo - see if the 3rd nybble is battery/status
//
static int acurite_th_callback(bitbuffer_t *bitbuf) {
    uint8_t *bb = NULL;
    int cksum, valid = 0;
    float tempc;
    uint8_t humidity;
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
	humidity = bb[3];

	data = data_make(
		     "time",		"",		DATA_STRING,	time_str,
		     "model",		"",		DATA_STRING,	"Acurite 609TXC Sensor",
		     "temperature_C", 	"Temperature",	DATA_FORMAT,	"%.1f C", DATA_DOUBLE, tempc,
		     "humidity",	"Humidity",	DATA_INT,	humidity,
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

static int acurite_txr_callback(bitbuffer_t *bitbuf) {
    int browlen;
    uint8_t *bb;
    float tempc, tempf, wind_dird, rainfall = 0.0, wind_speedmph;
    uint8_t humidity, sensor_status, repeat_no, message_type;
    char channel, *wind_dirstr = "";
    uint16_t sensor_id;
    int wind_speed, raincounter;


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

	if (bitbuf->bits_per_row[brow] < ACURITE_TXR_BITLEN ||
	    bitbuf->bits_per_row[brow] > ACURITE_5N1_BITLEN + 1) {
	    if (debug_output > 1 && bitbuf->bits_per_row[brow] > 16)
		fprintf(stderr,"acurite_txr: skipping wrong len\n");
	    continue;
	}

	// There will be 1 extra false zero bit added by the demod.
	// this forces an extra zero byte to be added
	if (bb[browlen - 1] == 0)
	    browlen--;

	if (!acurite_crc(bb,browlen - 1)) {
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
	    tempf = celsius2fahrenheit(tempc);

	    printf("%s Acurite tower sensor 0x%04X Ch %c: %3.1F C %3.1F F %d %% RH\n",
		   time_str, sensor_id, channel, tempc, tempf, humidity);

	    // currently 0x44 seens to be a normal status and/or type
	    // for tower sensors.  Battery OK/Normal == 0x40
	    if (sensor_status != 0x44)
		printf("%s Acurite tower sensor 0x%04X Ch %c, Status %02X\n",
		       time_str, sensor_id, channel, sensor_status);

	}

	// The 5-n-1 weather sensor messages are 8 bytes.
	if (browlen == ACURITE_5N1_BITLEN / 8) {
	    channel = acurite_getChannel(bb[0]);
	    sensor_id = acurite_5n1_getSensorId(bb[0],bb[1]);
	    repeat_no = acurite_5n1_getMessageCaught(bb[0]);
	    message_type = bb[2] & 0x3f;


	    if (message_type == 0x31) {
		// Wind speed, wind direction, and rain fall
	        wind_speed = acurite_getWindSpeed(bb[3], bb[4]);
		wind_speedmph = kmph2mph(wind_speed);
		wind_dird = acurite_5n1_winddirections[bb[4] & 0x0f];
		wind_dirstr = acurite_5n1_winddirection_str[bb[4] & 0x0f];
		raincounter = acurite_getRainfallCounter(bb[5], bb[6]);
		if (acurite_raincounter > 0) {
		    // track rainfall difference after first run
		    rainfall = ( raincounter - acurite_raincounter ) * 0.01;
		    if (raincounter < acurite_raincounter) {
			printf("%s Acurite 5n1 sensor 0x%04X Ch %c, rain counter reset or wrapped around (old %d, new %d)\n",
			       time_str, sensor_id, channel, acurite_raincounter, raincounter);
			acurite_raincounter = raincounter;
		    }
		} else {
		    // capture starting counter
		    acurite_raincounter = raincounter;
		    printf("%s Acurite 5n1 sensor 0x%04X Ch %c, Total rain fall since last reset: %0.2f\n",
			   time_str, sensor_id, channel, raincounter * 0.01);
		}

		printf("%s Acurite 5n1 sensor 0x%04X Ch %c, Msg %02x, Wind %d kmph / %0.1f mph %0.1f° %s (%d), rain gauge %0.2f in.\n",
		       time_str, sensor_id, channel, message_type,
		       wind_speed, wind_speedmph,
		       wind_dird, wind_dirstr, bb[4] & 0x0f, rainfall);

	    } else if (message_type == 0x38) {
		// Wind speed, temperature and humidity
		wind_speed = acurite_getWindSpeed(bb[3], bb[4]);
		wind_speedmph = kmph2mph((float) wind_speed);
		tempf = acurite_getTemp(bb[4], bb[5]);
		tempc = fahrenheit2celsius(tempf);
		humidity = acurite_getHumidity(bb[6]);

		printf("%s Acurite 5n1 sensor 0x%04X Ch %c, Msg %02x, Wind %d kmph / %0.1f mph, %3.1F C %3.1F F %d %% RH\n",
		       time_str, sensor_id, channel, message_type,
		       wind_speed, wind_speedmph, tempc, tempf, humidity);
	    } else {
		printf("%s Acurite 5n1 sensor 0x%04X Ch %c, Status %02X, Unknown message type 0x%02x\n",
			time_str, sensor_id, channel, bb[3], message_type);
	    }
	}
    }

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
    if ((bb[0][0] == 0) && (bb[1][4] == 0) && (bb[7][0] == 0x00) && ((bb[1][1] & 0x70) == 0)) {
        // calculate the checksum and only continue if we have a maching checksum
        uint8_t chk = Checksum(3, &bb[1][0]);

        if (chk == bb[1][3]) {
	    // Processing the temperature: 
            // Upper 4 bits are stored in nibble 1, lower 8 bits are stored in nibble 2
            // upper 4 bits of nibble 1 are reserved for other usages (e.g. battery status)
      	    temp = (int16_t)((uint16_t)(bb[1][1] << 12) | bb[1][2] << 4);
      	    temp = temp >> 4;

      	    temperature = temp / 10.0;
	    sensor_id = bb[1][0];
	    battery = bb[1][1] & 0x8f >> 7;

	    data = data_make("time",          "",            DATA_STRING, time_str,
                             "model",         "",            DATA_STRING, "Acurite 606TX Sensor",
                             "id",            "",            DATA_INT, sensor_id,
			     "battery",	      "Battery",     DATA_STRING, battery ? "OK" : "LOW",
                             "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temperature,
                             NULL);
 	    data_acquired_handler(data);

	}
    }

    return 0;
}

r_device acurite5n1 = {
    .name           = "Acurite 5n1 Weather Station",
    .modulation     = OOK_PULSE_PWM_RAW,
    .short_limit    = 280,
    .long_limit     = 520,
    .reset_limit    = 800,
    .json_callback  = &acurite5n1_callback,
    .disabled       = 1,
    .demod_arg      = 0,
};

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
    .disabled       = 1,
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
    .name           = "Acurite 592TXR Temperature/Humidity Sensor and 5n1 Weather Station",
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
