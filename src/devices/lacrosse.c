/* LaCrosse TX Temperature and Humidity Sensors
 * Tested: TX-7U and TX-6U (Temperature only)
 *
 * Not Tested but should work: TX-3, TX-4
 *
 * Protocol Documentation: http://www.f6fbb.org/domo/sensors/tx3_th.php
 *
 * Message is 44 bits, 11 x 4 bit nybbles:
 *
 * [00] [cnt = 10] [type] [addr] [addr + parity] [v1] [v2] [v3] [iv1] [iv2] [check]
 *
 * Notes:
 * - Zero Pulses are longer (1,400 uS High, 1,000 uS Low) = 2,400 uS
 * - One Pulses are shorter (  550 uS High, 1,000 uS Low) = 1,600 uS
 * - Sensor id changes when the battery is changed
 * - Values are BCD with one decimal place: vvv = 12.3
 * - Value is repeated integer only iv = 12
 * - Temperature is in Celsius with 50.0 added (to handle negative values)
 * - There is a 4 bit checksum and a parity bit covering the three digit value
 * - Parity check for TX-3 and TX-4 might be different.
 * - Msg sent with one repeat after 30 mS
 * - Temperature and humidity are sent as separate messages
 * - Frequency for each sensor may be could be off by as much as 50-75 khz
 */

#include "rtl_433.h"

// buffer to hold localized timestamp YYYY-MM-DD HH:MM:SS
#define LOCAL_TIME_BUFLEN	32

void local_time_str(time_t time_secs, char *buf) {
	time_t etime;
	struct tm *tm_info;

	if (time_secs == 0) {
		time(&etime);
	} else {
		etime = time_secs;
	}

	tm_info = localtime(&etime);

	strftime(buf, LOCAL_TIME_BUFLEN, "%Y-%m-%d %H:%M:%S", tm_info);
}

// Check for a valid LaCrosse Packet
//
// written for the version of pwm_p_decode() (OOK_PWM_P)
// pulse width detector with two anomalys:
// 1. bits are inverted
// 2. The first bit is discarded as a start bit
//
// If a fixed pulse width decoder is used this
// routine will need to be changed.
static int lacrossetx_detect(uint8_t *pRow, uint8_t *msg_nybbles) {
	int i;
	uint8_t rbyte_no, rbit_no, mnybble_no, mbit_no;
	uint8_t bit, checksum, parity_bit, parity = 0;

	// Actual Packet should start with 0x0A and be 6 bytes
	// actual message is 44 bit, 11 x 4 bit nybbles.
	if ((pRow[0] & 0xFE) == 0x14 && pRow[6] == 0 && pRow[7] == 0) {

		for (i = 0; i < 11; i++) {
			msg_nybbles[i] = 0;
		}

		// Move nybbles into a byte array
		// shifted by one to compensate for loss of first bit.
		for (i = 0; i < 43; i++) {
			rbyte_no = i / 8;
			rbit_no = 7 - (i % 8);
			mnybble_no = (i + 1) / 4;
			mbit_no = 3 - ((i + 1) % 4);
			bit = (pRow[rbyte_no] & (1 << rbit_no)) ? 1 : 0;
			msg_nybbles[mnybble_no] |= (bit << mbit_no);

			// Check parity on three bytes of data value
			// TX3U might calculate parity on all data including
			// sensor id and redundant integer data
			if (mnybble_no > 4 && mnybble_no < 8) {
				parity += bit;
			}

			//	    fprintf(stderr, "recv: [%d/%d] %d -> msg [%d/%d] %02x, Parity: %d %s\n", rbyte_no, rbit_no,
			//		    bit, mnybble_no, mbit_no, msg_nybbles[mnybble_no], parity,
			//		    ( mbit_no == 0 ) ? "\n" : "" );
		}

		parity_bit = msg_nybbles[4] & 0x01;
		parity += parity_bit;

		// Validate Checksum (4 bits in last nybble)
		checksum = 0;
		for (i = 0; i < 10; i++) {
			checksum = (checksum + msg_nybbles[i]) & 0x0F;
		}

		// fprintf(stderr,"Parity: %d, parity bit %d, Good %d\n", parity, parity_bit, parity % 2);

		if (checksum == msg_nybbles[10] && (parity % 2 == 0)) {
			return 1;
		} else {
			fprintf(stderr,
					"LaCrosse Checksum/Parity error: %d != %d, Parity %d\n",
					checksum, msg_nybbles[10], parity);
			return 0;
		}
	}

	return 0;
}

// LaCrosse TX-6u, TX-7u,  Temperature and Humidity Sensors
// Temperature and Humidity are sent in different messages bursts.
static int lacrossetx_callback(uint8_t bb[BITBUF_ROWS][BITBUF_COLS],
		int16_t bits_per_row[BITBUF_ROWS]) {

	int i, m, valid = 0;
	uint8_t *buf;
	uint8_t msg_nybbles[11];
	uint8_t sensor_id, msg_type, msg_len, msg_parity, msg_checksum;
	int msg_value_int;
	float msg_value = 0, temp_c = 0, temp_f = 0;
	time_t time_now;
	char time_str[25];

	static float last_msg_value = 0.0;
	static uint8_t last_sensor_id = 0;
	static uint8_t last_msg_type = 0;
	static time_t last_msg_time = 0;

	for (m = 0; m < BITBUF_ROWS; m++) {
		valid = 0;
		if (lacrossetx_detect(bb[m], msg_nybbles)) {

			msg_len = msg_nybbles[1];
			msg_type = msg_nybbles[2];
			sensor_id = (msg_nybbles[3] << 3) + (msg_nybbles[4] >> 1);
			msg_parity = msg_nybbles[4] & 0x01;
			msg_value = msg_nybbles[5] * 10 + msg_nybbles[6]
					+ msg_nybbles[7] / 10.0;
			msg_value_int = msg_nybbles[8] * 10 + msg_nybbles[9];
			msg_checksum = msg_nybbles[10];

			time(&time_now);

			// suppress duplicates
			if (sensor_id == last_sensor_id && msg_type == last_msg_type
					&& last_msg_value == msg_value
					&& time_now - last_msg_time < 50) {
				continue;
			}

			local_time_str(time_now, time_str);

			switch (msg_type) {
			case 0x00:
				temp_c = msg_value - 50.0;
				temp_f = temp_c * 1.8 + 32;
				printf(
						"%s LaCrosse TX Sensor %02x: Temperature %3.1f C / %3.1f F\n",
						time_str, sensor_id, temp_c, temp_f);
				break;

			case 0x0E:
				printf("%s LaCrosse TX Sensor %02x: Humidity %3.1f%%\n",
						time_str, sensor_id, msg_value);
				break;

			default:
				fprintf(stderr,
						"%s LaCrosse Sensor %02x: Unknown Reading % 3.1f (%d)\n",
						time_str, sensor_id, msg_value, msg_value_int);
			}

			time(&last_msg_time);
			last_msg_value = msg_value;
			last_msg_type = msg_type;
			last_sensor_id = sensor_id;

		} else {
			return 0;
		}
	}

	if (debug_output)
		debug_callback(bb, bits_per_row);
	return 1;
}

r_device lacrossetx = {
/* .id             = */11,
/* .name           = */"LaCrosse TX Temperature / Humidity Sensor",
/* .modulation     = */OOK_PWM_P,
/* .short_limit    = */238,
/* .long_limit     = */750,
/* .reset_limit    = */8000,
/* .json_callback  = */&lacrossetx_callback, };
