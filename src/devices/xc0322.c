#include "rtl_433.h"
#include "pulse_demod.h"


#include "bitbuffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


void bitbuffer_print_gl(const bitbuffer_t *bits) {
	int highest_indent, indent_this_col, indent_this_row, row_len;
	uint16_t col, row;

	/* Figure out the longest row of bit to get the highest_indent
	 */
	highest_indent = sizeof("[dd] {dd} ") - 1;
	for (row = indent_this_row = 0; row < bits->num_rows; ++row) {
		for (col = indent_this_col  = 0; col < (bits->bits_per_row[row]+7)/8; ++col) {
			indent_this_col += 2+1;
		}
		indent_this_row = indent_this_col;
		if (indent_this_row > highest_indent)
			highest_indent = indent_this_row;
	}
	// Label this "line" of output
  char label[7];
  if (fgets(label, 7, stdin) != NULL) fprintf(stderr, "%s, ", label);

	// fprintf(stderr, "nr[%d] ", bits->num_rows);
  
  // Filter out bad samples (too much noise, not enough sample)
  if ((bits->num_rows > 1) | (bits->bits_per_row[0] < 140)) {
		fprintf(stderr, "nr[%d] r[%02d] nc[%2d] ,", bits->num_rows, 0, bits->bits_per_row[0]);
    fprintf(stderr, "CORRUPTED data signal");
    return;
  }
  
  // Try some different format interpretations 
	float* fptr;
	double* dptr;
	
	for (row = 0; row < bits->num_rows; ++row) {
		fprintf(stderr, "nr[%d] r[%02d] nc[%2d] ,", bits->num_rows, row, bits->bits_per_row[row]);
		for (col = row_len = 0; col < (bits->bits_per_row[row]+7)/8; ++col) {
		  if ((col % 68) == 67) fprintf(stderr, " | \n"); // Chunk into useful bytes per line
		  /*
      fprintf(stderr, "(%02d)", col);
      */
			row_len += fprintf(stderr, "%02X ,", bits->bb[row][col]);
	  	//fprintf(stderr, " --> %04d ,", bits->bb[row][col]);
	  	/*
	  	fptr = &(bits->bb[row][col]);
	  	fprintf(stderr, " --> %04f ", *fptr);
	  	dptr = &(bits->bb[row][col]);
	  	fprintf(stderr, " --> %04f ", *dptr);
	  	//fprintf(stderr, " --> %04f ", 23.0);
	  	*/
      // Print binary values , 8 bits at a time
	  	
		 	for (uint16_t bit = 0; bit < 8; ++bit) {
			  if ((bit % 8) == 0)      // Add byte separators
			  	fprintf(stderr, "0b ");
		  	if (bits->bb[row][col] & (0x80 >> (bit % 8))) {
			  	fprintf(stderr, "1");
			  } else {
			  	fprintf(stderr, "0");
			  }
			  if ((bit % 8) == 7)      // Add byte separators
			  	fprintf(stderr, ",");
		  } 

			if ((col % 4) == 3) fprintf(stderr, " | ");
		  
	  	//fprintf(stderr, "\n");
		}
	}
}

static int xc0322_callback(bitbuffer_t *bitbuffer) {
    printf("\n\nBEGINNING XC0322\n\n");
    //bitbuffer_print(bitbuffer);
    bitbuffer_print_gl(bitbuffer);
    printf("\n\nENDING XC0322\n\n");

    return 0;
}

r_device xc0322 = {
    .name           = "XC0322",
    .modulation     = OOK_PULSE_PPM_RAW,
    .short_limit    = 190*4,
    .long_limit     = 300*4,
    .reset_limit    = 350*4,
    .json_callback  = &xc0322_callback,
};

