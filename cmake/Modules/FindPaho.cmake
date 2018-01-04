# - Find paho-mqtt
# Find the native paho-mqtt includes and libraries
#
#  PAHO_FOUND       - System has paho
#  PAHO_INCLUDE_DIR - The paho include directories
#  PAHO_LIBRARIES   - The libraries include directories

if (NOT PAHO_INCLUDE_DIR)
  find_path(PAHO_INCLUDE_DIR MQTTClient.h)
endif()

if (NOT PAHO_A_LIBRARY)
  find_library(
    PAHO_A_LIBRARY
    NAMES paho-mqtt3a)
endif()

if (NOT PAHO_C_LIBRARY)
  find_library(
    PAHO_C_LIBRARY
    NAMES paho-mqtt3c)
endif()

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(
  PAHO DEFAULT_MSG
  PAHO_INCLUDE_DIR PAHO_A_LIBRARY PAHO_C_LIBRARY)

message(STATUS "paho-mqtt include dir: ${PAHO_INCLUDE_DIR}")
message(STATUS "paho-mqtt libraries: ${PAHO_A_LIBRARY} ${PAHO_C_LIBRARY}")
set(PAHO_LIBRARIES ${PAHO_A_LIBRARY} ${PAHO_C_LIBRARY})

mark_as_advanced(PAHO_INCLUDE_DIR PAHO_A_LIBRARY PAHO_C_LIBRARY)
