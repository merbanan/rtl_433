INCLUDES = $(all_includes) -I$(top_srcdir)/include
AM_CFLAGS = ${CFLAGS} -fPIC ${SYMBOL_VISIBILITY}

bin_PROGRAMS         = rtl_433

rtl_433_SOURCES      = rtl_433.c \
                       devices/acurite.c \
                       devices/intertechno.c \
                       devices/newkaku.c \
                       devices/rubicson.c \
                       devices/waveman.c \
                       devices/alecto.c \
                       devices/lacrosse.c \
                       devices/oregon_scientific.c \
                       devices/silvercrest.c \
                       devices/elv.c \
                       devices/mebus.c \
                       devices/prologue.c \
                       devices/steffen.c \
		       devices/cardin.c \
                       devices/fineoffset.c \
		       devices/nexus.c \
		       devices/ambient_weather.c 
rtl_433_LDADD        = $(LIBRTLSDR) $(LIBM)
