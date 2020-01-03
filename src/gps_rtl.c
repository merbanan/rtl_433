#ifdef GPSD
#include <gps_rtl.h>
#include <errno.h>
#include <stdio.h>

#define LAT_SHORT_SHORT "lat"
#define LONG_SHORT_SHORT "long"
#define ALTITUDE_SHORT "alt"
#define GPS_STATUS_SHORT "gps_status"
#define GPS_MODE_SHORT "gps_mode"

int rtl_gps_open__(struct rtl_gps * p);
void rtl_gps_double_append( data_t *data, char* shortName, char* fmt, char* longName, double value);
void rtl_gps_csv_fields(list_t* list) {
    list_push(list, GPS_STATUS_SHORT);
    list_push(list, GPS_MODE_SHORT);
    list_push(list, LAT_SHORT_SHORT);
    list_push(list, LONG_SHORT_SHORT);
    list_push(list, ALTITUDE_SHORT);
}
int rtl_gps_open(struct rtl_gps * p) {
    int ret;
    ret = rtl_gps_open__(p);
    if (!ret) {
        fprintf(stderr, "No gpsd running or network error: %d, %s\n", errno, gps_errstr(errno));
        fprintf(stderr, "Attempted Connection to: %s, %s\n", p->host, p->port);
        ret = false;
    }
    return ret;
}

void rtl_gps_update(struct rtl_gps * p) {
    if(p->enabled) {
        if(p->connected) {
            int r = gps_read(&p->data);
            if(r < 0 ) {
                if(p->connected) {
                    fprintf(stderr, "GPSd Connection Lost\n");
                    time(&p->last_retry);
                }
                rtl_gps_close(p);
                p->data.status = 0;
            }
        } else {
            time_t now;
            time(&now);
            double timeDiff = difftime(now, p->last_retry);
            if(timeDiff >= 1.0) {
                //fprintf(stderr, "GPS Retry: %f\n", timeDiff);
                p->last_retry = now;
                int reconnect = rtl_gps_open__(p);
                if(reconnect) {
                    fprintf(stderr, "GPSd Connection Restored\n");
                }
            }
        }
    }
}

void rtl_gps_close(struct rtl_gps * p) {
    if(p->enabled && p->connected) {
        gps_close(&p->data);
        p->connected = false;
    }
}

/*
 * By default the double serialization functions (print_json_double, print_csv_double) force a %.3f format
 * If we update these to take the format parameter, many of the tests will fail as the current device code
 * produces formats that are not valid JSON. For instance the below code will not produce valid JSON as the
 * result is an unquoted string
 *
 *    data_append(data,
 *                 "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, tempc,
 *            NULL);
 *
 *
 */
//
void rtl_gps_double_append( data_t *data, char* shortName, char* longName, char* fmt, double value) {
    char buffer[256];
    snprintf(buffer, 256, fmt, value);
    data_append(data,
            shortName,longName,DATA_STRING,buffer,
            NULL);

}
void rtl_gps_data_append(struct rtl_gps * p, data_t *data) {
    if(p->enabled) {
        data_append(data, GPS_STATUS_SHORT,  "GpsFixStatus",        DATA_FORMAT, "%d", DATA_INT, p->data.status , NULL);
        if(p->connected && p->data.status != 0) {
            data_append(data, GPS_MODE_SHORT,  "GpsMode",        DATA_FORMAT, "%d", DATA_INT, p->data.fix.mode , NULL);
            if(p->data.fix.mode >= MODE_2D) {
                rtl_gps_double_append(data, LAT_SHORT_SHORT, "Latitude", "%.7f", p->data.fix.latitude);
                rtl_gps_double_append(data, LONG_SHORT_SHORT, "Longitude", "%.7f", p->data.fix.longitude);
            }
            if(p->data.fix.mode >= MODE_3D) {
                rtl_gps_double_append(data, ALTITUDE_SHORT, "Altitude", "%.3f", p->data.fix.altitude);
            }
        }
    }
}
int rtl_gps_open__(struct rtl_gps * p) {
    int openStatus;
    openStatus = gps_open(p->host, p->port, &p->data);
    if(openStatus == 0) {
        unsigned int flags = 0;
        flags |= WATCH_ENABLE;
        flags |= WATCH_JSON;
        gps_stream(&p->data, flags, NULL); //Third argument is optional.
        p->connected = 1;
    }
    return openStatus == 0;
}
#endif
