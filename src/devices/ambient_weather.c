#include "rtl_433.h"
#include "data.h"
#include "util.h"

static float
get_temperature (uint8_t * msg)
{
  int temp_f = msg[4];
  temp_f <<= 4;
  temp_f |= ((msg[5] & 0xf0) >> 4);
  temp_f -= 400;
  return (temp_f / 10.0f);
}

static uint8_t
get_humidity (uint8_t * msg)
{
  uint8_t humidity = ( (msg[5] & 0x0f) << 4) | ((msg[6] & 0xf0) >> 4);
  return humidity;
}

static uint8_t 
calculate_checksum (uint8_t *buff, int length)
{
  uint8_t mask = 0x7C;
  uint8_t checksum = 0x64;
  uint8_t data;
  int byteCnt;	

  for (byteCnt=0; byteCnt < length; byteCnt++)
  {
    int bitCnt;
    data = buff[byteCnt];

    for (bitCnt = 7; bitCnt >= 0 ; bitCnt--)
    {
      uint8_t bit;

      // Rotate mask right
      bit = mask & 1;
      mask =  (mask >> 1 ) | (mask << 7);
      if ( bit )
      {
	mask ^= 0x18;
      }

      // XOR mask into checksum if data bit is 1	
      if ( data & 0x80 )
      {
	checksum ^= mask;
      }
      data <<= 1; 
    }
  }
  return checksum;
}

static int
validate_checksum (uint8_t * msg, int len)
{
  uint8_t expected = ((msg[6] & 0x0f) << 4) | ((msg[7] & 0xf0) >> 4);
  
  uint8_t pkt[5];
  pkt[0] = ((msg[1] & 0x0f) << 4) | ((msg[2] & 0xf0) >> 4);
  pkt[1] = ((msg[2] & 0x0f) << 4) | ((msg[3] & 0xf0) >> 4);
  pkt[2] = ((msg[3] & 0x0f) << 4) | ((msg[4] & 0xf0) >> 4);
  pkt[3] = ((msg[4] & 0x0f) << 4) | ((msg[5] & 0xf0) >> 4);
  pkt[4] = ((msg[5] & 0x0f) << 4) | ((msg[6] & 0xf0) >> 4);
  uint8_t calculated = calculate_checksum (pkt, 5);

  if (expected == calculated)
    return 0;
  else {
      if (debug_output >= 1) {
          fprintf(stderr, "Checksum error in Ambient Weather message.  Expected: %02x  Calculated: %02x\n", expected, calculated);
          fprintf(stderr, "Message: "); int i; for (i=0; i<len; i++) fprintf(stderr, "%02x ", msg[i]); fprintf(stderr, "\n\n");
      }
    return -1;
  }
}

static uint16_t
get_device_id (uint8_t * msg)
{
  uint16_t deviceID = ( (msg[2] & 0x0f) << 4) | ((msg[3] & 0xf0)  >> 4);
  return deviceID;
}

static uint16_t
get_channel (uint8_t * msg)
{
  uint16_t channel = (msg[3] & 0x07) + 1;
  return channel;
}

static uint8_t
get_battery_status(uint8_t * msg)
{
  uint8_t status = (msg[3] & 8) != 0;
  return status; // if not zero, battery is low

}

static int
ambient_weather_parser (bitbuffer_t *bitbuffer)
{
  bitrow_t *bb = bitbuffer->bb;
  float temperature;
  uint8_t humidity;
  uint16_t channel;
  uint16_t deviceID;
  uint8_t isBatteryLow;

  char time_str[LOCAL_TIME_BUFLEN];
  data_t *data;
  local_time_str(0, time_str);

  if(bitbuffer->bits_per_row[0] != 195)	// There seems to be 195 bits in a correct message
    return 0;

  /* shift all the bits left 1 to align the fields */
  int i;
  for (i = 0; i < BITBUF_COLS-1; i++) {
    uint8_t bits1 = bb[0][i] << 1;
    uint8_t bits2 = (bb[0][i+1] & 0x80) >> 7;
    bits1 |= bits2;
    bb[0][i] = bits1;
  }

  /* DEBUG: print out the received packet */
  /*
  fprintf(stderr, "\n! ");
  for (i = 0 ; i < BITBUF_COLS ; i++) {
    fprintf (stderr, "%02x ", bb[0][i]);
  }
  fprintf (stderr,"\n\n");
  */

  if ( (bb[0][0] == 0x00) && (bb[0][1] == 0x14) && (bb[0][2] & 0x50) ) {

    if (validate_checksum (bb[0], BITBUF_COLS)) {
      return 0;
    }

    temperature = get_temperature (bb[0]);
    humidity = get_humidity (bb[0]);
    channel = get_channel (bb[0]);
    deviceID = get_device_id (bb[0]);
    isBatteryLow = get_battery_status(bb[0]);

    data = data_make("time", "", DATA_STRING, time_str,
			"model",	"",	DATA_STRING,	"Ambient Weather F007TH Thermo-Hygrometer",
		     "device", "House Code", DATA_INT, deviceID,
		     "channel", "Channel", DATA_INT, channel,
		     "battery", "Battery", DATA_STRING, isBatteryLow ? "Low" : "Ok",
		     "temperature_F", "Temperature", DATA_FORMAT, "%.1f", DATA_DOUBLE, temperature,
		     "humidity", "Humidity", DATA_FORMAT, "%u %%", DATA_INT, humidity,
		     NULL);
    data_acquired_handler(data);

    return 1;
  }

  return 0;
}

static int
ambient_weather_callback (bitbuffer_t *bitbuffer)
{
  return ambient_weather_parser (bitbuffer);
}

static char *output_fields[] = {
	"time",
	"device",
	"channel",
	"temperature_F",
	"humidity",
	NULL
};

r_device ambient_weather = {
    .name           = "Ambient Weather Temperature Sensor",
    .modulation     = OOK_PULSE_MANCHESTER_ZEROBIT,
    .short_limit    = 500,
    .long_limit     = 0, // not used
    .reset_limit    = 2400,
    .json_callback  = &ambient_weather_callback,
    .disabled       = 0,
    .demod_arg      = 0,
    .fields	    = output_fields
};
