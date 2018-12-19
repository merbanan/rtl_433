########################################################################
# Install example configuration files
########################################################################
file(GLOB RTL433_CONF_FILES "*.conf")

install(FILES
    ${RTL433_CONF_FILES}
    DESTINATION etc/rtl_433
)
