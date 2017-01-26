
#include "rtl_433.h"
#include "util.h"
#include "data.h"

#define LACROSSE_TX_BITLEN	44
#define LACROSSE_NYBBLE_CNT	11

// Check for a valid LaCrosse TX Packet
//
// Return message nybbles broken out into bytes
// for clarity.  The LaCrosse protocol is based
// on 4 bit nybbles.
// 
// Domodulation
// Long bits = 0
// short bits = 1
//
static int lacrossetx_detect(uint8_t *pRow, uint8_t *msg_nybbles, int16_t rowlen) {
	int i;
	uint8_t rbyte_no, rbit_no, mnybble_no, mbit_no;
	uint8_t bit, checksum, parity_bit, parity = 0;

	// Actual Packet should start with 0x0A and be 6 bytes
	// actual message is 44 bit, 11 x 4 bit nybbles.
	if (rowlen == LACROSSE_TX_BITLEN && pRow[0] == 0x0a) {

		for (i = 0; i < LACROSSE_NYBBLE_CNT; i++) {
			msg_nybbles[i] = 0;
		}

		// Move nybbles into a byte array
		// Compute parity and checksum at the same time.
		for (i = 0; i < 44; i++) {
			rbyte_no = i / 8;
			rbit_no = 7 - (i % 8);
			mnybble_no = i / 4;
			mbit_no = 3 - (i % 4);
			bit = (pRow[rbyte_no] & (1 << rbit_no)) ? 1 : 0;
			msg_nybbles[mnybble_no] |= (bit << mbit_no);

			// Check parity on three bytes of data value
			// TX3U might calculate parity on all data including
			// sensor id and redundant integer data
			if (mnybble_no > 4 && mnybble_no < 8) {
				parity += bit;
			}

			//	    fprintf(stdout, "recv: [%d/%d] %d -> msg [%d/%d] %02x, Parity: %d %s\n", rbyte_no, rbit_no,
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

		// fprintf(stdout,"Parity: %d, parity bit %d, Good %d\n", parity, parity_bit, parity % 2);

		if (checksum == msg_nybbles[10] && (parity % 2 == 0)) {
			return 1;
		} else {
			if (debug_output) {
			fprintf(stdout,
				"LaCrosse TX Checksum/Parity error: Comp. %d != Recv. %d, Parity %d\n",
				checksum, msg_nybbles[10], parity);
			}
			return 0;
		}
	} else {
	    if (debug_output) {
		// Debug: This is very noisy
		//fprintf(stderr,"LaCrosse TX Invalid packet: Start %02x, bit count %d\n",
		//	pRow[0], rowlen);
	    }
	}

	return 0;
}

// LaCrosse TX-6u, TX-7u,  Temperature and Humidity Sensors
// Temperature and Humidity are sent in different messages bursts.
static int lacrossetx35_callback(bitbuffer_t *bitbuffer) {
	fprintf(stderr,"!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n LACROSSE TX35 [\nnew_tmplate callback:\n");
    //bitbuffer_print(bitbuffer);
	fprintf(stderr,"!!!! LACROSSE TX35 ]\n");
	return 0;
}

static char *output_fields[] = {
    "time",
    "model",
    "id",
    "temperature_C",
    "humidity",
    NULL
};

r_device lacrosse_tx35 = {
    .name           = "LaCrosse TX35 Temperature / Humidity Sensor",
	.modulation     = FSK_PULSE_PCM,
	//.short_limit    = 55,
	//.long_limit     = 55,
	.short_limit    = 55,
	.long_limit     = 55,
	.reset_limit    = 5000,
	.json_callback  = &lacrossetx35_callback,
	.disabled       = 0,
	.demod_arg      = 0,
	.fields         = output_fields,
};
