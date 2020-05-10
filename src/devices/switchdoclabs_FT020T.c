/* 
 * SwitchDoc Labs WeatherSense FT020T All In One Weather Sensor Pack
 *
 */

#include "decoder.h"

// three repeats without gap
// full preamble is 0x00145 (the last bits might not be fixed, e.g. 0x00146)
// and on decoding also 0xffd45
static const uint8_t preamble_pattern[2] = {0x01, 0x45}; // 12 bits

static const uint8_t preamble_inverted[2] = {0xfd, 0x45}; // 12 bits

//============================================================================================
// CRC table
//============================================================================================
const unsigned char crc_table[256] = {
        0x00, 0x31, 0x62, 0x53, 0xc4, 0xf5, 0xa6, 0x97, 0xb9, 0x88, 0xdb, 0xea, 0x7d, 0x4c, 0x1f, 0x2e,
        0x43, 0x72, 0x21, 0x10, 0x87, 0xb6, 0xe5, 0xd4, 0xfa, 0xcb, 0x98, 0xa9, 0x3e, 0x0f, 0x5c, 0x6d,
        0x86, 0xb7, 0xe4, 0xd5, 0x42, 0x73, 0x20, 0x11, 0x3f, 0x0e, 0x5d, 0x6c, 0xfb, 0xca, 0x99, 0xa8,
        0xc5, 0xf4, 0xa7, 0x96, 0x01, 0x30, 0x63, 0x52, 0x7c, 0x4d, 0x1e, 0x2f, 0xb8, 0x89, 0xda, 0xeb,
        0x3d, 0x0c, 0x5f, 0x6e, 0xf9, 0xc8, 0x9b, 0xaa, 0x84, 0xb5, 0xe6, 0xd7, 0x40, 0x71, 0x22, 0x13,
        0x7e, 0x4f, 0x1c, 0x2d, 0xba, 0x8b, 0xd8, 0xe9, 0xc7, 0xf6, 0xa5, 0x94, 0x03, 0x32, 0x61, 0x50,
        0xbb, 0x8a, 0xd9, 0xe8, 0x7f, 0x4e, 0x1d, 0x2c, 0x02, 0x33, 0x60, 0x51, 0xc6, 0xf7, 0xa4, 0x95,
        0xf8, 0xc9, 0x9a, 0xab, 0x3c, 0x0d, 0x5e, 0x6f, 0x41, 0x70, 0x23, 0x12, 0x85, 0xb4, 0xe7, 0xd6,
        0x7a, 0x4b, 0x18, 0x29, 0xbe, 0x8f, 0xdc, 0xed, 0xc3, 0xf2, 0xa1, 0x90, 0x07, 0x36, 0x65, 0x54,
        0x39, 0x08, 0x5b, 0x6a, 0xfd, 0xcc, 0x9f, 0xae, 0x80, 0xb1, 0xe2, 0xd3, 0x44, 0x75, 0x26, 0x17,
        0xfc, 0xcd, 0x9e, 0xaf, 0x38, 0x09, 0x5a, 0x6b, 0x45, 0x74, 0x27, 0x16, 0x81, 0xb0, 0xe3, 0xd2,
        0xbf, 0x8e, 0xdd, 0xec, 0x7b, 0x4a, 0x19, 0x28, 0x06, 0x37, 0x64, 0x55, 0xc2, 0xf3, 0xa0, 0x91,
        0x47, 0x76, 0x25, 0x14, 0x83, 0xb2, 0xe1, 0xd0, 0xfe, 0xcf, 0x9c, 0xad, 0x3a, 0x0b, 0x58, 0x69,
        0x04, 0x35, 0x66, 0x57, 0xc0, 0xf1, 0xa2, 0x93, 0xbd, 0x8c, 0xdf, 0xee, 0x79, 0x48, 0x1b, 0x2a,
        0xc1, 0xf0, 0xa3, 0x92, 0x05, 0x34, 0x67, 0x56, 0x78, 0x49, 0x1a, 0x2b, 0xbc, 0x8d, 0xde, 0xef,
        0x82, 0xb3, 0xe0, 0xd1, 0x46, 0x77, 0x24, 0x15, 0x3b, 0x0a, 0x59, 0x68, 0xff, 0xce, 0x9d, 0xac
};

//============================================================================================
// Function: Calculate the CRC value
// Input:    crc: CRC initial value. lpBuff: string. ucLen: string length
// Return:   CRC value
//============================================================================================
uint8_t GetCRC(uint8_t crc, uint8_t * lpBuff,uint8_t ucLen)
{
        while (ucLen)
        {
                ucLen--;
                crc = crc_table[*lpBuff ^ crc];
                lpBuff++;
        }
        return crc;
}


