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
    fprintf(stderr, "Checksum error in Oregon Scientific message.  Expected: %02x  Calculated: %02x\n", checksum, sum_of_nibbles);
	fprintf(stderr, "Message: "); int i; for (i=0 ;i<((checksum_nibble_idx+4)>>1) ; i++) fprintf(stderr, "%02x ", msg[i]); fprintf(stderr, "\n\n");
	return 1;
  }
}

static int validate_os_v2_message(unsigned char * msg, int bits_expected, int valid_v2_bits_received,
                                int nibbles_in_checksum) {
  // Oregon scientific v2.1 protocol sends each bit using the complement of the bit, then the bit  for better error checking.  Compare number of valid bits processed vs number expected
  if (bits_expected == valid_v2_bits_received) {
    return (validate_os_checksum(msg, nibbles_in_checksum));
  } else {
    fprintf(stderr, "Bit validation error on Oregon Scientific message.  Expected %d bits, received error after bit %d \n",        bits_expected, valid_v2_bits_received);
    fprintf(stderr, "Message: "); int i; for (i=0 ;i<(bits_expected+7)/8 ; i++) fprintf(stderr, "%02x ", msg[i]); fprintf(stderr, "\n\n");
  }
  return 1;
}

static int oregon_scientific_v2_1_parser(uint8_t bb[BITBUF_ROWS][BITBUF_COLS], int16_t bits_per_row[BITBUF_ROWS]) {
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

	//fprintf(stderr, "OS v2.1 sync byte search - test_val=%08x pattern=%08x  mask=%08x\n", sync_test_val, pattern, mask);

	    if (((sync_test_val & mask) == pattern) ||
		    ((sync_test_val & mask) == pattern2)) {
		  //  Found sync byte - start working on decoding the stream data.
		  // pattern_index indicates  where sync nibble starts, so now we can find the start of the payload
	      int start_byte = 5 + (pattern_index>>3);
	      int start_bit = pattern_index & 0x07;
	//fprintf(stderr, "OS v2.1 Sync test val %08x found, starting decode at byte index %d bit %d\n", sync_test_val, start_byte, start_bit);
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

	//fprintf(stderr,"i=%d j=%d dest_bit=%02x bb=%02x msg=%02x\n",i, j, dest_bit, bb[0][i], msg[dest_bit>>3]);
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
		 if (sensor_id == 0x1d20) fprintf(stderr, "Weather Sensor THGR122N Channel %d ", channel);
		 else fprintf(stderr, "Weather Sensor THGR968  Outdoor   ");
		 fprintf(stderr, "Temp: %3.1f¬∞C  %3.1f¬∞F   Humidity: %d%%\n", temp_c, ((temp_c*9)/5)+32,get_os_humidity(msg, sensor_id));
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
	     fprintf(stderr,"Weather Sensor BHTR968  Indoor    Temp: %3.1f¬∞C  %3.1f¬∞F   Humidity: %d%%", temp_c, ((temp_c*9)/5)+32, get_os_humidity(msg, 0x5d60));
	     fprintf(stderr, " (%s) Pressure: %dmbar (%s)\n", comfort_str, ((msg[7] & 0x0f) | (msg[8] & 0xf0))+856, forecast_str);
	   }
	   return 1;
	} else if (sensor_id == 0x2d10) {
	   if (validate_os_v2_message(msg, 161, num_valid_v2_bits, 16) == 0) {
	   float rain_rate = (((msg[4] &0x0f)*100)+((msg[4]>>4)*10) + ((msg[5]>>4)&0x0f)) /10.0F;
       float total_rain = (((msg[7]&0xf)*10000)+((msg[7]>>4)*1000) + ((msg[6]&0xf)*100)+((msg[6]>>4)*10) + (msg[5]&0xf))/10.0F;
	   fprintf(stderr, "Weather Sensor RGR968   Rain Gauge  Rain Rate: %2.0fmm/hr Total Rain %3.0fmm\n", rain_rate, total_rain);
	   }
	   return 1;
	} else if (sensor_id == 0xec40 && num_valid_v2_bits==153) {
		if (  validate_os_v2_message(msg, 153, num_valid_v2_bits, 12) == 0) {
			int  channel = ((msg[2] >> 4)&0x0f);
			if (channel == 4)
				channel = 3; // sensor 3 channel number is 0x04
			float temp_c = get_os_temperature(msg, sensor_id);
			if (sensor_id == 0xec40) fprintf(stderr, "Thermo Sensor THR228N Channel %d ", channel);
			fprintf(stderr, "Temp: %3.1f¬∞C  %3.1f¬∞F\n", temp_c, ((temp_c*9)/5)+32);
		}
		return 1;
	} else if (sensor_id == 0xec40 && num_valid_v2_bits==129) {
		if (  validate_os_v2_message(msg, 129, num_valid_v2_bits, 12) == 0) {
                        int  channel = ((msg[2] >> 4)&0x0f);
			if (channel == 4)
				channel = 3; // sensor 3 channel number is 0x04
                        int battery_low = (msg[3] >> 2 & 0x01);
			unsigned char rolling_code = ((msg[2] << 4)&0xF0) | ((msg[3] >> 4)&0x0F);
			float temp_c = get_os_temperature(msg, sensor_id);
			if (sensor_id == 0xec40) fprintf(stderr, "Thermo Sensor THN132N, Channel %d, Battery: %s, Rolling-code 0x%0X, ", channel, battery_low?"Low":"Ok", rolling_code);
			fprintf(stderr, "Temp: %3.1f¬∞C  %3.1f¬∞F\n", temp_c, ((temp_c*9)/5)+32);
		}
		return 1;
	} else if (num_valid_v2_bits > 16) {
fprintf(stderr, "%d bit message received from unrecognized Oregon Scientific v2.1 sensor with device ID %x.\n", num_valid_v2_bits, sensor_id);
fprintf(stderr, "Message: "); for (i=0 ; i<20 ; i++) fprintf(stderr, "%02x ", msg[i]); fprintf(stderr,"\n\n");
    } else {
//fprintf(stderr, "\nPossible Oregon Scientific v2.1 message, but sync nibble wasn't found\n"); fprintf(stderr, "Raw Data: "); for (i=0 ; i<BITBUF_COLS ; i++) fprintf(stderr, "%02x ", bb[0][i]); fprintf(stderr,"\n\n");
    }
   } else {
//if (bb[0][3] != 0) int i; fprintf(stderr, "\nBadly formatted OS v2.1 message encountered."); for (i=0 ; i<BITBUF_COLS ; i++) fprintf(stderr, "%02x ", bb[0][i]); fprintf(stderr,"\n\n");}
   }
   return 0;
}

