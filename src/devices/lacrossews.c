/* LaCrosse WS-2310 433 Mhz Weather Station
	Packet Format is 53 bits/ 13 nibbles

	 bits	nibble
	 0- 3	0 - 0000
	 4- 7	1 - 1001
	 8-11	2 - Type  GPTT  G=0, P=Parity, Gust=Gust, TT=Type  GTT 000=Temp, 001=Humidity, 010=Rain, 011=Wind, 111-Gust
	12-15	3 - ID High
	16-19	4 - ID Low
	20-23	5 - Data Types  GWRH  G=Gust Sent, W=Wind Sent, R=Rain Sent, H=Humidity Sent
	24-27	6 - Parity TUU? T=Temp Sent, UU=Next Update, 00=8 seconds, 01=32 seconds, 10=?, 11=128 seconds, ?=?
	28-31	7 - Value1
	32-35	8 - Value2
	36-39	9 - Value3
	40-43	10 - ~Value1
	44-47	11 - ~Value2
	48-51	12 - Check Sum = Nibble sum of nibbles 0-11
 */

#include "rtl_433.h"
#include "util.h"

#define LACROSSE_WS_BITLEN	52

static int lacrossews_detect(uint8_t *pRow, uint8_t *msg_nybbles, int16_t rowlen) {
	int i;
	uint8_t rbyte_no, rbit_no, mnybble_no, mbit_no;
	uint8_t bit, checksum = 0, parity = 0;
	char time_str[LOCAL_TIME_BUFLEN];

	// Weather Station 2310 Packets
	if (rowlen == LACROSSE_WS_BITLEN && pRow[0] == 0x09) {

		for (i = 0; i < (LACROSSE_WS_BITLEN / 4); i++) {
			msg_nybbles[i] = 0;
		}

		// Move nybbles into a byte array
		// Compute parity and checksum at the same time.
		for (i = 0; i < LACROSSE_WS_BITLEN; i++) {
			rbyte_no = i / 8;
			rbit_no = 7 - (i % 8);
			mnybble_no = i / 4;
			mbit_no = 3 - (i % 4);
			bit = (pRow[rbyte_no] & (1 << rbit_no)) ? 1 : 0;
			msg_nybbles[mnybble_no] |= (bit << mbit_no);
			if(i == 9 || (i >= 27 && i <= 39))
				parity += bit;
		}

		for (i = 0; i < 12; i++) {
			checksum = (checksum + msg_nybbles[i]) & 0x0F;
		}

		if( msg_nybbles[0] == 0x0 &&
			msg_nybbles[1] == 0x9 &&
			msg_nybbles[7] == (~msg_nybbles[10] & 0xF) &&
			msg_nybbles[8] == (~msg_nybbles[11] & 0xF) &&
			(parity & 0x1) == 0x1 &&
			checksum == msg_nybbles[12])
			return 1;
		else {
			local_time_str(0, time_str);
			fprintf(stdout,
				"%s LaCrosse Packet Validation Failed error: Checksum Comp. %d != Recv. %d, Parity %d\n",
				time_str, checksum, msg_nybbles[12], parity);
			for (i = 0; i < (LACROSSE_WS_BITLEN / 4); i++) {
				fprintf(stderr, "%X", msg_nybbles[i]);
			}
			fprintf(stderr, "\n");
			return 0;
		}
	}

	return 0;
}

