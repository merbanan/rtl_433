#include "rtl_433.h"

/*
 * Cardin S466-TX2 generic garage door remote control on 27.195 Mhz
 * Use with "-f 27195000"
 * May be usefull for other Cardin product too
 */

static int cardin_callback(uint8_t bb[BITBUF_ROWS][BITBUF_COLS], int16_t bits_per_row[BITBUF_ROWS]) {
	int i, j, k;
	unsigned char dip[10] = {'-','-','-','-','-','-','-','-','-', '\0'};

	fprintf(stderr, "------------------------------\n");
	fprintf(stderr, "protocol       = Cardin S466\n");
	fprintf(stderr, "message        = ");
	for (i=0 ; i<3 ; i++) {
		for (k = 7; k >= 0; k--) {
			if (bb[0][i] & 1 << k)
				fprintf(stderr, "1");
			else
				fprintf(stderr, "0");
		}
		fprintf(stderr, " ");
	}
	if(bb[0][2] == 0) {
		fprintf(stderr, "\npartial message - abording\n");
		return 0;
	}
	fprintf(stderr, "\n\n");

	// Dip 1
	if(bb[0][0] & 8) {
		dip[0]='o';
		if(bb[0][1] & 8)
			dip[0]='+';
	}
	// Dip 2
	if(bb[0][0] & 16) {
		dip[1]='o';
		if(bb[0][1] & 16)
			dip[1]='+';
	}
	// Dip 3
	if(bb[0][0] & 32) {
		dip[2]='o';
		if(bb[0][1] & 32)
			dip[2]='+';
	}
	// Dip 4
	if(bb[0][0] & 64) {
		dip[3]='o';
		if(bb[0][1] & 64)
			dip[3]='+';
	}
	// Dip 5
	if(bb[0][0] & 128) {
		dip[4]='o';
		if(bb[0][1] & 128)
			dip[4]='+';
	}
	// Dip 6
	if(bb[0][2] & 128) {
		dip[5]='o';
		if(bb[0][2] & 64)
			dip[5]='+';
	}
	// Dip 7
	if(bb[0][0] & 1) {
		dip[6]='o';
		if(bb[0][1] & 1)
			dip[6]='+';
	}
	// Dip 8
	if(bb[0][0] & 2) {
		dip[7]='o';
		if(bb[0][1] & 2)
			dip[7]='+';
	}
	// Dip 9
	if(bb[0][0] & 4) {
		dip[8]='o';
		if(bb[0][1] & 4)
			dip[8]='+';
	}

	fprintf(stderr, "                 123456789\n");
	fprintf(stderr, "dipswitch      = %s\n\n",dip);

        fprintf(stderr, "                 -->ON\n");
	fprintf(stderr, "right button   = ");
	if((bb[0][2] & 3) == 3) {
		fprintf(stderr,                  "2 --o (this is right button)\n");
		fprintf(stderr, "                 1 --o\n");
	}
	if((bb[0][2] & 9) == 9) {
		fprintf(stderr,                  "2 --o (this is right button)\n");
		fprintf(stderr, "                 1 o--\n");
	}
	if((bb[0][2] & 12) == 12) {
		fprintf(stderr,                  "2 o-- (this is left button or two buttons on same channel)\n");
		fprintf(stderr, "                 1 o--\n");
	}
	if((bb[0][2] & 6) == 6) {
		fprintf(stderr,                  "2 o-- (this is right button)\n");
		fprintf(stderr, "                 1 --o\n");
	}

	return 1;
}

r_device cardin = {
    /* .id             = */ 12,
    /* .name           = */ "Cardin S466-TX2",
    /* .modulation     = */ OOK_PWM_D,
    /* .short_limit    = */ 303,
    /* .long_limit     = */ 400,
    /* .reset_limit    = */ 8000,
    /* .json_callback  = */ &cardin_callback,
    /* .disabled       = */ 0,
    /* .json_callback  = */ //&debug_callback,
};
