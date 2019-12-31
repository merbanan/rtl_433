#ifndef INCLUDE_GPS_RTL_H_
#define INCLUDE_GPS_RTL_H_
#include "gps.h"
#include "data.h"
#include "list.h"
#include <time.h>

struct rtl_gps {
    struct gps_data_t data;
    char* host;
    char* port;

    time_t last_retry;
    int enabled;
    int connected;
};
void rtl_gps_csv_fields(list_t* list);
int rtl_gps_open(struct rtl_gps* p);
void rtl_gps_update(struct rtl_gps* p);
void rtl_gps_close(struct rtl_gps* p);
void rtl_gps_data_append(struct rtl_gps* p, data_t* data);
#endif
