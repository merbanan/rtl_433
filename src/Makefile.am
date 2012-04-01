# This is _NOT_ the library release version, it's an API version.
# Please read Chapter 6 "Library interface versions" of the libtool documentation before making any modification
LIBVERSION=0:0:0

INCLUDES = $(all_includes) -I$(top_srcdir)/include
AM_CFLAGS = -fPIC -Wall

lib_LTLIBRARIES = librtlsdr.la

librtlsdr_la_SOURCES = rtl-sdr.c tuner_e4000.c tuner_fc0012.c tuner_fc0013.c tuner_fc2580.c
librtlsdr_la_LDFALGS = -version-info $(LIBVERSION)

bin_PROGRAMS         = rtl_sdr
rtl_sdr_SOURCES      = main.c
rtl_sdr_LDADD        = librtlsdr.la
