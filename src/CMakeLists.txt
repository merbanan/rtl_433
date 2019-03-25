########################################################################
# Build libraries and executables
########################################################################
add_executable(rtl_433
	abuf.c
	am_analyze.c
	baseband.c
	bitbuffer.c
	compat_paths.c
	compat_time.c
	confparse.c
	data.c
	decoder_util.c
	fileformat.c
	list.c
	mongoose.c
	optparse.c
	pulse_demod.c
	pulse_detect.c
	r_util.c
	rtl_433.c
	samp_grab.c
	sdr.c
	term_ctl.c
	util.c
	devices/acurite.c
	devices/akhan_100F14.c
	devices/alecto.c
	devices/ambient_weather.c
	devices/ambientweather_tx8300.c
	devices/ambientweather_wh31e.c
	devices/blyss.c
	devices/brennenstuhl_rcs_2044.c
	devices/bresser_3ch.c
	devices/bresser_5in1.c
	devices/bt_rain.c
	devices/calibeur.c
	devices/cardin.c
	devices/chuango.c
	devices/current_cost.c
	devices/danfoss.c
	devices/digitech_xc0324.c
	devices/dish_remote_6_3.c
	devices/dsc.c
	devices/efergy_e2_classic.c
	devices/efergy_optical.c
	devices/elro_db286a.c
	devices/elv.c
	devices/emontx.c
	devices/esa.c
	devices/esperanza_ews.c
	devices/fineoffset.c
	devices/fineoffset_wh1050.c
	devices/fineoffset_wh1080.c
	devices/flex.c
	devices/fordremote.c
	devices/fs20.c
	devices/ft004b.c
	devices/ge_coloreffects.c
	devices/generic_motion.c
	devices/generic_remote.c
	devices/generic_temperature_sensor.c
	devices/gt_wt_02.c
	devices/hideki.c
	devices/hondaremote.c
	devices/honeywell.c
	devices/honeywell_wdb.c
	devices/ht680.c
	devices/ibis_beacon.c
	devices/infactory.c
	devices/inovalley-kw9015b.c
	devices/interlogix.c
	devices/intertechno.c
	devices/kedsum.c
	devices/kerui.c
	devices/lacrosse.c
	devices/lacrosse_TX141TH_Bv2.c
	devices/lacrosse_tx35.c
	devices/lacrossews.c
	devices/lightwave_rf.c
	devices/m_bus.c
	devices/maverick_et73.c
	devices/maverick_et73x.c
	devices/mebus.c
	devices/new_template.c
	devices/newkaku.c
	devices/nexa.c
	devices/nexus.c
	devices/oil_standard.c
	devices/oil_watchman.c
	devices/opus_xt300.c
	devices/oregon_scientific.c
	devices/oregon_scientific_sl109h.c
	devices/oregon_scientific_v1.c
	devices/philips.c
	devices/prologue.c
	devices/proove.c
	devices/quhwa.c
	devices/radiohead_ask.c
	devices/rftech.c
	devices/rubicson.c
	devices/s3318p.c
	devices/schraeder.c
	devices/silvercrest.c
	devices/simplisafe.c
	devices/smoke_gs558.c
	devices/solight_te44.c
	devices/springfield.c
	devices/steelmate.c
	devices/tfa_pool_thermometer.c
	devices/tfa_twin_plus_30.3049.c
	devices/thermopro_tp11.c
	devices/thermopro_tp12.c
	devices/tpms_citroen.c
	devices/tpms_ford.c
	devices/tpms_jansite.c
	devices/tpms_pmv107j.c
	devices/tpms_renault.c
	devices/tpms_toyota.c
	devices/ttx201.c
	devices/vaillant_vrt340f.c
	devices/waveman.c
	devices/wg_pb12v1.c
	devices/wssensor.c
	devices/wt0124.c
	devices/wt450.c
	devices/x10_rf.c
	devices/x10_sec.c
)

if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
	SET_SOURCE_FILES_PROPERTIES(mongoose.c PROPERTIES COMPILE_FLAGS "-Wno-format")
endif()
if("${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
	SET_SOURCE_FILES_PROPERTIES(mongoose.c PROPERTIES COMPILE_FLAGS "-Wno-format-pedantic -Wno-large-by-value-copy")
endif()

if(MSVC)
	# needs CMake 3.1 but Windows builds should have that
	target_sources(rtl_433 PRIVATE getopt/getopt.c)
endif()

add_library(data data.c abuf.c term_ctl.c mongoose.c)
target_link_libraries(data ${NET_LIBRARIES})

target_link_libraries(rtl_433
	${SDR_LIBRARIES}
	${NET_LIBRARIES}
)

set(INSTALL_TARGETS rtl_433)
if(UNIX)
target_link_libraries(rtl_433 m)
endif()

# Explicitly say that we want C99
set_property(TARGET rtl_433 PROPERTY C_STANDARD 99)

########################################################################
# Install built library files & utilities
########################################################################
install(TARGETS ${INSTALL_TARGETS}
    RUNTIME DESTINATION bin              # .dll file
)
