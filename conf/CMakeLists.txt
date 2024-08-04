########################################################################
# Install example configuration files
########################################################################
file(GLOB RTL433_CONF_FILES "*.conf")

# Note that apparently bare `etc` or relative CMAKE_INSTALL_SYSCONFDIR
# always gets CMAKE_INSTALL_PREFIX prepended.
# Use absolute CMAKE_INSTALL_FULL_SYSCONFDIR to get /etc for /usr prefix.
# Note that CMAKE_STAGING_PREFIX should contain CMAKE_INSTALL_PREFIX but
# that component is not stripped here. Breaks cross-compile SYSCONFDIR.
install(FILES
    ${RTL433_CONF_FILES}
    DESTINATION ${CMAKE_STAGING_PREFIX}${CMAKE_INSTALL_FULL_SYSCONFDIR}/rtl_433
)
