// **** debug to csv utility functions ****

static volatile bool fgets_timeout;

void fgets_timeout_handler(int sigNum) {
    fgets_timeout = 1; // Non zero == True;
}

char csv_label[LOCAL_TIME_BUFLEN + 48] = {0};

void get_csv_label() {
	// Get a label for this "line" of output read from stdin.
	// In case stdin is empty, use a timestamp instead.
  if (strlen(csv_label) > 0 ) {
    return; //The label has already been read!
  }
  // Allow fgets 2 seconds to read label from stdin
  fgets_timeout = 0; // False;
  signal(SIGALRM, fgets_timeout_handler);
  alarm(2);
  char * lab = fgets(&csv_label[0], 48, stdin);
  csv_label[strcspn(csv_label, "\n")] = 0; //remove trailing newline
  if (fgets_timeout) {
    // Use a current time string as a default label
    time_t current;
    local_time_str(time(&current), &csv_label[0]);
  }
}


int bits2csv(FILE * stream, const uint8_t byte, const uint16_t numbits) {
    // Print binary values , 8 bits at a time

    int nprint = 0;
    
		for (uint16_t bit = 0; bit < 8; ++bit) {
		  if ((bit % 8) == 0)      // Separator to start a byte
		 	  nprint += fprintf(stream, "\t");
		 	if (bit < numbits) {
  			if (byte & (0x80 >> (bit % 8))) {
  		 	  nprint += fprintf(stream, "1");
  		  } else {
  		 	  nprint += fprintf(stream, "0");
  		  }
      } else {
         nprint += fprintf(stream, "-");
      }
		  if ((bit % 8) == 3)      // Separator between nibbles
		 	 nprint += fprintf(stream, " ");
		  if ((bit % 8) == 7)      // Separator to end a byte
		 	 nprint += fprintf(stream, ",");
		} 
		return nprint;
}

int byte2csv(FILE * stream, char * label, const uint8_t byte, 
             const uint16_t numbits) {
    //Print hex and binary in a csv column
    uint8_t maskedbyte, maskshift;
    if (numbits >= 8) {
      maskedbyte = byte;
    } else {
      maskshift = (8 - numbits);
      maskedbyte = (byte >> maskshift) << maskshift;
    };
    //`fprintf`_ing a tab character (\t) helps stop Excel stripping leading zeros
    int nprint = 0;
    nprint = fprintf(stream, "\t %s  %02X  ", label, maskedbyte);
    nprint += bits2csv(stream, maskedbyte, numbits);
    return nprint;
}

void startcsvline(FILE * stream, const char * line_label) {
    // Slightly (well ok, more than slightly) bodgy way to get file name 
    // labels for the "debug to csv" format outputs.
    if (strlen(csv_label) == 0 ) get_csv_label();
    fprintf(stream, "\n%s, %s, ", csv_label, line_label);
}

void endcsvline(FILE * stream) {
    fprintf(stream, "\n");
}

void partrow2csv(FILE * stream, bitbuffer_t *bits, const uint16_t row,
                 const uint16_t bitpos, const uint16_t numbits){
    //Print part of a bitbuffer row - start at bitpos, show up to numbits
    uint8_t b[BITBUF_COLS];
    uint16_t col, bits_available, bitsleft;
    // Extract the part row
    if (bitpos + numbits <= bits -> bits_per_row[row]) {
      bits_available = numbits;
    } else {
      bits_available = bits -> bits_per_row[row] - bitpos;
    }
    bitbuffer_extract_bytes(bits, row, bitpos, b, bits_available);
    // Display the part row
		fprintf(stream, "nr[%d] r[%02d] nsyn[%02d] nc[%2d] ,at bit [%03d], ", 
                    bits->num_rows, row, bits->syncs_before_row[row], 
                    bits->bits_per_row[row], bitpos);
		for (col = 0; col < (bits_available+7)/8; ++col) {
		  bitsleft = bits_available - col * 8;
			byte2csv(stream, "", b[col], bitsleft);
		}
    // Flag bad samples (too much noise, not enough sample, 
    // or package possibly segmented over multiple rows
    if (bits->num_rows > 1) {
      fprintf(stream, "Bad XC0324 package - more than 1 row, ");
      // But maybe there are usable fragments somewhere?
    }
    if (bits->bits_per_row[row] < MYDEVICE_BITLEN) {
      fprintf(stream, "Bad XC0324 package - less than %d bits, ", MYDEVICE_BITLEN);
      // Mmmm, not a full package, but is there a single message?
    }
    if (bits->bits_per_row[row] < MYMESSAGE_BITLEN) {
      fprintf(stream, "Bad XC0324 message - less than %d bits, ", MYMESSAGE_BITLEN);
      // No, not even a single message :-(
    }
}


void message2csv(FILE * stream, bitbuffer_t *bits, const uint16_t row,
                 const uint16_t bitpos){
   //Start a csvline containing one message's worth of bits in hex and binary
   //Leave the csvline "open", so other code can add extra csv columns
   //via eg `fprintf(stream, " foobar, ", )`
   //PS Note "," after foobar - it IS a csv line :-)
   startcsvline(stream, "XC0324:DD Message");
   partrow2csv(stream, bits, row, bitpos, MYMESSAGE_BITLEN);
}


void bitbuffer2csv(FILE * stream, bitbuffer_t *bits) {
  // Print all the rows in the bitbuffer in "debug to csv" format.
	uint16_t row;
	for (row = 0; row < bits->num_rows; ++row) {
    startcsvline(stream, "XC0324:D Package");
    partrow2csv(stream, bits, row, 0, bits -> bits_per_row[row]);
    endcsvline(stream);
	}
}

// Flag to ensure `-DDD` reference values output are only written once.
static volatile bool reference_values_written;

// End of debugging utilities
