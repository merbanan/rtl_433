// **** Digitech specific trace message utility functions ****

#include "bitbuffer.trace.c"

void xc0324_row_status(bitbuffer_t *bits, const uint16_t row,
                       char * format, ...) {
   va_list args;
   bool showbits = 1;
   // Flag bad samples (too much noise, not enough sample, 
   // or package possibly segmented over multiple rows
   uint8_t *buffer = bits->bb[row];
   buffer_pp_trace(stderr, buffer, bits->bits_per_row[row], showbits,
                   "XC0324:DD MESSAGE, " ); 
   if (bits->num_rows > 1) {
       fprintf(stderr, "Bad package - more than 1 row, ");
       // But maybe there are usable fragments somewhere?
   }
   if (bits->bits_per_row[row] < MYDEVICE_BITLEN) {
       fprintf(stderr, "Bad row - row %d length %d is less than %d bits, ", 
               row, bits->bits_per_row[row], MYDEVICE_BITLEN);
       // Mmmm, not a full package, but is there a single message?
   }
   if (bits->bits_per_row[row] < MYMESSAGE_BITLEN) {
       fprintf(stderr, "Bad message - row %d length %d is less than %d bits, ", 
               row, bits->bits_per_row[row], MYMESSAGE_BITLEN);
       // No, not even a single message :-(
   }
   va_start(args, format);
   vfprintf(stderr, format, args);
   va_end(args);
}

void xc0324_message_trace(uint8_t * buffer, 
                         // const uint16_t row, const uint16_t bitpos,
                          char * format, ...){
   //my_pp_row_tests(stream, bits, row, bitpos);
   //Start a trace csvline containing one message's worth of bits in hex and binary
   bool showbits =1;
   va_list args;
   buffer_pp_trace(stderr, buffer, MYMESSAGE_BITLEN, showbits, 
         "XC0324:DD MESSAGE, " );
   va_start(args, format);
   vfprintf(stderr, format, args);
   va_end(args);
   fprintf(stderr, "\n");
}

void xc0324_bitbuffer_trace(FILE * stream, bitbuffer_t *bits,
                            char * format, ...) {
  // Print all the rows in the bitbuffer in "debug to csv" format.
  bool showbits = 1;
  va_list args;
  va_start(args, format);
  vbitbuffer_pp_trace(stream, bits, showbits, format, args);
  va_end(args);
}


// Flag to ensure `-DDD` reference values output are only written once.
static volatile bool reference_values_written;

// End of XC0324 specific debugging utilities
