// Get a label for my debuggng output
// Preferably read from stdin (bodgy but easy to arrange with a pipe)
// but failing that, after waiting 2 seconds, use a local time string

static volatile bool fgets_timeout2;

void fgets_timeout2_handler(int sigNum) {
    fgets_timeout2 = 1; // Non zero == True;
}

// g005_433.922M_250k.cu8 is 22 chars long
// Assume LOCAL_TIME_BUFLEN is at least 7! on my machine is 32
// Make sure the buffer is well and truly big enough
char xc0324_label2[LOCAL_TIME_BUFLEN + 48] = {0};

//void get_xc0324_label2(char * label) {
void get_xc0324_label2() {
	// Get a label for this "line" of output read from stdin
	// Has a hissy fit if nothing there to read!!
	// so set an alarm and provide a default alternative in that case.
  char * lab;

  // Allow fgets 2 seconds to read label from stdin
  fgets_timeout2 = 0; // False;
  signal(SIGALRM, fgets_timeout2_handler);
  alarm(2);

  //fprintf(stderr, "Local time buflen is %d", LOCAL_TIME_BUFLEN);
  lab = fgets(&xc0324_label2[0], 48, stdin);

  if (fgets_timeout2) {
    // Use a current time string as a default label
    time_t current;
    local_time_str(time(&current), &xc0324_label2[0]);
  }
}


static int
xc0324_decode_temp(bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos, data_t ** data)
{   // Working buffers
    uint8_t b[MYMESSAGE_BYTELEN];
    
    // Extracted data values
    double temperature;
    uint8_t parity_check; //message integrity check == 0x00
    

    // Extract the message
    bitbuffer_extract_bytes(bitbuffer, row, bitpos, b, MYMESSAGE_BITLEN);

    // Examine the paritycheck and bail out if not OK
    parity_check = calculate_paritycheck(b, 6);
    if (parity_check != 0x00) {
       return 0;
    }
    
    // Decode temperature (b[2]), plus 1st 4 bits b[3], LSB order!
    // Tenths of degrees C, offset from the minimum possible (-40.0 degrees)
    
    uint16_t temp = ( (uint16_t)(reverse8(b[3]) & 0x0f) << 8) | reverse8(b[2]) ;
    temperature = (temp / 10.0) - 40.0 ;
    if (debug_output > 0) {
      fprintf(stderr, "\t%4.1f ,\n", temperature);
    }

    return 1;
}


static int xc0324_correct2csv_callback(bitbuffer_t *bitbuffer)
{
    int r; // a row index
    uint8_t *b; // bits of a row

    unsigned bitpos;
    int events = 0;
    data_t * data;
    int result;
    
    /*
     * A complete XC0324 package contains 3 repeats of a message in a single row.
     * But in case there are transmission or demodulation glitches,
     * loop over all rows and check for recognisable messages:
     */
      if (debug_output > 0) {
        // Start a "debug to csv" formatted version of the filename plus
        // the correct temperature.
        get_xc0324_label2();
        fprintf(stderr, "\n%s, XC0324:Temperature, ", xc0324_label2);
        // xc0324_decode_temp will send the rest of the "debug to csv" formatted
        // to stderr.
      }

    for (r = 0; r < bitbuffer->num_rows; ++r) {
        b = bitbuffer->bb[r];

        if (bitbuffer->bits_per_row[r] < MYMESSAGE_BITLEN) {
          continue; // to the next row  
        }
        // OK, at least we have enough bits
        /*
         * Search for a message preamble followed by enough bits 
         * that it could be a complete message:
         */
        bitpos = 0;
        while ((bitpos = bitbuffer_search(bitbuffer, r, bitpos,
                (const uint8_t *)&preamble_pattern, 8)) 
                + MYMESSAGE_BITLEN <= bitbuffer->bits_per_row[r]) {
            events += result = xc0324_decode_temp(bitbuffer, r, bitpos, &data);
            if (result) {
              return events; // for now, break after first successful message
            }
            bitpos += MYMESSAGE_BITLEN;
        }
    }
    // Finish off the debug to csv format line
    if (debug_output > 0) {
      fprintf(stderr, "Bad transmission, \n");
    }
    return events;
}

