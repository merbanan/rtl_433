#!/bin/bash
# When files are provided as arguments, no messages are excluded
FILES="$@"
EXCLUDE_OPTS=""
{
  PRJ_DIR=$(realpath $(dirname $(realpath $0)))/
  if [ "$FILES" == "" ] ; then
    FILES=$(find "${PRJ_DIR}" -type d \( -path "${PRJ_DIR}build" \) -prune -o -name '*.c'|grep -v -E '((tests/.*|src/(am_analyze|abuf|baseband|bitbuffer|confparse|data|data_tag|decoder_util|fileformat|http_server|list|mongoose|optparse|output_file|output_influx|output_log|output_mqtt|output_trigger|output_udp|pulse_analyzer|pulse_data|pulse_detect|pulse_slicer|rfraw|rtl_433|r_api|r_util|samp_grab|sdr|term_ctl|util|write_sigrok|devices/(acurite|acurite_01185m|alecto|ambientweather_tx8300|ambientweather_wh31e|ambient_weather|ant_antplus|archos_tbh|atech_ws308|auriol_4ld5661|auriol_aft77b2|auriol_hg02832|badger_water|baldr_rain|blueline|blyss|brennenstuhl_rcs_2044|bresser_5in1|bresser_6in1|bresser_7in1|bt_rain|burnhardbbq|calibeur|cardin|cavius|ced7000|celsia_czc1|cmr113|companion_wtr001|cotech_36_7959|current_cost|danfoss|digitech_xc0324|directv|dsc|ecodhome|ecowitt|efergy_e2_classic|efergy_optical|efth800|elro_db286a|elv|emax|emos_e6016|emos_e6016_rain|enocean_erp1|ert_idm|esic_emt7110|esperanza_ews|fineoffset|fineoffset_wh1050|fineoffset_wh1080|fineoffset_wh31l|fineoffset_wh45|fineoffset_wn34|fineoffset_ws80|fineoffset_ws90|flex|flowis|fordremote|fs20|ft004b|funkbus|gasmate_ba1008|generic_motion|generic_remote|geo_minim|ge_coloreffects|govee|gt_tmbbq05|gt_wt_02|gt_wt_03|hcs200|hideki|holman_ws5029|hondaremote|honeywell_cm921|ht680|ikea_sparsnas|infactory|inkbird_ith20r|inovalley-kw9015b|insteon|interlogix|intertechno|jasco|kedsum|klimalogg|lacrosse|lacrossews|lacrosse_r1|lacrosse_tx141x|lacrosse_tx31u|lacrosse_tx34|lacrosse_tx35|lacrosse_ws7000|lightwave_rf|markisol|marlec_solar|maverick_et73x|maverick_xr30|megacode|m_bus|neptune_r900|newkaku|nexa|nice_flor_s|norgo|oil_smart|oil_standard|oil_watchman|oil_watchman_advanced|oregon_scientific|oregon_scientific_sl109h|oregon_scientific_v1|philips_aj7010|proflame2|prologue|proove|quhwa|radiohead_ask|rainpoint|regency_fan|revolt_nc5462|rftech|rojaflex|rubicson_48659|rubicson_pool_48942|s3318p|schraeder|scmplus|secplus_v1|secplus_v2|sharp_spc775|simplisafe|simplisafe_gen3|somfy_iohc|somfy_rts|springfield|srsmith_pool_srs_2c_tx|steelmate|telldus_ft0385r|tfa_14_1504_v2|tfa_30_3196|tfa_30_3221|tfa_drop_30.3233|tfa_marbella|tfa_twin_plus_30.3049|thermopro_tp11|thermopro_tx2|thermopro_tx2c|tpms_eezrv|tpms_jansite_solar|tpms_kia|tpms_pmv107j|tpms_porsche|tpms_renault_0435r|tpms_truck|tpms_tyreguard400|ts_ft002|vaillant_vrt340f|vauno_en8822c|wec2103|wg_pb12v1|ws2032|wssensor|x10_rf|yale_hsa)))\.c|build)' )
    EXCLUDE_OPTS="
    -globs \
      +boolint \
      +charint \
      -exportlocal \
      -retvalint \
      -noeffect \
      -fcnuse \
      -compdef \
      -usedef \
      +skipsysheaders \
      +relaxtypes \
      -shiftnegative \
      -nullinit \
      -unrecog \
      -globstate \
      -paramuse \
      -branchstate \
      +matchanyintegral \
      -mayaliasunique \
      -nullassign \
      -nullret \
      -formatconst \
      -nullderef \
      -nullpass \
      -initallelements \
      -fullinitblock \
      -bufferoverflowhigh \
      -redef \
      -type \
      -statictrans \
      -nestcomment \
      -observertrans \
      -immediatetrans \
      -mustfreefresh \
      -mustfreeonly \
      -boolops \
      -shiftimplementation
    "
  fi
  echo "Check $FILES"
  splint \
    -I"${PRJ_DIR}include" \
    +trytorecover \
    +forcehints \
    +posixstrictlib \
    -preproc \
    $EXCLUDE_OPTS \
    -D__unix__ \
    -Drestrict= \
    -D_MSC_VER=1300 \
    "-Ddata_array_t=int" \
    "-Ddata_t=int" \
    "-Duint16_t=unsigned short" \
    "-Dint16_t=short" \
    "-DUINT16_MAX=0xFFFFU" \
    "-DSSIZE_T=unsigned long" \
    $FILES
} |& tee splint.log

