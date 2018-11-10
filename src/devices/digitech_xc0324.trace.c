// **** Digitech specific trace message utility functions ****

#include "bitbuffer.trace.c"

void my_pp_row_tests(FILE * stream, bitbuffer_t *bits, const uint16_t row,
                 const uint16_t bitpos) {
     fprintf(stream, "\n\nTESTING\n\n-->");
     fprintf(stream, "\n\nbitbuffer_pprint_partrow(stream, bits, row, bitpos, MYMESSAGE_BITLEN, 0)\n");
     bitbuffer_pprint_partrow(stream, bits, row, bitpos, MYMESSAGE_BITLEN, 0);
     fprintf(stream, "\n\nbitbuffer_pprint_partrow(stream, bits, row, bitpos, MYMESSAGE_BITLEN, 1)\n");
     bitbuffer_pprint_partrow(stream, bits, row, bitpos, MYMESSAGE_BITLEN, 1);
     //fprintf(stream, "\n\nJust before rowbits_trace\n\n");
     fprintf(stream, "\n\nbitbuffer_pp_partrow_trace(stream, bits, row, bitpos, MYMESSAGE_BITLEN, 0, ...\n");
     bitbuffer_pp_partrow_trace(stream, bits, row, bitpos, MYMESSAGE_BITLEN, 0,
           "XC0324:DD TEST1 MESSAGE, TEST no bitstr," );
     fprintf(stream, "\n\nbitbuffer_pp_partrow_trace(stream, bits, row, bitpos, MYMESSAGE_BITLEN, 1, ...\n");
     bitbuffer_pp_partrow_trace(stream, bits, row, bitpos, MYMESSAGE_BITLEN, 1, 
           "XC0324:DD TEST2 MESSAGE, TEST with bitstr, This is the end %s, this is the %s TEST,", "my friend", "end" );
     fprintf(stream, "<--\n\nEND OF TESTING\n\n");
}

void xc0324_message_trace(FILE * stream, bitbuffer_t *bits, 
                          const uint16_t row, const uint16_t bitpos,
                          char * format, ...){
   //my_pp_row_tests(stream, bits, row, bitpos);
   //Start a trace csvline containing one message's worth of bits in hex and binary
   bool showbits =1;
   va_list args;
   va_start(args, format);
   bitbuffer_pp_partrow_trace(stream, bits, row, bitpos, MYMESSAGE_BITLEN, showbits, 
         "%s, XC0324:DD MESSAGE, ", bitbuffer_label() );
   vfprintf(stream, format, args);
   va_end(args);
   // Flag bad samples (too much noise, not enough sample, 
   // or package possibly segmented over multiple rows
   if (bits->num_rows > 1) {
       fprintf(stream, "Bad package - more than 1 row, ");
       // But maybe there are usable fragments somewhere?
   }
   if (bits->bits_per_row[row] < MYDEVICE_BITLEN) {
       fprintf(stream, "Bad package - row of %d is less than %d bits, ", 
               bits->bits_per_row[row], MYDEVICE_BITLEN);
       // Mmmm, not a full package, but is there a single message?
   }
   if (bits->bits_per_row[row] < MYMESSAGE_BITLEN) {
       fprintf(stream, "Bad message - row of %d is less than %d bits, ", 
               bits->bits_per_row[row], MYMESSAGE_BITLEN);
       // No, not even a single message :-(
   }
   fprintf(stream, "\n");
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
