#include "rtl_433.h"

float get_os_temperature(unsigned char *message, unsigned int sensor_id) {
  // sensor ID included  to support sensors with temp in different position
  float temp_c = 0;
  temp_c = (((message[5]>>4)*100)+((message[4]&0x0f)*10) + ((message[4]>>4)&0x0f)) /10.0F;
  if (message[5] & 0x0f)
       temp_c = -temp_c;
  return temp_c;
}
unsigned int get_os_humidity(unsigned char *message, unsigned int sensor_id) {
 // sensor ID included to support sensors with temp in different position
 int humidity = 0;
    humidity = ((message[6]&0x0f)*10)+(message[6]>>4);
 return humidity;
}
unsigned int get_os_uv(unsigned char *message, unsigned int sensor_id) {
 // sensor ID included to support sensors with temp in different position
 int uvidx = 0;
    uvidx = ((message[4]&0x0f)*10)+(message[4]>>4);
 return uvidx;
}

unsigned int get_os_rollingcode(unsigned char *message, unsigned int sensor_id){
   int rc = 0;
   rc = (message[2]&0x0F) + (message[3]&0xF0);
   return rc;
}

unsigned short int power(const unsigned char* d){
  unsigned short int val = 0;
  val = ( d[4] << 8) + d[3];
  val = val & 0xFFF0;
  val = val * 1.00188;
  return val;
}

unsigned long long total(const unsigned char* d){
  unsigned long long val = 0;
  if ( (d[1]&0x0F) == 0 ){
    // Sensor returns total only if nibble#4 == 0
    val =  (unsigned long long)d[10]<< 40;
    val += (unsigned long long)d[9] << 32;   
    val += (unsigned long)d[8]<<24;
    val += (unsigned long)d[7] << 16;
    val += d[6] << 8;
    val += d[5];
  }
  return val ;
}

static int validate_os_checksum(unsigned char *msg, int checksum_nibble_idx) {
  // Oregon Scientific v2.1 and v3 checksum is a  1 byte  'sum of nibbles' checksum.
  // with the 2 nibbles of the checksum byte  swapped.
  int i;
  unsigned int checksum, sum_of_nibbles=0;
  for (i=0; i<(checksum_nibble_idx-1);i+=2) {
    unsigned char val=msg[i>>1];
	sum_of_nibbles += ((val>>4) + (val &0x0f));
  }
  if (checksum_nibble_idx & 1) {
     sum_of_nibbles += (msg[checksum_nibble_idx>>1]>>4);
     checksum = (msg[checksum_nibble_idx>>1] & 0x0f) | (msg[(checksum_nibble_idx+1)>>1]&0xf0);
  } else
     checksum = (msg[checksum_nibble_idx>>1]>>4) | ((msg[checksum_nibble_idx>>1]&0x0f)<<4);
  sum_of_nibbles &= 0xff;

  if (sum_of_nibbles == checksum)
    return 0;
  else {
    fprintf(stdout, "Checksum error in Oregon Scientific message.  Expected: %02x  Calculated: %02x\n", checksum, sum_of_nibbles);
	fprintf(stdout, "Message: "); int i; for (i=0 ;i<((checksum_nibble_idx+4)>>1) ; i++) fprintf(stdout, "%02x ", msg[i]); fprintf(stdout, "\n\n");
	return 1;
  }
}

static int validate_os_v2_message(unsigned char * msg, int bits_expected, int valid_v2_bits_received,
                                int nibbles_in_checksum) {
  // Oregon scientific v2.1 protocol sends each bit using the complement of the bit, then the bit  for better error checking.  Compare number of valid bits processed vs number expected
  if (bits_expected == valid_v2_bits_received) {
    return (validate_os_checksum(msg, nibbles_in_checksum));
  } else {
    fprintf(stdout, "Bit validation error on Oregon Scientific message.  Expected %d bits, received error after bit %d \n",        bits_expected, valid_v2_bits_received);
    fprintf(stdout, "Message: "); int i; for (i=0 ;i<(bits_expected+7)/8 ; i++) fprintf(stdout, "%02x ", msg[i]); fprintf(stdout, "\n\n");
  }
  return 1;
}

