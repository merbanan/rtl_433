# rtl_433 is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3, or (at your option)
# any later version.
#
# rtl_433 is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with GNU Radio; see the file COPYING.  If not, write to
# the Free Software Foundation, Inc., 51 Franklin Street,
# Boston, MA 02110-1301, USA.

########################################################################
# Build utility
########################################################################
add_executable(rtl_433 
	baseband.c
	bitbuffer.c
	data.c
	pulse_demod.c
	pulse_detect.c
	rtl_433.c
	util.c
	devices/acurite.c
	devices/alecto.c
	devices/ambient_weather.c
	devices/brennstuhl_rcs_2044.c
	devices/calibeur.c
	devices/cardin.c
	devices/chuango.c
	devices/current_cost.c
	devices/danfoss.c
	devices/dsc.c
	devices/ec3k.c
	devices/efergy_e2_classic.c
	devices/elv.c
	devices/emontx.c
	devices/esperanza_ews.c
	devices/fineoffset.c
	devices/fineoffset_wh1080.c
	devices/generic_remote.c
	devices/generic_temperature_sensor.c
	devices/gt_wt_02.c
	devices/hideki.c
	devices/ht680.c
	devices/inovalley-kw9015b.c
	devices/intertechno.c
	devices/lacrosse.c
	devices/lacrossews.c
	devices/mebus.c
	devices/newkaku.c
	devices/nexus.c
	devices/oil_watchman.c
	devices/oregon_scientific.c
	devices/oregon_scientific_v1.c
	devices/prologue.c
	devices/rubicson.c
	devices/silvercrest.c
	devices/steffen.c
	devices/tfa_twin_plus_30.3049.c
	devices/valeo.c
	devices/waveman.c
	devices/wt450.c
	devices/x10_rf.c
	devices/s3318p.c
	devices/akhan_100F14.c
	devices/quhwa.c
	devices/proove.c
	devices/bresser_3ch.c

)

add_library(data data.c)

target_link_libraries(rtl_433
	${LIBRTLSDR_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
)

	
set(INSTALL_TARGETS rtl_433)
if(UNIX)
target_link_libraries(rtl_433 m)
endif()

########################################################################
# Install built library files & utilities
########################################################################
install(TARGETS ${INSTALL_TARGETS}
    RUNTIME DESTINATION bin              # .dll file
)
