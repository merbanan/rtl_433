// **** debug to csv utility functions ****

#include <stdarg.h>
#include "rtl_433.h"

// Declarations
char bitbuffer_trace_label[LOCAL_TIME_BUFLEN + 48] = {0};

/// Get a label (in_filename or timestamp) for trace messages
char * bitbuffer_label();

/// Format byte as bit string
/// @param numbits : bits above numbits are masked
/// @param result returns bitstring, requires 10 characters (inc the \0)
void byte2bitstr(const uint8_t byte, const uint16_t numbits, char * result);

/// Format byte as hexstr
/// @param numbits : bits above numbits are masked
/// @param result returns hexstring, requires 3 characters (inc the \0)
void byte2hexstr(const uint8_t byte, const uint16_t numbits, char * result);

/// Format byte as string
/// @param numbits : bits above numbits are masked
/// @param showbits : if true, include bit strings as well as hex strings
/// @param result returns hexstring, requires up to 13 characters (inc the \0)
void byte2str(const uint8_t byte, const uint16_t numbits, const bool showbits,
              char * result);

/// Pretty print part of a bitbuffer row
/// @param bitpos : printing starts here
/// @param numbits : number of bits printed (provided available on row)
/// @param showbits : if true, print bit strings as well as hex strings
void bitbuffer_pprint_partrow(FILE * stream, bitbuffer_t *bits, const uint16_t row,
                 const uint16_t bitpos, const uint16_t numbits, const bool showbits);

/// Pretty print part of bitbuffer row, with trace messages,
/// @param bitpos : printing starts here
/// @param numbits : number of bits printed (provided available on row)
/// @param showbits : if true, print bit strings as well as hex strings
void bitbuffer_pp_partrow_trace(FILE * stream, bitbuffer_t *bits, const uint16_t row,
                 const uint16_t bitpos, const uint16_t numbits, const bool showbits, 
                 char * format, ...);
void vbitbuffer_pp_partrow_trace(FILE * stream, bitbuffer_t *bits, const uint16_t row,
                 const uint16_t bitpos, const uint16_t numbits, const bool showbits,
                 char * format, va_list args);


/// Print all the rows in the bitbuffer in "debug to csv" format.
void bitbuffer_pp_trace(FILE * stream, bitbuffer_t *bits,
                 const bool showbits, 
                 char * format, ...);
void vbitbuffer_pp_trace(FILE * stream, bitbuffer_t *bits,
                 const bool showbits, 
                 char * format, va_list args);


void vbitbuffer_basic_trace(bitbuffer_t * bits, char *format, va_list args) ;
void bitbuffer_basic_trace(bitbuffer_t * bits, char *format, ...);
    
// Definitions


extern char * in_filename;    

char * bitbuffer_label() {
	// Get a label for this "line" of output read from stdin.
	// In case stdin is empty, use a timestamp instead.
	
	static time_t last_time = -1;
  time_t current;

  if (in_filename && (strlen(bitbuffer_trace_label) > 0))  {
    return bitbuffer_trace_label; //The label has already been read!
  }
  if (in_filename) {
    strncpy(bitbuffer_trace_label, in_filename, 48);
  } else {
    // Running realtime, use the current time string as a default label
    time(&current);
    if (difftime(current, last_time) > 5.0) {
        //Assume it's a new package, reset the timestamp
        local_time_str(current, &bitbuffer_trace_label[0]);
        last_time = current;
    }
  }
  return bitbuffer_trace_label;
}


void byte2bitstr(const uint8_t byte, const uint16_t numbits, char * result) {
    uint8_t col = 0;
    
		for (uint16_t bit = 0; bit < 8; ++bit) {
		 	if (bit < numbits) {
  			if (byte & (0x80 >> (bit % 8))) {
  			    result[col] = '1'; col++;
  		  } else {
  		      result[col] = '0'; col++;
  		  }
      } else { //mask bit
          result[col] = '-'; col++;
      }
		  if ((bit % 8) == 3) {      // Separator between nibbles
		      result[col] = ' '; col++;
      }
		} 
}

