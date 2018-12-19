#ifndef INCLUDE_RTL_433_DEVICES_H_
#define INCLUDE_RTL_433_DEVICES_H_

#define DEVICES \
    DECL(silvercrest) \
    DECL(rubicson) \
    DECL(prologue) \
    DECL(waveman) \
    DECL(template) \
    DECL(elv_em1000) \
    DECL(elv_ws2000) \
    DECL(lacrossetx) \
    DECL(template) \
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
    DECL(ambient_weather) \
    DECL(calibeur_RF104) \
    DECL(X10_RF) \
    DECL(DSC) \
    DECL(brennenstuhl_rcs_2044) \
    DECL(gt_wt_02) \
    DECL(danfoss_CFR) \
    DECL(template) \
    DECL(template) \
    DECL(chuango) \
    DECL(generic_remote) \
    DECL(tfa_twin_plus_303049) \
    DECL(fineoffset_wh1080) \
    DECL(wt450) \
    DECL(lacrossews) \
    DECL(esperanza_ews) \
    DECL(efergy_e2_classic) \
    DECL(kw9015b) \
    DECL(generic_temperature_sensor) \
    DECL(wg_pb12v1) \
    DECL(acurite_txr) \
    DECL(acurite_986) \
    DECL(hideki_ts04) \
    DECL(oil_watchman) \
    DECL(current_cost) \
    DECL(emontx) \
    DECL(ht680) \
    DECL(s3318p) \
    DECL(akhan_100F14) \
    DECL(quhwa) \
    DECL(oregon_scientific_v1) \
    DECL(proove) \
    DECL(bresser_3ch) \
    DECL(springfield) \
    DECL(oregon_scientific_sl109h) \
    DECL(acurite_606) \
    DECL(tfa_pool_thermometer) \
    DECL(kedsum) \
    DECL(blyss) \
    DECL(steelmate) \
    DECL(schraeder) \
    DECL(lightwave_rf) \
    DECL(elro_db286a) \
    DECL(efergy_optical) \
    DECL(hondaremote) \
    DECL(template) \
    DECL(template) \
    DECL(radiohead_ask) \
    DECL(kerui) \
    DECL(fineoffset_wh1050) \
    DECL(honeywell) \
    DECL(maverick_et73x) \
    DECL(rftech) \
    DECL(lacrosse_TX141TH_Bv2) \
    DECL(acurite_00275rm) \
    DECL(lacrosse_tx35) \
    DECL(lacrosse_tx29) \
    DECL(vaillant_vrt340f) \
    DECL(fineoffset_WH25) \
    DECL(fineoffset_WH0530) \
    DECL(ibis_beacon) \
    DECL(oil_standard) \
    DECL(tpms_citroen) \
    DECL(oil_standard_ask) \
    DECL(thermopro_tp11) \
    DECL(solight_te44) \
    DECL(smoke_gs558) \
    DECL(generic_motion) \
    DECL(tpms_toyota) \
    DECL(tpms_ford) \
    DECL(tpms_renault) \
    DECL(infactory) \
    DECL(ft004b) \
    DECL(fordremote) \
    DECL(philips) \
    DECL(schrader_EG53MA4) \
    DECL(nexa) \
    DECL(thermopro_tp12) \
    DECL(ge_coloreffects) \
    DECL(x10_sec) \
    DECL(interlogix) \
    DECL(dish_remote_6_3) \
    DECL(ss_sensor) \
    DECL(sensible_living) \
    DECL(m_bus_mode_c_t) \
    DECL(m_bus_mode_s) \
    DECL(m_bus_mode_r) \
    DECL(m_bus_mode_f) \
    DECL(wssensor) \
    DECL(wt1024) \
    DECL(tpms_pmv107j) \
    DECL(ttx201) \
    DECL(ambientweather_tx8300) \
    DECL(ambientweather_wh31e) \
    DECL(maverick_et73) \
    DECL(honeywell_wdb) \
    DECL(honeywell_wdb_fsk) \
    DECL(esa) \
    DECL(bt_rain) \
    DECL(bresser_5in1) \
    DECL(digitech_xc0324)

struct bitbuffer;
struct data;

typedef struct r_device {
    unsigned protocol_num; // fixed sequence number, assigned in main()

    /* information provided by each decoder */
    char *name;
    unsigned modulation;
    float short_width;
    float long_width;
    float reset_limit;
    float gap_limit;
    float sync_width;
    float tolerance;
    int (*decode_fn)(struct r_device *decoder, struct bitbuffer *bitbuffer);
    unsigned disabled;
    char **fields; // List of fields this decoder produces; required for CSV output. NULL-terminated.

    /* public for each decoder */
    int verbose;
    int verbose_bits;
    void (*output_fn)(struct r_device *decoder, struct data *data);

    /* private for flex decoder and output callback */
    void *decode_ctx;
    void *output_ctx;

    /* private pulse limits (converted to count of samples) */
    float f_short_width; // precision reciprocal for PCM
    float f_long_width;  // precision reciprocal for PCM
    int s_short_width;
    int s_long_width;
    int s_reset_limit;
    int s_gap_limit;
    int s_sync_width;
    int s_tolerance;
} r_device;

#define DECL(name) extern r_device name;
DEVICES
#undef DECL

#endif /* INCLUDE_RTL_433_DEVICES_H_ */
