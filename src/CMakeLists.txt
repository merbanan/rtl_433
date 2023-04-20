########################################################################
# Build libraries and executables
########################################################################
# commodity libraries
# consider -fvisibility=hidden
# Proper object library type was only introduced with CMake 2.8.8
add_library(r_433 STATIC
    abuf.c
    am_analyze.c
    baseband.c
    bitbuffer.c
    compat_paths.c
    compat_time.c
    confparse.c
    data.c
    data_tag.c
    decoder_util.c
    fileformat.c
    http_server.c
    jsmn.c
    list.c
    logger.c
    mongoose.c
    optparse.c
    output_file.c
    output_influx.c
    output_log.c
    output_mqtt.c
    output_rtltcp.c
    output_trigger.c
    output_udp.c
    pulse_analyzer.c
    pulse_data.c
    pulse_detect.c
    pulse_detect_fsk.c
    pulse_slicer.c
    r_api.c
    r_util.c
    raw_output.c
    rfraw.c
    samp_grab.c
    sdr.c
    term_ctl.c
    util.c
    write_sigrok.c
    devices/abmt.c
    devices/acurite.c
    devices/acurite_01185m.c
    devices/akhan_100F14.c
    devices/alecto.c
    devices/ambient_weather.c
    devices/ambientweather_tx8300.c
    devices/ambientweather_wh31e.c
    devices/ant_antplus.c
    devices/archos_tbh.c
    devices/arexx_ml.c
    devices/atech_ws308.c
    devices/auriol_4ld5661.c
    devices/auriol_aft77b2.c
    devices/auriol_afw2a1.c
    devices/auriol_ahfl.c
    devices/auriol_hg02832.c
    devices/badger_water.c
    devices/baldr_rain.c
    devices/blueline.c
    devices/blyss.c
    devices/brennenstuhl_rcs_2044.c
    devices/bresser_3ch.c
    devices/bresser_5in1.c
    devices/bresser_6in1.c
    devices/bresser_7in1.c
    devices/bt_rain.c
    devices/burnhardbbq.c
    devices/calibeur.c
    devices/cardin.c
    devices/cavius.c
    devices/ced7000.c
    devices/celsia_czc1.c
    devices/chuango.c
    devices/cmr113.c
    devices/companion_wtr001.c
    devices/cotech_36_7959.c
    devices/current_cost.c
    devices/danfoss.c
    devices/digitech_xc0324.c
    devices/directv.c
    devices/dish_remote_6_3.c
    devices/dsc.c
    devices/ecodhome.c
    devices/ecowitt.c
    devices/efergy_e2_classic.c
    devices/efergy_optical.c
    devices/efth800.c
    devices/elro_db286a.c
    devices/elv.c
    devices/emax.c
    devices/emontx.c
    devices/emos_e6016.c
    devices/emos_e6016_rain.c
    devices/enocean_erp1.c
    devices/ert_idm.c
    devices/ert_scm.c
    devices/esa.c
    devices/esic_emt7110.c
    devices/esperanza_ews.c
    devices/eurochron.c
    devices/fineoffset.c
    devices/fineoffset_wh1050.c
    devices/fineoffset_wh1080.c
    devices/fineoffset_wh31l.c
    devices/fineoffset_wh45.c
    devices/fineoffset_wn34.c
    devices/fineoffset_ws80.c
    devices/fineoffset_ws90.c
    devices/flex.c
    devices/flowis.c
    devices/fordremote.c
    devices/fs20.c
    devices/ft004b.c
    devices/funkbus.c
    devices/gasmate_ba1008.c
    devices/ge_coloreffects.c
    devices/generic_motion.c
    devices/generic_remote.c
    devices/generic_temperature_sensor.c
    devices/geo_minim.c
    devices/govee.c
    devices/gt_tmbbq05.c
    devices/gt_wt_02.c
    devices/gt_wt_03.c
    devices/hcs200.c
    devices/hideki.c
    devices/holman_ws5029.c
    devices/hondaremote.c
    devices/honeywell.c
    devices/honeywell_cm921.c
    devices/honeywell_wdb.c
    devices/ht680.c
    devices/ibis_beacon.c
    devices/ikea_sparsnas.c
    devices/infactory.c
    devices/inkbird_ith20r.c
    devices/inovalley-kw9015b.c
    devices/insteon.c
    devices/interlogix.c
    devices/intertechno.c
    devices/jasco.c
    devices/kedsum.c
    devices/kerui.c
    devices/klimalogg.c
    devices/lacrosse.c
    devices/lacrosse_breezepro.c
    devices/lacrosse_r1.c
    devices/lacrosse_th3.c
    devices/lacrosse_tx141x.c
    devices/lacrosse_tx31u.c
    devices/lacrosse_tx34.c
    devices/lacrosse_tx35.c
    devices/lacrosse_wr1.c
    devices/lacrosse_ws7000.c
    devices/lacrossews.c
    devices/lightwave_rf.c
    devices/m_bus.c
    devices/markisol.c
    devices/marlec_solar.c
    devices/maverick_et73.c
    devices/maverick_et73x.c
    devices/maverick_xr30.c
    devices/mebus.c
    devices/megacode.c
    devices/missil_ml0757.c
    devices/neptune_r900.c
    devices/new_template.c
    devices/newkaku.c
    devices/nexa.c
    devices/nexus.c
    devices/nice_flor_s.c
    devices/norgo.c
    devices/oil_smart.c
    devices/oil_standard.c
    devices/oil_watchman.c
    devices/oil_watchman_advanced.c
    devices/opus_xt300.c
    devices/oregon_scientific.c
    devices/oregon_scientific_sl109h.c
    devices/oregon_scientific_v1.c
    devices/philips_aj3650.c
    devices/philips_aj7010.c
    devices/proflame2.c
    devices/prologue.c
    devices/proove.c
    devices/quhwa.c
    devices/radiohead_ask.c
    devices/rainpoint.c
    devices/regency_fan.c
    devices/revolt_nc5462.c
    devices/rftech.c
    devices/rojaflex.c
    devices/rubicson.c
    devices/rubicson_48659.c
    devices/rubicson_pool_48942.c
    devices/s3318p.c
    devices/schraeder.c
    devices/scmplus.c
    devices/secplus_v1.c
    devices/secplus_v2.c
    devices/sharp_spc775.c
    devices/silvercrest.c
    devices/simplisafe.c
    devices/simplisafe_gen3.c
    devices/smoke_gs558.c
    devices/solight_te44.c
    devices/somfy_iohc.c
    devices/somfy_rts.c
    devices/springfield.c
    devices/srsmith_pool_srs_2c_tx.c
    devices/steelmate.c
    devices/telldus_ft0385r.c
    devices/tfa_14_1504_v2.c
    devices/tfa_30_3196.c
    devices/tfa_30_3221.c
    devices/tfa_drop_30.3233.c
    devices/tfa_marbella.c
    devices/tfa_pool_thermometer.c
    devices/tfa_twin_plus_30.3049.c
    devices/thermopro_tp11.c
    devices/thermopro_tp12.c
    devices/thermopro_tx2.c
    devices/tpms_abarth124.c
    devices/tpms_ave.c
    devices/tpms_citroen.c
    devices/tpms_eezrv.c
    devices/tpms_elantra2012.c
    devices/tpms_ford.c
    devices/tpms_hyundai_vdo.c
    devices/tpms_jansite.c
    devices/tpms_jansite_solar.c
    devices/tpms_kia.c
    devices/tpms_pmv107j.c
    devices/tpms_porsche.c
    devices/tpms_renault.c
    devices/tpms_renault_0435r.c
    devices/tpms_toyota.c
    devices/tpms_truck.c
    devices/tpms_tyreguard400.c
    devices/ts_ft002.c
    devices/ttx201.c
    devices/vaillant_vrt340f.c
    devices/vauno_en8822c.c
    devices/visonic_powercode.c
    devices/waveman.c
    devices/wec2103.c
    devices/wg_pb12v1.c
    devices/ws2032.c
    devices/wssensor.c
    devices/wt0124.c
    devices/wt450.c
    devices/x10_rf.c
    devices/x10_sec.c
    devices/yale_hsa.c
)

if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
    # untouched upstream code, disable all warnings
    set_source_files_properties(mongoose.c PROPERTIES COMPILE_FLAGS "-w")
endif()

add_executable(rtl_433 rtl_433.c)
target_link_libraries(rtl_433 r_433)

# target_compile_definitions was only added with CMake 2.8.11
if(THREADS_HAVE_PTHREAD_ARG)
    set_target_properties(r_433 PROPERTIES COMPILE_OPTIONS "-pthread")
    set_target_properties(r_433 PROPERTIES INTERFACE_COMPILE_OPTIONS "-pthread")
endif()
if(CMAKE_THREAD_LIBS_INIT)
    target_link_libraries(rtl_433 "${CMAKE_THREAD_LIBS_INIT}")
endif()

if(MSVC)
    # needs CMake 3.1 but Windows builds should have that
    target_sources(rtl_433 PRIVATE getopt/getopt.c)
endif()

add_library(data data.c abuf.c)
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
set_target_properties(rtl_433 r_433 PROPERTIES C_STANDARD 99)

########################################################################
# Install built library files & utilities
########################################################################
install(TARGETS ${INSTALL_TARGETS}
    RUNTIME DESTINATION bin              # .dll file
)