static int lacrossews_callback(bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;

	int m;
	int events = 0;
	uint8_t msg_nybbles[(LACROSSE_WS_BITLEN / 4)];
	uint8_t ws_id, msg_type, sensor_id, msg_data, msg_unknown, msg_checksum;
	int msg_value_bcd, msg_value_bcd2, msg_value_bin;
	float temp_c, temp_f, wind_dir, wind_spd, rain_mm, rain_in;
	char time_str[LOCAL_TIME_BUFLEN];

	for (m = 0; m < BITBUF_ROWS; m++) {
		// break out the message nybbles into separate bytes
		if (lacrossews_detect(bb[m], msg_nybbles, bitbuffer->bits_per_row[m])) {

			ws_id = (msg_nybbles[0] << 4) + msg_nybbles[1];
			msg_type = ((msg_nybbles[2] >> 1) & 0x4) + (msg_nybbles[2] & 0x3);
			sensor_id = (msg_nybbles[3] << 4) + msg_nybbles[4];
			msg_data = (msg_nybbles[5] << 1) + (msg_nybbles[6] >> 3);
			msg_unknown = msg_nybbles[6] & 0x01;
			msg_value_bcd = msg_nybbles[7] * 100 + msg_nybbles[8] * 10 + msg_nybbles[9];
			msg_value_bcd2 = msg_nybbles[7] * 10 + msg_nybbles[8];
			msg_value_bin = (msg_nybbles[7] * 256 + msg_nybbles[8] * 16 + msg_nybbles[9]);
			msg_checksum = msg_nybbles[12];

			local_time_str(0, time_str);

			if (debug_output) 
				fprintf(stderr, "%1X%1X%1X%1X%1X%1X%1X%1X%1X%1X%1X%1X%1X   ",
									msg_nybbles[0], msg_nybbles[1], msg_nybbles[2], msg_nybbles[3],
									msg_nybbles[4], msg_nybbles[5], msg_nybbles[6], msg_nybbles[7],
									msg_nybbles[8], msg_nybbles[9], msg_nybbles[10], msg_nybbles[11],
									msg_nybbles[12]);

			switch (msg_type) {
			// Temperature
			case 0:
				temp_c = (msg_value_bcd - 300.0) / 10.0;
				temp_f = temp_c * 1.8 + 32;
				printf("%s LaCrosse WS %02X-%02X: Temperature %3.1f C / %3.1f F\n",
					time_str, ws_id, sensor_id, temp_c, temp_f);
				events++;
				break;
			// Humidity
			case 1:
				if(msg_nybbles[7] == 0xA && msg_nybbles[8] == 0xA)
					printf("%s LaCrosse WS %02X-%02X: Humidity Error\n",
						time_str, ws_id, sensor_id);
				else
					printf("%s LaCrosse WS %02X-%02X: Humidity %2d %%\n",
						time_str, ws_id, sensor_id, msg_value_bcd2);
					events++;
				break;
			// Rain
			case 2:
				rain_mm = 0.5180 * msg_value_bin;
				rain_in = 0.0204 * msg_value_bin;
				printf("%s LaCrosse WS %02X-%02X: Rain %3.2f mm / %3.2f in\n",
					time_str, ws_id, sensor_id, rain_mm, rain_in);
				events++;
				break;
			// Wind
			case 3:
			// Gust
			case 7:
				wind_dir = msg_nybbles[9] * 22.5;
				wind_spd = (msg_nybbles[7] * 16 + msg_nybbles[8])/ 10.0;
				if(msg_nybbles[7] == 0xF && msg_nybbles[8] == 0xE)
					printf("%s LaCrosse WS %02X-%02X: %s Not Connected\n",
						time_str, ws_id, sensor_id, msg_type == 3 ? "Wind":"Gust");
				else {
					printf("%s LaCrosse WS %02X-%02X: %s Dir %3.1f  Speed %3.1f m/s / %3.1f mph\n",
						time_str, ws_id, sensor_id, msg_type == 3 ? "Wind":"Gust", wind_dir, wind_spd, wind_spd * 2.236936292054);
					events++;
				}
				break;
			default:
				fprintf(stderr,
					"%s LaCrosse WS %02X-%02X: Unknown data type %d, bcd %d bin %d\n",
					time_str, ws_id, sensor_id, msg_type, msg_value_bcd, msg_value_bin);
				events++;
			}
		}
	}

	return events;
}

r_device lacrossews = {
 .name           = "LaCrosse WS-2310 Weather Station",
 .modulation     = OOK_PULSE_PWM_RAW,
 .short_limit    = 952,
 .long_limit     = 3000,
 .reset_limit    = 8000,
 .json_callback  = &lacrossews_callback, 
 .disabled       = 0,
 .demod_arg      = 0,
};