static int
switchdoclabs_weather_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    uint8_t b[16];
    data_t *data;
    int i;
    for (i=0; i< 16; i++)
   	b[i] = 0; 

    bitbuffer_extract_bytes(bitbuffer, row, bitpos, b, 16*8);
    /*
    for (i=0; i< 16; i++)
    {
	    fprintf(stderr,"%02x ", b[i]);
    }
    fprintf(stderr,"\n");
    */
    uint8_t  myDevice;
    uint8_t  mySerial;
    uint8_t  myFlags;
    uint8_t myBatteryLow;
    uint16_t  myAveWindSpeed;
    uint16_t  myGust;
    uint16_t  myWindDirection;
    uint32_t myCumulativeRain;
    uint8_t mySecondFlags;
    uint16_t myTemperature;
    uint8_t  myHumidity;
    uint32_t myLight;
    uint16_t myUV;
    uint8_t myCRC;
    uint8_t b2[16];	
    uint8_t myCalculated; 
      



	b2[0] = 0xd4; // Shift all 4 bits fix b2[0];
	for (i = 0; i < 15; i++)
	{
		b2[i+1] = ((b[i] &0x0f)<<4) + ((b[i+1] & 0xf0)>>4);
		b2[i] = b2[i+1]; // shift 8

	}

	/*
    	for (i=0; i< 16; i++)
    	{
	    	fprintf(stderr,"%02x ", b2[i]);
    	}
    	fprintf(stderr,"\n");
	*/

    uint8_t expected = b2[13];
		
    myCalculated = GetCRC(0xc0, b2, 13);
	

    uint8_t calculated = myCalculated;

    if (expected != calculated) {
        if (decoder->verbose) {
            fprintf(stderr, "Checksum error in SwitchDoc Labs Weather message.    Expected: %02x    Calculated: %02x\n", expected, calculated);
            fprintf(stderr, "Message: ");
            bitrow_print(b, 48);
        }
        return 0;
    }
	
   	myDevice = (b2[0] & 0xf0)>>4;
	if (myDevice !=  0x0c)
	{
		return 0; // not my device
	}
   	mySerial = (b2[0]&0x0f)<<4 & (b2[1] % 0xf0)>>4;
   	myFlags  = b2[1] & 0x0f;
	myBatteryLow = (myFlags & 0x08) >> 3;
   	myAveWindSpeed = b2[2] | ((myFlags & 0x01)<<8);
   	myGust         = b2[3] | ((myFlags & 0x02)<<7);
   	myWindDirection= b2[4] | ((myFlags & 0x04)<<6);
   	myCumulativeRain=(b2[5]<<8) + b2[6]; 
   	mySecondFlags  = (b2[7] & 0xf0)>>4;
	myTemperature = ((b2[7] & 0x0f)<<8) + b2[8];  
	myHumidity = b2[9];
	myLight = (b2[10]<<8) + b2[11] + ((mySecondFlags & 0x08)<<9);
	myUV = b2[12]; 
	//myUV = myUV + 10;
	myCRC = b2[13];

	
	if (myTemperature == 0xFF)
	{
		return 0; //  Bad Data
	}
	
	if (myAveWindSpeed ==  0xFF)
	{
		return 0; //  Bad Data
	}

	/*	
   	myDevice = (b[0]) & 0x0f;
   	mySerial = b[1];
   	myFlags  = (b[2]>>4) & 0x0f;
	myBatteryLow = (myFlags & 0x08) >> 3;
   	myAveWindSpeed = ((b[2] & 0x0f)<<4 ) + (b[3]>>4)+ ((myFlags & 0x01)<<8);
   	myGust         = ((b[3] & 0x0f)<<4 ) + (b[4]>>4)+ ((myFlags & 0x02)<<7);
   	myWindDirection= ((b[4] & 0x0f)<<4 ) + (b[5]>>4) + ((myFlags & 0x04)<<6);
   	myCumulativeRain=((((b[5] & 0x0f)<<4 ) + (b[6]>>4))<<8) + (((b[6] & 0x0f)<<4 ) + (b[7]>>4)) ;
   	mySecondFlags  = (b[7]) & 0x0f;
	myTemperature = ((b[8]& 0xf0)<<4) + ((b[8] &0x0f)<<4) + ((b[9] &0xf0) >>4);
	myHumidity = ((b[9] &0x0f)<<4) + (b[10]>>4);
	myLight = ((b[10] & 0x0f)<<12) + ((b[11] & 0xf0)<< 4) + ((b[11] &0x0f)<< 4) + (b[12]>>4)+ ((mySecondFlags & 0x08)<<9);
	myUV = ((b[12] & 0x0f) << 4) + ((b[13] & 0xf0)>>4);
	//myUV = myUV + 10;
	myCRC = ((b[13] & 0x0f) << 4) + ((b[14] & 0xf0)>>4);
*/
    /*
  	fprintf(stderr,"myDevice = %02x %d\n", myDevice, myDevice );
  	fprintf(stderr,"mySerial = %02x %d\n", mySerial, mySerial );
  	fprintf(stderr,"myFlags = %01x %d\n", myFlags, myFlags );
  	fprintf(stderr,"myBatteryLow  = %01x %d\n", myBatteryLow , myBatteryLow  );
  	fprintf(stderr,"myAveWindSpeed = %02x %d\n", myAveWindSpeed, myAveWindSpeed );
  	fprintf(stderr,"myGust = %02x %d\n", myGust, myGust );
  	fprintf(stderr,"myWindDirection = %02x %d\n", myWindDirection, myWindDirection );
  	fprintf(stderr,"myCumulativeRain = %04x %d\n", myCumulativeRain, myCumulativeRain );
  	fprintf(stderr,"mySecondFlags = %01x %d\n", mySecondFlags, mySecondFlags );
  	fprintf(stderr,"myTemperature = %04x %d\n", myTemperature, myTemperature );
  	fprintf(stderr,"myLight = %04x %d\n", myLight, myLight );
  	fprintf(stderr,"myUV = %02x %d\n", myUV, myUV );
  	fprintf(stderr,"myCRC = %02x %d\n", myCRC, myCRC );
  	fprintf(stderr,"myCalculated = %02x %d\n", myCalculated, myCalculated );

    */



    data = data_make(
            "model",          "",             DATA_STRING, "SwitchDocLabs-FT020T",
            "id",         "Device",   DATA_INT,    myDevice,
            "id",        "Serial Number",      DATA_INT,    mySerial,
            "batterylow",        "Battery Low",      DATA_INT, myBatteryLow,
            "avewindspeed",        "Ave Wind Speed",      DATA_INT, myAveWindSpeed,
            "gustwindspeed",        "Gust",      DATA_INT, myGust,
            "winddirection",        "Wind Direction",      DATA_INT, myWindDirection,
            "cumulativerain",        "Cum Rain",      DATA_INT, myCumulativeRain,
            "temperature",        "Temperature",      DATA_INT, myTemperature,
            "humidity",        "Humidity",      DATA_INT, myHumidity,
            "light",        "Light",      DATA_INT, myLight,
            "uv",        "UV Index",      DATA_INT, myUV,
            "mic",            "Integrity",    DATA_STRING, "CRC",
            NULL);
    decoder_output_data(decoder, data);

    return 1;
}

