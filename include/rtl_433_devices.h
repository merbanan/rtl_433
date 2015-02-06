#ifndef INCLUDE_RTL_433_DEVICES_H_
#define INCLUDE_RTL_433_DEVICES_H_

#define DEVICES \
		DECL(silvercrest) \
		DECL(rubicson) \
		DECL(prologue) \
		DECL(waveman) \
		DECL(steffen) \
		DECL(elv_em1000) \
		DECL(elv_ws2000) \
		DECL(lacrossetx) \
		DECL(acurite5n1) \
		DECL(acurite_rain_gauge) \
		DECL(acurite_th) \
		DECL(oregon_scientific) \
		DECL(mebus433) \
		DECL(intertechno) \
		DECL(newkaku) \
		DECL(alectov1) \
		DECL(cardin) \
		DECL(fineoffset_WH2) \
		DECL(nexus) \
		DECL(ambient_weather) 


#define BITBUF_COLS             34
#define BITBUF_ROWS             50

typedef struct {
	unsigned int id;
	char name[256];
	unsigned int modulation;
	unsigned int short_limit;
	unsigned int long_limit;
	unsigned int reset_limit;
	int (*json_callback)(uint8_t bits_buffer[BITBUF_ROWS][BITBUF_COLS],
			int16_t bits_per_row[BITBUF_ROWS]);
	unsigned int disabled;
} r_device;

#define DECL(name) extern r_device name;
DEVICES
#undef DECL

#endif /* INCLUDE_RTL_433_DEVICES_H_ */
