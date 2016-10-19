//for further information please refere to https://forums.adafruit.com/viewtopic.php?f=8&t=25414

#include "rtl_433.h"
#include "util.h"

#define MAV_MESSAGE_LENGTH 104
#define TEMPERATURE_START_POSITION_S1 8
#define TEMPERATURE_START_POSITION_S2 13
#define TEMPERATURE_BIT_COUNT 5

static unsigned int msg_converted[26];
static char msg_hex_combined[26];
static time_t time_now_t, last_time_t;
static int32_t session_id;

// quarternary convertion
int quart(unsigned char param) {
    int retval = 0;

    if (param=='5')
        retval = 0;
    if (param=='6')
        retval = 1;
    if (param=='9')
        retval = 2;
    if (param=='a' || param=='A')
        retval = 3;

    return retval;
}

//here we extract bitbuffer values, for easy data handling
static void convert_bitbuffer(bitbuffer_t *bitbuffer) {
        int i;
        for(i = 0; i < 13; i++) {
            char temp[2];
            sprintf(temp, "%02x", bitbuffer->bb[0][i]);
            msg_hex_combined[i*2] = temp[0];
            msg_hex_combined[i*2+1] = temp[1];
        }

        for(i = 0; i <= 25; i++) {
           msg_converted[i] = quart(msg_hex_combined[i]);
        }

        if(debug_output) {
            fprintf(stderr, "final hex string: %s\n",msg_hex_combined);
            fprintf(stderr, "final converted message: ");
            for(i = 0; i <= 25; i++) fprintf(stderr, "%d",msg_converted[i]);
            fprintf(stderr, "\n");
        }
}

static float get_temperature(unsigned int temp_start_index){
	//default offset
	float temp_c = -532.0;
	int i;
	
	for(i=0; i < TEMPERATURE_BIT_COUNT; i++) {
		temp_c += msg_converted[temp_start_index+i] * (1<<(2*(4-i)));
	}

	return temp_c;
}


//changes when thermometer reset button is pushed or powered on.
static char* get_status() {
    int stat = 0;
	char* retval = "unknown";
	
	//nibble 6 - 7 used for status
    stat += msg_converted[6] * (1<<(2*(1)));
    stat += msg_converted[7] * (1<<(2*(0)));

    if(stat == 2)
		retval = "default";

    if(stat == 7)
		retval = "init";

	if(debug_output)
        fprintf(stderr, "device status: \"%s\" (%d)\n", retval, stat);
	
    return retval;
}


static uint32_t checksum_data() {
    int32_t checksum = 0;
    int i;

    //nibble 6 - 17 used for checksum
    for(i=0; i<=11; i++) {
        checksum |= msg_converted[6+i] << (22 - 2*i);
    }

    if(debug_output)
        fprintf(stderr, "checksum data = %x\n", checksum);

    return checksum;
}


static uint32_t checksum_received() {
    uint32_t checksum = 0;
    int i;
	
    //nibble 18 - 25 checksum info from device
    for(i=0; i<=7; i++) {
         checksum |= msg_converted[18+i] << (14 - 2*i);
    }

    if(msg_hex_combined[24]=='1' || msg_hex_combined[24]=='2') {
        checksum |= (msg_converted[25]&1) << 3;
        checksum |= (msg_converted[25]&2) << 1;

        if(msg_hex_combined[24]=='2')
            checksum |= 0x02;
    }
    else {
        checksum |= msg_converted[24] << 2;
        checksum |= msg_converted[25];
    }

    if(debug_output)
        fprintf(stderr, "checksum received= %x\n", checksum);

    return checksum;
}

static uint16_t shiftreg(uint16_t currentValue) {
	uint8_t msb = (currentValue >> 15) & 1;
	currentValue <<= 1;
	
	// Toggle pattern for feedback bits
	// Toggle, if MSB is 1
	if (msb == 1)
		currentValue ^= 0x1021;
	
	return currentValue;
}

static uint16_t calculate_checksum(uint32_t data) {
	//initial value of linear feedback shift register
	uint16_t mask = 0x3331; 
	uint16_t csum = 0x0;
	int i;
	for(i = 0; i < 24; ++i)	{
		//data bit at current position is "1"
		//do XOR with mask
		if((data >> i) & 0x01)
			csum ^= mask;
		
		mask = shiftreg(mask);
	}
	return csum;
}

static int maverick_et73x_callback(bitbuffer_t *bitbuffer) {
    data_t *data;
    char time_str[LOCAL_TIME_BUFLEN];
	double diff_t = 0.0;
	int8_t b_use_message = 0;
	int32_t chk_xor;
	char* dev_state;

    //we need an inverted bitbuffer
    bitbuffer_invert(bitbuffer);

    if(bitbuffer->num_rows != 1)
        return 0;

    //check correct data length
    if(bitbuffer->bits_per_row[0] != MAV_MESSAGE_LENGTH)
        return 0;

    //check for correct header (0xAA9995)
    if((bitbuffer->bb[0][0] != 0xAA || bitbuffer->bb[0][0] != 0xaa ) || bitbuffer->bb[0][1] != 0x99 || bitbuffer->bb[0][2] != 0x95)
        return 0;
	
	//convert hex values into quardinary values
    convert_bitbuffer(bitbuffer);
	
	//checksum is used to represent a session. This means, we get a new session_id if a reset or battery exchange is done. 
	chk_xor = (calculate_checksum(checksum_data()) & 0xffff) ^ checksum_received();
	
	
	dev_state = get_status();
	
	//if the transmitter is in init state, we take the session_id for further checks. 
	if(strncmp(dev_state, "init", 4) == 0)
	{
		//assuming that having two times the same checksum, means there was no error in sending first message
		if(session_id != chk_xor)
			b_use_message = 1;
	}

	//second message
	if(session_id > 0) {
		time(&time_now_t);
		diff_t = difftime(time_now_t, last_time_t);

		//checksum error
		if(session_id != chk_xor && b_use_message == 0 && diff_t > 8)
			return 0;
	}
	

	//one message (of four) is enough for output
	if (diff_t < 8 && session_id > 0 && b_use_message == 0)
		return 0;
	
	session_id = chk_xor;
	time(&last_time_t);

    local_time_str(0, time_str);

    if(debug_output)
        fprintf(stderr, "checksum xor: %x\n", chk_xor);

    data = data_make("time",           "",                      DATA_STRING,                         time_str,
                     "id",             "Session_ID",            DATA_INT,                            chk_xor,
                     "temperature_C1", "TemperatureSensor1",    DATA_FORMAT, "%.02f C", DATA_DOUBLE, get_temperature(TEMPERATURE_START_POSITION_S1),
                     "temperature_C2", "TemperatureSensor2",    DATA_FORMAT, "%.02f C", DATA_DOUBLE, get_temperature(TEMPERATURE_START_POSITION_S2),
                     "status",         "Status",                DATA_STRING,                         dev_state,
                     NULL);
    data_acquired_handler(data);

    return bitbuffer->num_rows;
}

static char *output_fields[] = {
    "time",
    "session_id"
    "temperature_C1",
    "temperature_C2",
    "status",
    NULL
};


r_device maverick_et73x = {
    .name           = "Maverick ET-732/733 BBQ Sensor",
    .modulation     = OOK_PULSE_MANCHESTER_ZEROBIT,
    .short_limit    = 250,
    .long_limit     = 500,
    .reset_limit    = 4000,
    .json_callback  = &maverick_et73x_callback,
    .disabled       = 0,
    .demod_arg     = 0,
    .fields         = output_fields
};
