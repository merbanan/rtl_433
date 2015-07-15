/* Shenzhen Calibeur Industries Co. Ltd Wireless Thermometer RF-104 Temperature/Humidity sensor
 * aka Biltema Art. 84-056 (Sold in Denmark)
 * aka ...
 *
 * NB. Only 3 unique sensors can be detected!
 *
 * Update (LED flash) each 2:53
 *
 * Pulse Width Modulation with fixed rate and startbit 
 * Startbit     = 390 samples = 1.56 ms
 * Short pulse  = 190 samples = 0.76 ms = Logic 0
 * Long pulse   = 560 samples = 2.24 ms = Logic 1
 * Pulse rate   = 740 samples = 2.96 ms
 * Burst length = 81000 samples = 324 ms
 *
 * Sequence of 5 times 21 bit separated by start bit (total of 111 pulses)
 * S 21 S 21 S 21 S 21 S 21 S
 * 
 * Channel number is encoded into fractional temperature
 * Temperature is oddly arranged and offset for negative temperatures = <6543210> - 41 C
 * Allways an odd number of 1s (odd parity) 
 *
 * Encoding legend:
 * f = fractional temperature + <ch no> * 10
 * 0-6 = integer temperature + 41C
 * p = parity
 * H = Most significant bits of humidity [5:6]
 * h = Least significant bits of humidity [0:4]
 *
 * LSB                 MSB
 * ffffff45 01236pHH hhhhh Encoding
*/
#include "rtl_433.h"
#include "util.h"

//static int calibeur_rf104_callback(uint8_t bb[BITBUF_ROWS][BITBUF_COLS], int16_t bits_per_row[BITBUF_ROWS]) {
static int calibeur_rf104_callback(bitbuffer_t *bitbuffer) {

	uint8_t ID;
	float temperature;
	float humidity;
	bitrow_t *bb = bitbuffer->bb;

	// Validate package (row [0] is empty due to sync bit)
	if ((bitbuffer->bits_per_row[1] == 21)			// Dont waste time on a long/short package
	 && (crc8(bb[1], 3, 0x80) != 0)		// It should be odd parity
	 && (memcmp(bb[1], bb[2], 3) == 0)	// We want at least two messages in a row
	)
	{
		uint8_t bits;

		bits  = ((bb[1][0] & 0x80) >> 7);	// [0]
		bits |= ((bb[1][0] & 0x40) >> 5);	// [1]
		bits |= ((bb[1][0] & 0x20) >> 3);	// [2]
		bits |= ((bb[1][0] & 0x10) >> 1);	// [3]
		bits |= ((bb[1][0] & 0x08) << 1);	// [4]
		bits |= ((bb[1][0] & 0x04) << 3);	// [5]
		ID = bits / 10;
		temperature = (float)(bits % 10) / 10.0;

		bits  = ((bb[1][0] & 0x02) << 3);	// [4]
		bits |= ((bb[1][0] & 0x01) << 5);	// [5]
		bits |= ((bb[1][1] & 0x80) >> 7);	// [0]
		bits |= ((bb[1][1] & 0x40) >> 5);	// [1]
		bits |= ((bb[1][1] & 0x20) >> 3);	// [2]
		bits |= ((bb[1][1] & 0x10) >> 1);	// [3]
		bits |= ((bb[1][1] & 0x08) << 3);	// [6]
		temperature += (float)bits - 41.0;

		bits  = ((bb[1][1] & 0x02) << 4);	// [5]
		bits |= ((bb[1][1] & 0x01) << 6);	// [6]
		bits |= ((bb[1][2] & 0x80) >> 7);	// [0]
		bits |= ((bb[1][2] & 0x40) >> 5);	// [1]
		bits |= ((bb[1][2] & 0x20) >> 3);	// [2]
		bits |= ((bb[1][2] & 0x10) >> 1);	// [3]
		bits |= ((bb[1][2] & 0x08) << 1);	// [4]
		humidity = bits;

		fprintf(stdout, "Calibeur RF-104:\n");
		fprintf(stdout, "ID          = 0x%02X\n", ID);
		fprintf(stdout, "temperature = %.1f C\n", temperature);
		fprintf(stdout, "humidity    = %2.0f %%\n", humidity);

		return 1;
	}
	return 0;
}


r_device calibeur_RF104 = {
	.name           = "Calibeur RF-104 Sensor",
	.modulation     = OOK_PULSE_PWM_TERNARY,
	.short_limit    = 290,	// Short pulse 190, Startbit 390, Long pulse 560
	.long_limit     = 475,	// Maximum pulse period (long pulse + fixed gap)
	.reset_limit    = 800,	// Longest gap (740-190)
	.json_callback  = &calibeur_rf104_callback,
	.disabled       = 0,
	.demod_arg      = 1		// Startbit is middle bit
};