static int oregon_scientific_v2_1_parser(bitbuffer_t *bitbuffer) {
    bitrow_t *bb = bitbuffer->bb;
   // Check 2nd and 3rd bytes of stream for possible Oregon Scientific v2.1 sensor data (skip first byte to get past sync/startup bit errors)
   if ( ((bb[0][1] == 0x55) && (bb[0][2] == 0x55)) ||
	    ((bb[0][1] == 0xAA) && (bb[0][2] == 0xAA))) {
	  int i,j;
	  unsigned char msg[BITBUF_COLS] = {0};

	  // Possible  v2.1 Protocol message
	  int num_valid_v2_bits = 0;

	  unsigned int sync_test_val = (bb[0][3]<<24) | (bb[0][4]<<16) | (bb[0][5]<<8) | (bb[0][6]);
	  int dest_bit = 0;
	  int pattern_index;
	  // Could be extra/dropped bits in stream.  Look for sync byte at expected position +/- some bits in either direction
      for(pattern_index=0; pattern_index<8; pattern_index++) {
        unsigned int mask     = (unsigned int) (0xffff0000>>pattern_index);
		unsigned int pattern  = (unsigned int)(0x55990000>>pattern_index);
        unsigned int pattern2 = (unsigned int)(0xaa990000>>pattern_index);

	//fprintf(stdout, "OS v2.1 sync byte search - test_val=%08x pattern=%08x  mask=%08x\n", sync_test_val, pattern, mask);

	    if (((sync_test_val & mask) == pattern) ||
		    ((sync_test_val & mask) == pattern2)) {
		  //  Found sync byte - start working on decoding the stream data.
		  // pattern_index indicates  where sync nibble starts, so now we can find the start of the payload
	      int start_byte = 5 + (pattern_index>>3);
	      int start_bit = pattern_index & 0x07;
	//fprintf(stdout, "OS v2.1 Sync test val %08x found, starting decode at byte index %d bit %d\n", sync_test_val, start_byte, start_bit);
	      int bits_processed = 0;
		  unsigned char last_bit_val = 0;
		  j=start_bit;
	      for (i=start_byte;i<BITBUF_COLS;i++) {
	        while (j<8) {
			   if (bits_processed & 0x01) {
			     unsigned char bit_val = ((bb[0][i] & (0x80 >> j)) >> (7-j));

				 // check if last bit received was the complement of the current bit
				 if ((num_valid_v2_bits == 0) && (last_bit_val == bit_val))
				   num_valid_v2_bits = bits_processed; // record position of first bit in stream that doesn't verify correctly
				 last_bit_val = bit_val;

			     // copy every other bit from source stream to dest packet
				 msg[dest_bit>>3] |= (((bb[0][i] & (0x80 >> j)) >> (7-j)) << (7-(dest_bit & 0x07)));

	//fprintf(stdout,"i=%d j=%d dest_bit=%02x bb=%02x msg=%02x\n",i, j, dest_bit, bb[0][i], msg[dest_bit>>3]);
				 if ((dest_bit & 0x07) == 0x07) {
				    // after assembling each dest byte, flip bits in each nibble to convert from lsb to msb bit ordering
				    int k = (dest_bit>>3);
                    unsigned char indata = msg[k];
	                // flip the 4 bits in the upper and lower nibbles
	                msg[k] = ((indata & 0x11) << 3) | ((indata & 0x22) << 1) |
	   	                     ((indata & 0x44) >> 1) | ((indata & 0x88) >> 3);
		            }
				 dest_bit++;
			     }
				 else
				   last_bit_val = ((bb[0][i] & (0x80 >> j)) >> (7-j)); // used for v2.1 bit error detection
			   bits_processed++;
			   j++;
	        }
		    j=0;
		  }
		  break;
	    } //if (sync_test_val...
      } // for (pattern...


    int sensor_id = (msg[0] << 8) | msg[1];
	if ((sensor_id == 0x1d20) || (sensor_id == 0x1d30))	{
	   if (validate_os_v2_message(msg, 153, num_valid_v2_bits, 15) == 0) {
         int  channel = ((msg[2] >> 4)&0x0f);
	     if (channel == 4)
	       channel = 3; // sensor 3 channel number is 0x04
         float temp_c = get_os_temperature(msg, sensor_id);
         unsigned int rc = get_os_rollingcode(msg, sensor_id);
		 if (sensor_id == 0x1d20) {
            fprintf(stdout, "Weather Sensor THGR122N RC %x Channel %d ", rc, channel);
            //fprintf(stdout, "Message: "); for (i=0 ; i<20 ; i++) fprintf(stdout, "%02x ", msg[i]); 
         }
		 else fprintf(stdout, "Weather Sensor THGR968  Outdoor   ");
		 fprintf(stdout, "Temp: %3.1fC  %3.1fF   Humidity: %d%%\n", temp_c, ((temp_c*9)/5)+32,get_os_humidity(msg, sensor_id));
	   }
	   return 1;
    } else if (sensor_id == 0x5d60) {
	   if (validate_os_v2_message(msg, 185, num_valid_v2_bits, 19) == 0) {
	     unsigned int comfort = msg[7] >>4;
	     char *comfort_str="Normal";
	     if      (comfort == 4)   comfort_str = "Comfortable";
	     else if (comfort == 8)   comfort_str = "Dry";
	     else if (comfort == 0xc) comfort_str = "Humid";
	     unsigned int forecast = msg[9]>>4;
	     char *forecast_str="Cloudy";
	     if      (forecast == 3)   forecast_str = "Rainy";
	     else if (forecast == 6)   forecast_str = "Partly Cloudy";
	     else if (forecast == 0xc) forecast_str = "Sunny";
         float temp_c = get_os_temperature(msg, 0x5d60);
	     fprintf(stdout,"Weather Sensor BHTR968  Indoor    Temp: %3.1fC  %3.1fF   Humidity: %d%%", temp_c, ((temp_c*9)/5)+32, get_os_humidity(msg, 0x5d60));
	     fprintf(stdout, " (%s) Pressure: %dmbar (%s)\n", comfort_str, ((msg[7] & 0x0f) | (msg[8] & 0xf0))+856, forecast_str);
	   }
	   return 1;
	} else if (sensor_id == 0x2d10) {
	   if (validate_os_v2_message(msg, 161, num_valid_v2_bits, 16) == 0) {
	   float rain_rate = (((msg[4] &0x0f)*100)+((msg[4]>>4)*10) + ((msg[5]>>4)&0x0f)) /10.0F;
       float total_rain = (((msg[7]&0xf)*10000)+((msg[7]>>4)*1000) + ((msg[6]&0xf)*100)+((msg[6]>>4)*10) + (msg[5]&0xf))/10.0F;
	   fprintf(stdout, "Weather Sensor RGR968   Rain Gauge  Rain Rate: %2.0fmm/hr Total Rain %3.0fmm\n", rain_rate, total_rain);
	   }
	   return 1;
	} else if (sensor_id == 0xec40 && num_valid_v2_bits==153) {
		if (validate_os_v2_message(msg, 153, num_valid_v2_bits, 12) == 0) {
			int channel = ((msg[2] >> 4)&0x0f);
			if (channel == 4)
				channel = 3; // sensor 3 channel number is 0x04
			float temp_c = get_os_temperature(msg, sensor_id);
			if (sensor_id == 0xec40) fprintf(stdout, "Thermo Sensor THR228N Channel %d ", channel);
			fprintf(stdout, "Temp: %3.1fC  %3.1fF\n", temp_c, ((temp_c*9)/5)+32);
		}
		return 1;
	} else if (sensor_id == 0xec40 && num_valid_v2_bits==129) {
		if (validate_os_v2_message(msg, 129, num_valid_v2_bits, 12) == 0) {
			int channel = ((msg[2] >> 4)&0x0f);
			if (channel == 4)
				channel = 3; // sensor 3 channel number is 0x04
			int battery_low = (msg[3] >> 2 & 0x01);
			unsigned char rolling_code = ((msg[2] << 4)&0xF0) | ((msg[3] >> 4)&0x0F);
			float temp_c = get_os_temperature(msg, sensor_id);
			if (sensor_id == 0xec40) fprintf(stdout, "Thermo Sensor THN132N, Channel %d, Battery: %s, Rolling-code 0x%0X, ", channel, battery_low?"Low":"Ok", rolling_code);
			fprintf(stdout, "Temp: %3.1fC  %3.1fF\n", temp_c, ((temp_c*9)/5)+32);
		}
		return 1;
	} else if ((sensor_id >= 0x0cc3) && (sensor_id <= 0xfcc3)) {
		if (validate_os_v2_message(msg, 153, num_valid_v2_bits, 15) == 0) {
			int channel = ((msg[2] >> 4)&0x0f);
			int battery_low = (msg[3] >> 2 & 0x01);
			unsigned char rolling_code = ((msg[2] << 4)&0xF0) | ((msg[3] >> 4)&0x0F);
			float temp_c = get_os_temperature(msg, sensor_id);
			fprintf(stdout, "Thermo Hygro RF Clock Sensor RTGN318, Channel %d, Battery: %s, Rolling-code 0x%0X, ", channel, battery_low?"Low":"Ok", rolling_code);
			fprintf(stdout, "Temp: %3.1fC  %3.1fF   Humidity: %d%%\n", temp_c, ((temp_c*9)/5)+32, get_os_humidity(msg, sensor_id));
		}
		return 1;
	} else if (num_valid_v2_bits > 16) {
		fprintf(stdout, "%d bit message received from unrecognized Oregon Scientific v2.1 sensor with device ID %x.\n", num_valid_v2_bits, sensor_id);
		fprintf(stdout, "Message: "); for (i=0 ; i<20 ; i++) fprintf(stdout, "%02x ", msg[i]); fprintf(stdout,"\n\n");
    } else {
//fprintf(stdout, "\nPossible Oregon Scientific v2.1 message, but sync nibble wasn't found\n"); fprintf(stdout, "Raw Data: "); for (i=0 ; i<BITBUF_COLS ; i++) fprintf(stdout, "%02x ", bb[0][i]); fprintf(stdout,"\n\n");
    }
   } else {
//if (bb[0][3] != 0) int i; fprintf(stdout, "\nBadly formatted OS v2.1 message encountered."); for (i=0 ; i<BITBUF_COLS ; i++) fprintf(stdout, "%02x ", bb[0][i]); fprintf(stdout,"\n\n");}
   }
   return 0;
}

