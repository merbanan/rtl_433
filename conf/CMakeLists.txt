########################################################################
# Install example configuration files
########################################################################
file(GLOB RTL433_CONF_FILES "*.conf")

# Note that apparently bare `etc` or relative CMAKE_INSTALL_SYSCONFDIR
# always gets CMAKE_INSTALL_PREFIX prepended.
# Use absolute CMAKE_INSTALL_FULL_SYSCONFDIR to get /etc for /usr prefix.
install(FILES
    ${RTL433_CONF_FILES}
    DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/rtl_433
)