static int oregon_scientific_v3_parser(uint8_t bb[BITBUF_ROWS][BITBUF_COLS], int16_t bits_per_row[BITBUF_ROWS]) {

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
//fprintf(stderr, "OS v3 Sync nibble search - test_val=%08x pattern=%08x  mask=%08x\n", sync_test_val, pattern, mask);
	    if (((sync_test_val & mask) == pattern)  ||
            ((sync_test_val & mask) == pattern2) ||
            ((sync_test_val & mask) == pattern3)) {
		  //  Found sync byte - start working on decoding the stream data.
		  // pattern_index indicates  where sync nibble starts, so now we can find the start of the payload
	      int start_byte = 3 + (pattern_index>>3);
	      int start_bit = (pattern_index+4) & 0x07;
//fprintf(stderr, "Oregon Scientific v3 Sync test val %08x ok, starting decode at byte index %d bit %d\n", sync_test_val, start_byte, start_bit);
          j = start_bit;
	      for (i=start_byte;i<BITBUF_COLS;i++) {
	        while (j<8) {
			   unsigned char bit_val = ((bb[0][i] & (0x80 >> j)) >> (7-j));

			   // copy every  bit from source stream to dest packet
			   msg[dest_bit>>3] |= (((bb[0][i] & (0x80 >> j)) >> (7-j)) << (7-(dest_bit & 0x07)));

//fprintf(stderr,"i=%d j=%d dest_bit=%02x bb=%02x msg=%02x\n",i, j, dest_bit, bb[0][i], msg[dest_bit>>3]);
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
		 fprintf(stderr,"Weather Sensor THGR810  Channel %d Temp: %3.1f¬∞C  %3.1f¬∞F   Humidity: %d%%\n", channel, temp_c, ((temp_c*9)/5)+32, humidity);
	   }
	   return 1;
    } else if ((msg[0] != 0) && (msg[1]!= 0)) { //  sync nibble was found  and some data is present...
fprintf(stderr, "Message received from unrecognized Oregon Scientific v3 sensor.\n");
fprintf(stderr, "Message: "); for (i=0 ; i<BITBUF_COLS ; i++) fprintf(stderr, "%02x ", msg[i]); fprintf(stderr, "\n");
fprintf(stderr, "    Raw: "); for (i=0 ; i<BITBUF_COLS ; i++) fprintf(stderr, "%02x ", bb[0][i]); fprintf(stderr,"\n\n");
    } else if (bb[0][3] != 0) {
//fprintf(stderr, "\nPossible Oregon Scientific v3 message, but sync nibble wasn't found\n"); fprintf(stderr, "Raw Data: "); for (i=0 ; i<BITBUF_COLS ; i++) fprintf(stderr, "%02x ", bb[0][i]); fprintf(stderr,"\n\n");
    }
   }
   else { // Based on first couple of bytes, either corrupt message or something other than an Oregon Scientific v3 message
//if (bb[0][3] != 0) { fprintf(stderr, "\nUnrecognized Msg in v3: "); int i; for (i=0 ; i<BITBUF_COLS ; i++) fprintf(stderr, "%02x ", bb[0][i]); fprintf(stderr,"\n\n"); }
   }
   return 0;
}

static int oregon_scientific_callback(uint8_t bb[BITBUF_ROWS][BITBUF_COLS], int16_t bits_per_row[BITBUF_ROWS]) {
 int ret = oregon_scientific_v2_1_parser(bb, bits_per_row);
 if (ret == 0)
   ret = oregon_scientific_v3_parser(bb, bits_per_row);
 return ret;
}

r_device oregon_scientific = {
    /* .id             = */ 11,
    /* .name           = */ "Oregon Scientific Weather Sensor",
    /* .modulation     = */ OOK_MANCHESTER,
    /* .short_limit    = */ 125,
    /* .long_limit     = */ 0, // not used
    /* .reset_limit    = */ 600,
    /* .json_callback  = */ &oregon_scientific_callback,
};