static int oregon_scientific_v3_parser(bitbuffer_t *bitbuffer) {
    bitrow_t *bb = bitbuffer->bb;

   // Check stream for possible Oregon Scientific v3 protocol data (skip part of first and last bytes to get past sync/startup bit errors)
    if ((((bb[0][0]&0xf) == 0x0f) && (bb[0][1] == 0xff) && ((bb[0][2]&0xc0) == 0xc0)) ||
       (((bb[0][0]&0xf) == 0x00) && (bb[0][1] == 0x00) && ((bb[0][2]&0xc0) == 0x00))) {
        int i,j;
        unsigned char msg[BITBUF_COLS] = {0};
        unsigned int sync_test_val = (bb[0][2]<<24) | (bb[0][3]<<16) | (bb[0][4]<<8);
        int dest_bit = 0;
        int pattern_index;
        // Could be extra/dropped bits in stream.  Look for sync byte at expected position +/- some bits in either direction
        for(pattern_index=0; pattern_index<16; pattern_index++) {
            unsigned int     mask = (unsigned int)(0xfff00000>>pattern_index);
            unsigned int  pattern = (unsigned int)(0xffa00000>>pattern_index);
            unsigned int pattern2 = (unsigned int)(0xff500000>>pattern_index);
            unsigned int pattern3 = (unsigned int)(0x00500000>>pattern_index);
            unsigned int pattern4 = (unsigned int)(0x04600000>>pattern_index);
            //fprintf(stdout, "OS v3 Sync nibble search - test_val=%08x pattern=%08x  mask=%08x\n", sync_test_val, pattern, mask);
            if (((sync_test_val & mask) == pattern)  ||
                ((sync_test_val & mask) == pattern2) ||
                ((sync_test_val & mask) == pattern3) ||
                ((sync_test_val & mask) == pattern4)) {
                // Found sync byte - start working on decoding the stream data.
                // pattern_index indicates  where sync nibble starts, so now we can find the start of the payload
                int start_byte = 3 + (pattern_index>>3);
                int start_bit = (pattern_index+4) & 0x07;
                //fprintf(stdout, "Oregon Scientific v3 Sync test val %08x ok, starting decode at byte index %d bit %d\n", sync_test_val, start_byte, start_bit);
                j = start_bit;
                for (i=start_byte;i<BITBUF_COLS;i++) {
                    while (j<8) {
                        unsigned char bit_val = ((bb[0][i] & (0x80 >> j)) >> (7-j));

                        // copy every  bit from source stream to dest packet
                        msg[dest_bit>>3] |= (((bb[0][i] & (0x80 >> j)) >> (7-j)) << (7-(dest_bit & 0x07)));

            //fprintf(stdout,"i=%d j=%d dest_bit=%02x bb=%02x msg=%02x\n",i, j, dest_bit, bb[0][i], msg[dest_bit>>3]);
                        if ((dest_bit & 0x07) == 0x07) {
                            // after assembling each dest byte, flip bits in each nibble to convert from lsb to msb bit ordering
                            int k = (dest_bit>>3);
                            unsigned char indata = msg[k];
                            // flip the 4 bits in the upper and lower nibbles
                            msg[k] = ((indata & 0x11) << 3) | ((indata & 0x22) << 1) |
                                    ((indata & 0x44) >> 1) | ((indata & 0x88) >> 3);
                        }
                        dest_bit++;
                        j++;
                    }
                    j=0;
                }
                break;
            }
        }
    
	if ((msg[0] == 0xf8) && (msg[1] == 0x24))	{
	   if (validate_os_checksum(msg, 15) == 0) {
	     int  channel = ((msg[2] >> 4)&0x0f);
	     float temp_c = get_os_temperature(msg, 0xf824);
		 int humidity = get_os_humidity(msg, 0xf824);
		 fprintf(stdout,"Weather Sensor THGR810  Channel %d Temp: %3.1fC  %3.1fF   Humidity: %d%%\n", channel, temp_c, ((temp_c*9)/5)+32, humidity);
	   }
	   return 1;                  //msg[k] = ((msg[k] & 0x0F) << 4) + ((msg[k] & 0xF0) >> 4);
	} else if ((msg[0] == 0xd8) && (msg[1] == 0x74)) {
	   if (validate_os_checksum(msg, 13) == 0) {   // ok
	     int  channel = ((msg[2] >> 4)&0x0f);
	     int uvidx = get_os_uv(msg, 0xd874);
	     fprintf(stdout, "Weather Sensor UVN800 Channel %d  UV index: %d \n", channel, uvidx);
	   }
	} else if ((msg[0] == 0x19) && (msg[1] == 0x84)) {
	   if (validate_os_checksum(msg, 17) == 0) {
	     float gustWindspeed = (msg[11]+msg[10])/100;
             float quadrant = msg[8]*22.5;
	   fprintf(stdout, "Weather Sensor WGR800   Wind Gauge  Gust Wind Speed : %2.0f m/s Wind direction %3.0f dgrs\n", gustWindspeed, quadrant);
	   }
	   return 1;
    } else if ((msg[0] == 0x20) || (msg[0] == 0x21) || (msg[0] == 0x22)
              || (msg[0] == 0x23) || (msg[0] == 0x24)) { //  Owl CM160 Readings
        msg[0]=msg[0] & 0x0f;
        if (validate_os_checksum(msg, 22) == 0) {
          float rawAmp = (msg[4] >> 4 << 8 | (msg[3] & 0x0f )<< 4 | msg[3] >> 4);
          fprintf(stdout, "current measurement reading value   = %.0f\n", rawAmp);
          fprintf(stdout, "current watts (230v)   = %.0f\n", rawAmp /(0.27*230)*1000);
        }
    } else if (msg[0] == 0x26) { //  Owl CM180 readings
        int k;
        for (k=0; k<BITBUF_COLS;k++) {  // Reverse nibbles
            msg[k] = (msg[k] & 0xF0) >> 4 |  (msg[k] & 0x0F) << 4;
        }
        unsigned short int ipower = power(msg);
        unsigned long long itotal = total(msg); 
        float total_energy = itotal/3600/1000.0;
        if (itotal) 
            fprintf(stdout,"Energy Sensor CM180 Id %x%x power: %dW, total: %lluW, Total Energy: %.3fkWh\n", msg[0], msg[1], ipower, itotal, total_energy);
        else
            fprintf(stdout,"Energy Sensor CM180 Id %x%x power: %dW\n", msg[0], msg[1], ipower);  
   
    } else if ((msg[0] != 0) && (msg[1]!= 0)) { //  sync nibble was found  and some data is present...
        fprintf(stdout, "Message received from unrecognized Oregon Scientific v3 sensor.\n");
        fprintf(stdout, "Message: "); for (i=0 ; i<BITBUF_COLS ; i++) fprintf(stdout, "%02x ", msg[i]); fprintf(stdout, "\n");
        fprintf(stdout, "    Raw: "); for (i=0 ; i<BITBUF_COLS ; i++) fprintf(stdout, "%02x ", bb[0][i]); fprintf(stdout,"\n\n");    
    } else if (bb[0][3] != 0 ) {
        //fprintf(stdout, "\nPossible Oregon Scientific v3 message, but sync nibble wasn't found\n"); 
        //fprintf(stdout, "Raw Data: "); for (i=0 ; i<BITBUF_COLS ; i++) fprintf(stdout, "%02x ", bb[0][i]); fprintf(stdout,"\n\n");
    }
   }
   else { // Based on first couple of bytes, either corrupt message or something other than an Oregon Scientific v3 message
//if (bb[0][3] != 0) { fprintf(stdout, "\nUnrecognized Msg in v3: "); int i; for (i=0 ; i<BITBUF_COLS ; i++) fprintf(stdout, "%02x ", bb[0][i]); fprintf(stdout,"\n\n"); }
   }
   return 0;
}

static int oregon_scientific_callback(bitbuffer_t *bitbuffer) {
 int ret = oregon_scientific_v2_1_parser(bitbuffer);
 if (ret == 0)
   ret = oregon_scientific_v3_parser(bitbuffer);
 return ret;
}

r_device oregon_scientific = {
    .name           = "Oregon Scientific Weather Sensor",
    .modulation     = OOK_PULSE_MANCHESTER_ZEROBIT,
    .short_limit    = 110, // Nominal 1024Hz (122), but pulses are shorter than pauses 
    .long_limit     = 0, // not used
    .reset_limit    = 600,
    .json_callback  = &oregon_scientific_callback,
    .disabled       = 0,
    .demod_arg      = 0,
};
