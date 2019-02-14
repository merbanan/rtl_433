# Tries to find libgps (gpsd).
#
# Usage of this module as follows:
#
#     find_package(LibGPS)
#
# Variables used by this module, they can change the default behaviour and need
# to be set before calling find_package:
#
#  LIBGPS_ROOT_DIR  Set this variable to the root installation of
#                       GPS if the module has problems finding
#                       the proper installation path.
#
# Variables defined by this module:
#
#  LIBGPS_FOUND              System has GPS libs/headers
#  LIBGPS_LIBRARIES          The GPS libraries (tcmalloc & profiler)
#  LIBGPS_INCLUDE_DIR        The location of GPS headers

include(FindPackageHandleStandardArgs)

find_path(LIBGPS_INCLUDE_DIR
  NAMES gps.h
  HINTS ${LIBGPS_INCLUDE_DIRS})

find_library(LIBGPS_LIBRARIES
  NAMES gps
  HINTS ${LIBGPS_LIBRARY_DIRS})

# Handle the REQUIRED argument and set the <UPPERCASED_NAME>_FOUND variable
# The package is found if all variables listed are TRUE
find_package_handle_standard_args(
  LIBGPS
  DEFAULT_MSG
  LIBGPS_LIBRARIES
  LIBGPS_INCLUDE_DIR)

mark_as_advanced(
  LIBGPS_INCLUDE_DIR
  LIBGPS_LIBRARIES)
