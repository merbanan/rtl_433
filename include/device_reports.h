#ifndef INCLUDE_DEVICE_REPORTS_H_
#define INCLUDE_DEVICE_REPORTS_H_

static char* BATTERY_STATUS[] = { "LOW", "OK" };

typedef struct {
	char*	name;			// The model name of the device
	char*	id;				// A unique ID for this device
	int 	battery_status; // Index for the battery status array
	int		channel;		// Channel setting of the device
} r_device_details_t;

static char* DEVICE_FIELDS[] = { "Name", "ID", "Battery Status", "Channel" };

// Retrieve an initialized r_device_details struct pointer
// The user only needs to set the values the device actually supports
extern r_device_details_t* get_device_details();

// ------------- WEATHER DEVICES ---------------- //

static char* COMFORT_LEVELS[] = { "NORMAL", "COMFORTABLE", "DRY", "HUMID" };

static char* FORECASTS[] = { "CLOUDY", "RAINY", "PARTLY CLOUDY", "SUNNY" };

typedef struct {
	float outdoor_temperature;	// Outdoor temperature in degree Celsius
	float outdoor_humidity;		// Outdoor humidity in %
	float wind_speed;			// Wind speed in m/s
	float wind_direction;		// Degrees of a compass
	float rainfall;				// Rainfall in mm
	float barometric_pressure;	// Barometric pressure in hPa (mbar)
	float indoor_temperature;	// Indoor temperature in degree Celcius
	float indoor_humidity;		// Indoor humidity in %
	int   comfort_level;		// Comfort level call (index into the COMFORT_LEVEL array)
	int   forecast;				// Forecast (index into the FORCAST array)
} weather_report_t;

static char* WEATHER_REPORT_FIELDS[] = { "Outdoor Temp. [C]",
		"Outdoor Humidity [%]",
		"Wind Speed [m/s]",
		"Wind Direction [deg]",
		"Rainfall [mm]",
		"Barometric Pressure [hPa]",
		"Indoor Temp. [C]",
		"Indoor Humidity [%]",
		"Comfort Level",
		"Forecast" };

// Retrieve an initialized weather_report struct pointer
// The user only needs to set the values the device actually supports
extern weather_report_t* get_weather_report();

extern void print_weather_report(r_device_details_t *device_details,
		weather_report_t *report);

// routines used to convert from commonly used units to standard units
extern float fahrenheit_to_celcius(float fh);
extern float kph_to_mps(float kph);
extern float mph_to_mps(float mph);
extern float kt_to_mps(float kt);
extern float inhg_to_hpa(float inhg);
extern float in_to_mm(float in);

// conversion of standard units to commonly used units
extern float celcius_to_fahrenheit(float c);
extern float mps_to_kph(float mps);
extern float mps_to_mph(float mps);
extern float mps_to_kt(float mps);
extern float hpa_to_inhg(float hpa);
extern float mm_to_in(float mm);

#endif /* INCLUDE_DEVICE_REPORTS_H_ */
