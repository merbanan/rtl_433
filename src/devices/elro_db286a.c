/* Initial doorbell implementation for Elro DB286A devices
 * 
 * Note that each device seems to have two id patterns, which alternate
 * for every other button press.
 */
 
#include "rtl_433.h"
#include "pulse_demod.h"
#include "data.h"
#include "util.h"

//33 pulses per data pattern
#define	DB286A_PULSECOUNT		33
//One leading zero pulse + 15*33 data pattern - 1 missing trailing zero pulse
#define	DB286A_TOTALPULSES		DB286A_PULSECOUNT*15
//Minimum data pattern repetitions (14 is maximum)
#define	DB286A_MINPATTERN		5

static int doorbell_db286a_callback(bitbuffer_t *bitbuffer) {
	
	char time_str[LOCAL_TIME_BUFLEN];
	data_t *data;
	bitrow_t *bb = bitbuffer->bb;
	uint8_t *b = bb[0];
	unsigned bits = bitbuffer->bits_per_row[0];

	if (bits != DB286A_TOTALPULSES) {
		return 0;
	}
	
	//33 pulses + trailing null byte for C string
	//Example pattern: 001101111111011000101010011011001
	
	char id_string[DB286A_PULSECOUNT + 1];
	char *idsp = id_string;
	
	char bitrow_string[DB286A_TOTALPULSES + 1];
	char *brp = bitrow_string;
	
	const char *brpcount = bitrow_string;
	
	unsigned i;
	unsigned count = 0;

	//Get binary string representation of bitrow
	for (i = 0; i < bits; i++) {
	    brp += sprintf(brp, "%d", bitrow_get_bit(bb[0], i));	
	}
	bitrow_string[DB286A_TOTALPULSES] = '\0';
	
	//Get first id pattern in transmission
	strncpy(idsp, bitrow_string+1, DB286A_PULSECOUNT);
	id_string[DB286A_PULSECOUNT] = '\0';
	
	//Check if pattern is received at least x times
	
	while((brpcount = strstr(brpcount, id_string))) {
	   count++;
	   brpcount++;
	}
	
	if (count < DB286A_MINPATTERN) {
		return 0;
	}
	
	local_time_str(0, time_str);
	
	data = data_make(
				"time",			"",		DATA_STRING, time_str,
				"model",		"",		DATA_STRING, "Elro DB286A",
				"id",			"Code",	DATA_STRING, id_string,
				NULL);
	
	data_acquired_handler(data);
	    		
	return 1;
		
}

static char *output_fields[] = {
    "time",
    "model",
    "id",
    NULL
};


static PWM_Precise_Parameters pwm_precise_parameters_generic = {
	.pulse_tolerance	= 50,
	.pulse_sync_width	= 0,	// No sync bit used
};


r_device elro_db286a = {
	.name			= "Elro DB286A Doorbell",
	.modulation		= OOK_PULSE_PWM_PRECISE,
	.short_limit	= 450,
	.long_limit		= 1500,
	.reset_limit	= 8000,
	.json_callback	= &doorbell_db286a_callback,
	.disabled		= 0,
    .fields         = output_fields,
	.demod_arg		= (uintptr_t)&pwm_precise_parameters_generic
};
