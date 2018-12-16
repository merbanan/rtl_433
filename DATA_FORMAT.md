# JSON Data fields

## Message Data
These fields are the primary data fields containing the most basic message data and used to identify the specific device. 
For some devices these are the *only* fields contained in the message, as the message itself consitutes an event from this
particular device model.

* **time** (string) (Required)
  * Time stamp. String containing date and time of when the message was received. Format is dependent on
    current locale unless the `-U` command line argument is used.

* **model** (string) (Required)
  * Device model. Human readable string concisely describing the device by manufacturer name 
    and manufacturers model designation according to the following syntax: `"<Manufacturer>, <Model>"`. 
  * It is common for devices to be sold under different brands, however the Original Equipment Manufacturer name
    shall be used, where possible to identify. 
  * Avoid redundant word like "sensor", "wireless" etc. unless it is part of the manufacturers model designation. 
  * Avoid adding device type designations like "Switch", "Temperature", "Thermostat", "Weather Station" etc. Device type can 
    be inferred from the data content. 
  * Avoid the special characters: `"/&$*#+[]()"`.
  * Length of *model* string should be less than 32 characters.

* **id** (string) (Optional)
  * Device identification. Used to differentiate between devices of same *model*. 
    Depending of device model it may be a non-volatile value programmed into the device, 
    a volatile value that changes at each power on (or battery change), or a value configurable by
    user e.g. by switch or jumpers. No assumptions should be made to the id value other than it contains
    a unique sequence of alphanumeric characters.
  * Length of *id* should be less than 16 characters.

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

* **battery** (double) (Optional)
  * Battery status indication as a level between 0 (empty) and 1 (full). If the sensor can only report a binary status the value shall be 1 for "OK" and 0 for "LOW".

* **battery_V** (double) (Optional)
  * Battery level in Volts. Should be suppleanted by *battery* status indication if possible.

## Sensor Data
Due to the large variance in sensor types this list of common values is non-exhaustive. Additional data value fields should follow the form: `<Type>_<Unit>`, where *Unit* should be in sensors native units insofar possible with no conversion.
Examples:

* **temperature_C** (**temperature_F**) (double) (Optional)
  * Temperature from a temperature sensor in degrees Celcius (Fahrenheit).
  
* **setpoint_C** (**setpoint_F**) (double) (Optional)
  * Thermal set point of a thermostat device in degrees Celcius (Fahrenheit).
  
* **humidity** (double) (Optional)
  * Humidity from a hygrometer sensor in % relative humidity
  
* **wind_dir_deg** (double) (Optional)
  * Wind direction from wind sensor in compass direction degrees.
  
* **wind_speed_ms** (double) (Optional)
  * Average wind speed from wind sensor in m/s. Averaging time is sensor dependant.

* **gust_speed_ms** (double) (Optional)
  * Gust wind speed from wind sensor in m/s. 

* **rain_mm** (**rain_in**) (double) (Optional)
  * Rainfall from rain sensor in mm (inches) since last reset. Reset method is device dependant.
  
* **rain_rate_mmph** (**rain_rate_inph**) (double) (Optional)
  * Rainfall rate from rain sensor in mm per hour (inches per hour).
  
* **pressure_hPa** (**pressure_psi**) (double) (Optional)
  * Air pressure from barometer or Tire Pressure Monitor in hPa (psi)
  
