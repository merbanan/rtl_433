INCLUDES = $(all_includes) -I$(top_srcdir)/include
AM_CFLAGS = ${CFLAGS} -fPIC ${SYMBOL_VISIBILITY}

bin_PROGRAMS         = rtl_433

rtl_433_SOURCES      = baseband.c \
                       bitbuffer.c \
                       data.c \
                       pulse_demod.c \
                       pulse_detect.c \
                       rtl_433.c \
                       util.c \
                       devices/acurite.c \
                       devices/alecto.c \
                       devices/ambient_weather.c \
                       devices/blyss.c \
                       devices/brennstuhl_rcs_2044.c \
                       devices/calibeur.c \
                       devices/cardin.c \
                       devices/current_cost.c \
                       devices/chuango.c \
                       devices/danfoss.c \
                       devices/dsc.c \
                       devices/ec3k.c \
                       devices/efergy_e2_classic.c \
                       devices/efergy_optical.c \
                       devices/elro_db286a.c \
                       devices/elv.c \
                       devices/emontx.c \
                       devices/esperanza_ews.c \
                       devices/fineoffset.c \
                       devices/fineoffset_wh1080.c \
                       devices/generic_remote.c \
                       devices/generic_temperature_sensor.c \
                       devices/gt_wt_02.c \
                       devices/hideki.c \
                       devices/hondaremote.c \
                       devices/ht680.c \
                       devices/inovalley-kw9015b.c \
                       devices/intertechno.c \
                       devices/kedsum.c \
                       devices/lacrosse.c \
                       devices/lacrossews.c \
                       devices/lightwave_rf.c \
                       devices/mebus.c \
                       devices/newkaku.c \
                       devices/nexus.c \
                       devices/oil_watchman.c \
                       devices/oregon_scientific.c \
                       devices/oregon_scientific_v1.c \
                       devices/prologue.c \
                       devices/quhwa.c \
                       devices/rubicson.c \
                       devices/silvercrest.c \
                       devices/springfield.c \
                       devices/steffen.c \
                       devices/tfa_twin_plus_30.3049.c \
                       devices/tfa_pool_thermometer.c \
                       devices/valeo.c \
                       devices/waveman.c \
                       devices/wt450.c \
                       devices/x10_rf.c \
                       devices/s3318p.c \
                       devices/akhan_100F14.c \
                       devices/proove.c \
                       devices/bresser_3ch.c \
                       devices/oregon_scientific_sl109h.c \
                       devices/steelmate.c \
                       devices/schraeder.c \
                       devices/new_template.c \
                       devices/radiohead_ask.c

rtl_433_LDADD        = $(LIBRTLSDR) $(LIBM)
