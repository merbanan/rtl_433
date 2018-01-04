# - Find libmosquitto
# Find the native libmosquitto includes and libraries
#
#  MOSQUITTO_INCLUDE_DIR - where to find mosquitto.h, etc.
#  MOSQUITTO_LIBRARIES   - List of libraries when using libmosquitto.
#  MOSQUITTO_FOUND       - True if libmosquitto found.

if (NOT MOSQUITTO_INCLUDE_DIR)
  find_path(MOSQUITTO_INCLUDE_DIR mosquitto.h)
endif()

if (NOT MOSQUITTO_LIBRARY)
  find_library(
    MOSQUITTO_LIBRARY
    NAMES mosquitto)
endif()

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(
  MOSQUITTO DEFAULT_MSG
  MOSQUITTO_LIBRARY MOSQUITTO_INCLUDE_DIR)

message(STATUS "libmosquitto include dir: ${MOSQUITTO_INCLUDE_DIR}")
message(STATUS "libmosquitto: ${MOSQUITTO_LIBRARY}")
set(MOSQUITTO_LIBRARIES ${MOSQUITTO_LIBRARY})

mark_as_advanced(MOSQUITTO_INCLUDE_DIR MOSQUITTO_LIBRARY)
