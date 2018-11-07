// **** Digitech specific trace message utility functions ****

#include "bitbuffer.trace.c"

void xc0324_message_trace(FILE * stream, bitbuffer_t *bits, const uint16_t row,
                 const uint16_t bitpos){
   //Start a csvline containing one message's worth of bits in hex and binary
   //Leave the csvline "open", so other code can add extra csv columns
   //via eg `fprintf(stream, " foobar, ", )`
   //PS Note the "," after foobar - needed to make it a csv line :-)
   //bitbuffer_start_trace(stream, "XC0324:DD Message");
   //bitbuffer_pretty_print_rowbits(stream, bits, row, bitpos, MYMESSAGE_BITLEN);
   //fprintf(stream, "\n\nJust before rowbits_trace\n\n");
   rowbits_pp_trace(stream, bits, row, bitpos, MYMESSAGE_BITLEN,
         "XC0324:DD TEST1 MESSAGE,", 
         "TEST This is the end %s, this is the %s TEST,", "my friend", "end" );
   rowbits_pp_trace(stream, bits, row, bitpos, MYMESSAGE_BITLEN,
         "XC0324:DD TEST2 MESSAGE,", NULL );
   // Flag bad samples (too much noise, not enough sample, 
   // or package possibly segmented over multiple rows
   if (bits->num_rows > 1) {
       bitbuffer_add_trace(stream, "Bad XC0324 package - more than 1 row, ");
       // But maybe there are usable fragments somewhere?
   }
   if (bits->bits_per_row[row] < MYDEVICE_BITLEN) {
       bitbuffer_add_trace(stream, "Bad XC0324 package - less than %d bits, ", MYDEVICE_BITLEN);
       // Mmmm, not a full package, but is there a single message?
   }
   if (bits->bits_per_row[row] < MYMESSAGE_BITLEN) {
       bitbuffer_add_trace(stream, "Bad XC0324 message - less than %d bits, ", MYMESSAGE_BITLEN);
       // No, not even a single message :-(
   }
}

void xc0324_bitbuffer_trace(FILE * stream, bitbuffer_t *bits,
                            char * format, ...) {
  // Print all the rows in the bitbuffer in "debug to csv" format.
	uint16_t row;
  va_list args;
  va_start(args, format);
	for (row = 0; row < bits->num_rows; ++row) {
    vbitbuffer_pp_trace(stream, bits, "XC0324:D Package", format, args);
    vfprintf(stream, format, args);
    bitbuffer_end_trace(stream);
	}
  va_end(args);
}


// Flag to ensure `-DDD` reference values output are only written once.
static volatile bool reference_values_written;

// End of debugging utilities
