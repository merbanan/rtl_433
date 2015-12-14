/*
*RST temperature / humidity sensors *
*Copyright (c) 2015, Valery Rezvyakov, Kostroma, RUSSIA (ua3nbw)
* 
*temperature / humidity sensors  RST 025100, RST 02500
*
*Message Format can be found at: http://members.upc.nl/m.beukelaar/Crestaprotocol.pdf 
*/

#include "rtl_433.h"
#include "util.h"

#define RST_BITLEN		91
#define RST_BROWLEN		9
static char time_str[LOCAL_TIME_BUFLEN];



static int rst_weather_callback(bitbuffer_t *bitbuffer) {

  /* bitbuf->bits_per_row[brow]  bits 91. */
	if (bitbuffer->bits_per_row[0] != RST_BITLEN) { 
         return 0; 
     } 

uint8_t *br = bitbuffer->bb[0];
time_t time_now;
uint8_t csum = 0;
 /* OOK_PULSE_MANCHESTER_ZEROBIT as it is manchester with a startbit 0.. */
  int j,i=0;
  for (j = 0; j < RST_BROWLEN+2; j++) {
   /* shift all the bits left 1 to align the fields */   
  for (i = j; i < RST_BROWLEN+1; i++) {
    uint8_t bits1 = br[i] << 1;
    uint8_t bits2 = (br[i+1] & 0x80) >> 7;
    bits1 |= bits2;
    br[i] = bits1;
  }
}
 
            for (i = 1; i < RST_BROWLEN; i++) {
            /*  all bytes received, make sure checksum is okay */            	
              csum ^= br[i];
  	        /* Reverse all bits in a byte  */   
              br[i] = reverse8( br[i] );
            /* Decrypt raw received data byte */
              br[i] ^= br[i] << 1;
                }

 // fprintf (stdout, " checksum  =  %02x ", csum); 

if(csum !=0){
				return 0;
		}
                                 
  
//fprintf(stderr, "\n!!! ");
//bitbuffer_print(bitbuffer);

    time(&time_now);
    local_time_str(time_now, time_str);
  

 uint8_t channel = br[1] >> 5;
 uint8_t humidity = 10 * (br[6] >> 4) + (br[6] & 0x0f);
 float temperature = 100 * (br[5] & 0x0f) + 10 * (br[4] >> 4) + (br[4] & 0x0f);
	// temp is negative?
	if (!(br[5] & 0x80)) {
		temperature = -temperature;
	}


fprintf(stdout, "%s RST 02510 sensor\n", time_str);
fprintf(stdout, "Channel        = %d\n", channel);
fprintf(stdout, "Temperature    = %.1f C\n", temperature/10);
fprintf(stdout, "Humidity       = %d%%\n", humidity);



	return 0;
}

r_device rst = {

  .name           = "RST Temperature Sensor",
  .modulation     = OOK_PULSE_MANCHESTER_ZEROBIT,
  .short_limit    = 123,
  .long_limit     = 0,
  .reset_limit    = 375,
  .json_callback = &rst_weather_callback,
  .disabled      = 0,
  .demod_arg     = 0,
};