void byte2hexstr(const uint8_t byte, const uint16_t numbits, char * result) {
    uint8_t maskedbyte, maskshift;
    if (numbits >= 8) {
      maskedbyte = byte;
    } else {
      maskshift = (8 - numbits);
      maskedbyte = (byte >> maskshift) << maskshift;
    };
    snprintf(result, 3, "%02X", maskedbyte);
}

void byte2str(const uint8_t byte, const uint16_t numbits, const bool showbits,
              char * result) {
    char bitstr[10] = {0};
    char hexstr[3] = {0};
    if (showbits) byte2bitstr(byte, numbits, bitstr);
		byte2hexstr( byte, numbits, hexstr);
    snprintf (result, 13,  "%s %s", hexstr, bitstr);
}


void vbitbuffer_pp_partrow_trace(FILE * stream, bitbuffer_t *bits, const uint16_t row,
                 const uint16_t bitpos, const uint16_t numbits, const bool showbits, 
                 char * format, va_list args){
    uint8_t b[BITBUF_COLS];
    uint16_t col, bits_available, bitsleft;
    char bytestr[14] = {0};
    
    // Pretty print the part row
    // a Extract the part row
    if (bitpos + numbits <= bits -> bits_per_row[row]) {
      bits_available = numbits;
    } else {
      bits_available = bits -> bits_per_row[row] - bitpos;
    }
    bitbuffer_extract_bytes(bits, row, bitpos, b, bits_available);
    // Display the part row
		fprintf(stream, "\nnr[%d] r[%02d] nsyn[%02d] nc[%2d] ,at bit [%03d], ", 
                    bits->num_rows, row, bits->syncs_before_row[row], 
                    bits->bits_per_row[row], bitpos);
		for (col = 0; col < (bits_available+7)/8; ++col) {
  		  bitsleft = bits_available - col * 8;
        byte2str(b[col], bitsleft, showbits, bytestr);
        //`leading tab character (\t) helps stop Excel stripping leading zeros
        // trailing comma makes a "nicer" csv file
        if (showbits) fprintf (stream, "\t%s, ", bytestr);
        // else keep as close as possible to bitbuffer_print format
        else fprintf(stream,"%s", bytestr);
		}
		// Print any trace messages
    vfprintf(stream, format, args);
}

void bitbuffer_pp_partrow_trace(FILE * stream, bitbuffer_t *bits, const uint16_t row,
                 const uint16_t bitpos, const uint16_t numbits, const bool showbits, 
                 char * format, ...){
    va_list args;
    va_start(args, format);
    vbitbuffer_pp_partrow_trace(stream, bits, row, bitpos, numbits, showbits, 
                                format, args);
    va_end(args);
}

void bitbuffer_pprint_partrow(FILE * stream, bitbuffer_t *bits, const uint16_t row,
                 const uint16_t bitpos, const uint16_t numbits, const bool showbits){
		bitbuffer_pp_partrow_trace(stream, bits, row, bitpos, numbits, showbits, NULL);
}


void vbitbuffer_pp_trace(FILE * stream, bitbuffer_t *bits,
                 const bool showbits, 
                 char * format, va_list args){
  // Print all the rows in the bitbuffer in "debug to csv" format.
	uint16_t row;
	for (row = 0; row < bits->num_rows; ++row) {
    bitbuffer_pprint_partrow(stream, bits, row, 0, bits -> bits_per_row[row], showbits);
    fprintf(stream, " %s, ", bitbuffer_label());
    vfprintf(stream, format, args);
    fprintf(stream, "\n");
	}
}

void bitbuffer_pp_trace(FILE * stream, bitbuffer_t *bits,
                 const bool showbits, 
                 char * format, ...){
  // Print all the rows in the bitbuffer in "debug to csv" format.
  va_list args;
  va_start(args, format);
  vbitbuffer_pp_trace( stream, bits, showbits, format, args);
  va_end(args);
}


/*****************************/
void vbitbuffer_basic_trace(bitbuffer_t * bits, char *format, va_list args) {
    fprintf(stderr, "\n BASIC TRACE");
    bitbuffer_print(bits);
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
}

void bitbuffer_basic_trace(bitbuffer_t * bits, char *format, ...) {
    va_list args;
    va_start(args, format);
    vbitbuffer_basic_trace(bits, format, args);
    va_end(args);
}


// End of debugging utilities
