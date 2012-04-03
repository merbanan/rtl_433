# Copyright 2012 OSMOCOM Project
#
# This file is part of rtl-sdr
#
# GNU Radio is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3, or (at your option)
# any later version.
#
# GNU Radio is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with GNU Radio; see the file COPYING.  If not, write to
# the Free Software Foundation, Inc., 51 Franklin Street,
# Boston, MA 02110-1301, USA.

########################################################################
# Setup library
########################################################################
add_library(rtlsdr_shared SHARED
    rtl-sdr.c
    tuner_e4000.c
    tuner_fc0012.c
    tuner_fc0013.c
    tuner_fc2580.c
)

target_link_libraries(rtlsdr_shared
    ${LIBUSB_LIBRARIES}
)

set_target_properties(rtlsdr_shared PROPERTIES DEFINE_SYMBOL "rtlsdr_EXPORTS")
set_target_properties(rtlsdr_shared PROPERTIES OUTPUT_NAME rtlsdr)

add_library(rtlsdr_static STATIC
    rtl-sdr.c
    tuner_e4000.c
    tuner_fc0012.c
    tuner_fc0013.c
    tuner_fc2580.c
)

target_link_libraries(rtlsdr_static
    ${LIBUSB_LIBRARIES}
)

set_target_properties(rtlsdr_static PROPERTIES DEFINE_SYMBOL "rtlsdr_STATIC")

if(NOT WIN32)
# Force same library filename for static and shared variants of the library
set_target_properties(rtlsdr_static PROPERTIES OUTPUT_NAME rtlsdr)
endif()

########################################################################
# Build utility
########################################################################
add_executable(rtl_sdr main.c)
target_link_libraries(rtl_sdr rtlsdr_static
    ${LIBUSB_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
)

########################################################################
# Install built library files & utilities
########################################################################
install(TARGETS rtlsdr_shared rtlsdr_static rtl_sdr
    LIBRARY DESTINATION lib${LIB_SUFFIX} # .so/.dylib file
    ARCHIVE DESTINATION lib${LIB_SUFFIX} # .lib file
    RUNTIME DESTINATION bin              # .dll file
)
