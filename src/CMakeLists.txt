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
	rtl_433.c
	devices/silvercrest.c
	devices/rubicson.c
	devices/prologue.c
	devices/waveman.c
	devices/steffen.c
	devices/elv.c
	devices/lacrosse.c
	devices/acurite.c
	devices/oregon_scientific.c
	devices/mebus.c
	devices/intertechno.c
	devices/alecto.c
	devices/newkaku.c
	devices/fineoffset.c)

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
