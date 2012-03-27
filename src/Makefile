LDFLAGS=`pkg-config --libs libusb-1.0`
CFLAGS=-Wall -g -O0 `pkg-config --cflags libusb-1.0`

all: rtl-sdr

rtl-sdr: main.o rtl-sdr.o tuner_e4000.o tuner_fc0013.c
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	@rm -f rtl-sdr *.o
