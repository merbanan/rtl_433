/* OSv1 protocol
 *
 * MC with nominal bit width of 2930 us.
 * Pulses are somewhat longer than nominal half-bit width, 1748 us / 3216 us,
 * Gaps are somewhat shorter than nominal half-bit width, 1176 us / 2640 us.
 * After 12 preamble bits there is 4200 us gap, 5780 us pulse, 5200 us gap.
 *
 * Care must be taken with the gap after the sync pulse since it
 * is outside of the normal clocking.  Because of this a data stream
 * beginning with a 0 will have data in this gap.
 */

#include "decoder.h"

#define	OSV1_BITS	32

static int oregon_scientific_v1_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
	int ret = 0;
	int row;
	int cs;
	int i;
	int nibble[OSV1_BITS/4];
	int sid, channel, uk1;
	float tempC;
	int battery, uk2, sign, uk3, checksum;
	data_t *data;

	for (row = 0; row < bitbuffer->num_rows; row++) {
		if (bitbuffer->bits_per_row[row] != OSV1_BITS)
			continue;

		cs = 0;
		for (i = 0; i < OSV1_BITS / 8; i++) {
			uint8_t byte = reverse8(bitbuffer->bb[row][i]);
			nibble[i * 2    ] = byte & 0x0f;
			nibble[i * 2 + 1] = byte >> 4;
			if (i < ((OSV1_BITS / 8) - 1))
				cs += nibble[i * 2] + 16 * nibble[i * 2 + 1];
		}

		cs = (cs & 0xFF) + (cs >> 8);
		checksum = nibble[6] + (nibble[7] << 4);
		if (checksum != cs)
			continue;

		sid      = nibble[0];
		channel  = ((nibble[1] >> 2) & 0x03) + 1;
		uk1      = (nibble[1] >> 0) & 0x03;	/* unknown.  Seen change every 60 minutes */
		tempC    =  nibble[2] * 0.1 + nibble[3] + nibble[4] * 10.;
		battery  = (nibble[5] >> 3) & 0x01;
		uk2      = (nibble[5] >> 2) & 0x01;	/* unknown.  Always zero? */
		sign     = (nibble[5] >> 1) & 0x01;
		uk3      = (nibble[5] >> 0) & 0x01;	/* unknown.  Always zero? */

		if (sign)
			tempC = -tempC;

		data = data_make(
				"brand",		"",				DATA_STRING,	"OS",
				"model",		"",				DATA_STRING,	_X("Oregon-v1","OSv1 Temperature Sensor"),
				_X("id","sid"),			"SID",			DATA_INT,		sid,
				"channel",		"Channel",		DATA_INT,		channel,
				"battery",		"Battery",		DATA_STRING,	battery ? "LOW" : "OK",
				"temperature_C","Temperature",	DATA_FORMAT,	"%.01f C",				DATA_DOUBLE,	tempC,
				NULL);
		decoder_output_data(decoder, data);
		ret++;
	}
	return ret;
}

static char *output_fields[] = {
	"brand",
	"model",
	"sid", // TODO: delete this
	"id",
	"channel",
	"battery",
	"temperature_C",
	NULL
};

r_device oregon_scientific_v1 = {
	.name           = "OSv1 Temperature Sensor",
	.modulation     = OOK_PULSE_PWM_OSV1,
	.short_width    = 1465, // nominal half-bit width
	.sync_width     = 5780,
	.gap_limit      = 3500,
	.reset_limit    = 14000,
	.decode_fn      = &oregon_scientific_v1_callback,
	.disabled       = 0,
	.fields         = output_fields
};
