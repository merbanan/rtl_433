# JSON Data fields

## Message Data
These fields are the primary data fields containing the most basic message data and used to identify the specific device. 
For some devices these are the *only* fields contained in the message, as the message itself consitutes an event from this
particular device model.

* **time** (string) (Required)
  * Time stamp. String containing date and time of when the message was received. Format is dependent on
    current locale unless the `-U` command line argument is used.

* **model** (string) (Required)
  * Device model. Human readable string describing the model consisting of manufacturer name 
    and manufacturers model description

* **id** (string) (Optional)
  * Device identification. Used to differentiate between devices of same *model*. 
    Depending of device model it may be a non-volatile value programmed into the device, 
    a volatile value that changes at each power on (or battery change), or a value configurable by
    user e.g. by switch or jumpers. No assumptions should be made to the id value other than it contains
    a unique sequence of alphanumeric characters.

* **channel** (string) (Optional)
  * Secondary device identification. For devices with more than one identification value 
    (e.g. both an internal value and a switch).

* **mic** (string) (Optional)
  * Message integrity check. String describing the method used for ensuring the data integrity
    of the message. Protocol decoders for devices without mic will be disabled by default as 
    they are prone to excessive false positives.
  * Possible values:
    * "CRC" - Cyclic Redundancy Check.
    * "CHECKSUM" - Accumulated sum of data.
    * "PARITY" - Parity bit (odd, even, multiple)

## Common Device Data
Various data fields, which are common across devices of different types 

* **battery** (string) (Optional)
  * Battery status indication
  * Possible values: "OK", "LOW"

* **battery_V** (double) (Optional)
  * Battery level in Volts. Should always be suppleanted by *battery* status indication.

* **battery_level** (double) (Optional)
  * Battery level in %. Should always be suppleanted by *battery* status indication.

## Sensor Data
Due to the large variance in sensor types this list of common values is non-exhaustive. However 
insofar possible the sensors should use these data types even if conversion from native low-level
sensor data is needed.

* **temperature_C** (double) (Optional)
  * Temperature from a temperature sensor in degrees Celcius.
  
* **setpoint_C** (double) (Optional)
  * Thermal set point of a thermostat device in degrees Celcius.
  
* **humidity** (double) (Optional)
  * Humidity from a hygrometer sensor in % relative humidity
  
* **wind_dir_deg** (int) (Optional)
  * Wind direction from wind sensor in compass direction degrees.
  
* **wind_speed_ms** (double) (Optional)
  * Average wind speed from wind sensor in m/s. Averaging time is sensor dependant.

* **gust_speed_ms** (double) (Optional)
  * Gust wind speed from wind sensor in m/s. 

* **rainfall_mm** (double) (Optional)
  * Rainfall from rain sensor in mm since last reset. Reset method is device dependant.
  
* **pressure_hPa** (double) (Optional)
  * Air pressure from barometer or Tire Pressure Monitor in hPa (millibar)
  