static int
switchdoclabs_weather_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int row;
    unsigned bitpos;
    int events = 0;

    for (row = 0; row < bitbuffer->num_rows; ++row) {
        bitpos = 0;
        // Find a preamble with enough bits after it that it could be a complete packet
        while ((bitpos = bitbuffer_search(bitbuffer, row, bitpos,
                (const uint8_t *)&preamble_pattern, 12)) + 8+6*8 <=
                bitbuffer->bits_per_row[row]) {
		//fprintf(stderr,"before decode1\n");
            events += switchdoclabs_weather_decode(decoder, bitbuffer, row, bitpos + 8);
            if (events) return events; // for now, break after first successful message
            bitpos += 16;
        }
        bitpos = 0;
        while ((bitpos = bitbuffer_search(bitbuffer, row, bitpos,
                (const uint8_t *)&preamble_inverted, 12)) + 8+6*8 <=
                bitbuffer->bits_per_row[row]) {
	    //fprintf(stderr,"before decode2\n");
            events += switchdoclabs_weather_decode(decoder, bitbuffer, row, bitpos + 8);
            if (events) return events; // for now, break after first successful message
            bitpos += 15;
        }
    }

    return events;
}

static char *output_fields[] = {
    "model",
    "device", 
    "id",
    "batterylow",
    "avewindspeed",
    "gustwindspeed",
    "winddirection",
    "cumulativerain",
    "temperature",
    "humidity",
    "light",
    "uv",
    "mic",
    NULL
};

r_device switchdoclabs_FT020T = {
    .name          = "SwitchDoc Labs Weather FT020T Sensors",
    .modulation    = OOK_PULSE_MANCHESTER_ZEROBIT,
    .short_width   = 488,
    .long_width    = 0, // not used
    .reset_limit   = 2400,
    .decode_fn     = &switchdoclabs_weather_callback,
    .disabled      = 0,
    .fields        = output_fields
};
