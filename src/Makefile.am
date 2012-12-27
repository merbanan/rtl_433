# This is _NOT_ the library release version, it's an API version.
# Please read Chapter 6 "Library interface versions" of the libtool documentation before making any modification
LIBVERSION=0:0:0

INCLUDES = $(all_includes) -I$(top_srcdir)/include
AM_CFLAGS = ${CFLAGS} -fPIC ${SYMBOL_VISIBILITY}

lib_LTLIBRARIES = librtlsdr.la

librtlsdr_la_SOURCES = librtlsdr.c tuner_e4k.c tuner_fc0012.c tuner_fc0013.c tuner_fc2580.c tuner_r820t.c
librtlsdr_la_LDFLAGS = -version-info $(LIBVERSION)

bin_PROGRAMS         = rtl_sdr rtl_tcp rtl_test rtl_fm rtl_eeprom rtl_adsb

rtl_sdr_SOURCES      = rtl_sdr.c
rtl_sdr_LDADD        = librtlsdr.la

rtl_tcp_SOURCES      = rtl_tcp.c
rtl_tcp_LDADD        = librtlsdr.la

rtl_test_SOURCES      = rtl_test.c
rtl_test_LDADD        = librtlsdr.la $(LIBM)

rtl_fm_SOURCES      = rtl_fm.c
rtl_fm_LDADD        = librtlsdr.la $(LIBM)

rtl_eeprom_SOURCES      = rtl_eeprom.c
rtl_eeprom_LDADD        = librtlsdr.la $(LIBM)

rtl_adsb_SOURCES      = rtl_adsb.c
rtl_adsb_LDADD        = librtlsdr.la $(LIBM)
