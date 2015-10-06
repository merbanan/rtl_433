/* LaCrosse WS-2310 433 Mhz Weather Station
	Packet Format is 53 bits/ 13 nibbles

	nibble
	   0 - 0000
	   1 - 1001
	   2 - Type  ?DTT  ?=0, D=?, TT=Type 00=Temp, 01=Humidity, 10=Rain, 11=Wind
	   3 - ID High
	   4 - ID Low
	   5 - Packet Count - 1
	   6 - Parity 1UUP UU=Next Update, 00=8 seconds, 11=128 seconds, P=Parity of ? bits?
	   7 - Value (Tens)
	   8 - Value (Ones)
	   9 - Value (Tenths)
	  10 - ~Value (Tens)
	  11 - ~Value (Ones)
	  12 - Check Sum = Low nibble of sum of nibbles 0-11
 */

#include "rtl_433.h"
#include "util.h"

#define LACROSSE_WS_BITLEN	52

static int lacrossews_detect(uint8_t *pRow, uint8_t *msg_nybbles, int16_t rowlen) {
	int i;
	uint8_t rbyte_no, rbit_no, mnybble_no, mbit_no;
	uint8_t bit, checksum = 0, parity = 0;
	time_t time_now;
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
			if (mnybble_no > 0 && mnybble_no < 6) {
				parity += bit;
			}
		}

		for (i = 0; i < 12; i++) {
			checksum = (checksum + msg_nybbles[i]) & 0x0F;
		}

		if( msg_nybbles[0] == 0x0 &&
			msg_nybbles[1] == 0x9 &&
			msg_nybbles[7] == (~msg_nybbles[10] & 0xF) &&
			msg_nybbles[8] == (~msg_nybbles[11] & 0xF) &&
			// (parity & 0x1) == 0x0 &&
			checksum == msg_nybbles[12])
			return 1;
		else {
			time(&time_now);
			local_time_str(time_now, time_str);
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
	uint8_t sensor_id, msg_type, msg_len, msg_cnt, msg_parity, msg_checksum;
	int msg_value_int;
	float msg_value, temp_c, temp_f, wind_dir, wind_spd, rain_mm, rain_in;
	time_t time_now;
	char time_str[LOCAL_TIME_BUFLEN];

	for (m = 0; m < BITBUF_ROWS; m++) {
		// break out the message nybbles into separate bytes
		if (lacrossews_detect(bb[m], msg_nybbles, bitbuffer->bits_per_row[m])) {

			msg_len = msg_nybbles[1];
			msg_type = msg_nybbles[2];
			sensor_id = (msg_nybbles[3] << 4) + msg_nybbles[4];
			msg_cnt = msg_nybbles[5];
			msg_parity = msg_nybbles[6] & 0x01;
			msg_value = msg_nybbles[7] * 10 + msg_nybbles[8]
					+ msg_nybbles[9] / 10.0;
			msg_value_int = msg_nybbles[7] * 10 + msg_nybbles[8];
			msg_checksum = msg_nybbles[12];

			time(&time_now);

			local_time_str(time_now, time_str);

#if 0
			fprintf(stderr, "%1X%1X %1X %1X%1X %1X %1X %1X%1X%1X %1X%1X %1X   ",
									msg_nybbles[0], msg_nybbles[1], msg_nybbles[2], msg_nybbles[3],
									msg_nybbles[4], msg_nybbles[5], msg_nybbles[6], msg_nybbles[7],
									msg_nybbles[8], msg_nybbles[9], msg_nybbles[10], msg_nybbles[11],
									msg_nybbles[12]);
#endif

			switch (msg_type & 0xb) {
			// Temperature
			case 0:
				temp_c = msg_value - 30.0;
				temp_f = temp_c * 1.8 + 32;
				printf("%s LaCrosse WS %02X: Temperature %3.1f C / %3.1f F\n",
					time_str, sensor_id, temp_c, temp_f);
				events++;
				break;
			// Humidity
			case 1:
				if(msg_nybbles[7] == 0xA && msg_nybbles[8] == 0xA)
					printf("%s LaCrosse WS %02X: Humidity Error\n",
						time_str, sensor_id);
				else
					printf("%s LaCrosse WS %02X: Humidity %2d %%\n",
						time_str, sensor_id, msg_value_int);
					events++;
				break;
			// Rain
			case 2:
				rain_mm = 0.5180 * (msg_nybbles[7] * 256 + msg_nybbles[8] * 16 + msg_nybbles[9]);
				rain_in = 0.0204 * (msg_nybbles[7] * 256 + msg_nybbles[8] * 16 + msg_nybbles[9]);
				printf("%s LaCrosse WS %02X: Rain %3.2f mm / %3.2f in\n",
					time_str, sensor_id, rain_mm, rain_in);
				events++;
				break;
			// Wind
			case 3:
				wind_dir = msg_nybbles[9] * 22.5;
				wind_spd = (msg_nybbles[7] * 16 + msg_nybbles[8])/ 10.0;
				if(msg_nybbles[7] == 0xF && msg_nybbles[8] == 0xE)
					printf("%s LaCrosse WS %02X: Wind Not Connected\n",
						time_str, sensor_id);
				else {
					printf("%s LaCrosse WS %02X: Wind Dir %3.1f  Speed %3.1f m/s / %3.1f mph\n",
						time_str, sensor_id, wind_dir, wind_spd, wind_spd * 2.236936292054);
					events++;
				}
				break;
			default:
				fprintf(stderr,
					"%s LaCrosse WS %02X: Unknown data type %d, % 3.1f (%d)\n",
					time_str, sensor_id, msg_type, msg_value, msg_value_int);
				events++;
			}
		}
	}

	return events;
}

r_device lacrossews = {
 .name           = "LaCrosse WS-2310 Weather Station",
 .modulation     = OOK_PULSE_PWM_RAW,
 .short_limit    = 238,
 .long_limit     = 750,
 .reset_limit    = 2000,
 .json_callback  = &lacrossews_callback, 
 .disabled       = 0,
 .demod_arg      = 0,
};
